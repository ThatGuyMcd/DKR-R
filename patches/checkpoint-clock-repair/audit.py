"""Verify the clock ordering, retained launch contracts and final source bindings."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.11'
def need(value,message):
 if not value:raise ValueError(message)
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def sha(path):return pipeline.sha(path.read_bytes())
def inspect(stage,binary=None,baseline=None,previous=None):
 swaps,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
 need(swaps==json.loads((stage/'swaps.json').read_text()),'Clock staging receipts changed')
 for name,data in outputs.items():
  need(not (stage/name).resolve().is_relative_to(ROOT),'Generated clock output escaped build')
  need((stage/name).read_bytes()==data,'Clock output differs from authored pipeline: '+name)
 def mapped(rows):
  result=[]
  for row in rows:
   found=[r for r in swaps if Path(r['path'])==Path(row['new'])]
   need(len(found)<=1,'Ambiguous clock source mapping')
   result.append({**row,'new':found[0]['new']} if found else row)
  return result
 saved={n:sys.modules.get(n) for n in ('pipeline','transforms')}
 try:
  sys.modules['transforms']=load('checkpoint_prior_transforms',ROOT/'patches/party-start-repair/transforms.py')
  sys.modules['pipeline']=load('checkpoint_prior_pipeline',ROOT/'patches/party-start-repair/pipeline.py')
  prior=load('checkpoint_prior_audit',ROOT/'patches/party-start-repair/audit.py')
 finally:
  for n,v in saved.items():
   if v is None:sys.modules.pop(n,None)
   else:sys.modules[n]=v
 prior.VERSION=VERSION
 bind=prior.bindings
 prior.bindings=lambda old_stage,rows:bind(old_stage,mapped(rows))
 retained=prior.inspect(stage.parent/'party-start-repair')
 if previous:
  old=json.loads(previous.read_text())['audit']
  need(retained['party_start_contract']==old['party_start_contract'],'Accepted countdown/start layer changed')
  need(retained['retained']['lobby_link_contract']==old['retained']['lobby_link_contract'],'Accepted lobby layer changed')
 count=0;planning=[]
 if baseline:
  status=(baseline.parent/'GIT-STATUS.txt').read_text()
  for row in json.loads(baseline.read_text()):
   path=ROOT/row['path'];current=sha(path)
   if row['path'].startswith('patches/lobby-discovery-plan-20261023/'):
    need('?? '+row['path'] in status,'Cannot exempt tracked planning work')
    planning.append({'path':row['path'],'before_sha256':row['sha256'],'current_sha256':current,'note':'Pre-existing concurrent untracked planning; not authored by this repair.'})
   else:need(current==row['sha256'],'Pre-existing source changed: '+row['path']);count+=1
 party=outputs['party_runtime.cpp'].decode();runtime=outputs['experimental_runtime.cpp'].decode()
 start=party.index('    void message(');end=party.index('    void sample(',start);body=party[start:end]
 need('Clock::time_point now' in body.splitlines()[0],'Message owner clock was not passed')
 need('Clock::now()' not in body,'Message handling samples a later clock')
 need(party.count('message(')==3,'Review all packet and local-swap dispatch sites before changing the clock contract')
 for marker in ('try {message(packet.source,packet.bytes,now);}',
                'message(local,envelope.b,now);',
                'receiver.begin(offer,match,world.contract().schema,offer.bytes,now,e)',
                'receiver.receive(chunk,now,e)','receiver.active()&&receiver.expired(now)',
                'receiver.active()||held||decoding.valid()',
                'Conflicting checkpoint offer retry.',
                '[party][checkpoint] first chunk received',
                '[party][checkpoint] offer acknowledged',
                '[party][checkpoint] expired; received=',
                'Required party admission did not complete within 120 seconds.',
                'world.admit_initial(*held->state,e)',
                'digest!=delivery->second.raw_digest'):
  need(marker in party,'Missing clock/admission invariant: '+marker)
 pulse=party[party.index('    bool pulse('):]
 need(pulse.index('message(packet.source,packet.bytes,now)')<pulse.index('transfer(now)'),'Expiry must follow packet processing on the same pulse instant')
 receiver=(ROOT/'patches/spectators/native/checkpoint_transfer.cpp').read_text()
 for marker in ('now<started_','std::chrono::seconds(30)','std::chrono::minutes(5)',
                'hash_(*state_)!=offer_->digest','o.bytes<=ConfirmedFeed::MaximumState'):
  need(marker in receiver,'Receiver safety contract removed: '+marker)
 need(runtime.count(pipeline.transforms.contract())==1,'Clock fix compatibility identity not pinned')
 need({Path(r['new']).name for r in swaps}=={'party_runtime.cpp','experimental_runtime.cpp'},'Clock repair changed another unit')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'[party][checkpoint] first chunk received',b'[party][checkpoint] offer acknowledged',
                 b'[party][checkpoint] expired; received=',b'[lobby][countdown] five seconds',
                 b'[lobby][start-release]',b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE'):
   need(marker in data,'Missing linked repair or retained feature: '+repr(marker))
 return {'schema':1,'version':VERSION,'checkpoint_clock_contract':pipeline.transforms.contract(),
  'clock_order':'One monotonic pulse timestamp supplies message transitions, receiver.begin/receive and subsequent expiry; no later transfer start is compared to an earlier expiry instant.',
  'unchanged_authored_files':count,'concurrent_planning':planning,'swaps':swaps,
  'compiler_bindings':bind(stage,swaps),'retained':retained,
  'validation':'Compile/link and static source/binary/archive checks only. No game, mod, test executable or AppImage was run. Real-network playtest remains with the user.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','previous','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
