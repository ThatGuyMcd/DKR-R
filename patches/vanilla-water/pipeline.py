"""First-party, renderer-only animation repair. Inputs/dependencies stay read-only."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Water animation anchor changed: '+old[:120])
    return text.replace(old,new)

def stage(sdk,out):
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Choose a NEW build stage')
    accepted=json.loads((sdk/'sdk2-receipt.json').read_text())
    for name,expected in accepted['files'].items():
        if sha(sdk/name)!=expected:raise ValueError('SDK input changed: '+name)
    inputs={}
    def read(path):inputs[str(path)]=sha(path);return path.read_text()
    native=out/'native';native.mkdir(parents=True)
    def put(name,text):(native/name).write_text(text,encoding='utf-8',newline='\n')
    text=read(ROOT/'runtime-recomp/src/game/enhanced_water_registry.hpp')
    text=once(text,'const TextureScrolls& scrolls={},bool collect_glow=false) {',
        'const TextureScrolls& scrolls={},bool collect_glow=false,bool animation_only=false) {')
    text=once(text,'auto glow=collect_glow ?','auto glow=collect_glow && !animation_only ?')
    text=once(text,'auto rays=collect_rays ?','auto rays=collect_rays && !animation_only ?')
    text=once(text,'                auto kind=classify(e);', '''                auto kind=classify(e);
                // Vanilla animation needs only validated batch ranges and
                // texture periods. Do not build flow meshes, shoreline data,
                // opacity masks, emitters, ray geometry or lighting caches.
                if(animation_only) {
                    Surface animation{(tris+f*16)&0x1FFFFFFFU,(tris+fe*16)&0x1FFFFFFFU,
                        batch&0x1FFFFFFFU,kind,0,{}};
                    if((kind==Kind::Waterfall || kind==Kind::Fountain) && texture<nt && e.texture_valid) {
                        const auto header=m.u32(textures+texture*8);
                        const std::array<float,2> period{float(m.u8(header)),float(m.u8(header+1))};
                        if(period[0]>=1 && period[1]>=1)
                            animation.scroll_period=period;
                    }
                    // Retain non-flowing ranges too: an excluded/reused level
                    // texture must not inherit the object-image fallback.
                    surfaces_.push_back(animation);
                    continue;
                }''')
    put('enhanced_water_registry.hpp',text)
    text=read(ROOT/'runtime-recomp/src/game/enhanced_water_rt64.hpp')
    text=once(text,'task_registry(RT64::State& state,std::uint32_t scene) {',
        'task_registry(RT64::State& state,std::uint32_t scene,bool animation_only=false) {')
    text=once(text,'const bool lighting=lava_lighting_enabled.load()!=0',
        'const bool lighting=!animation_only && lava_lighting_enabled.load()!=0')
    text=once(text,'const bool rays=hardware_ray_capability.load(',
        'const bool rays=!animation_only && hardware_ray_capability.load(')
    text=once(text,'(rays ? 1U : 0U)|(glow ? 2U : 0U)',
        '(rays ? 1U : 0U)|(glow ? 2U : 0U)|(animation_only ? 4U : 0U)')
    text=once(text,'const auto scrolls=texture_scrolls(m,',
        'const auto scrolls=animation_only ? TextureScrolls{} : texture_scrolls(m,')
    text=once(text,'rays,preserve_flow_phase,scrolls,glow);',
        'rays,preserve_flow_phase,scrolls,glow,animation_only);')
    text=once(text,'        if(key[4] && cache.trace_lines<64) {',
        '        if(!animation_only && key[4] && cache.trace_lines<64) {')
    put('enhanced_water_rt64.hpp',text)
    text=read(sdk/'native/src/game/f3ddkr_rt64.cpp')
    old='''    if(enhancements::modern_presentation_enabled() &&
        water::enhanced::requested_quality.load(std::memory_order_relaxed)!=water::enhanced::Quality::Original) {
        data_->water_registry=&water::enhanced::task_registry(*state,presentation::task_scene_generation());
        data_->water_time=float(presentation::task_presentation_time());'''
    text=once(text,old,'''    const bool enhanced_water=water::enhanced::requested_quality.load(std::memory_order_relaxed)!=water::enhanced::Quality::Original;
    if(!enhanced_water)state->waterRayGeometry.reset();
    if(enhancements::modern_presentation_enabled()) {
        data_->water_registry=&water::enhanced::task_registry(*state,presentation::task_scene_generation(),!enhanced_water);
        data_->water_time=float(presentation::task_presentation_time());
        static thread_local bool animation_notice=false;
        if(!animation_notice) {
            std::fprintf(stderr,"[water][animation] independent vanilla flow interpolation; optics remain optional\\n");
            animation_notice=true;
        }
    }
    if(data_->water_registry && enhanced_water) {''')
    text=once(text,'    if(data.water_registry) {\n        water::enhanced::apply_material(',
        '    if(data.water_registry && water::enhanced::requested_quality.load(std::memory_order_relaxed)!=water::enhanced::Quality::Original) {\n        water::enhanced::apply_material(')
    old='''    const bool skip_equal_uv = procedural_water && water::equal_uv_enabled();'''
    text=once(text,old,'''#if defined(DKR_ENHANCED_WATER)
    // Collect wrap periods independently of optical replacement. The existing
    // queued RT64 UV matcher consumes this immutable per-vertex metadata in
    // BOTH modes, so disabling RTX cannot disable authored texture scrolling.
    std::array<float,2> flow_period{};
    if(data.water_registry && data.water_camera_valid &&
       presentation::task_interpolation_allowed() && !data.hud_alignment_depth &&
       !data.hud_widget_depth && !data.interpolation_groups.has_rejected_scope()) {
        const auto surface=data.water_registry->find(source,count);
        if(surface) {
            if(surface->kind==water::enhanced::Kind::Waterfall || surface->kind==water::enhanced::Kind::Fountain)
                flow_period=surface->scroll_period;
        } else flow_period=data.water_registry->vertical_scroll_period(state->rdp->texture.address);
    }
#endif
    const bool skip_equal_uv = procedural_water && water::equal_uv_enabled();''')
    # This exact non-shadow triangle site preserves the effective indices after
    # RT64's texcoord-copy and culling decisions. Never key on reusable N64 slots.
    text=once(text,'''        rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);
    }
#if defined(DKR_ENHANCED_WATER)
    // Seal the optical batch''', '''#if defined(DKR_ENHANCED_WATER)
        const auto flow_face_begin=draw.faceIndices.size();
#endif
        rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);
#if defined(DKR_ENHANCED_WATER)
        if(flow_period[0]>=1 && flow_period[1]>=1 && draw.faceIndices.size()==flow_face_begin+3) {
            for(unsigned corner=0;corner<3;++corner) {
                const auto vertex=draw.faceIndices[flow_face_begin+corner];
                if(vertex<draw.tcFloats.size()/2)
                    draw.waterfallPeriods[vertex]={flow_period[0],flow_period[1]};
            }
        }
#endif
    }
#if defined(DKR_ENHANCED_WATER)
    // Seal the optical batch''')
    put('f3ddkr_rt64.cpp',text)
    receipt={'schema':1,'version':'1.0.7+beta.10','inputs':inputs,
        'authors':{p.relative_to(ROOT).as_posix():sha(p) for p in AUTH.iterdir() if p.is_file()},
        'files':{p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file()},
        'policy':'Renderer metadata only. No shader, RT64, dependency, guest/replay or save changes. Owner performs gameplay testing.'}
    (out/'water-animation-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')

def verify(out):
    doc=json.loads((out/'water-animation-receipt.json').read_text())
    for name,expected in doc['inputs'].items():
        name=name.replace('\\','/')
        if os.name!='nt' and re.match(r'^[A-Za-z]:/',name):name='/mnt/'+name[0].lower()+name[2:]
        if sha(Path(name))!=expected:raise ValueError('An accepted water input changed: '+name)
    for name,expected in doc['files'].items():
        if sha(out/name)!=expected:raise ValueError('Water stage changed: '+name)
    native=out/'native'
    cpp=(native/'f3ddkr_rt64.cpp').read_text()
    registry=(native/'enhanced_water_registry.hpp').read_text()
    # Inspect separation/capture boundaries without executing the application.
    if 'scene_generation(),!enhanced_water)' not in cpp or 'draw.faceIndices[flow_face_begin+corner]' not in cpp:raise ValueError('Animation capture boundary missing')
    mode=registry[registry.index('if(animation_only) {'):registry.index('// Native, immutable level geometry only.')]
    if 'continue;' not in mode or 'flow_mapping(' in mode:raise ValueError('Vanilla path performs optical geometry work')
    print('Water animation composition inspected; no target execution or dependency changes.')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('stage','verify'))
    p.add_argument('--sdk',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.action=='stage':stage(a.sdk.resolve(strict=True),a.output)
    verify(a.output)
if __name__=='__main__':main()
