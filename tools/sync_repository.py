"""Export project sources and public documentation; never export device data."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT_FILES=['README.md','CMakeLists.txt','partitions.csv','sdkconfig.defaults',
            'dependencies.lock','.gitignore','.gitattributes','.editorconfig',
            'requirements-dev.txt','THIRD_PARTY_NOTICES.md','LICENSE',
            'CONTRIBUTING.md','CODE_OF_CONDUCT.md','SECURITY.md','CHANGELOG.md',
            'NOTICE','COMMERCIAL_LICENSE.md']
DOC_FILES=['SOURCES.md','ARCHITECTURE.md','API.md','DEVELOPMENT.md','CALIBRATION.md',
           'HARDWARE.md','VALIDATION.md','MEDIA.md','LICENSING.md']
# Only these owner-approved photos and authored illustrations may leave the workspace.
MEDIA_FILES=['flowcount-hero.svg','architecture.svg','hardware.svg','counting.svg',
             'ui-overview.jpg','prototype-front-retouched.png','prototype-rear-retouched.png',
             'prototype-front-original.jpeg','prototype-rear-original.jpeg','deai-isotype.png']
COMMUNITY_FILES=['.github/workflows/ci.yml','.github/ISSUE_TEMPLATE/bug_report.yml',
                 '.github/ISSUE_TEMPLATE/feature_request.yml','.github/ISSUE_TEMPLATE/config.yml',
                 '.github/pull_request_template.md']
SOURCE_SUFFIXES={'.c','.h','.cpp','.hpp','.py','.ps1','.html','.js','.json','.yml'}

def selected_files(source):
    paths=[Path(name) for name in ROOT_FILES]
    paths += [Path('docs')/name for name in DOC_FILES]
    paths += [Path('docs/media')/name for name in MEDIA_FILES]
    paths += [Path(name) for name in COMMUNITY_FILES]
    paths += [Path('examples/network-access.example.json')]
    for folder in ['main','components','config','tools','tests']:
        for p in (source/folder).rglob('*'):
            if not p.is_file() or any(part in {'__pycache__','host-build','.venv'} for part in p.parts):
                continue
            if p.is_symlink():
                raise ValueError(f'Symlinks are not exported: {p.name}')
            if p.suffix in SOURCE_SUFFIXES or p.name in {'CMakeLists.txt','Kconfig.projbuild','LICENSE'}:
                paths.append(p.relative_to(source))
    for path in paths:
        if not (source/path).is_file():
            raise FileNotFoundError(f'Required file missing: {path}')
        if (source/path).is_symlink():
            raise ValueError(f'Symlinks are not exported: {path}')
    return sorted(set(paths))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--zip',action='store_true')
    args=parser.parse_args()
    source=Path(__file__).resolve().parents[1]
    destination=args.output.resolve()
    if destination==source or source in destination.parents or destination in source.parents:
        parser.error('Destination must be independent of the source project.')
    if destination.exists() and any(destination.iterdir()):
        project=destination/'CMakeLists.txt'
        if not project.is_file() or 'project(people_counter)' not in project.read_text(encoding='utf-8'):
            parser.error('Destination contains another project. Use an empty directory.')
    paths=selected_files(source)
    # The hardware key is never a legitimate source-code or documentation value.
    private=source/'.private/network-access.json'
    if private.exists():
        key=json.loads(private.read_text(encoding='utf-8-sig')).get('ap_password','')
        if key and any(key.encode() in (source/p).read_bytes() for p in paths):
            raise ValueError('A selected file contains the private device key. Export blocked.')
    destination.mkdir(parents=True,exist_ok=True)
    changed=0
    for path in paths:
        target=destination/path
        if destination not in target.resolve().parents:
            raise ValueError(f'Path outside destination: {path}')
        target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() or target.read_bytes()!=(source/path).read_bytes():
            shutil.copy2(source/path,target);changed+=1
    # Remove only known, superseded public documentation from older exports.
    for name in ['FUENTES.md','ARQUITECTURA.md','DESARROLLO.md','CALIBRACION.md']:
        obsolete=destination/'docs'/name
        if obsolete.is_file():
            obsolete.unlink()
    manifest={'files':{p.as_posix():hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths}}
    (destination/'PUBLICATION_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(f'Repository updated: {len(paths)} files, {changed} changed.')
    if args.zip:
        archive=destination.with_suffix('.zip')
        temporary=archive.with_suffix('.zip.tmp')
        with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as output:
            for path in paths:
                output.write(destination/path,(Path(destination.name)/path).as_posix())
            output.write(destination/'PUBLICATION_MANIFEST.json',
                         (Path(destination.name)/'PUBLICATION_MANIFEST.json').as_posix())
        with zipfile.ZipFile(temporary) as output:
            assert output.testzip() is None,'Invalid ZIP'
        temporary.replace(archive)
        print(f'ZIP updated: {archive.name} ({archive.stat().st_size} bytes).')
        print('SHA-256: '+hashlib.sha256(archive.read_bytes()).hexdigest())

if __name__=='__main__':
    main()
