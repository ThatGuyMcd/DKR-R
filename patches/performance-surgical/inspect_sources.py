"""Static source-equivalence checks only; does not execute compiled game code."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]


def block(text,first,last):
    start=text.index(first)
    return text[start:text.index(last,start)]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,required=True)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if not args.output.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Static report belongs under build')
    checks={}
    def equal(name,a,b):
        if a!=b:raise ValueError('Static safety invariant changed: '+name)
        checks[name]={'sha256':hashlib.sha256(a.encode()).hexdigest(),'identical':True}
    old=(args.base/'native/flow_history_rt64.hpp').read_text()
    new=(args.stage/'native/flow_history_rt64.hpp').read_text()
    equal('flow_matching_wrap_resets_and_conflicts',block(old,'public:\n    void begin','inline FlowHistory&'),
        block(new,'public:\n    void begin','template<class K,class V>'))
    old=(args.base/'renderer/rt64_generated_mips.h').read_text()
    new=(args.stage/'renderer/rt64_generated_mips.h').read_text()
    equal('mip_alpha_weighting_odd_dimensions_and_rounding',
        block(old,'        dst.pixels.resize','        return dst;').rstrip(),
        block(new,'        dst.pixels.resize','    }\n    inline Level downsample').rstrip())
    equal('mip_gpu_mapping_upload_barriers_and_result',
        block(old,'        auto *data=','    // Record only after native RGBA decode.'),
        block(new,'        auto *data=','    // Record only after native RGBA decode.'))
    equal('optional_native_texture_mip_path',old[old.index('    // Record only after native RGBA decode.'):],
        new[new.index('    // Record only after native RGBA decode.'):])
    equal('native_resource_allocation_guards',(args.base/'renderer/native_resource_safety.hpp').read_text(),
        (args.stage/'renderer/native_resource_safety.hpp').read_text())
    for name in ('credits_viewport_rt64.hpp','credits_sky_rt64.hpp','intro_caption_rt64.hpp','intro_sky_rt64.hpp',
                 'intro_viewport_rt64.hpp','title_text_rt64.hpp','flow_phase_rt64.hpp','flow_vertex_scope_rt64.hpp'):
        equal(name,(args.base/'native'/name).read_text(),(args.stage/'native'/name).read_text())
    old=(ROOT/'build/sdk2-beta7-20261018-r18/stage/native/src/game/mods/magic_code_world.cpp').read_text()
    new=(args.stage/'native/mods/magic_code_world.cpp').read_text()
    equal('checkpoint_canonical_fields_and_memory_reads',
        block(old,'w.word(ExecutionVersion);','void World::verify_immutable()'),
        block(new,'w.word(ExecutionVersion);','void World::verify_immutable()'))
    equal('code_world_restore_permissions_and_gateway',old[old.index('void World::verify_immutable()'):],
        new[new.index('void World::verify_immutable()'):])
    report={'validation':'Static source inspection only; no game, replay, benchmark, unit test or changed C++ function was executed.',
        'checks':checks,'acceptance':'Owner playback, determinism and comparative performance captures pending.'}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('Static source invariants inspected:',len(checks))


if __name__=='__main__':main()
