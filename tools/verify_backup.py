"""Verify the complete flash backup before any destructive programming command."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('backup',type=Path)
    args=parser.parse_args()
    manifest=json.loads(args.backup.with_suffix('.json').read_text())
    data=args.backup.read_bytes()
    if not (manifest.get('verified') is True and manifest.get('chip')=='ESP32-P4'
            and len(data)==manifest.get('size')==16*1024*1024
            and hashlib.sha256(data).hexdigest()==manifest.get('sha256')):
        raise SystemExit('Backup incomplete or modified. Erasing and flashing are blocked.')
    print('Complete 16 MB backup verified with SHA-256.')


if __name__=='__main__':
    main()
