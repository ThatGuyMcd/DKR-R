"""Configure the isolated v1.0.9 custom-car playtest composition."""
from pathlib import Path
import argparse
import importlib.util
import sys

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--capsule', type=Path, required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--cmake', default='cmake')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('release109_vehicle_baseline', ROOT / 'patches/release-prep/v1.0.7/release.py')
    release = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(release)
    release.AUTH = AUTH
    release.VERSION = '1.0.9-vehicle-playtest.13'
    release.configure(args.build, args.log, args.cmake, args.capsule)
