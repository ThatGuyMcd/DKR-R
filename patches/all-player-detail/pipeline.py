"""All-player model detail. Only compose new isolated source copies; never run targets."""
from pathlib import Path
import argparse
from collections import Counter
import hashlib
import json
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
sys.path.insert(0,str(ROOT/'patches/custom-vehicles'))
from guest_pipeline import body

def once(text,old,new):
 if text.count(old)!=1:raise ValueError('All-player detail anchor changed: '+old[:150])
 return text.replace(old,new,1)

def identity(text):
 text=once(text,'#include "presentation_identity.hpp"','#include "presentation_identity.hpp"\n#include "detail_contract.hpp"\n#include <cstring>')
 text=once(text,'struct SubmittedFrame {','struct SubmittedFrame {\n    std::vector<dkr_owned_draw_event> details;')
 text=once(text,'std::deque<SubmittedFrame> g_submitted_frames;',
  'std::array<std::vector<dkr_owned_draw_event>,2> g_detail_maps;\nthread_local std::vector<dkr_owned_draw_event> g_active_detail;\nstd::deque<SubmittedFrame> g_submitted_frames;')
 text=text.replace('g_active_marker_map.clear();','g_active_marker_map.clear();g_active_detail.clear();')
 for index in ('g_recording_buffer','g_recording_buffer & 1U'):
  text=text.replace('g_marker_maps['+index+'].clear();','g_marker_maps['+index+'].clear();g_detail_maps['+index+'].clear();')
 text=once(text,'    g_active_marker_map = std::move(frame.markers);','    g_active_marker_map = std::move(frame.markers);\n    g_active_detail=std::move(frame.details);')
 text=once(text,'    frame.matrices = std::move(g_matrix_maps[g_recording_buffer & 1U]);',
  '    frame.details=std::move(g_detail_maps[g_recording_buffer&1U]);\n    g_detail_maps[g_recording_buffer&1U].reserve(dkr::runtime::detail::Observations);\n    frame.matrices = std::move(g_matrix_maps[g_recording_buffer & 1U]);')
 text=once(text,'        frame.markers.clear();','        frame.markers.clear();frame.details.clear();')
 text=once(text,'    g_lifetimes.clear();','    for(auto& rows:g_detail_maps)rows.clear();\n    g_lifetimes.clear();')
 return text+'\n#include "detail_observer.inl"\n'

def enhancements(text):
 text=once(text,'#include "runtime_enhancements.hpp"','#include "runtime_enhancements.hpp"\n#include "runtime_netplay.hpp"\n#include "detail_contract.hpp"')
 start=text.index('extern "C" void dkr_apply_maximum_racer_detail(');end=text.index('extern "C" void ',start+14)
 part=text[start:end]
 part=once(part,'    if (!dkr::runtime::enhancements::maximum_detail_enabled()) {',
  '''    // Online geometry selection stays canonical, even when peers choose
    // different quality. Its immutable render copy applies the local policy.
    if(dkr::runtime::netplay::session().runtime_view().active||
       MEM_W(0,RdramAddress(dkr::runtime::revision_addresses::ViewportLayout))!=0)return;
    if (!dkr::runtime::enhancements::maximum_detail_enabled()) {''')
 pattern=r'        static std::atomic<unsigned> detail_reports\{0\};.*?static_cast<unsigned>\(context->r8\)\);'
 replacement='''        const auto view=dkr::runtime::revision_addresses::selected_revision()==dkr::runtime::rom::Revision::UsV80?0x8011B634U:0x8011B0B4U;
        dkr::runtime::detail::note({dkr::runtime::presentation::recording_scene_generation(),
            static_cast<unsigned>(context->r16),static_cast<unsigned>(MEM_W(0,RdramAddress(view))),
            authored_lod,static_cast<unsigned>(context->r8),0,true,"native highest resident"});'''
 part,count=re.subn(pattern,lambda m:replacement,part,flags=re.S)
 if count!=1:raise ValueError('Native detail diagnostic site changed')
 return text[:start]+part+text[end:]

