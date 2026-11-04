"""Compose confirmed spectator observation in external build copies only."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
import link_pipeline
import owned_pipeline
import runtime_pipeline
import ui_pipeline
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def write(path,data):
 if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)

def once(text,old,new):
 if text.count(old)!=1:raise ValueError('Spectator integration anchor changed: '+old[:150])
 return text.replace(old,new,1)

def driver(text):
 text='#include "online_stability/live_delay.hpp"\n#include "confirmed_observer.hpp"\n'+text
 text=once(text,'Step Driver::step(bool allow_advance) {',
  'Step Driver::step(bool allow_advance) {\n    dkr::online_stability::WorkScope response_work(statistics_.next_frame,statistics_.replayed_frames);')
 text=once(text,'    value.actual[owner] = input;',
  '    dkr::online_stability::input_arrival(owner,frame);\n    value.actual[owner] = input;')
 old='''        confirmed_inputs_ = frame->actual;
        ++statistics_.confirmed_frames;'''
 new='''        // An observer sees actual, committed inputs only. Observation is
        // noexcept and opt-in; spectator failures never abort this commit.
        dkr::runtime::spectators::observe_confirmed(configuration_.epoch,frame->number,
            {{{frame->actual[0].buttons,frame->actual[0].stick_x,frame->actual[0].stick_y,frame->actual[0].accelerator,frame->actual[0].brake,frame->actual[0].driving_flags},
              {frame->actual[1].buttons,frame->actual[1].stick_x,frame->actual[1].stick_y,frame->actual[1].accelerator,frame->actual[1].brake,frame->actual[1].driving_flags},
              {frame->actual[2].buttons,frame->actual[2].stick_x,frame->actual[2].stick_y,frame->actual[2].accelerator,frame->actual[2].brake,frame->actual[2].driving_flags},
              {frame->actual[3].buttons,frame->actual[3].stick_x,frame->actual[3].stick_y,frame->actual[3].accelerator,frame->actual[3].brake,frame->actual[3].driving_flags}}},
            frame->actual_command.words,frame->output.scene_boundary);
'''+old
 return once(text,old,new)

def runtime(text):
 text='#include "owner_export.hpp"\n'+text
 text=once(text,'                Session session(*world);','''                dkr::runtime::spectators::OwnerExport spectator_export(launch->match_id,initial.host);
                Session session(*world);''')
 anchor='''                    const auto& stats=session.statistics();
                    if(now>=report_at) {'''
 return once(text,anchor,'''                    const auto& stats=session.statistics();
                    spectator_export.poll(session.epoch(),stats.next_frame,stats.confirmed_frames,
                        session.presentation_ready(),*world,[](std::span<const std::uint8_t> bytes){return digest(bytes);});
                    if(now>=report_at) {''')

def main(a):
 out=a.output.resolve()
 if out.is_relative_to(ROOT):raise ValueError('Spectator output must stay outside the checkout')
 out.mkdir(parents=True,exist_ok=True)
 for p in (AUTH/'native').iterdir():
  if p.is_file():write(out/p.name,p.read_bytes())
 rows=json.loads(a.inputs.read_text(encoding='utf-8'))
 transforms={'experimental_runtime.cpp':runtime_pipeline.runtime,'experimental_rollback.cpp':driver,
  'experimental_timeline.cpp':runtime_pipeline.timeline,
  'direct_session.cpp':link_pipeline.direct,'netplay_protocol.cpp':link_pipeline.protocol,'online_mod_admission.cpp':link_pipeline.mods,
  'quick_join_transport.cpp':link_pipeline.transport,
  'camera_rt64.cpp':runtime_pipeline.camera,'game_main.cpp':runtime_pipeline.game_main,'replay_probe_capture.cpp':runtime_pipeline.capture,
  'game_registration.cpp':runtime_pipeline.registration}
 if not set(transforms).issubset({Path(r['path']).name for r in rows}):
  raise ValueError('Expected final owned runtime, rollback driver and authenticated lobby sources')
 headers={}
 for leaf,kind in [('experimental_eeprom','eeprom'),('experimental_pak','paks'),('experimental_magic_codes','magic')]:
  cpp=next(Path(r['path']) for r in rows if Path(r['path']).name==leaf+'.cpp')
  h=cpp.with_suffix('.hpp');new=out/(leaf+'.hpp')
  write(new,owned_pipeline.header(h.read_text(encoding='utf-8'),kind,h).encode('utf-8'))
  headers[leaf+'.hpp']=new
 # Keep the transitive Pak class definition coherent too. The output mailbox
 # header is supplied by the final shared-save overlay, not necessarily the
 # same capsule directory as the netplay core.
 for row in rows:
  origin=Path(row['path']);source=origin.read_text(encoding='utf-8')
  found=re.search(r'^#include "([^"\n]*experimental_confirmed_output[.]hpp)"',source,re.M)
  if not found:continue
  candidate=Path(found[1])
  if not candidate.is_absolute():candidate=origin.parent/candidate
  if not candidate.is_file():continue
  content=candidate.read_text(encoding='utf-8')
  for leaf,h in headers.items():content=re.sub(r'^#include "[^"\n]*'+re.escape(leaf)+r'"','#include "'+h.as_posix()+'"',content,flags=re.M)
  def output_include(m):
   p=candidate.parent/m[1]
   return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
  content=re.sub(r'^#include "([^"\n]+)"',output_include,content,flags=re.M)
  new=out/'experimental_confirmed_output.hpp';write(new,content.encode('utf-8'));headers[new.name]=new;break
 if 'experimental_confirmed_output.hpp' not in headers:raise ValueError('Final confirmed output/Pak header could not be resolved')
 swaps=[]
 for row in rows:
  old=Path(row['path']);raw=old.read_bytes();text=raw.decode('utf-8').replace('\r\n','\n')
  if old.name=='runtime_ui.cpp':text=ui_pipeline.ui(text,old,out)
  elif old.name in transforms:text=transforms[old.name](text)
  elif old.name=='probe_driver.cpp' and row['target'].startswith('DKRExperimentalOwnedGame'):text=owned_pipeline.driver(text)
  elif not any(leaf in text for leaf in headers):continue
  for leaf,h in headers.items():
   text=re.sub(r'^#include "[^"\n]*'+re.escape(leaf)+r'"','#include "'+h.as_posix()+'"',text,flags=re.M)
  def adjacent(m):
   candidate=old.parent/m[1]
   return '#include "'+candidate.resolve().as_posix()+'"' if candidate.is_file() else m[0]
  text=re.sub(r'^#include "([^"\n]+)"',adjacent,text,flags=re.M)
  new=out/row['target']/old.name;new.parent.mkdir(parents=True,exist_ok=True);data=text.encode('utf-8');write(new,data)
  swaps.append({**row,'new':str(new),'input_sha256':hashlib.sha256(raw).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest()})
 (out/'swaps.json').write_text(json.dumps(swaps,indent=2)+'\n',encoding='utf-8')
 (out/'contract.json').write_text(json.dumps({'schema':2,'spectator_runtime':runtime_pipeline.contract(),'ports':4,'spectators':8},indent=2)+'\n',encoding='utf-8')
 print('Confirmed spectator stream staged; no speculative snapshot export, gameplay or test execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
