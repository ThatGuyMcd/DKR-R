"""Inspect both revised service endpoints and every retained online layer."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import sys

sys.dont_write_bytecode = True
import pipeline

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
VERSION = '1.0.9-music-playtest.1'


def need(ok, message):
    if not ok:
        raise ValueError(message)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inspect(build, binary, baseline, accepted):
    stage = build / 'generated/music-preview-repair'
    swaps, includes, outputs = pipeline.compose(json.loads((stage / 'inputs.json').read_text()), stage)
    need(swaps == json.loads((stage / 'swaps.json').read_text()), 'Repair source receipts differ')
    need(includes == json.loads((stage / 'includes.json').read_text()), 'Repair header receipt differs')
    for name, data in outputs.items():
        need((stage / name).read_bytes() == data, 'Stale repair output: ' + name)
    bindings = []
    for row in swaps:
        project = build / (row['target'] + '.vcxproj')
        if project.exists():
            compiled = {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"', project.read_text(encoding='utf-8-sig'))}
        else:
            compiled = {Path(m[1]).resolve() for line in (build / 'build.ninja').read_text().splitlines()
                        if line.startswith('build CMakeFiles/' + row['target'] + '.dir/')
                        if (m := re.search(r': [^ ]+ (\S+)', line))}
        need(Path(row['new']).resolve() in compiled, 'Repair output is not compiled: ' + row['new'])
        need(Path(row['path']).resolve() not in compiled, 'Superseded source still compiled')
        bindings.append({'target': row['target'], 'source': row['new']})
    resident = outputs['DKRLegacyModLaunch/resident_music.cpp'].decode()
    preview = resident.split('PreviewScope::PreviewScope', 1)[1].split('PreparationScope::PreparationScope', 1)[0]
    need('++silent_preparation_depth' in preview and '--silent_preparation_depth' in preview, 'Preview suppression is not balanced')
    need(not re.search(r'(?<!silent_)preparation_depth', preview), 'Preview scope grants decoder permission')
    for marker in ('if(!allow_decode || !preparation_depth)', 'activate(level,false)',
                   'silent_preparation_depth?', 'sha256_pcm_le(pcm->samples)!=r->proof'):
        need(marker in resident, 'Decoder/checkpoint/proof guard missing: ' + marker)
    runtime = outputs['DKRPortGame/runtime_legacy_mods.cpp'].decode()
    need('arity(1);\n            if(args[0]>1)' in runtime, 'Course service accepts an invalid policy')
    need('PreviewScope preview(args[0]!=0)' in runtime, 'Scoped preview suppression is missing')
    for revision in (77, 80):
        native = outputs[f'DKRExperimentalOwnedGameV{revision}/probe_native.c'].decode()
        need('silent_music_preview=inline_track_preview(rdram) && confirmed_menu_tick' in native, 'Preview is not confirmed and inline')
        need('ctx,&silent_music_preview,1,0,0,&ignored)' in native, 'Guest/native service argument differs')
        for marker in ('unowned-confirmed-track-preview', 'resource-confirmation-required', 'incomplete-confirmed-track-preview'):
            need(marker in native, 'Native preview admission guard missing')
    saved = {name: sys.modules.get(name) for name in ('pipeline', 'transforms')}
    try:
        directory = ROOT / 'patches/online-directory'
        sys.modules['transforms'] = load('music_directory_transforms', directory / 'transforms.py')
        sys.modules['pipeline'] = load('music_directory_pipeline', directory / 'pipeline.py')
        prior = load('music_directory_audit', directory / 'audit.py')
        prior.VERSION = VERSION
        retained = prior.inspect(build / 'generated/online-directory', binary, baseline)
    finally:
        for name, value in saved.items():
            if value is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = value
    previous = json.loads(accepted.read_text())['audit']
    need(retained['directory_contract'] == previous['directory_contract'], 'Public directory logic changed')
    need(retained['retained']['scoreboard_contract'] == previous['retained']['scoreboard_contract'], 'Scoreboard logic changed')
    contracts = json.loads((stage / 'contracts.json').read_text())
    need(set(contracts) == {'77', '80'} and all(re.fullmatch('[a-f0-9]{64}', v) for v in contracts.values()), 'Missing revision-specific peer contracts')
    if binary:
        data = binary.read_bytes()
        for marker in (b'Invalid inline music preview policy.', b'Custom music was not prepared at a confirmed loading boundary.'):
            need(marker in data, 'Linked repair/guard is missing')
        for value in contracts.values():
            need(value.encode() in data, 'Linked peer contract missing')
    return {'schema': 1, 'version': VERSION, 'rollback_build_ids': contracts,
            'compiler_bindings': bindings, 'swaps': swaps, 'includes': includes,
            'retained': retained, 'unchanged_authored_files': retained['unchanged_authored_files'],
            'directory_contract': retained['directory_contract'],
            'scoreboard_contract': retained['retained']['scoreboard_contract'],
            'validation': 'Compilation/linking and static source/binary/package inspection only. No game, mod, native test or AppImage execution.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'binary', 'baseline', 'accepted', 'output'):
        parser.add_argument('--' + name, type=Path, required=name != 'binary')
    args = parser.parse_args()
    result = inspect(args.build, args.binary, args.baseline, args.accepted)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('Both preview endpoints, decoder guards, compiled bindings and retained online layers verified.')
