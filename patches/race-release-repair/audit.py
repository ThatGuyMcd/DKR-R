"""Verify the composed fix, peer contracts and every retained online audit."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import sys
import types

sys.dont_write_bytecode = True
import pipeline

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
VERSION = '1.0.9-stability-playtest.2'


def need(ok, reason):
    if not ok:
        raise ValueError(reason)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inspect(build, binary, baseline, accepted):
    stage = build / 'generated/race-release-repair'
    rows, data = pipeline.compose(json.loads((stage / 'inputs.json').read_text()), stage)
    need(rows == json.loads((stage / 'swaps.json').read_text()), 'Release repair receipt differs')
    need((stage / 'party_runtime.cpp').read_bytes() == data, 'Stale release repair source')
    need(not stage.resolve().is_relative_to(ROOT), 'Generated source escaped external build')
    project = build / 'DKRPortGame.vcxproj'
    if project.exists():
        compiled = {Path(v).resolve() for v in re.findall(r'<ClCompile Include="([^"]+)"', project.read_text(encoding='utf-8-sig'))}
    else:
        compiled = {Path(m[1]).resolve() for line in (build / 'build.ninja').read_text().splitlines()
                    if line.startswith('build CMakeFiles/DKRPortGame.dir/')
                    if (m := re.search(r': [^ ]+ (\S+)', line))}
    need(Path(rows[0]['new']).resolve() in compiled, 'Repaired runtime is not compiled')
    need(Path(rows[0]['path']).resolve() not in compiled, 'Superseded runtime is still compiled')
    text = data.decode()
    for marker in ('activated_release=std::pair{at,controls}', 'at==activated_release->first',
                   'generation<=activated_release->second', 'if(generation<controls)return;',
                   'at.frame<cursor.frame&&generation<=controls', 'state.host!=host', 'if(!server()&&!from_host)return;',
                   'phase==Phase::SceneReady&&server()&&all_votes(frozen)',
                   'if(m.role==Role::Racer)ids.insert(m.id)', '[party][scene] prepared'):
        need(marker in text, 'Release/authority safeguard missing: ' + marker)
    need('go&&at.epoch==cursor.epoch&&at.frame<=cursor.frame' not in text, 'Loaded cursor still rejects first release')
    need('go&&phase==Phase::Running&&at==cursor' not in text, 'Observer admission still rejects first release')
    ledger = 'party-activated-release-v1\n'
    for name in ('pipeline.py', 'transforms.py', 'ReleaseOverlay.cmake'):
        ledger += name + ':' + pipeline.sha((AUTH / name).read_bytes()) + '\n'
    contract = hashlib.sha256(ledger.encode()).hexdigest()
    need((stage / 'contract.txt').read_text().strip() == contract, 'Release repair ledger differs')
    prior_ids = json.loads((stage.parent / 'lobby-stability/contracts.json').read_text())
    contracts = {k: hashlib.sha256((v + '\n' + ledger).encode()).hexdigest() for k, v in prior_ids.items()}
    need(set(contracts) == {'77', '80'}, 'Both ROM revision contracts are required')
    need(contracts == json.loads((stage / 'contracts.json').read_text()), 'Release peer ID derivation differs')

    # Retain the old audits' source recomposition, contracts and final compiler
    # graph checks. Only linked-final-ID expectations advance to this layer.
    saved = {n: sys.modules.get(n) for n in ('pipeline', 'transforms')}
    try:
        directory = ROOT / 'patches/lobby-stability'
        sys.modules['transforms'] = load('release_stability_transforms', directory / 'transforms.py')
        sys.modules['pipeline'] = load('release_stability_pipeline', directory / 'pipeline.py')
        source = (directory / 'audit.py').read_text()
        before = "for value in contracts.values():\n            need(value.encode() in data, 'Linked final peer ID missing')"
        need(source.count(before) == 1, 'Retained linked identity check changed')
        source = source.replace(before, "for value in final_contracts.values():\n            need(value.encode() in data, 'Linked final peer ID missing')", 1)
        before = "stage.parent / 'lobby-stability/contracts.json'"
        need(source.count(before) == 1, 'Retained music identity routing changed')
        source = source.replace(before, "stage.parent / 'race-release-repair/contracts.json'", 1)
        prior = types.ModuleType('release_retained_stability_audit')
        prior.__file__ = str(directory / 'audit.py')
        prior.__dict__['final_contracts'] = contracts
        exec(compile(source, prior.__file__, 'exec'), prior.__dict__)
        prior.VERSION = VERSION
        retained = prior.inspect(build, binary, baseline, accepted)
    finally:
        for name, value in saved.items():
            if value is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = value
    if binary:
        linked = binary.read_bytes()
        for marker in (VERSION, '[party][release-receive] accepted', '[party][release-receive] ignored=duplicate',
                       '[party][release-receive] ignored=stale', '[party][scene] prepared', *contracts.values()):
            need(marker.encode() in linked, 'Linked release repair missing: ' + marker)
    return {'schema': 1, 'version': VERSION, 'release_contract': contract,
            'rollback_build_ids': contracts, 'swaps': rows, 'retained': retained,
            'unchanged_authored_files': retained['unchanged_authored_files'],
            'validation': 'Exact source recomposition, compiler binding, peer contracts and retained online/security checks. Host-only handler regression tests recorded separately; physical multiplayer acceptance pending.'}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'binary', 'baseline', 'accepted', 'output'):
        p.add_argument('--' + name, type=Path, required=name != 'binary')
    a = p.parse_args()
    result = inspect(a.build, a.binary, a.baseline, a.accepted)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print('Race release repair and every retained online layer verified.')
