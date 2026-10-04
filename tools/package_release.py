"""Build and verify the ready-to-run Hide archive. Python is packaging-only."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--version', default='v1.0.0-beta.3')
args = parser.parse_args()
release = root / 'release'
release.mkdir(exist_ok=True)
archive = release / 'Hide-windows-x64.zip'
required = ['Hide.exe', 'Hide.ico', 'Start-Hide.cmd', 'Recover-Cursor.cmd',
            '启动.cmd', '恢复鼠标.cmd', 'README.md', 'CHANGELOG.md']
selected = [root / name for name in required]
for path in selected:
    if not path.is_file():
        raise FileNotFoundError(path)
for folder in ['docs']:
    selected.extend(p for p in (root / folder).rglob('*') if p.is_file())
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for path in sorted(selected):
        z.write(path, 'Hide/' + path.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    names = z.namelist()
    assert len(names) == len(set(names))
    assert all(n.startswith('Hide/') and '..' not in n.split('/') and '/state/' not in n for n in names)
    assert z.read('Hide/Hide.exe') == (root / 'Hide.exe').read_bytes()
    assert z.read('Hide/Start-Hide.cmd').decode('ascii').find('Hide.exe') >= 0
    assert z.read('Hide/Recover-Cursor.cmd').decode('ascii').find('--recover') >= 0
    assert not any(n.startswith('Hide/resources/') or n.startswith('Hide/evidence/') for n in names)
sha = hashlib.sha256(archive.read_bytes()).hexdigest()
(release / 'SHA256SUMS.txt').write_text(sha + '  ' + archive.name + '\n', encoding='ascii')
summary = {'version': args.version, 'asset': archive.name, 'bytes': archive.stat().st_size,
           'files': len(selected), 'sha256': sha,
           'exe_sha256': hashlib.sha256((root / 'Hide.exe').read_bytes()).hexdigest(), 'passed': True}
(release / 'package-verification.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary))
