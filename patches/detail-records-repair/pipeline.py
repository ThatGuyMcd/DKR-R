"""Stage the focused records/multiplayer repair; never edit inputs or run targets."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def once(text,old,new):
 if text.count(old)!=1:raise ValueError('Repair anchor changed: '+old[:150])
 return text.replace(old,new,1)
def model(text):
 text=once(text,'#include "detail_contract.hpp"','#include "detail_contract.hpp"\n#include "detail_metrics.hpp"')
 text=once(text,'    std::vector<Image> images;','''    std::vector<Image> images;
    // Sorted, merged successful replacement spans. Query cost is logarithmic
    // instead of scanning every racer/scenery observation per triangle.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> suppressed;
    detail::FrameMetrics profile;
    unsigned kernel_builds=0,vertex_uploads=0,batches=0,texture_hits=0,texture_captures=0;
    void suppress(std::uint32_t first,std::uint32_t last) {
        auto at=std::lower_bound(suppressed.begin(),suppressed.end(),first,[](const auto& row,auto value){return row.second<value;});
        while(at!=suppressed.end()&&at->first<=last){first=std::min(first,at->first);last=std::max(last,at->second);at=suppressed.erase(at);}
        suppressed.insert(at,{first,last});
    }
    #include "detail_texture_cache.inl"''')
 text=once(text,'auto tex=std::make_shared<const Texture>(netplay::experimental::capture_local_texture(ram,p));','auto tex=retain_texture(r,p);')
 text=once(text,'        if(!kernel(ram.data(),ram.size(),&request,&packet.model)) {','        ++kernel_builds;\n        if(!kernel(ram.data(),ram.size(),&request,&packet.model)) {')
 text=once(text,'    auto& p=*impl_;p.observations.clear();p.images.clear();p.ram=ram;p.scene=scene;p.cursor=kTextureBegin;p.draws=0;',
 '''    auto& p=*impl_;
    p.profile.begin(scene,unsigned(events.size()),p.kernel_builds,p.vertex_uploads,p.batches,p.texture_hits,p.texture_captures);
    detail::MicroScope prepare_cost{p.profile.prepare_us};
    if(p.texture_scene!=scene||p.texture_frame==UINT64_MAX){p.retained_textures.clear();p.retained_texture_bytes=0;p.texture_scene=scene;p.texture_frame=0;}
    ++p.texture_frame;p.suppressed.clear();
    p.observations.clear();p.images.clear();p.ram=ram;p.scene=scene;p.cursor=kTextureBegin;p.draws=0;''')
 # Ensure address order even when a task contains racers but no extra scenery.
 anchor='''        // Preflight racers first. Optional scenery cannot consume their pose,'''
 text=once(text,anchor,'''        std::stable_sort(p.observations.begin(),p.observations.end(),[](const auto& a,const auto& b){return a.e.address<b.e.address;});
        // Preflight racers first. Optional scenery cannot consume their pose,''')
 # A bad optional scenery observation must not discard valid racer packets.
 text=once(text,'    }catch(...){p.enabled=false;}','''    }catch(const std::exception& error){
        std::erase_if(p.observations,[](const auto& o){return o.extra;});
        detail::note({scene,0,0,0,0,1,maximum,error.what()});
    }catch(...){std::erase_if(p.observations,[](const auto& o){return o.extra;});}''')
 old='''    for(const auto& o:p.observations)if(o.replaced && !o.extra && command>=o.e.address-0x80000000U && command<o.e.parameters[0]-0x80000000U)return true;
    return false;'''
 text=once(text,old,'''    const auto at=std::upper_bound(p.suppressed.begin(),p.suppressed.end(),command,[](auto value,const auto& row){return value<row.first;});
    return at!=p.suppressed.begin()&&command<std::prev(at)->second;''')
 text=once(text,'''    for(auto& o:p.observations) {
        if(o.consumed||o.e.address-0x80000000U!=command_address)continue;o.consumed=true;''',
 '''    const auto address=command_address+0x80000000U;
    auto found=std::lower_bound(p.observations.begin(),p.observations.end(),address,[](const auto& o,auto value){return o.e.address<value;});
    if(found==p.observations.end()||found->e.address!=address)return;
    detail::MicroScope submit_cost{p.profile.submit_us};
    for(;found!=p.observations.end()&&found->e.address==address;++found) {
        auto& o=*found;if(o.consumed)continue;o.consumed=true;''')
 begin=text.index('        for(const auto& step:packet->steps) {',text.index('        select(rsp,presentation::normalise_identity('))
 end=text.index('        state.flush();o.replaced=true;++p.draws;',begin)
 text=text[:begin]+'        #include "detail_batch.inl"\n'+text[end:]
 text=once(text,'        state.flush();o.replaced=true;++p.draws;',
  '        state.flush();o.replaced=true;++p.draws;\n        if(!o.extra)p.suppress(o.e.address-0x80000000U,o.e.parameters[0]-0x80000000U);')
 return text
def main(a):
 if a.output.resolve().is_relative_to(ROOT):raise ValueError('Stage outside the checkout')
 a.output.mkdir(parents=True,exist_ok=True)
 for p in (AUTH/'native').iterdir():
  if p.is_file():(a.output/p.name).write_bytes(p.read_bytes())
 # Keep the online sidecar; local split-screen uses the native selector and
 # must not build an expensive parallel copy of the same racers.
 observer=a.output.parent/'all-player-detail/detail_observer.inl'
 data=observer.read_text(encoding='utf-8')
 data=once(data,'!netplay::external_side_effects_allowed())return;',
  '!netplay::external_side_effects_allowed()||!netplay::session().runtime_view().active)return;')
 (a.output/'detail_observer.inl').write_text(data,encoding='utf-8',newline='\n')
 swaps=[]
 for row in json.loads(a.inputs.read_text(encoding='utf-8')):
  old=Path(row['path']);raw=old.read_bytes();text=raw.decode('utf-8').replace('\r\n','\n')
  if old.name=='runtime_enhancements.cpp':
   text=once(text,'''    if(dkr::runtime::netplay::session().runtime_view().active||
       MEM_W(0,RdramAddress(dkr::runtime::revision_addresses::ViewportLayout))!=0)return;''',
    '''    // One local machine can safely use the original fast selector for
    // ALL viewports. Only online peers require independent render-only LODs.
    if(dkr::runtime::netplay::session().runtime_view().active)return;''')
   text=once(text,'"native highest resident"','"native highest resident; local-all-views-v2"')
  elif old.name=='runtime_legacy_mods.cpp':
   text='#include <array>\n#include <cstring>\n'+text
   text,n=re.subn(r'^#include "[^"\n]*record_guest\.inl"','#include "records_guest.inl"',text,flags=re.M)
   if n!=1:raise ValueError('Missing records observer')
   for before,after in [
    ('throw std::runtime_error("Could not capture custom-course records from the owned guest.");',
     'throw std::runtime_error("Could not capture custom-course records from the owned guest: "+(world.error().empty()?std::string("missing records service result"):world.error()));'),
    ('throw std::runtime_error("Custom records could not be installed at the initial owned boundary. Originals were retained.");',
     'throw std::runtime_error("Custom records could not be installed at the initial owned boundary; originals were retained: "+(world.error().empty()?std::string("record tables are not ready"):world.error()));')]:text=once(text,before,after)
   text+='\n// checked-record-observer-v2: revision-owned RAM access, no guest callback.\n'
  elif old.name=='local_model_rt64.cpp':text=model(text)
  elif old.name!='presentation_identity.cpp':raise ValueError('Unexpected repair input')
  def adjacent(m):
   candidate=old.parent/m[1]
   return '#include "'+candidate.resolve().as_posix()+'"' if candidate.is_file() else m[0]
  text=re.sub(r'^#include "([^"\n]+)"',adjacent,text,flags=re.M)
  new=a.output/old.name;data=text.encode('utf-8');new.write_bytes(data)
  swaps.append({**row,'new':str(new.resolve()),'input_sha256':hashlib.sha256(raw).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest()})
 (a.output/'swaps.json').write_text(json.dumps(swaps,indent=2)+'\n',encoding='utf-8')
 print('Records fenced-callback repair, native local LODs and indexed/batched online detail staged; no target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
