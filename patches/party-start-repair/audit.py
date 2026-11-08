"""Static launch gates, retained contracts, exact staged outputs and linked build."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.10'
def need(value,message):
 if not value:raise ValueError(message)
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def sha(path):return pipeline.sha(path.read_bytes())
def bindings(stage,rows):
 build=stage.parent.parent;result=[]
 for r in rows:
  new,old=Path(r['new']),Path(r['path']);project=build/(r['target']+'.vcxproj')
  if project.is_file():
   sources={Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
   need(new.resolve() in sources and old.resolve() not in sources,'Stale compiler binding: '+new.name)
  else:
   graph=(build/'build.ninja').read_text();lines=[x for x in graph.splitlines() if x.startswith('build CMakeFiles/'+r['target']+'.dir/')]
   need(any(' '+new.as_posix()+' ' in x+' ' for x in lines),'Ninja misses final source: '+new.name)
   need(not any(' '+old.as_posix()+' ' in x+' ' for x in lines),'Ninja compiles superseded source: '+old.name)
  result.append({'target':r['target'],'source':new.name})
 return result
def inspect(stage,binary=None,baseline=None,previous=None):
 rows=json.loads((stage/'inputs.json').read_text());swaps,outputs=pipeline.compose(rows,stage)
 need(swaps==json.loads((stage/'swaps.json').read_text()),'Start source receipts changed')
 for name,data in outputs.items():
  need(not (stage/name).resolve().is_relative_to(ROOT),'Output escaped external build')
  need((stage/name).read_bytes()==data,'Unexpected start output: '+name)
 # Previous audits still verify their exact generated stages. Only compiler
 # bindings are followed through these explicit final-source substitutions.
 def mapped(prior_rows):
  result=[]
  for r in prior_rows:
   found=[s for s in swaps if Path(s['path'])==Path(r['new'])]
   need(len(found)<=1,'Ambiguous start compiler substitution')
   result.append({**r,'new':found[0]['new']} if found else r)
  return result
 saved={n:sys.modules.get(n) for n in ('pipeline','transforms')}
 try:
  sys.modules['transforms']=load('start_prior_transforms',ROOT/'patches/lobby-link-repair/transforms.py')
  sys.modules['pipeline']=load('start_prior_pipeline',ROOT/'patches/lobby-link-repair/pipeline.py')
  parent=load('start_prior_audit',ROOT/'patches/lobby-link-repair/audit.py')
 finally:
  for n,v in saved.items():
   if v is None:sys.modules.pop(n,None)
   else:sys.modules[n]=v
 parent.VERSION=VERSION
 parent.bindings=lambda old_stage,old_rows:bindings(stage,mapped(old_rows))
 parent_load=parent.load
 def retained_load(name,path):
  module=parent_load(name,path)
  if path==ROOT/'patches/rollback-connection/audit.py':
   rollback_load=module.load
   def further_load(next_name,next_path):
    result=rollback_load(next_name,next_path)
    if next_path==ROOT/'patches/upstream-sync/audit.py':
     original=result.build_bindings
     result.build_bindings=lambda old_stage,old_rows:original(old_stage,mapped(old_rows))
    return result
   module.load=further_load
  return module
 parent.load=retained_load
 retained=parent.inspect(stage.parent/'lobby-link-repair',None,None,None)
 if previous:
  old=json.loads(previous.read_text())['audit']
  need(retained['lobby_link_contract']==old['lobby_link_contract'],'Playtest 9 authoring changed')
  need(retained['retained']['retained_contracts']==old['retained']['retained_contracts'],'An accepted game/lobby/Store/graphics contract changed')
 baseline_count=0;planning=[]
 if baseline:
  status=(baseline.parent/'GIT-STATUS.txt').read_text()
  for r in json.loads(baseline.read_text()):
   current=sha(ROOT/r['path'])
   if r['path'].startswith('patches/lobby-discovery-plan-20261023/'):
    need('?? '+r['path'] in status,'Cannot exempt tracked planning source')
    planning.append({'path':r['path'],'before_sha256':r['sha256'],'current_sha256':current,'note':'Pre-existing concurrent untracked planning work; not changed by this pipeline.'})
   else:need(current==r['sha256'],'Pre-existing file changed: '+r['path']);baseline_count+=1
 direct=(stage/'direct_session.cpp').read_text();runtime=(stage/'experimental_runtime.cpp').read_text();party=(stage/'party_runtime.cpp').read_text()
 for marker in ('launch_countdown_deadline_=now+std::chrono::seconds(5)',
                'launch_countdown_active_=true','required_start(gate->prepared)','required_start(gate->committed)',
                'gate->roster==party->party.snapshot().generation','gate->rules==room_view_.rules',
                'gate->save_generation==session_save_generation_',
                'gate->launch->compatibility_hash==manifest_hash(manifest_)',
                'TransportTrafficClass::Checkpoint','receipt(message.body,*gate)',
                'protocol::encode_start({0,launch,launch_descriptor_hash(launch)})',
                'start.stage==0','start.stage==1','start.descriptor_hash!=gate->retired',
                'if(prepared||party->local_watching())','std::chrono::seconds(30)',
                'std::chrono::seconds(15)','[lobby][start-release]','[lobby][start-cancel]',
                'dkr::runtime::party_start::forget(this)'):
  need(marker in direct,'Start gate invariant missing: '+marker)
 need('room.countdown_active=false' not in direct,'Party start still suppresses countdown metadata')
 need('protocol::encode_start({2,' not in direct,'Invalid launch stage remains')
 a=direct.index('if(gate->phase==StartPhase::Committing&&required_start(gate->committed))')
 b=direct.index('launch_requested_=true;',a)
 need(b>a and b-a<800,'Host can launch before racer acknowledgements')
 for marker in ('box.quiescent()','world.admit_initial(*held->state,e)','held->magic_actions==0',
                'Conflicting checkpoint offer retry.','ready_retry=now+std::chrono::milliseconds(500)',
                'if(at.epoch<cursor.epoch','[party][admission-progress]',
                '[party][checkpoint] canonical admission verified; Ready sent',
                'admit_owned_observer(&world,*held,e)','Required party admission did not complete within 120 seconds.'):
  need(marker in party,'Checkpoint admission invariant missing: '+marker)
 need(runtime.count(pipeline.transforms.contract())==1,'Start compatibility identity not pinned')
 need('[party][failure]' in runtime,'Runtime loses the actual handoff error')
 # No renderer implementation, guest payload or dependency is replaced here.
 need({Path(s['new']).name for s in swaps}=={'direct_session.cpp','experimental_runtime.cpp','party_runtime.cpp'},'Repair exceeded reviewed translation units')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'[lobby][countdown] five seconds',b'[lobby][start-release]',
                 b'[party][boot]',b'[party][admission-progress]',b'[party][failure]',
                 b'Check Rollback connection',b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE'):
   need(marker in data,'Missing linked feature: '+repr(marker))
 contract=hashlib.sha256()
 for p in sorted([AUTH/'ReleaseOverlay.cmake',AUTH/'pipeline.py',AUTH/'transforms.py',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if p.is_file():contract.update(p.name.encode());contract.update(p.read_bytes().replace(b'\r\n',b'\n'))
 return {'schema':1,'version':VERSION,'party_start_contract':contract.hexdigest(),'runtime_identity_extension':pipeline.transforms.contract(),
  'unchanged_authored_files':baseline_count,'concurrent_planning':planning,'compiler_bindings':bindings(stage,swaps),
  'swaps':swaps,'retained':retained,
  'validation':'Compile/link and static source/binary/archive inspection only. No game, mod, test executable or AppImage was launched. Multiplayer acceptance remains with the user.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','previous','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
