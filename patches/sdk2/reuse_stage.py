"""Read-only receipts/header inspection for optional stable-source build reuse."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(path):
    path=path.resolve(strict=True)
    receipt=json.loads((path/'sdk2-receipt.json').read_text())
    files={p.relative_to(path).as_posix():sha(p) for p in path.rglob('*') if p.is_file() and p.name!='sdk2-receipt.json'}
    if receipt['schema']!=1 or receipt['files']!=files or any(p.is_symlink() for p in path.rglob('*')):
        raise ValueError('SDK source stage no longer matches its immutable receipt: '+str(path))
    return {name:value for name,value in files.items() if name.startswith('native/')}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--stage',type=Path,required=True);a=p.parse_args()
    old,new=checked(a.baseline),checked(a.stage)
    # Directories may be reused only with identical source/header inventories.
    # Any non-translation-unit change forces a normal new-stage rebuild.
    if old.keys()!=new.keys():raise ValueError('Native file inventory differs; omit stable-source reuse')
    changed=[name for name in new if old[name]!=new[name]]
    if any(Path(name).suffix not in ('.c','.cpp','.cc') for name in changed):
        raise ValueError('A header/include/data file changed; omit stable-source reuse')
    print('Immutable stages inspected; unchanged files may reuse old compile paths. Changed translation units:')
    for name in sorted(changed):print('  '+name)


if __name__=='__main__':main()
