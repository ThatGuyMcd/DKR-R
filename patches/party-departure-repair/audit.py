"""Static departure/authentication/retirement checks and retained startup audit."""
from pathlib import Path
import argparse,importlib.util,json,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.12'
def need(value,message):
 if not value:raise ValueError(message)
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def inspect(stage,binary=None,baseline=None,previous=None):
 swaps,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
 need(swaps==json.loads((stage/'swaps.json').read_text()),'Departure source receipts changed')
 for name,data in outputs.items():
  need(not (stage/name).resolve().is_relative_to(ROOT),'Departure output escaped build')
  need((stage/name).read_bytes()==data,'Unexpected departure stage: '+name)
 def mapped(rows):
  result=[]
  for row in rows:
   found=[r for r in swaps if Path(r['path'])==Path(row['new'])]
   need(len(found)<=1,'Ambiguous final departure source')
   result.append({**row,'new':found[0]['new']} if found else row)
  return result
 saved={n:sys.modules.get(n) for n in ('pipeline','transforms')}
 try:
  sys.modules['transforms']=load('departure_clock_transforms',ROOT/'patches/checkpoint-clock-repair/transforms.py')
  sys.modules['pipeline']=load('departure_clock_pipeline',ROOT/'patches/checkpoint-clock-repair/pipeline.py')
  prior=load('departure_clock_audit',ROOT/'patches/checkpoint-clock-repair/audit.py')
 finally:
  for n,v in saved.items():
   if v is None:sys.modules.pop(n,None)
   else:sys.modules[n]=v
 prior.VERSION=VERSION;parent_load=prior.load;bindings=[]
 def retained_load(name,path):
  module=parent_load(name,path)
  if path==ROOT/'patches/party-start-repair/audit.py':
   bind=module.bindings;bindings.append(bind)
   module.bindings=lambda old_stage,rows:bind(old_stage,mapped(rows))
  return module
 prior.load=retained_load
 retained=prior.inspect(stage.parent/'checkpoint-clock-repair')
 need(len(bindings)==1,'Final compiler bindings were not checked')
 if previous:
  old=json.loads(previous.read_text())['audit']
  need(retained['checkpoint_clock_contract']==old['checkpoint_clock_contract'],'Working checkpoint fix changed')
  need(retained['retained']['party_start_contract']==old['retained']['party_start_contract'],'Accepted countdown changed')
 count=0;planning=[]
 if baseline:
  status=(baseline.parent/'GIT-STATUS.txt').read_text()
  for row in json.loads(baseline.read_text()):
   current=pipeline.sha((ROOT/row['path']).read_bytes())
   if row['path'].startswith('patches/lobby-discovery-plan-20261023/'):
    need('?? '+row['path'] in status,'Cannot exempt tracked planning work')
    planning.append({'path':row['path'],'before_sha256':row['sha256'],'current_sha256':current,'note':'Pre-existing concurrent untracked planning; not authored by this repair.'})
   else:need(current==row['sha256'],'Pre-existing source changed: '+row['path']);count+=1
 direct=outputs['direct_session.cpp'].decode();runtime=outputs['experimental_runtime.cpp'].decode();header=outputs['departure.hpp'].decode()
 for marker in ('auto retire_party_member=',
                'retire_party_member(packet_sender,"leave notice")',
                'retire_party_member(packet_sender,"disconnect notice")',
                'retire_party_member(member->id,"transport closed")',
                'retire_party_member(id,"authenticated traffic silent for 10 seconds")',
                'const bool racer=member->role==watch::Role::Racer,live=racer&&bool(party->launch)',
                'if(live){party->stopping=party->stop_requested=true;party->running=false;',
                'party->party.remove(id,reason)','party->ready.erase(id)',
                'transport_->release_peer_route(address);party->advertised={}',
                'case watch::LinkKind::Returned:',
                'leaving::begin(this,sender_id_)','leaving::acknowledged(this)',
                'leaving::forget(this)','steady_clock::now()<deadline',
                'notice(reply.body,2,sender_id_)','notice(message.body,1,packet_sender)',
                '[lobby][departure] role=','[lobby][leave] application close notice'):
  need(marker in direct,'Departure invariant missing: '+marker)
 need(direct.index('.accept(packet_sequence)')<direct.index('notice(reply.body,2,sender_id_)'),'Receipt bypasses authenticated replay validation')
 leave=direct.index('case watch::LinkKind::Leave:')
 need(direct.index('flush_outbound_locked();',leave)<direct.index('retire_party_member(packet_sender,"leave notice")',leave),'Host removes route before sending goodbye receipt')
 for marker in ('SilenceLimit=std::chrono::seconds(10)','CloseGrace=std::chrono::milliseconds(400)',
                'b.size()!=14','b[4]!=1','b[5]!=stage','p->second.member==id'):
  need(marker in header,'Missing receipt/bounded-liveness guard: '+marker)
 need('now-it->heard<std::chrono::seconds(30)' not in direct,'Old party silence loop remains')
 need(runtime.count(pipeline.transforms.contract())==1,'Departure compatibility contract not pinned')
 need('[party][exit] role=%s requested=%u lifecycle=%d' in runtime,'Missing client shutdown evidence')
 party=outputs['party_runtime.cpp'].decode()
 need('if(hub->stopping||!hub->enabled)return; // A retiring owner cannot republish its old racing roster.' in party,'A departing racer can race an in-flight roster publication')
 need({Path(r['new']).name for r in swaps}=={'direct_session.cpp','experimental_runtime.cpp','party_runtime.cpp'},'Departure repair exceeded reviewed units')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'[lobby][leave] application close notice',b'[lobby][departure] role=',
                 b'authenticated traffic silent for 10 seconds',b'[party][exit] role=',
                 b'[party][checkpoint] first chunk received',b'[lobby][countdown] five seconds',
                 b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE'):
   need(marker in data,'Linked departure/retained feature missing: '+repr(marker))
 return {'schema':1,'version':VERSION,'party_departure_contract':pipeline.transforms.contract(),
  'unchanged_authored_files':count,'concurrent_planning':planning,'swaps':swaps,
  'compiler_bindings':bindings[0](stage,swaps),'retained':retained,
  'close_grace_ms':400,'authenticated_silence_seconds':10,
  'validation':'Compile/link and static source/binary/archive checks only. No game, mod, test executable or AppImage was run. Gameplay/network acceptance remains with the user.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','previous','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
