"""Compose the final first-party party runtime in an external build directory."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

sys.dont_write_bytecode = True
import transforms

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def compose(rows, output):
    if len(rows) != 1 or rows[0]['target'] != 'DKRPortGame' or Path(rows[0]['path']).name != 'party_runtime.cpp':
        raise ValueError('Expected the final first-party party runtime only')
    source = Path(rows[0]['path'])
    if any(p.lower() in ('extern', 'recompiledfuncs', 'recompiledpatches') for p in source.parts):
        raise ValueError('Protected source is not a release repair input')
    raw = source.read_bytes()
    text = transforms.runtime(raw.decode().replace('\r\n', '\n'))

    def include(m):
        path = Path(m[1])
        if not path.is_absolute():
            path = source.parent / path
        return '#include "' + path.resolve().as_posix() + '"' if path.is_file() else m[0]

    text = re.sub(r'^#include "([^"\n]+)"', include, text, flags=re.M)
    data = text.encode()
    return [{**rows[0], 'new': str(output / source.name), 'input_sha256': sha(raw), 'output_sha256': sha(data)}], data


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    output = a.output.resolve()
    if output.is_relative_to(ROOT):
        raise ValueError('Generated release repair must stay outside the checkout')
    rows, data = compose(json.loads(a.inputs.read_text()), output)
    write(output / 'party_runtime.cpp', data)
    write(output / 'swaps.json', (json.dumps(rows, indent=2) + '\n').encode())
    print('Race release repair staged above retained lobby stability fixes.')