def renderer(text):
 text='#include "online_stability/response_trace.hpp"\n#include "online_presentation/frame_policy.hpp"\n#include "online_stability/battle_view.hpp"\n'+text
 text=once(text,'    std::uint32_t scene=0;\n    {',
  '    dkr::online_presentation::ResourceScope resource_scope(lease->epoch());\n    std::uint32_t scene=0;\n    {')
 text=once(text,'        scene=presentation::normalise_identity(event.parameters[0]^',
  '        resource_scope.scene(event.parameters[0]);\n        scene=presentation::normalise_identity(event.parameters[0]^')
 text=once(text,'        last_owned_report_=now;',
  '        dkr::online_presentation::report();\n        last_owned_report_=now;')
 text=once(text,'    queued=true;\n    const auto now=std::chrono::steady_clock::now();',
  '    queued=true;\n    const auto now=std::chrono::steady_clock::now();\n    last_wait_presentation_=now;')
 text=once(text,'    if(now-last_wait_presentation_<std::chrono::milliseconds(33))return;',
  '    if(!dkr::online_presentation::idle_refresh_due(now-last_wait_presentation_,present_count_!=0))return;\n    dkr::online_presentation::count(dkr::online_presentation::Event::IdleRepeat);')
 text=once(text,'    // Salt BEFORE constructing any matrix/shadow identities.',
  '''    const auto view_decision=dkr::online_stability::view_eligibility(online_view_session_.owner,online_view_session_.players,lease->draw_events());
    dkr::online_presentation::view(owned_decode_scratch_->online.focused(),unsigned(view_decision.reason),unsigned(std::max(0,width)),unsigned(std::max(0,height)),cover);
    // Salt BEFORE constructing any matrix/shadow identities.''')
 text=once(text,'     update_screen();}',
  '     update_screen();}\n    if(lease)dkr::online_stability::response::reached(lease->epoch(),lease->descriptor().frame,dkr::online_stability::response::Stage::Renderer);')
 text=once(text,'#include "presentation_identity.hpp"','#include "presentation_identity.hpp"\n#include "detail_contract.hpp"')
 scope='''    dkr::runtime::presentation::TaskIdentityScope identity_scope(
        rdram_snapshot, task->t.data_ptr);'''
 text=once(text,scope,'')
 text=once(text,'    std::uint8_t* decode_bytes=rdram_snapshot;',scope+'''
    const bool detail_requested=enhancements::maximum_detail_enabled()&&!detail::ordinary_events().empty();
    std::uint8_t* decode_bytes=rdram_snapshot;''')
 text=once(text,'    if(camera_requested||cars_requested) {','    if(camera_requested||cars_requested||detail_requested) {')
 anchor='''        camera_online.local_slot,unsigned(platform::online_input_profile()));'''
 text=once(text,anchor,anchor+'''
    // A submission snapshot is only 8 MiB. The existing private camera/car
    // workspace supplies the disjoint 16-MiB decode arena; never write beyond
    // the snapshot, live RDRAM, or the canonical guest's 8-MiB ownership.
    static thread_local LocalModelRT64Pass ordinary_models;
    ordinary_models.prepare(detail_requested?detail::ordinary_events():std::span<const dkr_owned_draw_event>{},
        detail_requested?std::span<std::uint8_t>{camera_decoder_}:std::span<std::uint8_t>{},presentation::task_scene_generation());''')
 old='f3ddkr_.process(*application_, *task,nullptr,nullptr,nullptr,camera_requested?camera_pass_.get():nullptr);'
 count=text.count(old)
 if count!=3:raise ValueError('Normal decoder calls changed: '+str(count))
 return text.replace(old,'f3ddkr_.process(*application_, *task,nullptr,nullptr,detail_requested?&ordinary_models:nullptr,camera_requested?camera_pass_.get():nullptr);')

