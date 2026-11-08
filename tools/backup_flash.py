"""Read-only esptool 4.x backup with a single packet in flight and MD5 checks.

The default stub read window can overrun some USB-UART bridges on Windows.
Only the RAM stub is uploaded. No flash or eFuse write operations are used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
import esptool


def read_block(esp, offset, length):
    esp.check_command('read flash', esp.ESP_READ_FLASH,
                      struct.pack('<IIII', offset, length, 4096, 1))
    data = bytearray()
    while len(data) < length:
        packet = esp.read()
        if len(packet) != min(4096, length-len(data)):
            raise RuntimeError(f'Invalid packet at {offset+len(data):#x}: {len(packet)}')
        data.extend(packet)
        esp.write(struct.pack('<I', len(data)))
    digest = esp.read()
    if digest != hashlib.md5(data).digest():
        raise RuntimeError('Block MD5 mismatch')
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='COM5')
    parser.add_argument('--baud', type=int, default=460800)
    parser.add_argument('--size', type=lambda x: int(x, 0), default=16*1024*1024)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    partial = args.out.with_suffix('.partial')
    if args.out.exists():
        raise SystemExit(f'Refusing to overwrite complete backup: {args.out}')
    offset = partial.stat().st_size if partial.exists() else 0
    esp = None
    try:
        with partial.open('ab') as output:
            while offset < args.size:
                for attempt in range(5):
                    try:
                        if esp is None:
                            esp = esptool.detect_chip(args.port, 115200)
                            if esp.CHIP_NAME != 'ESP32-P4':
                                raise RuntimeError('Unexpected chip, stopping')
                            esp = esp.run_stub()
                            esp.change_baud(args.baud)
                            esp.flash_spi_attach(0)
                            esp.flash_set_parameters(args.size)
                            if offset:
                                output.flush()
                                with partial.open('rb') as prior:
                                    if esp.flash_md5sum(0,offset) != hashlib.md5(prior.read()).hexdigest():
                                        raise RuntimeError('Existing partial backup does not match this device')
                        block = read_block(esp, offset, min(65536, args.size-offset))
                        output.write(block)
                        output.flush()
                        offset += len(block)
                        print(f'BACKUP {offset}/{args.size} ({offset*100//args.size}%)', flush=True)
                        break
                    except Exception as exc:
                        print(f'Retry {attempt+1}: {exc}', flush=True)
                        if esp:
                            esp._port.close()
                            esp = None
                        if attempt == 4:
                            raise
                        time.sleep(.5)
        data = partial.read_bytes()
        flash_md5 = esp.flash_md5sum(0,args.size)
        if flash_md5 != hashlib.md5(data).hexdigest():
            raise RuntimeError('Full flash MD5 mismatch')
        partial.rename(args.out)
        manifest = {'port': args.port, 'chip':'ESP32-P4', 'size': len(data),
                    'sha256': hashlib.sha256(data).hexdigest(), 'flash_md5': flash_md5,
                    'verified': True}
        args.out.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print('BACKUP COMPLETE AND VERIFIED', flush=True)
        esp.hard_reset()
    finally:
        if esp:
            esp._port.close()


if __name__ == '__main__':
    main()
