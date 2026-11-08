"""Receipt-checked, reversible CPU reuse and presentation-clock qualification."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys
sys.dont_write_bytecode = True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text, old, new):
    if text.count(old)!=1:
        raise ValueError('Surgical performance anchor changed: '+old[:120])
    return text.replace(old,new)


def translate(name):
    name=name.replace('\\','/')
    if os.name!='nt' and re.match(r'^[A-Za-z]:/',name):
        name='/mnt/'+name[0].lower()+name[2:]
    return Path(name)


def verify(out):
    receipt=json.loads((out/'performance-receipt.json').read_text())
    for name, expected in receipt['inputs'].items():
        if sha(translate(name))!=expected:raise ValueError('Frozen performance input changed: '+name)
    for name, expected in receipt['files'].items():
        if sha(out/name)!=expected:raise ValueError('Performance stage changed: '+name)
    credits=(out/'native/credits_viewport_rt64.hpp').read_text()
    if 'constrain(pair.drawDepthRect);' not in credits or 'clip.uly=-' in credits:
        raise ValueError('Accepted native credits bounds lost')
    present=(out/'renderer/rt64_present_queue.cpp').read_text()
    if present.count('ext.presentGraphicsWorker->wait();')!=receipt['present_wait_count']:
        raise ValueError('GPU completion fence count changed')
    print('Surgical performance composition inspected; no game, replay or tests executed.')


def stage(base, renderer, output):
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Choose a NEW performance stage under build')
    accepted=json.loads((base/'presentation-polish-receipt.json').read_text())
    for name, expected in accepted['files'].items():
        if sha(base/name)!=expected:raise ValueError('Accepted presentation input changed: '+name)
    inputs={}
    def read(path):
        inputs[str(path)]=sha(path)
        return path.read_text()
    def put(name,text):
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(text,encoding='utf-8',newline='\n')
    for path in sorted((base/'native').glob('*')):
        if path.is_file():put('native/'+path.name,read(path))
    for path in sorted((base/'renderer').glob('*')):
        if path.is_file():put('renderer/'+path.name,read(path))
    for name in ('performance_switches.hpp','performance_observation.hpp','reusable_flow_map.hpp','texture_binding_index.hpp','presentation_pacer.hpp'):
        put('native/'+name,read(AUTH/name))

    flow=read(base/'native/flow_history_rt64.hpp')
    flow=once(flow,'#include <unordered_map>','#include <unordered_map>\n#include "reusable_flow_map.hpp"\n#include "performance_switches.hpp"')
    flow=once(flow,'class FlowHistory {','template<template<class,class>class Table> class BasicFlowHistory {')
    flow=once(flow,'std::unordered_map<std::uint32_t,Sample> previous_,current_;','Table<std::uint32_t,Sample> previous_,current_;')
    flow=once(flow,'inline FlowHistory& flow_history()', '''template<class K,class V>using LegacyFlowMap=std::unordered_map<K,V>;
class FlowHistory {
    BasicFlowHistory<LegacyFlowMap> legacy_;
    BasicFlowHistory<dkr::runtime::surgical::ReusableFlowMap> reusable_;
public:
    void begin(std::uint32_t scene,std::uint32_t model,double time,bool allowed,std::uint64_t generation=0) {
        if(surgical::flow_reuse())reusable_.begin(scene,model,time,allowed,generation);
        else legacy_.begin(scene,model,time,allowed,generation);
    }
    FlowMotion observe(std::uint32_t source,FlowCorners uv,std::array<float,2> period,std::uint32_t image) {
        return surgical::flow_reuse()?reusable_.observe(source,uv,period,image):legacy_.observe(source,uv,period,image);
    }
};
inline FlowHistory& flow_history()''')
    put('native/flow_history_rt64.hpp',flow)

    registry=read(base/'native/enhanced_water_registry.hpp')
    registry=once(registry,'#include <cstring>','#include <cstring>\n#include "texture_binding_index.hpp"\n#include "performance_observation.hpp"')
    registry=once(registry,'        const auto previous_flows=preserve_flow_phase',
        '        surgical::Scope registry_timing(surgical::Region::Registry);\n        const auto previous_flows=preserve_flow_phase')
    registry=once(registry,'        std::vector<MaterialEvidence> materials(nt);', '''        std::optional<surgical::TextureBindingIndex> binding_index;
        if(surgical::texture_index() && nt>=8 && texture_count>=32) {
            try {binding_index.emplace(m,cache,texture_count,texture_3d_id);}
            catch(const std::bad_alloc&) {binding_index.reset();} // Exact original lookup is the fallback.
        }
        std::vector<MaterialEvidence> materials(nt);''')
    registry=once(registry,'            for(std::uint32_t c=0;c<texture_count;++c) if(m.u32(cache+c*8+4)==ptr) {',
        '            if(binding_index)e.texture_id=binding_index->find(ptr);\n            else for(std::uint32_t c=0;c<texture_count;++c) if(m.u32(cache+c*8+4)==ptr) {')
    put('native/enhanced_water_registry.hpp',registry)

    warmup=read(base/'native/water_residency_rt64.hpp')
    warmup=once(warmup,'        loaded_=loaded;model_=model;',
        '        surgical::Scope warmup_timing(surgical::Region::LiquidWarmup);\n        loaded_=loaded;model_=model;')
    put('native/water_residency_rt64.hpp',warmup)

    decoder=read(base/'native/f3ddkr_rt64.cpp')
    decoder=once(decoder,'#include "water_residency_rt64.hpp"','#include "water_residency_rt64.hpp"\n#include "presentation_pacer.hpp"')
    decoder=once(decoder,'    RT64::State* state = application.state.get();', '''    RT64::State* state = application.state.get();
    if(surgical::observing()) {
    unsigned surgical_water_quality=0;
#if defined(DKR_ENHANCED_WATER)
    surgical_water_quality=unsigned(water::enhanced::requested_quality.load(std::memory_order_relaxed));
#endif
    surgical::context(presentation::task_scene_generation(),ReadU32(state->RDRAM,revision_addresses::CurrentMapId),
        ReadU32(state->RDRAM,kCurrentMenuIdAddress),application.userConfig.refreshRateTarget,
        surgical_water_quality);
    surgical::publish_context(presentation::task_scene_generation(),ReadU32(state->RDRAM,revision_addresses::CurrentMapId),ReadU32(state->RDRAM,kCurrentMenuIdAddress));
    }
    surgical::Scope decode_timing(surgical::Region::Decode);''')
    put('native/f3ddkr_rt64.cpp',decoder)
    native=read(base/'native/rt64_renderer.cpp')
    native=once(native,'#include "vi_presentation_policy.hpp"','#include "vi_presentation_policy.hpp"\n#include "performance_switches.hpp"')
    native=once(native,'    dkr::runtime::texture_packs::renderer_started();','''    dkr::runtime::texture_packs::renderer_started();
    std::fprintf(stderr,"[perf][surgical-features] reusable-flow=%u texture-index=%u mip-scratch=%u monotonic-pacer=%u; quality, guest clocks and GPU fences preserved\\n",
        unsigned(surgical::flow_reuse()),unsigned(surgical::texture_index()),unsigned(surgical::mip_scratch()),unsigned(surgical::monotonic_pacer()));''')
    put('native/rt64_renderer.cpp',native)

    mips=read(base/'renderer/rt64_generated_mips.h')
    mips=once(mips,'#include "native_resource_safety.hpp"','#include "native_resource_safety.hpp"\n#include "../native/performance_switches.hpp"')
    mips=once(mips,'    inline Level downsample(const Level &src) {\n        Level dst{std::max(src.width / 2, 1U), std::max(src.height / 2, 1U), {}};',
        '    inline void downsample_into(const Level &src,Level &dst) {\n        dst.width=std::max(src.width/2,1U);dst.height=std::max(src.height/2,1U);')
    mips=once(mips,'        return dst;\n    }\n    struct Scratch {','''    }
    inline Level downsample(const Level& src) {Level dst;downsample_into(src,dst);return dst;}
'''+read(AUTH/'mip_scratch.inl')+'\n    struct Scratch {')
    mips=once(mips,'        std::vector<Level> levels;\n        std::vector<uint64_t> offsets;', '''        CpuMipScratch local;
        auto& reusable=reusable_mip_scratch();
        auto& scratch=dkr::runtime::surgical::mip_scratch() && !reusable.busy ? reusable : local;
        CpuMipLease cpu_lease(scratch);
        auto& levels=scratch.levels;auto& offsets=scratch.offsets;''')
    mips=once(mips,'            levels.push_back({w,h,{}}); levels[0].pixels.resize(size_t(w)*h*4);',
        '            levels.resize(count);offsets.clear();offsets.reserve(count);\n            levels[0].width=w;levels[0].height=h;levels[0].pixels.resize(size_t(w)*h*4);')
    mips=once(mips,'            for(uint32_t m=1;m<count;++m) levels.emplace_back(downsample(levels.back()));',
        '            for(uint32_t m=1;m<count;++m) downsample_into(levels[m-1],levels[m]);')
    put('renderer/rt64_generated_mips.h',mips)

    present=read(renderer/'hle/rt64_present_queue.cpp')
    waits=present.count('ext.presentGraphicsWorker->wait();')
    present=once(present,'#include "rt64_present_queue.h"','#include "rt64_present_queue.h"\n#include "presentation_pacer.hpp"')
    present=once(present,"Timer::preciseSleepUntil(presentTimestamp + std::chrono::nanoseconds(1'000'000'000 / targetRate));",
        '''if(dkr::runtime::surgical::monotonic_pacer())
                        dkr::runtime::surgical::presentation_pacer().wait(this,presentTimestamp,targetRate);
                    else Timer::preciseSleepUntil(presentTimestamp + std::chrono::nanoseconds(1'000'000'000 / targetRate));''')
    present=once(present,'                presentTimestamp = Timer::current();', '''                presentTimestamp = Timer::current();
                dkr::runtime::surgical::presentation_pacer().submitted(this,presentTimestamp,targetRate);''')
    present=once(present,'                if (swapChainValid) dkr::runtime::performance_trace::record_present();', '''                if (swapChainValid) {
                    dkr::runtime::performance_trace::record_present();
                    dkr::runtime::surgical::observe_present(ext.swapChain->getWidth(),ext.swapChain->getHeight(),targetRate);
                }''')
    put('renderer/rt64_present_queue.cpp',present)

    code=read(ROOT/'build/sdk2-beta7-20261018-r18/stage/native/src/game/mods/magic_code_world.cpp')
    code=once(code,'    Writer w;w.word(ExecutionVersion);', '''    // Exact canonical size; preserve every field and zero-padding decision.
    // One allocation instead of repeatedly growing the serialized image.
    std::size_t serialized=80;
    for(const auto& s:state_)serialized+=16+s.blocks.size()*12;
    for(const auto& effect:effects_)serialized+=8+effect.text.size();
    for(const auto& region:linked_->regions)if(region.writable)serialized+=8+region.size;
    Writer w;w.bytes.reserve(serialized);w.word(ExecutionVersion);''')
    put('native/mods/magic_code_world.cpp',code)
    receipt={'schema':1,'version':'1.0.7+beta.10','accepted_presentation':accepted,
        'inputs':inputs,'present_wait_count':waits,
        'authors':{p.name:sha(p) for p in AUTH.iterdir() if p.is_file()},
        'files':{p.relative_to(output).as_posix():sha(p) for p in output.rglob('*') if p.is_file()},
        'policy':'Patch Pipeline only; staged dependency patch explicitly approved. No checked-out dependencies, generated guest functions, quality, saves, guest clocks, or GPU lifetime changes. Owner performs gameplay acceptance.'}
    (output/'performance-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    verify(output)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('stage','verify'))
    p.add_argument('--base',type=Path)
    p.add_argument('--renderer',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.action=='stage':stage(args.base.resolve(strict=True),args.renderer.resolve(strict=True),args.output)
    else:verify(args.output)


if __name__=='__main__':main()
