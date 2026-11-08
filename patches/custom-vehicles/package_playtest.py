"""Allowlisted car playtest packages; never launches a game/helper/AppImage."""
from pathlib import Path
import argparse
import importlib.util
import json
import re
import sys

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
VERSION = '1.0.9-vehicle-playtest.13'


def baseline():
    path = ROOT / 'patches/release-prep/v1.0.7/package_release.py'
    spec = importlib.util.spec_from_file_location('vehicle_allowlisted_packaging', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.VERSION = VERSION
    def notes(root, label):
        module.copy(AUTH / 'PLAYTEST.txt', root / 'README.txt')
    module.notes = notes
    return module


def main(args):
    module = baseline()
    if args.output.exists(): raise ValueError('Choose a NEW playtest output directory')
    args.output.mkdir(parents=True)
    root, artifact, files = getattr(module, args.platform)(args)
    binary = root / ('libexec/dkr-r/DKR-R-Runtime.exe' if args.platform == 'windows' else 'usr/bin/DKR-R')
    data = binary.read_bytes()
    for marker in (b'reviewed-character-ownership=v1', b'[custom-vehicles][picker]', b'outward-arrows=alpha-blended', b'[custom-vehicles][cpu-roster]', b'seat-anchor-units=studio-to-world', b'[custom-vehicles][native-menu]', b'Custom car metadata differs from the host offer.', b'Verifying custom cars',
                   b'[custom-vehicles][draw]', b'[online-mods][music-residency]', b'selected-music-v1',
                   b'opacity-source=primitive-not-fog', b'[online-mods][asset-addresses]', b'kart-unit-scale=',
                   b'steering-sign=-1', b'wheel-motion=arc-phase64', b'[custom-vehicles][wheel-interpolation]'):
        if marker not in data: raise ValueError('Missing integrated car feature: ' + repr(marker))
    worker=root / ('libexec/dkr-r/DKR-R-ModWorker.exe' if args.platform == 'windows' else 'usr/libexec/dkr-r/DKR-R-ModWorker')
    worker_data=worker.read_bytes()
    # The worker validates/packages content; RuntimeResources and its music/
    # virtual-DMA bus run in the main runtime. Do not demand dead-stripped
    # runtime-only strings (or a launcher-only version macro) from the worker.
    for marker in (b'This build cannot safely prepare the required custom car.',):
        if marker not in worker_data:raise ValueError('Online worker is stale or missing repair: '+repr(marker))
    contracts = json.loads((args.stage / 'vehicle-contracts.json').read_text())
    if set(contracts) != {'77', '80'} or any(not re.fullmatch('[a-f0-9]{64}', v) for v in contracts.values()):
        raise ValueError('Incomplete portable rollback contract')
    # Check the actual linked strings, not only a configure receipt.
    for value in contracts.values():
        if value.encode() not in data: raise ValueError('The linked game has a different rollback contract')
    guest_receipts={}
    for target in ('DKRRecompiledFuncsV77','DKRRecompiledFuncsV80','DKRExperimentalOwnedGameV77','DKRExperimentalOwnedGameV80'):
        swaps=json.loads((args.stage/target/'swaps.json').read_text())
        if not swaps:raise ValueError('Missing final guest vehicle hooks: '+target)
        for swap in swaps:
            if module.sha(Path(swap['old']))!=swap['input_sha256'] or module.sha(Path(swap['new']))!=swap['output_sha256']:
                raise ValueError('A guest input/output changed after configuration')
        guest_receipts[target]=swaps
    native_receipt=json.loads((args.stage/'vehicle-receipt.json').read_text())
    for path,expected in native_receipt['inputs'].items():
        if module.sha(Path(path))!=expected:raise ValueError('A read-only source input changed')
    for path,expected in native_receipt['outputs'].items():
        if module.sha(args.stage/path)!=expected:raise ValueError('A staged source output changed')
    receipt = {
        'schema': 1, 'version': VERSION, 'platform': args.platform, 'arch': args.arch,
        'artifact': {'name': artifact.name, 'bytes': artifact.stat().st_size, 'sha256': module.sha(artifact)},
        'files': files, 'rollback_build_ids': contracts,
        'vehicle_pipeline': native_receipt, 'guest_hooks':guest_receipts,
        'validation': 'Compilation/link and static package/source inspection only; no game, mod, test or AppImage execution.',
        'car_only': True, 'hovercrafts_supported': False, 'planes_supported': False,
        'automatic_updates_signed': False, 'published': False,
    }
    (args.output / 'BUILD-RECEIPT.json').write_text(json.dumps(receipt, indent=2) + '\n')
    (args.output / 'SHA256SUMS.txt').write_text(module.sha(artifact) + '  ' + artifact.name + '\n')
    print('Custom-car playtest packaged without target execution:', artifact)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('platform', choices=('windows', 'linux'))
    for name in ('compiled', 'template', 'output', 'stage', 'trophy'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--arch', choices=('x86_64', 'aarch64'), default='x86_64')
    p.add_argument('--runtime-image', type=Path)
    main(p.parse_args())
