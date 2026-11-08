"""Export committed first-party text changes for the authenticated GitHub connector.

Read-only Git input; output stays outside the checkout. Never exports untracked
caches, working files, credentials, dependencies or generated guest code.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[4]
CHUNK = 24000


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def export(base, output):
    if output.resolve().is_relative_to(ROOT):
        raise ValueError('Keep publication payloads outside the checkout')
    rows = git('diff', '--raw', '-z', '--no-abbrev', '--no-renames', base, 'HEAD').split(b'\0')
    elements = []
    for index in range(0, len(rows) - 1, 2):
        header = rows[index].decode().split()
        name = rows[index + 1].decode('utf-8')
        if name.startswith(('extern/', 'runtime-recomp/RecompiledFuncs/', 'runtime-recomp/RecompiledPatches/', '.wrangler/')):
            raise ValueError('Protected/local publication path: ' + name)
        mode, blob, status = header[1], header[3], header[4]
        if status == 'D':
            elements.append({'path': name, 'mode': header[0][1:], 'type': 'blob', 'sha': None})
        else:
            if mode not in ('100644', '100755'):
                raise ValueError('Unexpected file type: ' + name)
            content = git('cat-file', 'blob', blob).decode('utf-8')
            if '\0' in content:
                raise ValueError('Binary source needs explicit blob publication: ' + name)
            elements.append({'path': name, 'mode': mode, 'type': 'blob', 'content': content})
    value = {'base_commit': git('rev-parse', base).decode().strip(),
             'base_tree': git('show', '-s', '--format=%T', base).decode().strip(),
             'source_commit': git('rev-parse', 'HEAD').decode().strip(),
             'source_tree': git('show', '-s', '--format=%T', 'HEAD').decode().strip(),
             'tree_elements': elements}
    data = json.dumps(value, ensure_ascii=True, separators=(',', ':')).encode('utf-8')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as stream:
        stream.write(data)
    print(json.dumps({'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                      'chunks': (len(data) + CHUNK - 1) // CHUNK,
                      'changed_files': len(elements), 'source_commit': value['source_commit'],
                      'source_tree': value['source_tree'], 'base_commit': value['base_commit']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('export', 'read'))
    parser.add_argument('--base', default='origin/main')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--chunk', type=int, default=0)
    args = parser.parse_args()
    if args.action == 'export':
        export(args.base, args.output)
    else:
        if args.chunk < 0:
            parser.error('Chunk must be non-negative')
        with args.output.open('rb') as stream:
            stream.seek(args.chunk * CHUNK)
            print(base64.b64encode(stream.read(CHUNK)).decode('ascii'))