def local_model(text):
 text=once(text,'#include "local_model_rt64.hpp"','#include "local_model_rt64.hpp"\n#include "detail_contract.hpp"')
 text=once(text,'    std::array<CachedPacket,96> cache;','''    std::array<CachedPacket,96> cache;
    std::array<CachedPacket,128> racer_cache;
    std::size_t racer_cached=0,racer_steps=0;
    unsigned racer_operations=0;
    std::array<unsigned,4> racer_triangles{};''')
 start=text.index('    const Packet* build(');end=text.index('\n};',start)
 part=text[start:end]
 part=once(part,'    const Packet* build(const dkr_owned_draw_event& e) {','''    const Packet* build(const dkr_owned_draw_event& e,bool extra) {
        auto packets=extra?std::span<CachedPacket>{cache}:std::span<CachedPacket>{racer_cache};
        auto& packet_count=extra?cached:racer_cached;
        auto& operations=extra?kernel_operations:racer_operations;
        auto& prepared_steps=extra?packet_steps:racer_steps;
        const auto operation_limit=extra?12000000U:detail::KernelOperations;
        const auto step_limit=extra?65536U:detail::PreparedSteps;''')
 part=part.replace('i<cached','i<packet_count').replace('cache[i]','packets[i]')
 part=once(part,'        if(cached==cache.size() || kernel_operations>=12000000U || packet_steps>=65536)return nullptr;',
  '        if(packet_count==packets.size()||operations>=operation_limit||prepared_steps>=step_limit)throw std::runtime_error(extra?"scenery preparation capacity":"racer preparation capacity");')
 part=once(part,'        auto& entry=cache[cached++];','        auto& entry=packets[packet_count++];')
 part=part.replace('charge{packet_steps,packet}','charge{prepared_steps,packet}').replace('kernel_operations+=packet.model.operations;','operations+=packet.model.operations;')
 part=once(part,'packet.steps.size()>32768 || packet_steps+packet.steps.size()>65536','packet.steps.size()>32768 || prepared_steps+packet.steps.size()>step_limit')
 text=text[:start]+part+text[end:]
 text=once(text,'    p.submitted_triangles=0;','    p.submitted_triangles=0;p.racer_cached=p.racer_steps=p.racer_operations=0;p.racer_triangles.fill(0);')
 text=once(text,'    if(retained>131072)for(auto& entry:p.cache)std::vector<Step>().swap(entry.packet.steps);','''    if(retained>131072)for(auto& entry:p.cache)std::vector<Step>().swap(entry.packet.steps);
    retained=0;for(const auto& entry:p.racer_cache)retained+=entry.packet.steps.capacity();
    if(retained>detail::PreparedSteps*2U)for(auto& entry:p.racer_cache)std::vector<Step>().swap(entry.packet.steps);''')
 text=once(text,'p.observations.size()==128','p.observations.size()==detail::Observations')
 anchor='''        Reader r{ram};
        std::array<std::vector<std::uint32_t>,4> drawn;'''
 text=once(text,anchor,'''        // Preflight racers first. Optional scenery cannot consume their pose,
        // packet or texture preparation capacity. Poses matching across views
        // use one checked packet; only transforms/head angles remain per view.
        for(const auto& o:p.observations)try{(void)p.build(o.e,false);}
        catch(const std::exception& error){detail::note({scene,o.e.parameters[1],o.e.token,0,0,1,maximum,error.what()});}
        Reader r{ram};
        std::array<std::vector<std::uint32_t>,4> drawn;''')
 old='''            packet=p.build(o.e);if(!packet || packet->triangles>32768U-p.submitted_triangles)continue;'''
 text=once(text,old,'''            packet=p.build(o.e,o.extra);
            if(!packet){detail::note({p.scene,o.e.parameters[1],o.e.token,0,0,1,true,"invalid prepared model; original retained"});continue;}
            auto& triangles=o.extra?p.submitted_triangles:p.racer_triangles[o.e.token];
            const auto limit=o.extra?32768U:detail::ViewTriangles;
            if(packet->triangles>limit-triangles){detail::note({p.scene,o.e.parameters[1],o.e.token,0,0,1,true,"view triangle capacity; original retained"});continue;}''')
 old='''        }catch(const std::exception& error){if(p.logged++<8)std::fprintf(stderr,"[graphics][local-detail] retained original mesh: %s\\n",error.what());continue;}'''
 text=once(text,old,'''        }catch(const std::exception& error){detail::note({p.scene,o.e.parameters[1],o.e.token,0,0,1,true,error.what()});continue;}''')
 text=once(text,'        state.flush();o.replaced=true;++p.draws;p.submitted_triangles+=packet->triangles;',
  '''        state.flush();o.replaced=true;++p.draws;
        if(o.extra)p.submitted_triangles+=packet->triangles;else p.racer_triangles[o.e.token]+=packet->triangles;
        if(!o.extra)detail::note({p.scene,o.e.parameters[1],o.e.token,Reader{p.ram}.byte(o.e.parameters[1]+0x3a),0,1,true,"highest resident presentation mesh"});''')
 # Scope identities include actual model and pose variant, not only object/view.
 text=once(text,'p.scene^o.e.parameters[1]^0x4D4F444CU^(o.e.token*0x9E3779B9U)',
  'p.scene^o.e.parameters[1]^presentation::mix_identity(o.e.parameters[2])^(o.e.parameters[7]<<28)^0x4D4F444CU^(o.e.token*0x9E3779B9U)')
 return text

