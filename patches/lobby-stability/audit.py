"""Recompose the new layer and retain every previous repair/security audit."""
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
VERSION = '1.0.9-stability-playtest.1'


def need(ok, message):
    if not ok:
        raise ValueError(message)


def plain_load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inspect(build, binary, baseline, accepted):
    stage = build / 'generated/lobby-stability'
    adapter = json.loads((stage / 'task-adapter.json').read_text())
    task_source = Path(adapter['path'])
    swaps, includes, outputs = pipeline.compose(json.loads((stage / 'inputs.json').read_text()), stage, task_source)
    need(swaps == json.loads((stage / 'swaps.json').read_text()), 'Stability source receipts differ')
    need(includes == json.loads((stage / 'includes.json').read_text()), 'Stability include receipts differ')
    for name, data in outputs.items():
        need(not (stage / name).resolve().is_relative_to(ROOT), 'Output escaped external build')
        need((stage / name).read_bytes() == data, 'Stale stability output: ' + name)
    graph = {}
    for receipt in stage.parent.glob('*/swaps.json'):
        for row in json.loads(receipt.read_text()):
            if isinstance(row, dict) and all(k in row for k in ('target', 'path', 'new')):
                graph[(row['target'], Path(row['path']).resolve())] = Path(row['new']).resolve()

    def resolve_binding(target, path):
        seen = set()
        while (target, path) in graph:
            need(path not in seen, 'Compiler substitution cycle')
            seen.add(path)
            path = graph[(target, path)]
        return path

    def sources(target):
        project = build / (target + '.vcxproj')
        if not project.exists() and target=='librecomp':
            project=build/'n64-modern-runtime/librecomp/librecomp.vcxproj'
        if project.exists():
            return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"', project.read_text(encoding='utf-8-sig'))}
        return {Path(m[1]).resolve() for line in (build / 'build.ninja').read_text().splitlines()
                if re.match(r'build (?:[^ ]*/)?CMakeFiles/'+target+r'[.]dir/',line) if (m := re.search(r': [^ ]+ (\S+)', line))}

    bindings = []
    for row in swaps:
        compiled = sources(row['target'])
        need(Path(row['new']).resolve() in compiled, 'New stability source is not compiled')
        need(Path(row['path']).resolve() not in compiled, 'Superseded stability source is compiled')
        bindings.append({'target': row['target'], 'source': row['new']})
    need(task_source.resolve() not in sources('librecomp') and (stage/'task_submission.cpp').resolve() in sources('librecomp'), 'Owned SDK task adapter binding missing')
    need(adapter['input_sha256']==pipeline.sha(task_source.read_bytes()), 'Delegated SDK source changed')
    need(adapter['output_sha256']==pipeline.sha(outputs['task_submission.cpp']), 'Owned SDK task adapter receipt differs')
    bindings.append({'target':'librecomp','source':adapter['new']})
    adapter_project=build/'n64-modern-runtime/librecomp/librecomp.vcxproj'
    if adapter_project.exists():
        import xml.etree.ElementTree as ET
        ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
        nodes=ET.parse(adapter_project).getroot().findall('.//m:ClCompile',ns)
        node=next(n for n in nodes if Path(n.attrib.get('Include','')).name=='task_submission.cpp')
        need(any(n.tag.endswith('ExceptionHandling') and n.text=='SyncCThrow' for n in node),'Owned adapter does not enable C ABI exception unwinding')
        need(any(n.tag.endswith('AdditionalOptions') and '/EHc-' in (n.text or '') for n in node),'Owned adapter may skip scoped exception cleanup')
    sync = outputs['online_mod_sync.cpp'].decode()
    profile = outputs['online_mod_profile.cpp'].decode()
    ui = outputs['runtime_online_mods_ui.inl'].decode()
    for marker in ('MaxPending', 'download_slots', 'if(queued&&message->operation==Operation::PayloadChunk)break;',
                   'gaps.exhausted()', 'deadlines.expired', 'if(!runtime)throw Error', 'link_.verify_local',
                   'download.prepared(profile->digest)', 'message.manifest!=download.digest()', 'stage_heartbeat'):
        need(marker in sync, 'Synchronization safeguard missing: ' + marker)
    need(sync.index('ready.stage="Host mods verified') < sync.index('retain_profile('), 'Offline retention blocks admission')
    need(sync.index('download.prepared(profile->digest)') < sync.index('Message proof{Operation::Verified'), 'Proof precedes resource preparation')
    for marker in ('validate_profile(*out);cancelled(stop)', 'ensure_worker()', 'Cannot safely refresh the stale native course cache.',
                   'validate_profile(prepared)', 'prepared-payloads', 'quarantined-payloads'):
        need(marker in profile, 'Profile verification/repair guard missing: ' + marker)
    need('COPY JOIN DIAGNOSTICS' in ui and 'RETRY JOIN' in ui and 'p->join_watch=g_stability_retry_watch' in ui, 'Recovery UI/role missing')
    need('RejectionScope scope(&session_,request,reason)' in outputs['online_mod_sync_link.hpp'].decode(), 'Rejection reason is discarded')
    need('stalled.type' in outputs['game_main.cpp'].decode(), 'Task type diagnostic missing')
    need('return dkr::lobby_stability::bootstrap_audio_worker_allowed()' in outputs['game_main.cpp'].decode(), 'Scoped bootstrap audio routing missing')
    need('#include DKR_STABILITY_ORIGINAL_SP' in outputs['task_submission.cpp'].decode(), 'SDK source is not delegated unchanged')
    need('session_configuration.audio_callbacks.queue_samples=+[](std::int16_t*,std::size_t){};' in outputs['game_main.cpp'].decode(), 'Bootstrap audio output is not muted')

    ledger = 'bounded-mod-admission-v1\n'
    paths = [AUTH / 'pipeline.py', AUTH / 'transforms.py', AUTH / 'ReleaseOverlay.cmake', *sorted((AUTH / 'native').iterdir())]
    for path in paths:
        ledger += path.name + ':' + hashlib.sha256(path.read_bytes()).hexdigest() + '\n'
    ledger += 'delegated-sdk-task-source:'+pipeline.sha(task_source.read_bytes())+'\n'
    contract = hashlib.sha256(ledger.encode()).hexdigest()
    need((stage / 'contract.txt').read_text().strip() == contract, 'Stability contract does not match authoring')
    need((stage / 'build_contract.hpp').read_text() == '#pragma once\n#define DKR_LOBBY_STABILITY_CONTRACT "'+contract+'"\n', 'Build identity header differs')
    original = json.loads((stage.parent / 'music-preview-repair/contracts.json').read_text())
    contracts = {key: hashlib.sha256((value + '\n' + ledger).encode()).hexdigest() for key, value in original.items()}
    need(contracts == json.loads((stage / 'contracts.json').read_text()), 'Peer build ID derivation differs')

    def load(name, path):
        if path.name != 'audit.py':
            return plain_load(name, path)
        text = path.read_text()
        if path.parent.name == 'music-preview-repair':
            old = "Path(row['new']).resolve() in compiled"
            need(text.count(old) == 1, 'Retained music compiler guard changed')
            text = text.replace(old, "resolve_binding(row['target'],Path(row['new']).resolve()) in compiled", 1)
            old = "contracts = json.loads((stage / 'contracts.json').read_text())"
            need(text.count(old) == 1, 'Retained music contract guard changed')
            text = text.replace(old, "contracts = json.loads((stage.parent / 'lobby-stability/contracts.json').read_text())", 1)
        elif path.parent.name == 'online-directory':
            old = "Path(row['new']).resolve() in sources(row['target'])"
            need(text.count(old) == 1, 'Retained directory compiler guard changed')
            text = text.replace(old, "resolve_binding(row['target'],Path(row['new']).resolve()) in sources(row['target'])", 1)
        module = types.ModuleType(name)
        module.__file__ = str(path)
        module.__dict__['resolve_binding'] = resolve_binding
        exec(compile(text, str(path), 'exec'), module.__dict__)
        if hasattr(module, 'load'):
            module.load = load
        return module

    saved = {n: sys.modules.get(n) for n in ('pipeline', 'transforms')}
    try:
        directory = ROOT / 'patches/music-preview-repair'
        sys.modules['pipeline'] = load('stability_music_pipeline', directory / 'pipeline.py')
        prior = load('stability_music_audit', directory / 'audit.py')
        prior.VERSION = VERSION
        retained = prior.inspect(build, binary, baseline, accepted)
    finally:
        for name, value in saved.items():
            if value is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = value
    if binary:
        data = binary.read_bytes()
        for marker in (VERSION, contract, 'COPY JOIN DIAGNOSTICS', 'RETRY JOIN', '[lobby-start][bootstrap]',
                       'No mod bytes arrived after five repair attempts.', 'Host download slot ready',
                       'audio-queued-to-SP-worker', 'Invalid bootstrap task address.'):
            need(marker.encode() in data, 'Linked stability feature missing: ' + marker)
        for value in contracts.values():
            need(value.encode() in data, 'Linked final peer ID missing')
    return {'schema': 1, 'version': VERSION, 'stability_contract': contract, 'rollback_build_ids': contracts,
            'swaps': swaps, 'includes': includes, 'task_adapter':adapter, 'compiler_bindings': bindings, 'retained': retained,
            'unchanged_authored_files': retained['unchanged_authored_files'],
            'validation': 'Recomposition, compiler binding, retained security/repair, source baseline and linked binary checks. Host-only tests recorded separately; multiplayer acceptance pending.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'binary', 'baseline', 'accepted', 'output'):
        parser.add_argument('--' + name, type=Path, required=name != 'binary')
    args = parser.parse_args()
    result = inspect(args.build, args.binary, args.baseline, args.accepted)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('Stability and retained music/online layers verified against final compiler bindings.')
