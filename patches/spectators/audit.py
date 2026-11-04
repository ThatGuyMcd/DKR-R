"""Static receipts/scope checks only. Never executes a game or test target."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
import runtime_pipeline
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.2'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,message):
 if not ok:raise ValueError(message)
def section(text,start,end):
 pos=text.index(start);return text[pos:text.index(end,pos)]
def inspect(stage,binary=None,baseline=None):
 rows=json.loads((stage/'swaps.json').read_text(encoding='utf-8'))
 allowed={'DKRNetplayCore','DKRPortGame','DKRCameraPresentation','DKRExperimentalOwnedGameV77','DKRExperimentalOwnedGameV80'}
 require(rows and all(r['target'] in allowed for r in rows),'Unexpected spectator source target')
 owned=0
 for row in rows:
  old,new=Path(row['path']),Path(row['new'])
  require(new.suffix=='.cpp' and not new.resolve().is_relative_to(ROOT),'Spectator edit escaped the external C++ stage')
  require(sha(old)==row['input_sha256'] and sha(new)==row['output_sha256'],'Staged source changed after its receipt: '+new.name)
  before=old.read_text(encoding='utf-8');after=new.read_text(encoding='utf-8')
  if old.name=='experimental_rollback.cpp':
   arrival='    dkr::online_stability::input_arrival(owner,frame);'
   work='    dkr::online_stability::WorkScope response_work(statistics_.next_frame,statistics_.replayed_frames);'
   require(after.splitlines().count(arrival)==1,'Remote-arrival observer is missing or duplicated')
   require(after.splitlines().count(work)==1,'Owner-step timing observer is missing or duplicated')
   # Compare policy lines read-only after removing exactly the input observer
   # and wall-clock scope. Neither observer mutates canonical driver state.
   for a,b in [('InputResult Driver::receive(','FrameInputs Driver::predict('),('Step Driver::step(','} // namespace dkr::runtime::netplay::experimental')]:
    if a in before and b in before:require(section(before,a,b).splitlines()==[line for line in section(after,a,b).splitlines() if line not in (arrival,work)],'Racing input/prediction/rollback policy changed')
   require('observe_confirmed(configuration_.epoch,frame->number' in after,'Committed input hook is missing')
  if old.name in ('experimental_eeprom.cpp','experimental_pak.cpp','experimental_magic_codes.cpp'):
   name={'experimental_eeprom.cpp':'Eeprom','experimental_pak.cpp':'Paks','experimental_magic_codes.cpp':'MagicCodes'}[old.name]
   a='bool '+name+'::restore(';b='bool '+name+'::commit('
   require(section(before,a,b)==section(after,a,b),'Ordinary irreversible-cursor rollback guard changed')
  if old.name=='probe_driver.cpp':
   owned+=1
   for a,b in [('    PreparationStep prepare_scene(std::uint64_t scene_hash','private:\n    bool has_confirmed_scene()'),
               ('    dkr_probe_result run_private(','    std::unique_ptr<dkr::runtime::legacy::ModWorld> mods_;')]:
    require(section(before,a,b)==section(after,a,b),'Guest work limits or scene transaction changed')
   for marker in ('register_owned_observer(static_cast<OwnedGame*>(this)', 'ConfirmedAdmission staged{save_,paks_,magic_}',
                  'if(!restore_state(bytes,error,&staged))','if(!presentation_->quiescent())return Work::Pending;',
                  'if(scene_sessions_&&!observer_read_only_)','admission?0:confirmed_count_'):
    require(marker in after,'Owned spectator admission/safety is missing: '+marker)
  if old.name=='direct_session.cpp':
   require('if(party_join&&!party->enabled&&!watch_join)party_join=false;' in after,
           'Racer joining does not follow the host spectator policy')
   require('This host has disabled spectators. Join as a racer.' in after,
           'A host with spectators disabled must reject watch requests')
  if old.name=='runtime_ui.cpp':
   require('g_online_party_join' not in after,'The removed join-side capability toggle remains')
   require(after.count('"online_allow_spectators"')==1 and '"online_allow_spectators="' in after,
           'The host spectator setting is not persisted and loaded')
 require(owned==2,'Both ROM revision owners must be staged')
 online_ui=(stage/'runtime_online_ui.inl').read_text(encoding='utf-8')
 require('OlSwitchRow("##spectators","Spectator-capable lobby"' in online_ui,
         'Host settings are missing the spectator-capable lobby control')
 require('ImGui::Checkbox("Spectator-capable lobby"' not in online_ui,
         'Join Lobby still contains the removed spectator capability checkbox')
 native=[]
 for source in sorted((AUTH/'native').iterdir()):
  if not source.is_file():continue
  require(sha(stage/source.name)==sha(source),'Authored native source was not staged: '+source.name)
  native.append({'name':source.name,'sha256':sha(source)})
 contract=json.loads((stage/'contract.json').read_text(encoding='utf-8'))
 require(contract['schema']==2 and contract['ports']==4 and contract['spectators']==8 and re.fullmatch('[a-f0-9]{64}',contract['spectator_runtime']),'Party contract is missing')
 require(contract['spectator_runtime']==runtime_pipeline.contract(),'Staged party contract does not match the current authored pipeline')
 runtime=next(Path(r['new']).read_text(encoding='utf-8') for r in rows if Path(r['new']).name=='experimental_runtime.cpp')
 require('base+="'+contract['spectator_runtime']+'"' in runtime,'Peer admission does not include the spectator runtime contract')
 registration=next(Path(r['new']).read_text(encoding='utf-8') for r in rows if Path(r['new']).name=='game_registration.cpp')
 require('dkr_experimental_bootstrap_active()&&dkr::runtime::spectators::party_bootstrap_active()' in registration,
         'The verified memory-only party boot is not admitted by game registration')
 kept=[]
 if baseline:
  for row in json.loads(baseline.read_text(encoding='utf-8')):
   require(sha(ROOT/row['path'])==row['sha256'],'Pre-existing authored file changed: '+row['path']);kept.append(row['path'])
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'Party runtime transport queue is full.',b'Observer baseline has inconsistent irreversible participants.',
     b'Preparing player swap',b'T.T.-style director',b'custom vehicle submitted',b'complete-family selection',b'local-all-views-v2',b'scene-load-work-v1'):
   require(marker in data,'Linked feature/retained code marker is missing: '+repr(marker))
 return {'schema':2,'version':VERSION,'contract':contract,'swaps':rows,'authored_native':native,
   'pre_existing_files_unchanged':len(kept),'binary_sha256':sha(binary) if binary else None,
   'validation':'Compilation/linking and static source/binary/archive checks only. No game, mod, test executable or AppImage was launched. Owner gameplay acceptance remains required.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path)
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
 else:print(json.dumps(result,indent=2))
