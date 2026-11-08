"""Static receipts, source invariants and compiled-image inspection; no tests."""
from pathlib import Path
import argparse
import hashlib
import json
import re

AUTH=Path(__file__).resolve().parent


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(stage,binary=None,linked_contracts=None):
    receipt=json.loads((stage/'repair-receipt.json').read_text())
    for filename,value in receipt['inputs'].items():
        if sha(Path(filename))!=value:raise ValueError('Read-only input changed: '+filename)
    for filename,value in receipt['outputs'].items():
        if sha(stage/filename)!=value:raise ValueError('Staged source changed: '+filename)
    for filename,value in receipt['authored'].items():
        if sha(AUTH/filename)!=value:raise ValueError('Authored repair changed after configuration: '+filename)
    native=stage/'native/mods'
    vm=(native/'magic_code_vm.cpp').read_text()
    world=(native/'magic_code_world.cpp').read_text()
    game=(native/'magic_code_game_world.cpp').read_text()
    link=(native/'magic_code_link.cpp').read_text()
    operations=(native/'sdk2_operations.inl').read_text()
    identity=(stage/'native/game/presentation_identity.cpp').read_text()
    bridge=(stage/'native/game/f3ddkr_rt64.cpp').read_text()
    required=(('instruction budget exceeded',vm),('maximum callback depth exceeded',vm),
        ('execution::instruction()',vm),('execution::OwnerScope execution_owner',world),
        ('if(depth>=8)',world),('execution::native(budget);',game),('native_depth_>=8',game),
        ('Game event recursion exceeds its bound.',game),('!execution::active(&world_)',game),
        ('!execution::active(&world_)',(native/'sdk2_simulation.inl').read_text()),('only one simulation control provider',link),
        ('only one racer tuning provider',link),('enabled code exceeds the session memory budget',link),
        ('"work-policy",std::string(execution::Policy)',(native/'magic_code_package.cpp').read_text()),
        ('empty-scope-compaction=v1',identity),('kMaxDKRVertices = 32',bridge),
        ('index >= kMaxDKRVertices',bridge),('ReportMalformedInput(state,command',bridge))
    for marker,text in required:
        if marker not in text:raise ValueError('Missing source invariant: '+marker)
    for marker,text in (('native call budget exceeded',vm),('reserved_native',world),
                        ('SDK 2 work profiles exceed the portable session budget',link),
                        ('--budget.native_calls',operations)):
        if marker in text:raise ValueError('Stale shared/double accounting: '+marker)
    hooks={}
    for target in ('DKRRecompiledFuncsV77','DKRRecompiledFuncsV80','DKRExperimentalOwnedGameV77','DKRExperimentalOwnedGameV80'):
        swaps=json.loads((stage/target/'swaps.json').read_text())
        if len(swaps)!=1 or swaps[0]['function']!='init_track':raise ValueError('Unexpected guest repair scope')
        row=swaps[0]
        if sha(Path(row['old']))!=row['input_sha256'] or sha(Path(row['new']))!=row['output_sha256']:
            raise ValueError('A guest input/output changed')
        source=Path(row['new']).read_text()
        if source.count('post-mod-init-track=v1')!=1 or source.count('dkr_render_heap_reserve(rdram,&repair_context);')!=1:
            raise ValueError('Wrong drawing heap repair count')
        if 'RecompiledFuncs/' in row['new'].replace('\\','/') or 'RecompiledPatches/' in row['new'].replace('\\','/'):
            raise ValueError('Output points into a protected checkout')
        hooks[target]={'input':row['input_sha256'],'output':row['output_sha256']}
    contracts=json.loads((stage/'repair-contracts.json').read_text())
    if set(contracts)!={'77','80'} or any(not re.fullmatch('[a-f0-9]{64}',v) for v in contracts.values()):
        raise ValueError('Missing portable contract')
    if binary:
        data=binary.read_bytes()
        for stale in (b'native call budget exceeded',b'SDK 2 work profiles exceed the portable session budget'):
            if stale in data:raise ValueError('An old quota implementation was linked: '+repr(stale))
        for marker in (b'per-owner-work-v2',b'[render][heap-headroom]',b'post-mod-init-track=v1',
            b'[render][invalid-input]',b'[hud][resource-context]',b'[custom-vehicles][picker]',
            b'wheel-motion=arc-phase64',b'outward-arrows=alpha-blended'):
            if marker not in data:raise ValueError('Missing linked repair: '+repr(marker))
        expected=contracts if linked_contracts is None else linked_contracts
        if set(expected)!={'77','80'} or any(not re.fullmatch('[a-f0-9]{64}',v) for v in expected.values()):
            raise ValueError('Missing final composed portable contract')
        for value in expected.values():
            if value.encode() not in data:raise ValueError('Wrong linked rollback identity')
    return {'schema':1,'scope':'Static source/receipt and optional linked-byte inspection only; no game/mod/test execution',
            'source_invariants':len(required),'rollback_build_ids':contracts,'guest_hooks':hooks,
            'input_count':len(receipt['inputs']),'output_count':len(receipt['outputs'])}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=inspect(args.stage,args.binary)
    if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
