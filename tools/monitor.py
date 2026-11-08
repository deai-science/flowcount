"""USB calibration monitor. All detection and counting happen in the ESP32-P4.

Requires pyserial (already included in the ESP-IDF Python environment).
No video is saved and no external network service is used.
"""
import argparse
import base64
from collections import deque
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import threading
import time
import zlib
import serial


class Monitor:
    def __init__(self, port, baud, reset=False, video=False):
        self.port, self.baud, self.reset = port, baud, reset
        self.video = video
        self.write_lock = threading.Lock()
        self.lock = threading.Lock()
        self.command_lock = threading.Lock()
        self.ack_ready = threading.Event()
        self.ack = None
        self.serial = None
        self.connected = False
        self.state = {}
        self.image = b''
        self.image_state = {}
        self.updated = 0
        self.logs = deque(maxlen=40)
        self.events = deque(maxlen=25)
        self.networks = {'scanning':False,'networks':[],'error':''}
        self.stop = threading.Event()

    def snapshot(self):
        with self.lock:
            result=dict(state=self.state, image_state=self.image_state,
                        connected=self.connected, age_s=round(time.monotonic()-self.updated,1) if self.updated else None,
                        logs=list(self.logs), events=list(self.events))
            if self.state.get('network'):
                result['network']=self.state['network']
            return result

    def send(self, command):
        # Only complete known commands are accepted by the local HTTP endpoint.
        wifi_command=False
        if command.startswith('wifi ') and len(command)<512 and '\n' not in command and '\r' not in command:
            try:
                config=json.loads(command[5:])
                wifi_command=isinstance(config,dict) and (config.get('forget') is True or config.get('reconnect') is True or (
                    isinstance(config.get('ssid'),str) and isinstance(config.get('password'),str)))
            except ValueError:
                pass
        camera_command=re.fullmatch(r'camera (?:save|restore|defaults|stream \d{1,2} \d{1,2}|auto \d{1,6}|manual \d{1,6} \d{1,3}|image -?\d{1,2} \d{1,3} \d{1,3} [01] [01])',command)
        if not wifi_command and not camera_command and not re.fullmatch(r'(save|reset|scan|body [01]|preview [01]|score (?:0?\.\d+)|gate [hv] (?:[01]?(?:\.\d+)?) (?:0?\.\d+) [01] (?:[01]?(?:\.\d+)?) (?:[01]?(?:\.\d+)?))',command):
            return {'ok':False, 'error':'Invalid command'}
        with self.command_lock:
            self.ack_ready.clear()
            with self.lock:
                connection = self.serial
            if not connection:
                return {'ok':False, 'error':'USB disconnected'}
            try:
                with self.write_lock:
                    connection.write((command+'\n').encode('ascii'))
                if not self.ack_ready.wait(3):
                    return {'ok':False, 'error':'No response from firmware'}
                with self.lock:
                    return dict(self.ack)
            except (serial.SerialException,OSError) as exc:
                return {'ok':False,'error':str(exc)}

    def read_loop(self):
        while not self.stop.is_set():
            connection = None
            try:
                connection = serial.Serial(port=None,baudrate=self.baud,timeout=.5,write_timeout=2)
                connection.dtr = False
                connection.rts = False
                connection.port = self.port
                connection.open()
                if hasattr(connection,'set_buffer_size'):
                    connection.set_buffer_size(rx_size=131072,tx_size=65536)
                if self.reset:
                    connection.rts = True
                    time.sleep(.1)
                    connection.rts = False
                    self.reset = False
                with self.lock:
                    self.serial = connection
                    self.connected = True
                renewed=0
                while not self.stop.is_set():
                    if self.video and time.monotonic()-renewed>=5:
                        with self.write_lock:
                            connection.write(b'usbpreview 1\n')
                        renewed=time.monotonic()
                    raw = connection.readline(8192)
                    if not raw:
                        continue
                    self.process(raw,connection)
            except (serial.SerialException,OSError,ValueError) as exc:
                with self.lock:
                    self.logs.append(f'USB: {exc}')
            finally:
                with self.lock:
                    self.serial = None
                    self.connected = False
                if connection:
                    connection.close()
            self.stop.wait(2)

    def process(self, raw, connection):
        raw = raw.strip()
        try:
            if raw.startswith(b'@JPEG '):
                header = json.loads(raw[6:])
                size = int(header['size'])
                if not 0<size<=65536:
                    return
                payload = bytearray()
                deadline = time.monotonic()+3
                while len(payload)<size and time.monotonic()<deadline:
                    payload.extend(connection.read(size-len(payload)))
                # CRLF is harmless; the following reader skips empty lines.
                if len(payload)!=size or zlib.crc32(payload)&0xffffffff!=header['crc32']:
                    with self.lock:
                        self.logs.append('USB image discarded: CRC mismatch')
                    return
                with self.lock:
                    if self.state.get('frame')==header['frame']:
                        self.image = bytes(payload)
                        self.image_state = self.state.copy()
                return
            if raw.startswith(b'@STATE '):
                state = json.loads(raw[7:])
                with self.lock:
                    self.state = state
                    self.updated = time.monotonic()
                return
            if raw.startswith(b'@EVENT '):
                event = json.loads(raw[7:])
                with self.lock:
                    self.events.append(event)
                return
            if raw.startswith(b'@SCAN '):
                scan=json.loads(raw[6:])
                if isinstance(scan,dict) and isinstance(scan.get('networks'),list):
                    with self.lock:
                        self.networks=scan
                return
            if raw.startswith(b'@ACK '):
                ack = json.loads(raw[5:])
                if ack.get('command')=='usbpreview':
                    return  # Keepalive acknowledgement must never complete a user's command.
                with self.lock:
                    self.ack = ack
                self.ack_ready.set()
                return
            if raw:
                text = raw.decode('utf-8',errors='replace')
                print(text,flush=True)
                with self.lock:
                    self.logs.append(text)
        except (ValueError,KeyError,TypeError):
            with self.lock:
                self.logs.append('Incomplete USB packet discarded')


