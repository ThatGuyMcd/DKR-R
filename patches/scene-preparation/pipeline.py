"""Stage only two owned drivers; never modify a source input or guest payload."""
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
 if text.count(old)!=1:raise ValueError('Scene preparation anchor changed: '+old[:150])
 return text.replace(old,new,1)
def driver(text):
 text='#include "scene_load_work.hpp"\n'+text
 text=once(text,'''        const auto result=run_private(background ? dkr_probe_menu_background_resume : dkr_probe_authored_scene_resume,
            ram_.data(),ram_.size(),background ? &background_context:&context_,40000000);''','''        const auto scene_work_limit=dkr_scene_work::limit(background);
        const auto result=run_private(background ? dkr_probe_menu_background_resume : dkr_probe_authored_scene_resume,
            ram_.data(),ram_.size(),background ? &background_context:&context_,scene_work_limit);''')
 anchor='''        native_=dkr_probe_native_capture();input_=dkr_probe_input_capture();
        dkr_probe_audio_guards_capture(audio_.guards.data());
        std::array<dkr_probe_motor_event,128> motors{};const auto motor_count=dkr_probe_motor_events(motors.data());'''
 text=once(text,anchor,'''        std::fprintf(stderr,"[rollback][scene-work] scene-load-work-v1 site=%u background=%u budget=%llu used=%llu memory-ops=%llu entries=%llu elapsed-us=%lld completed=%u\\n",
            loaded_site,unsigned(background),static_cast<unsigned long long>(scene_work_limit),
            static_cast<unsigned long long>(result.operations),static_cast<unsigned long long>(result.memory_accesses),
            static_cast<unsigned long long>(result.guest_entries),static_cast<long long>(loader_us),unsigned(result.completed));
'''+anchor)
 text=once(text,'''            error=result.blocked ? result.blocked:"Incomplete confirmed suspended frame";''','''            error=result.blocked ? result.blocked:"Incomplete confirmed suspended frame";
            if(result.blocked&&std::strcmp(result.blocked,"operation-budget")==0)
                error+=" (scene loading used its "+std::to_string(scene_work_limit)+" operation allowance)";''')
 return text
def main(a):
 if a.output.resolve().is_relative_to(ROOT):raise ValueError('Stage outside checkout only')
 a.output.mkdir(parents=True,exist_ok=True)
 for p in (AUTH/'native').iterdir():
  if p.is_file():(a.output/p.name).write_bytes(p.read_bytes())
 rows=json.loads(a.inputs.read_text());swaps=[]
 if {r['target'] for r in rows}!={'DKRExperimentalOwnedGameV77','DKRExperimentalOwnedGameV80'} or len(rows)!=2:
  raise ValueError('Both owned revisions need exactly one driver')
 for row in rows:
  old=Path(row['path']);raw=old.read_bytes()
  if old.name!='probe_driver.cpp':raise ValueError('Only stage the owned driver')
  text=driver(raw.decode('utf-8').replace('\r\n','\n'))
  def adjacent(m):
   candidate=old.parent/m[1]
   return '#include "'+candidate.resolve().as_posix()+'"' if candidate.is_file() else m[0]
  text=re.sub(r'^#include "([^"\n]+)"',adjacent,text,flags=re.M)
  new=a.output/row['target']/old.name;new.parent.mkdir(parents=True,exist_ok=True)
  data=text.encode();new.write_bytes(data)
  swaps.append({**row,'new':str(new.resolve()),'input_sha256':hashlib.sha256(raw).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest()})
 (a.output/'swaps.json').write_text(json.dumps(swaps,indent=2)+'\n',encoding='utf-8')
 print('Confirmed scene-load work isolated from menu/race ticks; allocator, memory and transaction guards unchanged. No target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
