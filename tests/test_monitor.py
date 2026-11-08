import importlib.util
import io
import json
from pathlib import Path
import unittest
import zlib
import threading
from urllib.request import Request, urlopen
from urllib.error import HTTPError

spec=importlib.util.spec_from_file_location('monitor',Path(__file__).parents[1]/'tools'/'monitor.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ProtocolTests(unittest.TestCase):
    def test_state_and_event(self):
        monitor=module.Monitor('unused',921600)
        monitor.process(b'@STATE {"frame":5,"entries":3}\n',None)
        monitor.process(b'@EVENT {"id":2,"direction":"entry"}\n',None)
        self.assertEqual(monitor.snapshot()['state']['entries'],3)
        self.assertEqual(monitor.snapshot()['events'][0]['id'],2)

    def test_binary_jpeg_preserves_newlines(self):
        monitor=module.Monitor('unused',921600)
        payload=b'\xff\xd8\x00\r\n@STATE not metadata\n\xff\xd9'
        monitor.process(b'@STATE {"frame":5}\n',None)
        header={'frame':5,'size':len(payload),'crc32':zlib.crc32(payload)}
        monitor.process(b'@JPEG '+json.dumps(header).encode(),io.BytesIO(payload))
        self.assertEqual(monitor.image,payload)
        self.assertEqual(monitor.image_state['frame'],5)

    def test_corrupt_jpeg_rejected(self):
        monitor=module.Monitor('unused',921600)
        monitor.process(b'@STATE {"frame":5}\n',None)
        monitor.process(b'@JPEG {"frame":5,"size":3,"crc32":0}',io.BytesIO(b'bad'))
        self.assertFalse(monitor.image)

    def test_unmatched_image_does_not_get_wrong_boxes(self):
        monitor=module.Monitor('unused',921600)
        monitor.process(b'@STATE {"frame":6}\n',None)
        payload=b'abc'
        header={'frame':5,'size':3,'crc32':zlib.crc32(payload)}
        monitor.process(b'@JPEG '+json.dumps(header).encode(),io.BytesIO(payload))
        self.assertFalse(monitor.image)

    def test_bad_json_survives(self):
        monitor=module.Monitor('unused',921600)
        monitor.process(b'@STATE {truncated',None)
        self.assertFalse(monitor.state)
        self.assertTrue(monitor.logs)

    def test_disconnected_command_and_validation(self):
        monitor=module.Monitor('unused',921600)
        self.assertFalse(monitor.send('reset')['ok'])
        self.assertFalse(monitor.send('reset\nsave')['ok'])
        self.assertFalse(monitor.send('arbitrary command')['ok'])

    def test_wifi_setup_is_one_ascii_serial_command(self):
        monitor=module.Monitor('unused',921600)
        sent=[]
        class Connection:
            def write(self,data):
                sent.append(data)
                monitor.process(b'@ACK {"command":"wifi","ok":true}',None)
        monitor.serial=Connection()
        config={'ssid':'Test café','password':'test-password'}
        result=monitor.send('wifi '+json.dumps(config,ensure_ascii=True,separators=(',',':')))
        self.assertTrue(result['ok'])
        self.assertEqual(len(sent),1)
        self.assertEqual(sent[0].count(b'\n'),1)
        self.assertEqual(json.loads(sent[0].decode('ascii')[5:]),config)
        self.assertFalse(monitor.logs)

    def test_wifi_command_cannot_inject_another_command(self):
        monitor=module.Monitor('unused',921600)
        self.assertFalse(monitor.send('wifi {}\nreset')['ok'])
        self.assertFalse(monitor.send('wifi []')['ok'])

    def test_usb_setup_exposes_network_status(self):
        monitor=module.Monitor('unused',921600)
        monitor.process(b'@STATE {"frame":1,"network":{"ready":true,"sta_connected":false}}',None)
        self.assertTrue(monitor.snapshot()['network']['ready'])

    def test_video_keepalive_cannot_acknowledge_a_user_command(self):
        monitor=module.Monitor('unused',921600)
        monitor.process(b'@ACK {"command":"usbpreview","ok":true}',None)
        self.assertFalse(monitor.ack_ready.is_set())
        monitor.process(b'@ACK {"command":"camera","ok":false}',None)
        self.assertTrue(monitor.ack_ready.is_set())
        self.assertFalse(monitor.ack['ok'])

    def test_camera_command_is_one_validated_serial_line(self):
        monitor=module.Monitor('unused',921600)
        sent=[]
        class Connection:
            def write(self,data):
                sent.append(data)
                monitor.process(b'@ACK {"command":"camera","ok":true}',None)
        monitor.serial=Connection()
        self.assertTrue(monitor.send('camera image -12 105 95 1 0')['ok'])
        self.assertEqual(sent,[b'camera image -12 105 95 1 0\n'])
        self.assertFalse(monitor.send('camera save\nreset')['ok'])
        self.assertFalse(monitor.send('camera stream 8 60 extra')['ok'])

    def test_http_jpeg_returns_its_own_metadata(self):
        monitor=module.Monitor('unused',921600)
        monitor.image=b'\xff\xd8test\xff\xd9'
        monitor.image_state={'frame':11,'tracks':[{'id':4,'box':[.1,.2,.3,.4]}]}
        monitor.state={'frame':99}
        server=module.ThreadingHTTPServer(('127.0.0.1',0),module.make_handler(monitor,0))
        worker=threading.Thread(target=server.serve_forever,daemon=True)
        worker.start()
        try:
            with urlopen(Request(f'http://127.0.0.1:{server.server_port}/image.jpg',headers={'Authorization':'Basic YWRtaW46YWRtaW4='})) as response:
                self.assertEqual(response.read(),monitor.image)
                metadata=json.loads(response.headers['X-Frame-State'])
                self.assertEqual(metadata,monitor.image_state)
                self.assertNotEqual(metadata['frame'],monitor.state['frame'])
        finally:
            server.shutdown();server.server_close();worker.join()

    def test_scan_preserves_exact_network_name(self):
        monitor=module.Monitor('unused',921600)
        monitor.process(b'@SCAN {"scanning":false,"networks":[{"ssid":"Oficina_24","rssi":-45}],"error":""}',None)
        self.assertEqual(monitor.networks['networks'][0]['ssid'],'Oficina_24')
        self.assertFalse(monitor.networks['scanning'])

    def test_body_filter_command_is_validated(self):
        monitor=module.Monitor('unused',921600)
        sent=[]
        class Connection:
            def write(self,data):
                sent.append(data)
                monitor.process(b'@ACK {"command":"body","ok":true}',None)
        monitor.serial=Connection()
        self.assertTrue(monitor.send('body 1')['ok'])
        self.assertEqual(sent,[b'body 1\n'])
        self.assertFalse(monitor.send('body 2')['ok'])
        self.assertFalse(monitor.send('body 0\nreset')['ok'])

    def test_scan_and_reconnect_use_validated_serial_commands(self):
        monitor=module.Monitor('unused',921600)
        sent=[]
        class Connection:
            def write(self,data):
                sent.append(data)
                name=data.split(b' ',1)[0].strip().decode()
                monitor.process(('@ACK '+json.dumps({'command':name,'ok':True})).encode(),None)
        monitor.serial=Connection()
        self.assertTrue(monitor.send('scan')['ok'])
        self.assertTrue(monitor.send('wifi {"reconnect":true}')['ok'])
        self.assertEqual(sent,[b'scan\n',b'wifi {"reconnect":true}\n'])
        self.assertFalse(monitor.send('scan\nreset')['ok'])

    def test_usb_scan_results_are_served_over_http(self):
        monitor=module.Monitor('unused',921600)
        monitor.networks={'scanning':False,'networks':[{'ssid':'Oficina_24'}],'error':''}
        server=module.ThreadingHTTPServer(('127.0.0.1',0),module.make_handler(monitor,0))
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        try:
            url=f'http://127.0.0.1:{server.server_port}/networks'
            with self.assertRaises(HTTPError) as denied:urlopen(url)
            self.assertEqual(denied.exception.code,401)
            with urlopen(Request(url,headers={'Authorization':'Basic YWRtaW46YWRtaW4='})) as response:
                self.assertEqual(json.load(response)['networks'][0]['ssid'],'Oficina_24')
            with self.assertRaises(HTTPError) as denied:
                urlopen(Request(url,headers={'Authorization':'Basic YWRtaW46d3Jvbmc='}))
            self.assertEqual(denied.exception.code,401)
        finally:
            server.shutdown();server.server_close();worker.join()


if __name__=='__main__':
    unittest.main()
