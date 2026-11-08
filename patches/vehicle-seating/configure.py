"""Configure fixed-reference car support; never run a game, mod or target."""
from pathlib import Path
import argparse
import importlib.util
import sys

sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-vehicle-playtest.18'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','capsule','log'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--cmake',default='cmake')
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('fixed_seats_configure',ROOT/'patches/release-prep/v1.0.7/release.py')
    release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release)
    release.AUTH=AUTH;release.VERSION=VERSION
    release.configure(args.build,args.log,args.cmake,args.capsule)
