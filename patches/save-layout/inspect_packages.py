"""Static archive and binary inspection only. Never launch delivered code."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('task', type=Path, help='WSL-mounted pipeline output root')
args = parser.parse_args()
task = args.task
release = task/'release'
version = '1.0.7+beta.5'
sha = lambda data: hashlib.sha256(data).hexdigest()
patterns = [r'Owned v77 rollback contract: ([0-9a-f]{64})',
            r'Owned v80 rollback contract: ([0-9a-f]{64})',
            r'Owned rollback contract: ([0-9a-f]{64})']


def contracts(path):
    text = path.read_text(encoding='utf-8-sig')
    return [re.findall(pattern, text)[-1] for pattern in patterns]


expected = contracts(task/'windows-version-configure.log')
if expected != contracts(task/'linux-configure.log'):
    raise ValueError('Windows/Linux rollback contracts differ')
report = {'version': version, 'inspection': 'Static only; no game or runtime tests',
          'contracts': dict(zip(('v77', 'v80', 'combined'), expected)), 'packages': {}}
denied = {'.z64', '.v64', '.n64', '.eep', '.mpk', '.sra', '.fla', '.dmp', '.log', '.pem', '.key'}


def paths(names):
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or path.suffix.lower() in denied:
            raise ValueError('Unsafe/unwanted archive path: '+name)
        if any(part.lower() in {'saves', 'crash-dumps', '.git'} for part in path.parts):
            raise ValueError('Private state in archive: '+name)


def binary(data, pe=False):
    if pe:
        at = struct.unpack_from('<I', data, 0x3c)[0]
        if data[:2] != b'MZ' or data[at:at+4] != b'PE\0\0' or struct.unpack_from('<H', data, at+4)[0] != 0x8664:
            raise ValueError('Wrong Windows executable architecture')
    elif data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 62:
        raise ValueError('Wrong Linux executable architecture')
    for marker in (version.encode(), b'[save-layout][fatal]', *[x.encode() for x in expected[:2]]):
        if marker not in data:
            raise ValueError('Missing version/save fix/rollback contract: '+repr(marker))
    for marker in (b'DKR_REPLAY_CAPTURE_FILE', b'DKR_UPDATE_TEST_VERIFIER', b'invalid-local-visual-heaps'):
        if marker in data:
            raise ValueError('Unexpected qualification/withdrawn visual marker')
    return {'runtime_bytes': len(data), 'runtime_sha256': sha(data), 'architecture': 'x86_64'}


win = release/f'DKR-R-{version}-Windows-x64.zip'
with zipfile.ZipFile(win) as archive:
    if archive.testzip() is not None:
        raise ValueError('ZIP CRC failure')
    names = archive.namelist()
    paths(names)
    result = binary(archive.read(next(n for n in names if n.endswith('/libexec/dkr-r/DKR-R-Runtime.exe'))), True)
    if not any(n.endswith('/SAVE-LAYOUT-NOTES.md') for n in names):
        raise ValueError('Missing save-fix notes')
    result.update(archive_bytes=win.stat().st_size, archive_sha256=sha(win.read_bytes()))
    report['packages'][win.name] = result

app = release/f'DKR-R-{version}-Linux-x86_64.AppImage'
data = app.read_bytes()
at = data.find(b'hsqs')
while at >= 0:
    major, minor = struct.unpack_from('<HH', data, at+28)
    block = struct.unpack_from('<I', data, at+12)[0]
    if (major, minor) == (4, 0) and block >= 4096 and not block & (block-1):
        break
    at = data.find(b'hsqs', at+4)
if at <= 0:
    raise ValueError('Missing AppImage squashfs')


def read(name):
    return subprocess.run(['unsquashfs', '-o', str(at), '-cat', str(app), name], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


listing = subprocess.run(['unsquashfs', '-o', str(at), '-ll', str(app)], check=True,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.decode()
paths(line.split('squashfs-root/', 1)[1].split(' -> ', 1)[0] for line in listing.splitlines() if 'squashfs-root/' in line)
result = binary(read('usr/bin/DKR-R'))
result.update(archive_bytes=len(data), archive_sha256=sha(data), squashfs_offset=at)
report['packages'][app.name] = result
print(json.dumps(report, indent=2))
