"""Compose presentation repairs over the accepted beta 10 stage. No guest edits."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Presentation anchor changed: ' + old[:120])
    return text.replace(old, new)


def verify(out):
    doc = json.loads((out / 'viewport-flow-receipt.json').read_text())
    for group in ('inputs', 'files'):
        for name, expected in doc[group].items():
            if group == 'files':
                path = out / name
            else:
                name = name.replace('\\', '/')
                if os.name != 'nt' and re.match(r'^[A-Za-z]:/', name):
                    name = '/mnt/' + name[0].lower() + name[2:]
                path = Path(name)
            if sha(path) != expected:
                raise ValueError('Presentation receipt changed: ' + str(path))
    print('Viewport/flow composition inspected; no application or tests executed.')


def stage(base, out, intro=False):
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Choose a NEW stage under build')
    accepted = json.loads((base / 'water-animation-receipt.json').read_text())
    for name, expected in accepted['files'].items():
        if sha(base / name) != expected:
            raise ValueError('Accepted water input changed: ' + name)
    inputs = {}
    def read(path):
        inputs[str(path)] = sha(path)
        return path.read_text()
    native = out / 'native'
    native.mkdir(parents=True)
    def put(name, text):
        (native / name).write_text(text, encoding='utf-8', newline='\n')

    text = read(base / 'native/enhanced_water_registry.hpp')
    text = once(text, '#include <memory>', '#include <memory>\n#include <map>')
    text = once(text, '    std::vector<Surface> surfaces_;', '''    std::vector<Surface> surfaces_;
    struct FlowVertices {std::uint32_t end; std::array<float,2> period;};
    std::map<std::uint32_t,FlowVertices> flow_vertices_;''')
    text = once(text, 'public:\n    const std::shared_ptr<const GlowGeometry>&', '''public:
    std::array<float,2> flow_vertex_period(std::uint32_t source,std::uint32_t count) const {
        source &= 0x1FFFFFFFU;
        if(!count)return {};
        auto it=flow_vertices_.upper_bound(source);
        if(it==flow_vertices_.begin())return {};
        --it;
        if((source-it->first)%10 || std::uint64_t(source)+std::uint64_t(count)*10>it->second.end)return {};
        return it->second.period;
    }
    const std::shared_ptr<const GlowGeometry>&''')
    text = once(text, '        flow_triangles_.clear();', '        flow_triangles_.clear();\n        flow_vertices_.clear();')
    # The producer wraps level scrolls at width/height << 8, whereas the DKR
    # triangle decoder supplies UV/32. The matching domain is therefore 8x
    # the texture dimension, not the optical shader's texture-pixel period.
    text = once(text, '                auto kind=classify(e);', '''                auto kind=classify(e);
                if((kind==Kind::Waterfall || kind==Kind::Fountain) && texture<nt && e.texture_valid) {
                    const auto header=m.u32(textures+texture*8);
                    const std::array<float,2> period{float(m.u8(header))*8.F,float(m.u8(header+1))*8.F};
                    if(period[0]>=8 && period[1]>=8) {
                        const auto begin=(verts+v*10)&0x1FFFFFFFU,end=(verts+ve*10)&0x1FFFFFFFU;
                        auto [at,inserted]=flow_vertices_.emplace(begin,FlowVertices{end,period});
                        if(!inserted && (at->second.end!=end || at->second.period!=period))at->second.period={};
                    }
                }''')
    text = text.replace('surfaces_.clear();flow_triangles_.clear();wet_triangles_.clear();procedural_.reset();return;',
        'surfaces_.clear();flow_triangles_.clear();flow_vertices_.clear();wet_triangles_.clear();procedural_.reset();return;')
    put('enhanced_water_registry.hpp', text)
    put('enhanced_water_rt64.hpp', read(base / 'native/enhanced_water_rt64.hpp'))

    text = read(base / 'native/f3ddkr_rt64.cpp')
    text = once(text, '#include "water_uv_rt64.hpp"', '#include "water_uv_rt64.hpp"\n#include "flow_vertex_scope_rt64.hpp"')
    text = once(text, '#include "enhanced_water_rt64.hpp"',
        '#include "enhanced_water_rt64.hpp"\n#include "flow_phase_rt64.hpp"\n#include "flow_cadence_rt64.hpp"\n#include "flow_history_rt64.hpp"')
    text = once(text, '    float water_time = 0.0F;', '''    float water_time = 0.0F;
    water::FlowFractions flow_fractions{};
    unsigned flow_loads=0,flow_faces=0,fractional_faces=0,manual_faces=0,held_faces=0,image_changes=0;
    std::array<bool,32> manual_flow_slots{};''')
    text = once(text, '        data_->water_time=float(presentation::task_presentation_time());',
        '''        data_->water_time=float(presentation::task_presentation_time());
        data_->flow_fractions=water::capture_flow_fractions(*state);
        water::enhanced::Memory flow_memory(std::span<const std::uint8_t>(state->RDRAM,0x800000));
        water::flow_history().begin(presentation::task_scene_generation(),
            flow_memory.u32(water::enhanced::level_model_symbol()),presentation::task_presentation_time(),
            presentation::task_interpolation_allowed());''')
    text = once(text, '    rsp.setVertex(kScratchVertexAddress, count, destination);', '''    // Match each authored flowing-water vertex load independently of other
    // terrain. Window/aspect changes can change a segment's total vertex
    // count; RT64 otherwise drops all UV matching for that shared transform.
#if defined(DKR_ENHANCED_WATER)
    const auto stable_flow_period=data.water_registry ? data.water_registry->flow_vertex_period(source,count) : std::array<float,2>{};
    water::FlowVertexScope flow_scope(rsp,source,count,destination,
        stable_flow_period[0]>=1 && stable_flow_period[1]>=1 &&
        presentation::task_interpolation_allowed() && data.water_camera_valid &&
        !data.billboard && !data.shadow_scope_active && !data.hud_alignment_depth &&
        !transformed_hud && !data.interpolation_groups.active_group().procedural_water &&
        !data.interpolation_groups.has_rejected_scope(),
        water::enhanced::requested_quality.load(std::memory_order_relaxed)==water::enhanced::Quality::Original);
#endif
    rsp.setVertex(kScratchVertexAddress, count, destination);
#if defined(DKR_ENHANCED_WATER)
    data.flow_loads+=flow_scope.active();
    for(unsigned i=0;i<count;++i)data.manual_flow_slots[destination+i]=flow_scope.active() &&
        water::enhanced::requested_quality.load(std::memory_order_relaxed)==water::enhanced::Quality::Original;
    flow_scope.restore();
#endif''')
    text = once(text, '    std::array<float,2> flow_period{};',
        '    std::array<float,2> flow_period{},flow_fraction{};')
    text = once(text, '                flow_period=surface->scroll_period;', '''                // Level texture scrolls wrap at dimension << 8 before the
                // triangle decoder converts to UV/32. Optical flow mapping
                // retains its own texture-pixel units.
                flow_period={surface->scroll_period[0]*8.F,surface->scroll_period[1]*8.F};''')
    text = once(text, '        } else flow_period=data.water_registry->vertical_scroll_period(state->rdp->texture.address);', '''            if(flow_period[0]>=1 && flow_period[1]>=1) {
                water::enhanced::Memory m(std::span<const std::uint8_t>(state->RDRAM,0x800000));
                if(m.valid(surface->batch,12))flow_fraction=data.flow_fractions[m.u8(surface->batch)];
            }
        } else flow_period=data.water_registry->vertical_scroll_period(state->rdp->texture.address);''')
    text = once(text, '            skipped += water::set_texcoord(rsp, draw, vertices[corner], s[corner], t[corner], skip_equal_uv);', '''            skipped += water::set_texcoord(rsp, draw, vertices[corner], s[corner], t[corner], skip_equal_uv);
#if defined(DKR_ENHANCED_WATER)
            // set_texcoord first restores raw authored ST on the actual
            // current/cloned vertex. Adding the remainder here never
            // accumulates across shared corners, triangles or tasks.
            if((flag&0x80U)==0 && flow_period[0]>=1 && flow_period[1]>=1) {
                const auto at=std::size_t(rsp.indices[vertices[corner]])*2;
                if(at+1<draw.tcFloats.size()) {
                    draw.tcFloats[at]+=flow_fraction[0];
                    draw.tcFloats[at+1]+=flow_fraction[1];
                }
            }
#endif''')
    text = once(text, '        rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);\n#if defined(DKR_ENHANCED_WATER)\n        if(flow_period', '''#if defined(DKR_ENHANCED_WATER)
        // Static original water carries its own source-triangle UV history.
        // Its isolated groups have texcoord matching SKIP, and no period tags
        // are published for this path: the queued matcher cannot overwrite it.
        const bool manual_flow=flow_period[0]>=1 && flow_period[1]>=1 &&
            data.manual_flow_slots[vertices[0]] && data.manual_flow_slots[vertices[1]] && data.manual_flow_slots[vertices[2]];
        if(manual_flow) {
            water::FlowCorners uv{};
            for(unsigned corner=0;corner<3;++corner) {
                const auto at=std::size_t(rsp.indices[vertices[corner]])*2;
                uv[corner]={draw.tcFloats[at],draw.tcFloats[at+1]};
            }
            const auto motion=water::flow_history().observe(address,uv,flow_period,state->rdp->texture.address);
            ++data.manual_faces;data.held_faces+=motion.matched&&motion.held;data.image_changes+=motion.image_changed;
            for(unsigned corner=0;corner<3;++corner) {
                const auto at=std::size_t(rsp.indices[vertices[corner]])*2;
                if(at+1<draw.tcVelFloats.size()) {
                    draw.tcVelFloats[at]=motion.velocity[corner][0];
                    draw.tcVelFloats[at+1]=motion.velocity[corner][1];
                }
            }
        }
#endif
        rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);
#if defined(DKR_ENHANCED_WATER)
        if(flow_period[0]>=1 && flow_period[1]>=1) {
            ++data.flow_faces;
            if((flag&0x80U)==0 && (flow_fraction[0]!=0 || flow_fraction[1]!=0))++data.fractional_faces;
        }
        if(!manual_flow && flow_period''')
    text = once(text, '    g_completed_tasks.store(data_->task_count, std::memory_order_release);', '''#if defined(DKR_ENHANCED_WATER)
    if(enhancements::modern_presentation_enabled())water::report_flow_cadence(application,
        presentation::task_scene_generation(),presentation::task_interpolation_allowed(),
        data_->flow_loads,data_->flow_faces,data_->fractional_faces,data_->manual_faces,data_->held_faces,data_->image_changes);
#endif
    g_completed_tasks.store(data_->task_count, std::memory_order_release);''')
    text = once(text, 'independent vanilla flow interpolation; optics remain optional\\n',
        'independent vanilla flow interpolation; optics remain optional; native triangle UV history and fractional scroll phase\\n')
    if intro:
        text = once(text, '#include "flow_vertex_scope_rt64.hpp"',
            '#include "flow_vertex_scope_rt64.hpp"\n#include "intro_viewport_rt64.hpp"')
        text = once(text, '    float world_window_aspect = 4.0F / 3.0F;',
            '    float world_window_aspect = 4.0F / 3.0F;\n    presentation::IntroViewport intro_viewport;')
        text = once(text, '    RT64::State* state = application.state.get();', '''    RT64::State* state = application.state.get();
    data_->intro_viewport.begin(enhancements::modern_presentation_enabled() &&
        enhancements::fit_to_window_enabled() && ReadU32(state->RDRAM,kCurrentMenuIdAddress)==1U);''')
        # Aggregate geometry is producer-owned only until fullSync. Retain the
        # native 320-wide aspect reference rather than leaking wide fill bounds
        # into the framebuffer size inference (the old Track Select regression).
        text = once(text, '    if (opcode == kFullSyncOpcode && ((active_->data_->online_view', '''    if(opcode==kFullSyncOpcode && active_->data_->intro_viewport.enabled()) {
        state->flush();
        auto& workload=state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor];
        for(unsigned i=0;i<workload.fbPairCount;++i)
            active_->data_->intro_viewport.normalise(workload.fbPairs[i]);
    }
    if (opcode == kFullSyncOpcode && ((active_->data_->online_view''')
        text = once(text, '    AdjustSplitViewportCommand(state, command, opcode);', '''    if(opcode==kSetScissorOpcode)data.intro_viewport.scissor(*state);
    else if(opcode==kMoveMemOpcode && command->p0(16,8)==kViewportMoveMemType)
        data.intro_viewport.viewport(*state);
    AdjustSplitViewportCommand(state, command, opcode);''')
        text = once(text, '    if(data.online_view&&data.online_view->suppress_rectangle(*state))return;',
            '    if(data.online_view&&data.online_view->suppress_rectangle(*state))return;\n    if(data.intro_viewport.fill(*state,**display_list))return;')
    put('f3ddkr_rt64.cpp', text)
    for path in AUTH.glob('*.hpp'):
        put(path.name, read(path))
    receipt = {'schema':1, 'version':'1.0.7+beta.10', 'intro':intro, 'inputs':inputs,
        'authors':{p.relative_to(ROOT).as_posix():sha(p) for p in AUTH.iterdir() if p.is_file()},
        'files':{p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file()},
        'policy':'First-party presentation only. Protected sources and dependencies untouched. Owner performs gameplay testing.'}
    (out / 'viewport-flow-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('stage','verify'))
    p.add_argument('--base', type=Path)
    p.add_argument('--intro', action='store_true')
    p.add_argument('--output', type=Path, required=True)
    a=p.parse_args()
    if a.action=='stage':stage(a.base.resolve(strict=True), a.output, a.intro)
    verify(a.output)

if __name__=='__main__':main()
