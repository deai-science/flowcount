"""USB diagnostics without starting a web server on the computer.

Network access information is saved only to the explicitly selected local file.
"""
import argparse
import json
from pathlib import Path
import time
from monitor import Monitor


class Console(Monitor):
    def __init__(self,port,baud,reset,access_file,state_file):
        super().__init__(port,baud,reset)
        self.access_file=access_file
        self.state_file=state_file
        self.last_written=0

    def process(self,raw,connection):
        if raw.startswith(b'@NETWORK '):
            try:
                access=json.loads(raw[9:])
                if self.access_file:
                    self.access_file.parent.mkdir(parents=True,exist_ok=True)
                    self.access_file.write_text(json.dumps(access,indent=2)+'\n',encoding='utf-8')
                print(f"Wi-Fi: {access['ap_ssid']} | UI: http://{access['ap_ip']}/ | key saved in local file",flush=True)
                return
            except (ValueError,KeyError):
                pass
        super().process(raw,connection)
        if raw.startswith(b'@STATE ') and self.state_file and time.monotonic()-self.last_written>=1:
            self.state_file.parent.mkdir(parents=True,exist_ok=True)
            temporary=self.state_file.with_suffix('.tmp')
            temporary.write_text(json.dumps(self.state)+'\n',encoding='utf-8')
            temporary.replace(self.state_file)
            self.last_written=time.monotonic()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM5')
    parser.add_argument('--baud',type=int,default=921600)
    parser.add_argument('--reset',action='store_true')
    parser.add_argument('--access-file',type=Path)
    parser.add_argument('--state-file',type=Path)
    args=parser.parse_args()
    console=Console(args.port,args.baud,args.reset,args.access_file,args.state_file)
    try:
        console.read_loop()
    except KeyboardInterrupt:
        console.stop.set()


if __name__=='__main__':
    main()
