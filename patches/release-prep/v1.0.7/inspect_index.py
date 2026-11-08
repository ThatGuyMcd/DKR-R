"""Inspect frozen release objects as Git will store them; never execute targets."""
from pathlib import Path
import hashlib
import subprocess

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[2]
PREFIX = AUTH.relative_to(ROOT).as_posix() + '/source-capsule-r2/'


def main():
    entries = subprocess.check_output(
        ['git', '-C', str(ROOT), 'ls-files', '--stage', '-z', '--', PREFIX]
    ).decode().split('\0')
    objects = []
    for entry in entries:
        if not entry:
            continue
        metadata, name = entry.split('\t', 1)
        mode, oid, stage = metadata.split()
        if mode != '100644' or stage != '0':
            raise ValueError('Unexpected capsule index entry: ' + name)
        objects.append((oid, name))
    if not objects:
        raise ValueError('Release capsule has not been staged')
    raw = subprocess.check_output(
        ['git', '-C', str(ROOT), 'cat-file', '--batch'],
        input=('\n'.join(oid for oid, _ in objects) + '\n').encode()
    )
    at = 0
    for oid, name in objects:
        end = raw.index(b'\n', at)
        stored_oid, kind, size = raw[at:end].decode().split()
        at = end + 1
        data = raw[at:at + int(size)]
        at += int(size)
        if stored_oid != oid or kind != 'blob' or raw[at:at + 1] != b'\n':
            raise ValueError('Invalid Git object stream')
        at += 1
        path = ROOT / name
        if data != path.read_bytes():
            raise ValueError('Git text conversion changed frozen bytes: ' + name)
        if path.name != 'manifest.json' and hashlib.sha256(data).hexdigest() != path.stem:
            raise ValueError('Content-addressed object hash differs: ' + name)
    if at != len(raw):
        raise ValueError('Unexpected Git object stream suffix')
    print('Git index preserves all', len(objects), 'frozen capsule objects byte-for-byte.')


if __name__ == '__main__':
    main()
