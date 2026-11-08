"""Source composition, retained contracts, compiler bindings and binary checks."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.9'
def need(ok,message):
 if not ok:raise ValueError(message)
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def sha(path):return pipeline.sha(path.read_bytes())
def bindings(stage,rows):
 build=stage.parent.parent;result=[]
 for row in rows:
  new,old=Path(row['new']),Path(row['path']);project=build/(row['target']+'.vcxproj')
  if project.is_file():
   sources={Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
   need(new.resolve() in sources and old.resolve() not in sources,'Stale final compiler binding: '+new.name)
  else:
   graph=(build/'build.ninja').read_text();lines=[x for x in graph.splitlines() if x.startswith('build CMakeFiles/'+row['target']+'.dir/')]
   need(any(' '+new.as_posix()+' ' in x+' ' for x in lines),'Ninja misses final source: '+new.name)
   need(not any(' '+old.as_posix()+' ' in x+' ' for x in lines),'Ninja still compiles superseded source: '+old.name)
  result.append({'target':row['target'],'source':new.name})
 return result
def inspect(stage,binary=None,baseline=None,previous=None):
 rows=json.loads((stage/'inputs.json').read_text());swaps,includes,outputs=pipeline.compose(rows,stage)
 need(json.loads((stage/'swaps.json').read_text())==swaps,'Source replacement receipts changed')
 need(json.loads((stage/'includes.json').read_text())==includes,'Include replacement receipts changed')
 for name,data in outputs.items():
  need(not (stage/name).resolve().is_relative_to(ROOT),'Repair output escaped external staging')
  need((stage/name).read_bytes()==data,'Unreviewed staged output: '+name)
 # Load the complete previous audit with its own adjacent module bindings.
 prior={name:sys.modules.get(name) for name in ('pipeline','transforms')}
 try:
  sys.modules['transforms']=load('repair_retained_transforms',ROOT/'patches/rollback-connection/transforms.py')
  sys.modules['pipeline']=load('repair_retained_pipeline',ROOT/'patches/rollback-connection/pipeline.py')
  parent=load('repair_retained_audit',ROOT/'patches/rollback-connection/audit.py')
 finally:
  for name,value in prior.items():
   if value is None:sys.modules.pop(name,None)
   else:sys.modules[name]=value
 parent.VERSION=VERSION
 # Earlier compiler-binding assertions must follow the explicitly recorded
 # final source substitutions, not require compiling superseded copies.
 def map_rows(prior_rows):
  out=[]
  for r in prior_rows:
   matches=[n for n in swaps if n['path']==r['new']]
   need(len(matches)<=1,'Ambiguous final source composition')
   out.append({'target':r['target'],'path':r['path'],'new':matches[0]['new']} if matches else r)
  return out
 parent.bindings=lambda old_stage,old_rows:bindings(stage,map_rows(old_rows))
 original_load=parent.load
 def retained_load(name,path):
  module=original_load(name,path)
  if path==ROOT/'patches/upstream-sync/audit.py':
   original_bindings=module.build_bindings
   module.build_bindings=lambda old_stage,old_rows:original_bindings(old_stage,map_rows(old_rows))
  return module
 parent.load=retained_load
 retained=parent.inspect(stage.parent/'rollback-connection',None,None,None)
 if previous:
  old=json.loads(previous.read_text())['audit']
  need(retained['connection_contract']==old['connection_contract'],'Prior Rollback diagnostic authoring changed')
  need(retained['retained_contracts']==old['retained_contracts'],'An accepted game/lobby/Store/spectator contract changed')
 unchanged=0;concurrent_planning=[]
 if baseline:
  status=(baseline.parent/'GIT-STATUS.txt').read_text()
  for r in json.loads(baseline.read_text()):
   current=sha(ROOT/r['path'])
   if r['path'].startswith('patches/lobby-discovery-plan-20261023/'):
    need('?? '+r['path'] in status,'Cannot exempt a tracked file from the baseline audit')
    concurrent_planning.append({'path':r['path'],'before_sha256':r['sha256'],'current_sha256':current,
     'reason':'Pre-existing untracked planning work updated concurrently outside this repair; not modified by this pipeline.'})
    continue
   need(current==r['sha256'],'Pre-existing file changed: '+r['path']);unchanged+=1
 direct=(stage/'direct_session.cpp').read_text();context=(stage/'lobby_context.inl').read_text()
 for marker in ('for(unsigned p=0;p<room.players.size();++p)room.players[p].slot=std::uint8_t(p);',
                'protocol::encode_start({1,launch,launch_descriptor_hash(launch)})',
                'protocol::encode_start({1,*party->launch,launch_descriptor_hash(*party->launch)})',
                'start.stage==1','if(room_bytes.empty())','room.generation==party->party.snapshot().generation',
                'room_view_.rules=room.rules;room_view_.phase=room.phase;input_delay_=room.input_delay_frames;',
                'Waiting for a completed two-way racer connection.',
                'room_view_.generation==party->party.snapshot().generation',
                'An acceptance retry cannot reset a live client',
                'if(state_==ConnectionState::Failed||state_==ConnectionState::Offline)continue;'):
  need(marker in direct,'Lobby repair invariant missing: '+marker)
 need('protocol::encode_start({2,' not in direct,'Invalid stage-2 launch remains')
 begin=direct.index('std::uint64_t& DirectSession::outbound_sequence(');end=direct.index('bool DirectSession::send_join_request()',begin)
 sequence=direct[begin:end]
 need(sequence.index('party->peer(address)')<sequence.index('for (PendingRecord& pending'),'Provisional counter still overrides promoted peer counter')
 need('view.state==ConnectionState::Failed||view.state==ConnectionState::Offline' in context,'UI still masks terminal failure')
 for leaf,marker,forbidden in [('party_link.cpp','LinkRegistry& link_registry()','std::mutex links_mutex;'),
                               ('rollback_check.cpp','CheckRegistry& check_registry()','std::mutex registry_mutex;'),
                               ('lobby_adapter.cpp','RemovalRegistry& removal_registry()','std::mutex removal_mutex;')]:
  text=(stage/leaf).read_text()
  need(marker in text and forbidden not in text,'Unsafe cross-translation-unit registry lifetime: '+leaf)
 need('forget_party_link(this)' in direct,'Explicit lobby cleanup was lost')
 # Bind against the *actual compiled codecs*, not an assumed wire format.
 protocol=json.loads((stage.parent/'spectators/swaps.json').read_text())
 codec=Path(next(r['new'] for r in protocol if Path(r['new']).name=='netplay_protocol.cpp')).read_text()
 need('payload.stage > 1U' in codec and 'bytes[0] > 1U' in codec,'Launch-stage codec contract changed')
 need('if (player.slot != index ||' in codec,'Vacant-slot codec contract changed')
 # No dependency, generated guest or simulation renderer source is replaced.
 for r in swaps:
  need(not any(p.lower() in ('extern','recompiledfuncs','recompiledpatches') for p in Path(r['path']).parts),'Forbidden source replacement')
 need({Path(r['new']).name for r in swaps}=={'direct_session.cpp','runtime_ui.cpp','party_link.cpp','lobby_adapter.cpp','rollback_check.cpp'},'Repair expanded outside the reviewed scope')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'[lobby][launch] committed stage=1',b'[lobby][room] synchronized generation=',
                 b'[lobby][wire-error] room serialization failed',b'The host changed the lobby save during a running match.',
                 b'Check Rollback connection',b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE'):
   need(marker in data,'Linked repair/retained feature missing: '+repr(marker))
 contract=hashlib.sha256()
 for p in sorted([AUTH/'ReleaseOverlay.cmake',AUTH/'pipeline.py',AUTH/'transforms.py'],key=lambda p:p.name):
  contract.update(p.name.encode());contract.update(p.read_bytes().replace(b'\r\n',b'\n'))
 return {'schema':1,'version':VERSION,'lobby_link_contract':contract.hexdigest(),'unchanged_authored_files':unchanged,'concurrent_planning':concurrent_planning,
  'compiler_bindings':bindings(stage,swaps),'swaps':swaps,'includes':includes,'retained':retained,
  'validation':'Compilation/linking and static source/binary/archive inspection only. No game, mod, test executable or AppImage was launched.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','previous','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
