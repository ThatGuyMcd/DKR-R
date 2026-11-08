"""Configure the v1.0.9 development UI over the verified release composition."""
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
    path = ROOT / 'patches/release-prep/v1.0.7/release.py'
    spec = importlib.util.spec_from_file_location('release109_baseline', path)
    release = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(release)
    release.AUTH = AUTH
    release.VERSION = '1.0.9-dev'
    release.configure(args.build, args.log, args.cmake, args.capsule)
