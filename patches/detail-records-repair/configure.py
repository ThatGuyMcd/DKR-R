"""Compose the records/LOD repair over the admitted all-player pipeline."""
from pathlib import Path
import argparse
import importlib.util
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-detail-playtest.2'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('build','capsule','log'):p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--cmake',default='cmake');a=p.parse_args()
 spec=importlib.util.spec_from_file_location('repair_release',ROOT/'patches/release-prep/v1.0.7/release.py');release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release)
 release.AUTH=AUTH;release.VERSION=VERSION;release.configure(a.build,a.log,a.cmake,a.capsule)