def make_handler(monitor, http_port):
    class Handler(BaseHTTPRequestHandler):
        def authenticated(self):
            if self.headers.get('Authorization')=='Basic '+base64.b64encode(b'admin:admin').decode():
                return True
            self.respond(401,b'Sign in with admin / admin.','text/plain; charset=utf-8',
                         {'WWW-Authenticate':'Basic realm="FlowCount"'})
            return False
        def log_message(self,*args):
            pass

        def respond(self,status,body,kind='application/json; charset=utf-8',extra_headers=None):
            self.send_response(status)
            self.send_header('Content-Type',kind)
            self.send_header('Content-Length',str(len(body)))
            self.send_header('Cache-Control','no-store')
            self.send_header('X-Content-Type-Options','nosniff')
            for name,value in (extra_headers or {}).items():
                self.send_header(name,value)
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError):
                pass

        def do_GET(self):
            path = self.path.split('?')[0]
            if path!='/health' and not self.authenticated():return
            if path=='/':
                self.respond(200,Path(__file__).with_name('monitor.html').read_bytes(),'text/html; charset=utf-8')
            elif path=='/health':
                self.respond(200,b'{"ok":true}')
            elif path=='/state':
                self.respond(200,json.dumps(monitor.snapshot(),allow_nan=False).encode())
            elif path=='/image.jpg':
                with monitor.lock:
                    image = monitor.image
                    metadata = json.dumps(monitor.image_state,separators=(',',':'))
                self.respond(200 if image else 404,image,'image/jpeg',{'X-Frame-State':metadata})
            elif path=='/network':
                self.respond(200,json.dumps(monitor.snapshot().get('network',{})).encode())
            elif path=='/networks':
                with monitor.lock:
                    scan=dict(monitor.networks)
                self.respond(200,json.dumps(scan).encode())
            else:
                self.respond(404,b'{}')

        def do_POST(self):
            if not self.authenticated():return
            origin = self.headers.get('Origin')
            if self.path not in ('/command','/network','/scan') or (origin and origin not in
                    (f'http://127.0.0.1:{http_port}',f'http://localhost:{http_port}')):
                self.respond(403,b'{}')
                return
            try:
                size = int(self.headers.get('Content-Length','0'))
                if not 0<size<=512 or self.headers.get_content_type()!='application/json':
                    raise ValueError()
                payload = json.loads(self.rfile.read(size))
                command = 'scan' if self.path=='/scan' else 'wifi '+json.dumps(payload,ensure_ascii=True,separators=(',',':')) if self.path=='/network' else payload['command']
                if self.path=='/scan':
                    with monitor.lock:
                        monitor.networks={'scanning':True,'networks':[],'error':''}
                if not isinstance(command,str):
                    raise ValueError()
                self.respond(200,json.dumps(monitor.send(command)).encode())
            except (ValueError,KeyError,TypeError):
                self.respond(400,b'{"ok":false}')
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM5')
    parser.add_argument('--baud',type=int,default=921600)
    parser.add_argument('--http-port',type=int,default=8765)
    parser.add_argument('--reset',action='store_true',help='Reset the board when connecting')
    args = parser.parse_args()
    monitor = Monitor(args.port,args.baud,args.reset,video=True)
    worker = threading.Thread(target=monitor.read_loop,daemon=True)
    worker.start()
    server = ThreadingHTTPServer(('127.0.0.1',args.http_port),make_handler(monitor,args.http_port))
    print(f'Monitor: http://127.0.0.1:{args.http_port} | {args.port} | Ctrl+C to close',flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        monitor.stop.set()
        server.server_close()
        worker.join(timeout=4)


if __name__=='__main__':
    main()