def car(text):
 text=once(text,'#include "vehicle_rt64.hpp"','#include "vehicle_rt64.hpp"\n#include "detail_contract.hpp"')
 return once(text,'    return indices!=0&&in_front;',
  '''    detail::note({presentation::task_scene_generation(),object_,view_,original_lod_,vehicle_lod_,2,maximum_detail_,
        indices!=0&&in_front?"custom vehicle submitted":"custom vehicle not visible; original retained"});
    return indices!=0&&in_front;''')

def bridge(text):
 text='#include "online_presentation/frame_policy.hpp"\n'+text
 text=once(text,'        data_->water_registry=&water::enhanced::task_registry(*state,',
  '''        const auto resource_scene=dkr::online_presentation::asset_scene(presentation::task_scene_generation(),
            water::streaming::cache_state(&state->ext.textureCache->textureMap.replacementMap).era);
        data_->water_registry=&water::enhanced::task_registry(*state,''')
 for call in ('water::enhanced::task_registry(*state,','water::residency().prepare(*state,*data_->water_registry,'):
  text=once(text,call+'presentation::task_scene_generation(),',
   call+'resource_scene,')
 text=once(text,'    if(cars_requested)kRDRAMSize=0x1000000U;','    if(cars_requested||models)kRDRAMSize=0x1000000U;')
 return once(text,'if(auto* models=active_->data_->local_models)',
  'if(auto* models=active_->data_->local_models;models&&(!active_->data_->online_view||!active_->data_->online_view->suppress_rectangle(*state)))')

def owned(text):
 text=once(text,'static dkr_owned_draw_event local_models[128],local_objects[512];','static dkr_owned_draw_event local_models[256],local_objects[512];')
 return once(text,'if(local_model_count==128)','if(local_model_count==256)')

def kernel(text):
 # Allocate each vertex buffer 128 KiB; an 8192-vertex model cannot overlap
 # its other buffer/deltas. The whole private arena remains below D00000.
 for old,new in [('VERTEX2=0xC30000','VERTEX2=0xC40000'),('DELTAS=0xC40000','DELTAS=0xC60000'),('FRACTION=0xC50000','FRACTION=0xC80000'),('COMMANDS=0xC60000','COMMANDS=0xC90000'),('COMMAND_END=0xCA0000','COMMAND_END=0xCD0000'),('MAX_VERTICES=4096','MAX_VERTICES=8192')]:
  text=once(text,old,new)
 return text

def guest(text,normal):
 counts=Counter()
 span=body(text,'spawn_object')
 if span:
  start,end=span;part=text[start:end]
  # Both reviewed revisions save the original flags at sp+6c and restore
  # them later. Do not touch ABI-visible registers or non-racer objects.
  if 'MEM_W(0X6C, ctx->r29) = ctx->r5;' not in part:raise ValueError('Spawn flags ABI changed')
  at=part.index('    load_object_header(rdram, ctx);');match=re.search(r'    after_\d+:',part[at:])
  if not match:raise ValueError('Header-return anchor missing')
  pos=at+match.end()
  part=part[:pos]+'''
    // all-player-residency-v1: every racer spawn uses identical residency on
    // all peers, independent of local quality. Ghost/boss calls with no skip
    // bits are unchanged, as are intentional NO_LODS previews.
    if((uint32_t)ctx->r2>=0x80000000U&&(uint32_t)ctx->r2<=0x807FFF88U&&
        MEM_BU(0x54,ctx->r2)==1U)
        MEM_W(0X6C,ctx->r29)&=~12U;
'''+part[pos:]
  text=text[:start]+part+text[end:];counts['residency']=1
 if normal:
  span=body(text,'render_mesh')
  if span:
   start,end=span;part=text[start:end]
   before='''
    uint32_t detail_event[19]={0};
    extern void dkr_detail_mesh_begin(uint8_t*,recomp_context*,uint32_t*);
    extern void dkr_detail_mesh_end(uint8_t*,recomp_context*,uint32_t*);
    dkr_detail_mesh_begin(rdram,ctx,detail_event);
'''
   part,n=re.subn(r'(?m)^    return;$','    dkr_detail_mesh_end(rdram,ctx,detail_event);\n    return;',part)
   if n!=1:raise ValueError('Mesh observer needs its single reviewed exit')
   text=text[:start]+before+part+text[end:];counts['mesh']=1
 return text,counts

