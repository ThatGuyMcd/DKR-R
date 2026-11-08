"""Verify the complete accepted playtest.4 source chain at the stable version."""
from pathlib import Path
import argparse, importlib.util, json, sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION='1.0.10'

def inspect(build,binary,baseline,accepted):
    previous=ROOT/'patches/spectator-admission-repair'
    saved=sys.path[:];sys.path.insert(0,str(previous))
    try:
        s=importlib.util.spec_from_file_location('release110_observer_audit',previous/'audit.py')
        m=importlib.util.module_from_spec(s);s.loader.exec_module(m);m.VERSION=VERSION
        result=m.inspect(build,binary,baseline,accepted)
    finally:sys.path[:]=saved
    if (ROOT/'VERSION').read_text().strip()!=VERSION:raise ValueError('Root release version differs')
    if binary:
        data=binary.read_bytes()
        for old in ('1.0.9-stability-playtest.','1.0.9-music-playtest.','1.0.9-public-lobbies-playtest.'):
            if old.encode() in data or old.encode('utf-16le') in data:raise ValueError('Old playtest version linked')
    result['label']='v1.0.10 Release Build'
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','binary','baseline','accepted','output'):p.add_argument('--'+n,type=Path,required=n!='binary')
    a=p.parse_args();r=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print('Stable version and full playtest.4 source chain verified')
