"""Configure lobby-link repair playtest. Compile/static validation only."""
from pathlib import Path
import argparse,importlib.util,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.9'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('build','capsule','log'):p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--cmake',default='cmake');a=p.parse_args()
 spec=importlib.util.spec_from_file_location('lobby_repair_release',ROOT/'patches/release-prep/v1.0.7/release.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 m.AUTH=AUTH;m.VERSION=VERSION;m.configure(a.build,a.log,a.cmake,a.capsule)
