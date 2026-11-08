"""Check the exact public source selection, local links and accidental private data."""
import hashlib
import json
from pathlib import Path
import re
import sys

from sync_repository import selected_files


def main():
    root = Path(__file__).resolve().parents[1]
    selected = selected_files(root)
    exported = {p.as_posix() for p in selected}
    forbidden = {'.private', 'backups', 'captures', 'managed_components', 'build', '__pycache__'}
    errors = []
    for path in selected:
        if forbidden.intersection(path.parts) or path.name in {'sdkconfig', 'sdkconfig.old'}:
            errors.append(f'Forbidden export: {path}')
        if path.suffix in {'.bin', '.elf', '.log', '.partial', '.mp4', '.pdf'}:
            errors.append(f'Forbidden artifact: {path}')
        if path.suffix in {'.md', '.html'}:
            text = (root / path).read_text(encoding='utf-8')
            links = re.findall(r'\]\(([^)]+)\)|(?:src|href)="([^"]+)"', text)
            for parts in links:
                target = next((part for part in parts if part), '')
                if not target or target.startswith(('#', 'http:', 'https:', 'data:', 'mailto:')):
                    continue
                target = target.split('#')[0]
                resolved = (root / path.parent / target).resolve()
                if not resolved.is_relative_to(root):
                    errors.append(f'Link escapes project: {path}: {target}')
                elif not resolved.exists():
                    errors.append(f'Missing local link: {path}: {target}')
                elif resolved.is_file() and resolved.relative_to(root).as_posix() not in exported:
                    errors.append(f'Link points to unexported file: {path}: {target}')
    # Exact private access values must not occur in selected text files. Public
    # defaults (admin/admin, flowcount.local, 192.168.4.1) are intentionally allowed.
    private = root / '.private/network-access.json'
    if private.exists():
        access = json.loads(private.read_text(encoding='utf-8-sig'))
        secrets = [access.get('ap_password', '')]
        for path in selected:
            data = (root / path).read_bytes()
            if any(secret and secret.encode() in data for secret in secrets):
                errors.append(f'Private device key in {path}')
    html = (root / 'tools/monitor.html').read_text(encoding='utf-8')
    if '<html lang="en">' not in html or '@fair' in html:
        errors.append('UI language or responsive CSS localization error')
    license_text = (root / 'LICENSE').read_text(encoding='utf-8')
    if not license_text.startswith('# PolyForm Noncommercial License 1.0.0'):
        errors.append('The agreed noncommercial license is missing')
    if 'Required Notice:' not in (root / 'NOTICE').read_text(encoding='utf-8'):
        errors.append('The required copyright notice is missing')
    readme = (root / 'README.md').read_text(encoding='utf-8')
    if 'license-MIT' in readme or 'source-available' not in readme:
        errors.append('README licensing summary does not match the selected license')
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        raise SystemExit(1)
    digest = hashlib.sha256('\n'.join(sorted(exported)).encode()).hexdigest()
    print(f'PASS: {len(selected)} selected public files; local links and private-key check passed.')
    print(f'Selection SHA-256: {digest}')


if __name__ == '__main__':
    main()
