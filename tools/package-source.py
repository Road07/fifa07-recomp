"""Export the complete project source tree, with an explicit file allowlist.

Game assets, downloaded toolchains, SDK binaries, saves and raw captures are
never traversed. SDK source/dependencies are obtained from dependencies.lock.json.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--name', default='fifa07-github-source')
args = parser.parse_args()
if not args.name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.name):
    raise SystemExit('Use only letters, digits, hyphens and underscores for --name')
destination = root / 'dist' / args.name
archive = destination.with_suffix('.zip')
if destination.exists() or archive.exists():
    raise SystemExit('Export already exists; use a new --name to preserve the previous export.')

files = set(root.glob('*.md'))
files.update(root / name for name in ('.gitignore', '.gitattributes', 'LICENSE', 'requirements-dev.txt'))
for directory in ('port/src', 'port/tests', 'tools/vmx_harness', 'tools/xma_reference', 'tools/diagnostics', 'LICENSES', 'docs/validation'):
    for path in (root / directory).rglob('*'):
        if path.is_file() and path.suffix not in ('.pyc', '.obj', '.exe', '.dll', '.pdb'):
            files.add(path)
for path in (root / 'tools').iterdir():
    if path.is_file() and path.suffix in ('.py', '.ps1', '.cmd', '.json') and path.name != 'rexglue-release.json':
        files.add(path)
files.update(root / path for path in ('port/CMakeLists.txt', 'port/CMakePresets.json',
                                    'port/fifa07_manifest.toml', 'port/generated/rexglue.cmake'))
for path in (root / 'port/generated/default').iterdir():
    if path.suffix in ('.cpp', '.h', '.cmake', '.json'):
        files.add(path)

forbidden = {'.exe', '.dll', '.obj', '.pdb', '.lib', '.iso', '.xex', '.big', '.bh',
             '.wmv', '.vp6', '.wav', '.f32be', '.7z', '.zip', '.bin'}
records = []
for source in sorted(files):
    relative = source.relative_to(root)
    if source.is_symlink() or source.suffix.lower() in forbidden:
        raise SystemExit(f'Unexpected binary/link in source allowlist: {relative}')
    if source.stat().st_size >= 100 * 1024 * 1024:
        raise SystemExit(f'File exceeds GitHub source-file size: {relative}')
    data = source.read_bytes()
    # This is a publication check, not an automatic rewrite of source code.
    private_tokens = (b'C:' + bytes([92]) + b'Users' + bytes([92]),
                      b'C:' + b'/Users/', b'codex' + b'-runtimes')
    if any(token in data for token in private_tokens):
        raise SystemExit(f'Personal machine path found: {relative}')
    records.append({'path': relative.as_posix(), 'bytes': len(data),
                    'sha256': hashlib.sha256(data).hexdigest()})

for record in records:
    target = destination / record['path']
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / record['path'], target)

manifest = {'project': 'FIFA 07 ReXGlue port', 'schema': 1, 'files': records,
            'fileCount': len(records), 'sourceBytes': sum(item['bytes'] for item in records),
            'gameAssetsIncluded': False, 'generatedCppIncluded': True,
            'sdkSource': 'Pinned upstream source via tools/setup-dependencies.ps1'}
(destination / 'SOURCE-MANIFEST.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
    for source in sorted(destination.rglob('*')):
        if source.is_file():
            output.write(source, Path(destination.name) / source.relative_to(destination))
with zipfile.ZipFile(archive) as output:
    corrupt = output.testzip()
    if corrupt:
        raise SystemExit(f'Archive verification failed: {corrupt}')
print(json.dumps({'folder': str(destination), 'zip': str(archive),
                  'files': len(records), 'sourceBytes': manifest['sourceBytes'],
                  'zipBytes': archive.stat().st_size,
                  'zipSha256': hashlib.sha256(archive.read_bytes()).hexdigest()}, indent=2))
