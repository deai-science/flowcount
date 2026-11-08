"""Run 320x240 JPEGs through the P4 detector over physical USB, saving JSONL.

This suspends live counting during the replay (15 s lease), without changing
totals, calibration or Wi-Fi. Close other serial monitors first. Images stay
local; use a private output folder. The output is a diagnostic, not live FPS.
"""
import argparse
import json
from pathlib import Path
import time
import zlib
import serial


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM5')
    parser.add_argument('--frames',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--fps',type=float,default=8)
    parser.add_argument('--pattern',default='*.jpg')
    args=parser.parse_args()
    if not 0<args.fps<=60: parser.error('FPS must be in (0,60].')
    frames=sorted(args.frames.glob(args.pattern))
    if not frames: parser.error('No JPEG frames found.')
    connection=serial.Serial(port=None,baudrate=921600,timeout=.5,write_timeout=5)
    connection.dtr=False;connection.rts=False;connection.port=args.port
    connection.open()
    def wait(prefix,timeout=15):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            line=connection.readline()
            if line.startswith(b'@REPLAY_ERROR'):raise RuntimeError('P4 rejected frame (CRC, size or JPEG).')
            if line.startswith(prefix):return line[len(prefix):]
        raise TimeoutError(prefix)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    try:
        connection.write(b'usbpreview 0\n')
        with args.output.open('w',encoding='utf-8') as output:
            for index,path in enumerate(frames):
                payload=path.read_bytes()
                if not 4<=len(payload)<=65536:raise ValueError('JPEG size outside firmware limit.')
                timestamp=round(index*1000/args.fps)
                connection.write(f'replay {len(payload)} {zlib.crc32(payload)&0xffffffff} {timestamp}\n'.encode())
                wait(b'@REPLAY_READY')
                for offset in range(0,len(payload),1024):
                    # CH340 adapters can overrun the UART FIFO under Wi-Fi/CSI
                    # interrupt load. Pace small writes; CRC still rejects loss.
                    chunk=payload[offset:offset+1024]
                    for part in range(0,len(chunk),64):
                        connection.write(chunk[part:part+64]);time.sleep(.003)
                    acknowledged=int(wait(b'@REPLAY_CHUNK '))
                    if acknowledged!=min(offset+1024,len(payload)):
                        raise RuntimeError(f'Incomplete UART chunk in {path.name}: {acknowledged} != {min(offset+1024,len(payload))}.')
                result=json.loads(wait(b'@REPLAY '));result['source']=path.name
                output.write(json.dumps(result)+'\n');output.flush()
                if index%16==0:print(f'{index+1}/{len(frames)} frames',flush=True)
    finally:
        connection.write(b'replay end\n')
        connection.close()


if __name__=='__main__':main()
