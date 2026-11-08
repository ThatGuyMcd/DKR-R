"""Package matching complete stability test builds without running the game."""
from pathlib import Path
import argparse
import importlib.util
import json
import sys

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
VERSION = '1.0.9-stability-playtest.1'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=('windows', 'linux'))
    for name in ('compiled', 'template', 'output', 'build', 'trophy', 'baseline', 'accepted'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--arch', choices=('x86_64', 'aarch64'), default='x86_64')
    parser.add_argument('--runtime-image', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Choose a fresh package folder')
    binary = args.compiled / ('libexec/dkr-r/DKR-R-Runtime.exe' if args.platform == 'windows' else 'DKR-R')
    checked = load('stability_package_audit', AUTH / 'audit.py').inspect(args.build, binary, args.baseline, args.accepted)
    base = load('stability_allowlist', ROOT / 'patches/release-prep/v1.0.7/package_release.py')
    base.VERSION = VERSION

    def notes(root, label):
        base.copy(AUTH / 'PLAYTEST.txt', root / 'README.txt')
        base.copy(AUTH / 'PLAYTEST.txt', root / 'RELEASE-NOTES.txt')

    base.notes = notes
    args.output.mkdir(parents=True)
    package_root, artifact, files = getattr(base, args.platform)(args)
    receipt = {'schema': 1, 'version': VERSION, 'platform': args.platform, 'arch': args.arch,
               'artifact': {'name': artifact.name, 'sha256': base.sha(artifact), 'bytes': artifact.stat().st_size},
               'files': files, 'audit': checked, 'published': False, 'automatic_updates_signed': False}
    (args.output / 'BUILD-RECEIPT.json').write_text(json.dumps(receipt, indent=2) + '\n')
    (args.output / 'SHA256SUMS.txt').write_text(base.sha(artifact) + '  ' + artifact.name + '\n')
    print('Stability playtest packaged:', artifact)
