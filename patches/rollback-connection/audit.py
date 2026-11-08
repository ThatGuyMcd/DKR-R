"""Static composition/link/source/archive checks; never run the game or tests."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.8'
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
   need(new.resolve() in sources and old.resolve() not in sources,'Final compiler binding is stale: '+new.name)
   if row['target']=='DKRNetplayCore':need((stage/'rollback_check.cpp').resolve() in sources,'Diagnostic core is not compiled')
  else:
   graph=(build/'build.ninja').read_text();lines=[x for x in graph.splitlines() if x.startswith('build CMakeFiles/'+row['target']+'.dir/')]
   need(any(' '+new.as_posix()+' ' in x+' ' for x in lines),'Ninja misses final source: '+new.name)
   need(not any(' '+old.as_posix()+' ' in x+' ' for x in lines),'Ninja still uses superseded source: '+old.name)
   if row['target']=='DKRNetplayCore':need(any(' '+(stage/'rollback_check.cpp').as_posix()+' ' in x+' ' for x in lines),'Ninja misses diagnostic core')
  result.append({'target':row['target'],'source':new.name})
 return result
def inspect(stage,binary=None,baseline=None,previous=None):
 rows=json.loads((stage/'inputs.json').read_text());swaps,includes,outputs=pipeline.compose(rows,stage)
 need(json.loads((stage/'swaps.json').read_text())==swaps,'Source receipts differ')
 need(json.loads((stage/'includes.json').read_text())==includes,'Include receipts differ')
 for name,data in outputs.items():
  need(not (stage/name).resolve().is_relative_to(ROOT),'Output escaped external staging')
  need((stage/name).read_bytes()==data,'Unreviewed staged change: '+name)
 # The previous layer's compiled UI is deliberately replaced here. Its other
 # compiler bindings remain mandatory, and its reviewed source receipts and
 # complete retained audits still run. We verify the final UI binding above.
 # Prior scripts intentionally use adjacent short module names. Bind those
 # names only while loading them, then restore this layer's own modules.
 prior={name:sys.modules.get(name) for name in ('pipeline','transforms')}
 try:
  sys.modules['transforms']=load('connection_upstream_transforms',ROOT/'patches/upstream-sync/transforms.py')
  sys.modules['pipeline']=load('connection_upstream_pipeline',ROOT/'patches/upstream-sync/pipeline.py')
  retained_audit=load('connection_retained_upstream',ROOT/'patches/upstream-sync/audit.py')
 finally:
  for name,value in prior.items():
   if value is None:sys.modules.pop(name,None)
   else:sys.modules[name]=value
 retained_audit.VERSION=VERSION
 prior_bindings=retained_audit.build_bindings
 def composed_bindings(prior_stage,prior_rows):
  mapped=[]
  for r in prior_rows:
   matches=[n for n in swaps if n['path']==r['new']]
   if matches:
    need(len(matches)==1,'Ambiguous final composition')
    mapped.append({'target':r['target'],'path':r['path'],'new':matches[0]['new']})
   else:mapped.append(r)
  return prior_bindings(prior_stage,mapped)
 retained_audit.build_bindings=composed_bindings
 retained=retained_audit.inspect(stage.parent/'upstream-sync',None,None,None)
 contracts={
  'upstream':retained['sync_contract'],
  'lobby':retained['retained']['lobby_restoration_contract'],
  'store':retained['retained']['retained']['vehicle_store_contract'],
  'rollback':retained['retained']['retained']['retained']['rollback_policy_contract'],
  'spectators':retained['retained']['retained']['retained']['invitations']['spectators']['contract']['spectator_runtime'],
 }
 if previous:
  old=json.loads(previous.read_text())['audit']
  expected={'upstream':old['sync_contract'],'lobby':old['retained']['lobby_restoration_contract'],
   'store':old['retained']['retained']['vehicle_store_contract'],
   'rollback':old['retained']['retained']['retained']['rollback_policy_contract'],
   'spectators':old['retained']['retained']['retained']['invitations']['spectators']['contract']['spectator_runtime']}
  need(contracts==expected,'An accepted feature contract changed')
 untouched=0
 if baseline:
  for row in json.loads(baseline.read_text()):
   need(sha(ROOT/row['path'])==row['sha256'],'Pre-existing authored file changed: '+row['path']);untouched+=1
 final=(stage/'direct_session.cpp').read_text();core=(stage/'rollback_check.cpp').read_text();ui=(stage/'connection_ui.inl').read_text()+(stage/'report_ui.inl').read_text()
 for token in ('dkr::runtime::rollback_check::request(this,rollback_context(),error)',
               'if(dkr::runtime::rollback_check::diagnostic_type(packet.header.type))',
               'dkr::runtime::rollback_check::service(this,rollback_context(),rollback_send,'):
  need(final.count(token)==1,'Missing/duplicate diagnostic integration: '+token)
 need(final.index('if(provisional_mod_peer')<final.index('if(dkr::runtime::rollback_check::diagnostic_type'),'Admission privilege ordering changed')
 need(final.index('.accept(packet_sequence)')<final.index('if(dkr::runtime::rollback_check::diagnostic_type'),'Replay validation ordering changed')
 need(final.index('rollback_check::service(this')<final.index('// Inside pump_locked after ordinary receive/flush'),'Party early return bypasses service')
 need('representative online traffic' not in final and 'overall_score >= 8' not in final,'Old preflight scoring is still reachable')
 for token in ('1000000000/30','now+std::chrono::seconds(2)','s.measure_at+std::chrono::seconds(6)',
               's.drain_at+std::chrono::seconds(2)','s.client_seen[b[7]][packet.sequence]',
               's.nonce!=get(b,8,8)','kind==HelloAck','pending.size()>=MaxSamples','c.queued>QueueLimit',
               'if(sender!=c.host_id)return','!same(s.context,c)||!c.idle'):
  need(token in core,'Diagnostic isolation/bound/measurement safeguard missing: '+token)
 for forbidden in ('receive_input(', 'receive_control(', 'run_runtime(', 'world.', 'controller_sample', 'memcpy('):
  need(forbidden not in core,'Diagnostic can mutate game state: '+forbidden)
 for token in ('experimental::automatic_delay(worst,v.window)','two prediction frames reserved',
               'not measured gameplay packet loss','Spectators do not consume a controller port',
               'experimental::runtime_view()','Nothing has been changed automatically.',
               'if(!v.complete)','##rollback-check-report','ImGui::EndChild();PaddockGap(12.F)'):
  need(token in ui,'Rollback-specific UI statement missing: '+token)
 online=(stage/'runtime_online_ui.inl').read_text();context=(stage/'lobby_context.inl').read_text()
 need('g_ol_party_probe' not in online+context+(stage/'lobby_controls.inl').read_text(),'Old UI-only sampling remains reachable')
 need('view.local_slot=255' in context and 'g_ol_party.link.party.controls[port].member' in context,'Spectator/controller identity projection changed')
 if binary:
  data=binary.read_bytes()
  for token in (VERSION.encode(),b'Check Rollback connection',b'ROLLBACK CONNECTION REPORT',
                b'This report is no longer current. Run a fresh Rollback check from the lobby.',
                b'Only the host can check Rollback connections',b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE',
                b"That track's manifest id can't be used as a folder name."):
   need(token in data,'Linked feature/version missing: '+repr(token))
 contract=hashlib.sha256()
 for path in sorted([AUTH/'ReleaseOverlay.cmake',AUTH/'transforms.py',AUTH/'pipeline.py',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if path.is_file():contract.update(path.name.encode());contract.update(path.read_bytes().replace(b'\r\n',b'\n'))
 return {'schema':1,'version':VERSION,'connection_contract':contract.hexdigest(),'retained_contracts':contracts,
  'unchanged_authored_files':untouched,'swaps':swaps,'includes':includes,'compiler_bindings':bindings(stage,swaps),
  'retained':retained,'validation':'Compilation/linking and static source/binary/archive inspection only. No game, mod, test executable or AppImage was launched.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','previous','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