def main(a):
 if a.output.resolve().is_relative_to(ROOT):raise ValueError('Use an isolated build outside the checkout')
 a.output.mkdir(parents=True,exist_ok=True)
 for path in (AUTH/'native').iterdir():
  if path.is_file():(a.output/path.name).write_bytes(path.read_bytes())
 transforms={'presentation_identity.cpp':identity,'runtime_enhancements.cpp':enhancements,'rt64_renderer.cpp':renderer,
  'local_model_rt64.cpp':local_model,'vehicle_rt64.cpp':car,'f3ddkr_rt64.cpp':bridge,'probe_native.c':owned,'visual_model_kernel.c':kernel}
 swaps=[];counts={};seen={}
 for row in json.loads(a.inputs.read_text(encoding='utf-8')):
  path=Path(row['path']);raw=path.read_bytes();text=raw.decode('utf-8').replace('\r\n','\n');name=path.name
  if row.get('guest'):
   normal=row['target'].startswith('DKRRecompiledFuncs');text,found=guest(text,normal)
   counts.setdefault(row['target'],Counter()).update(found)
   if not found:continue
  elif name=='f3ddkr_rt64.cpp':
   # Keep the accepted header and bind the normal appearance module just
   # before its existing single material comparison/flush.
   material=Path(row['water_header']).read_text(encoding='utf-8')
   material='#include "water_style/appearance.hpp"\n'+material
   material=once(material,'    if(std::memcmp(&material,&state.waterMaterial,sizeof(material))!=0) {',
    '''    static_assert(dkr::runtime::water::style::kDkrStyleFlag==WATER_STYLE_DKR);
    if(material.identity.x>0 && surface)
        dkr::runtime::water::style::apply(material,surface->kind,
            registry.metadata() && registry.metadata()->profile.has_value());
    if(std::memcmp(&material,&state.waterMaterial,sizeof(material))!=0) {''')
   (a.output/'styled_water_rt64.hpp').write_text(material,encoding='utf-8',newline='\n')
   text=bridge(text)
   text=once(text,'#include "enhanced_water_rt64.hpp"','#include "styled_water_rt64.hpp"')
  elif name=='runtime_ui.cpp':
   # The final UI TU already pins its adjacent graphics include. Stage only
   # that include; keep every other established launcher section unchanged.
   match=re.search(r'^#include "([^"\n]*runtime_graphics_ui\.inl)"',text,re.M)
   if not match:raise ValueError('Missing final graphics UI include')
   graphics=Path(match[1]);graphics=graphics if graphics.is_absolute() else path.parent/graphics
   if not graphics.is_file():graphics=Path(row['graphics'])
   ui=graphics.read_text(encoding='utf-8')
   ui=once(ui,'Use the most detailed racer models.','Use the highest-detail characters and vehicles in every view, online and offline. Split-screen uses more graphics power.')
   # Existing transition binding; the shared setting UI lives in normal source.
   ui=once(ui,'        if(SettingsDropdown("water-reflections",',
    '        #include "water_style/settings_ui.inl"\n        if(SettingsDropdown("water-reflections",')
   ui=once(ui,'water::reset_appearance();SaveSettings();','water::reset_appearance();dkr::runtime::water::style::reset();SaveSettings();')
   (a.output/'all_detail_graphics.inl').write_text(ui,encoding='utf-8',newline='\n')
   text=text[:match.start()]+'#include "all_detail_graphics.inl"'+text[match.end():]
  else:text=transforms[name](text)
  def adjacent(m):
   p=path.parent/m[1]
   return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
  text=re.sub(r'^#include "([^"\n]+)"',adjacent,text,flags=re.M)
  target=a.output/row['target']/name;target.parent.mkdir(parents=True,exist_ok=True)
  data=text.encode('utf-8')
  if target in seen and seen[target]!=data:raise ValueError('Conflicting source target')
  seen[target]=data;target.write_bytes(data)
  swaps.append({**row,'new':str(target.resolve()),'input_sha256':hashlib.sha256(raw).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest()})
 for target,values in counts.items():
  expected=Counter(residency=1,mesh=1) if target.startswith('DKRRecompiledFuncs') else Counter(residency=1)
  if values!=expected:raise ValueError('Missing all-mode hooks: '+target+str(values))
 if len(counts)!=4:raise ValueError('Both ordinary and owned revisions are required')
 (a.output/'swaps.json').write_text(json.dumps(swaps,indent=2)+'\n',encoding='utf-8')
 (a.output/'coverage.json').write_text(json.dumps({key:dict(value) for key,value in counts.items()},indent=2)+'\n',encoding='utf-8')
 print('All-player detail staged: immutable normal/owned rendering, per-view racer capacity, identical residency; no target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
