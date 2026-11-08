"""Stage the final owned-menu bridge without changing any input or dependency."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]

def once(text,old,new):
 if text.count(old)!=1:raise ValueError('Owned vehicle menu anchor changed: '+old[:140])
 return text.replace(old,new,1)

def runtime(text):
 text='#include "owned_vehicle_menu.hpp"\n'+text
 text=once(text,'thread_local WorldState* private_world=nullptr;', '''thread_local WorldState* private_world=nullptr;
struct OwnedVehicleDrawCallbacks {RecompiledEntrypoint frame=nullptr,text_width=nullptr;};
thread_local OwnedVehicleDrawCallbacks owned_vehicle_draw_callbacks;''')
 text=once(text,'        if(name=="dkr_achievements_context") {','''        if(name=="dkr_owned_vehicle_menu_draw") {
            arity(0);
            const auto callbacks=owned_vehicle_draw_callbacks;
            if(!state_->private_owner||!state_->vehicles||!callbacks.frame||!callbacks.text_width)
                throw mods::Error("Owned vehicle menu has no private drawing binding.");
            state_->vehicles->draw({ram,guest_memory_size()},*ctx,state_->calls,callbacks.frame,callbacks.text_width);
            result=1;
            static std::atomic<unsigned> reports{0};
            if(reports.fetch_add(1,std::memory_order_relaxed)<4)
                std::fprintf(stderr,"[custom-vehicles][owned-menu] owned-vehicle-menu-v1: private picker, portraits, frame and text submitted\\n");
        }
        else if(name=="dkr_achievements_context") {''')
 text=once(text,'void ModWorld::mix_music(','''int draw_owned_vehicle_menu(ModWorld& world,std::uint8_t* ram,recomp_context* ctx,
    RecompiledEntrypoint frame,RecompiledEntrypoint text_width) noexcept {
    if(!ram||!ctx||!frame||!text_width)return -1;
    // Nested native callbacks retain the same world and restore their caller's
    // entrypoints even when the checked guest trampoline rejects a callback.
    struct Binding {
        OwnedVehicleDrawCallbacks before=owned_vehicle_draw_callbacks;
        Binding(RecompiledEntrypoint f,RecompiledEntrypoint w){owned_vehicle_draw_callbacks={f,w};}
        ~Binding(){owned_vehicle_draw_callbacks=before;}
    } binding(frame,text_width);
    std::uint64_t result=0;
    const auto status=world.dispatch("dkr_owned_vehicle_menu_draw",ram,ctx,nullptr,0,nullptr,0,result);
    return status==1&&result==1?1:-1;
}
void ModWorld::mix_music(''')
 return text

def driver(text):
 text=once(text,'#include "owned_mod_calls.hpp"','#include "owned_mod_calls.hpp"\n#include "owned_vehicle_menu.hpp"\n#include "owned_vehicle_calls.inl"')
 text=once(text,'                if(std::strcmp(name,"dkr_sdk2_pulse")==0) {','''                if(std::strcmp(name,"dkr_owned_vehicle_menu_draw")==0) {
                    if(!result||count||!world.mods_)return -1;
                    const auto status=dkr::runtime::legacy::draw_owned_vehicle_menu(*world.mods_,ram,ctx,
                        dkr_owned_vehicle_menu::frame,dkr_owned_vehicle_menu::text_width);
                    *result=status==1?1U:0U;return status;
                }
                if(std::strcmp(name,"dkr_sdk2_pulse")==0) {''')
 return text

def main(a):
 if a.output.resolve().is_relative_to(ROOT):raise ValueError('Stage outside checkout only')
 a.output.mkdir(parents=True,exist_ok=True)
 for p in (AUTH/'native').iterdir():
  if p.is_file():(a.output/p.name).write_bytes(p.read_bytes())
 sys.path.insert(0,str(ROOT/'patches/custom-vehicles'))
 spec=importlib.util.spec_from_file_location('original_vehicle_guest',ROOT/'patches/custom-vehicles/guest_pipeline.py')
 guest=importlib.util.module_from_spec(spec);spec.loader.exec_module(guest)
 swaps=[];coverage={'runtime':0,'driver':0,'77':0,'80':0}
 for row in json.loads(a.inputs.read_text(encoding='utf-8')):
  old=Path(row['path']);raw=old.read_bytes();text=raw.decode('utf-8').replace('\r\n','\n')
  if old.name=='runtime_legacy_mods.cpp':text=runtime(text);coverage['runtime']+=1
  elif old.name=='probe_driver.cpp':text=driver(text);coverage['driver']+=1
  elif old.suffix=='.c':
   span=guest.body(text,'trackmenu_setup_render')
   if not span:continue
   start,end=span;body=text[start:end]
   body=once(body,'''        extern void dkr_vehicle_menu_draw(uint8_t*,recomp_context*,void(*)(uint8_t*,recomp_context*),void(*)(uint8_t*,recomp_context*));
        dkr_vehicle_menu_draw(rdram,&vc,menu_element_render,get_text_width);''','''        uint64_t vehicle_draw_result=0;
        if(dkr_probe_mod_dispatch("dkr_owned_vehicle_menu_draw",rdram,&vc,0,0,0,0,&vehicle_draw_result)!=1 || !vehicle_draw_result)
            dkr_probe_block("owned-vehicle-menu-draw-unavailable");''')
   text=text[:start]+body+text[end:]
   coverage[row['target'][-2:]]+=1
  else:raise ValueError('Unexpected menu input')
  # Preserve adjacent quote lookup from the composed source, not an older SDK
  # header. New, uniquely named bridge includes resolve in this new stage.
  def adjacent(m):
   candidate=old.parent/m[1]
   return '#include "'+candidate.resolve().as_posix()+'"' if candidate.is_file() else m[0]
  text=re.sub(r'^#include "([^"\n]+)"',adjacent,text,flags=re.M)
  new=a.output/row['target']/old.name;new.parent.mkdir(parents=True,exist_ok=True)
  data=text.encode('utf-8');new.write_bytes(data)
  swaps.append({**row,'new':str(new.resolve()),'input_sha256':hashlib.sha256(raw).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest()})
 if coverage!={'runtime':1,'driver':2,'77':1,'80':1}:raise ValueError('Incomplete owned menu coverage: '+str(coverage))
 (a.output/'swaps.json').write_text(json.dumps(swaps,indent=2)+'\n',encoding='utf-8')
 (a.output/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n',encoding='utf-8')
 print('Owned vehicle menu staged for both revisions; ordinary drawing and guest inputs unchanged.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
