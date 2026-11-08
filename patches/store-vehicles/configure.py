"""Configure Vehicle Store playtests using the existing isolated release pipeline."""
from pathlib import Path
import argparse
import importlib.util
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.5'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('build','capsule','log'):p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--cmake',default='cmake');a=p.parse_args()
 s=importlib.util.spec_from_file_location('store_vehicle_release',ROOT/'patches/release-prep/v1.0.7/release.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
 m.AUTH=AUTH;m.VERSION=VERSION;m.configure(a.build,a.log,a.cmake,a.capsule)
