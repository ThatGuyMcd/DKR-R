"""Read-only scene-loading scope, retained safety and package-byte checks."""
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
VERSION='1.0.9-detail-playtest.4'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,why):
 if not ok:raise ValueError(why)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def inspect(stage,binary=None):
 retained=load('retained_online_menu',ROOT/'patches/online-vehicle-menu/audit.py').inspect(stage.parent/'online-vehicle-menu')
 rows=json.loads((stage/'swaps.json').read_text())
 require(len(rows)==2 and {r['target'] for r in rows}=={'DKRExperimentalOwnedGameV77','DKRExperimentalOwnedGameV80'},'Scene work changed unexpected targets')
 for row in rows:
  old,new=Path(row['path']),Path(row['new'])
  require(sha(old)==row['input_sha256'] and sha(new)==row['output_sha256'],'Scene work source receipt mismatch')
  require(not new.resolve().is_relative_to(ROOT) and new.name=='probe_driver.cpp','Scene work touched checkout/guest')
  before=old.read_text();text=new.read_text()
  for marker in ('scene_work_limit=dkr_scene_work::limit(background)','background ? &background_context:&context_,scene_work_limit','scene-load-work-v1','result.memory_accesses','result.guest_entries','scene loading used its '):
   require(marker in text,'Missing scene work scope/diagnostic: '+marker)
  # Prove the transaction and gameplay/menu budgets are unchanged, not merely
  # that familiar strings happen to remain somewhere in the source.
  for start,end in (('    dkr_probe_result run_private(','    std::unique_ptr<dkr::runtime::legacy::ModWorld> mods_;'),
                    ('    PreparationStep prepare_scene(std::uint64_t scene_hash','private:\n    bool has_confirmed_scene()')):
   require(before[before.index(start):before.index(end,before.index(start))]==text[text.index(start):text.index(end,text.index(start))],'Unrelated owner/transaction policy changed')
  calls=lambda s:re.findall(r'run_private\([\s\S]*?;',s)
  old_calls,new_calls=calls(before),calls(text)
  require(len(old_calls)==len(new_calls),'Guest invocation count changed')
  changed=[(a,b) for a,b in zip(old_calls,new_calls) if a!=b]
  require(len(changed)==1 and 'dkr_probe_authored_scene_resume' in changed[0][0] and changed[0][0].replace('&context_,40000000)','&context_,scene_work_limit)')==changed[0][1],'A non-loader work limit changed')
  for marker in ('dkr_probe_scene_unload_authorize(confirmed_scene_)','if(!box.quiescent())return PreparationStep::Pending;','native_.pending_scene_site || !valid_native(native_)','context_.r29!=stack+stack_delta','native_.video_frames!=frame','!staged_save.commit(current_epoch_,frame,saves)','!staged_save.begin_epoch(next_epoch)'):
   require(marker in text,'Confirmed loading safety guard lost: '+marker)
 header=stage/'scene_load_work.hpp'
 require(sha(header)==sha(AUTH/'native/scene_load_work.hpp'),'Scene work policy not staged')
 policy=header.read_text()
 require('PreviewOperations=40000000ULL' in policy and 'ConfirmedOperations=256000000ULL' in policy and 'background?PreviewOperations:ConfirmedOperations' in policy,'Unexpected/unlimited loader policy')
 guards={}
 for row in json.loads((stage.parent/'online-vehicle-menu/inputs.json').read_text()):
  p=Path(row['path'])
  if p.suffix!='.c':continue
  text=p.read_text()
  if 'static void dkr_code_body_mempool_slot_find(' not in text:continue
  require('dkr_find_visits' in text and '++dkr_find_visits > dkr_find_max' in text and '>= dkr_find_max' in text,'Allocator traversal/index guards absent')
  guards[row['target']]=sha(p)
 require(set(guards)=={'DKRExperimentalOwnedGameV77','DKRExperimentalOwnedGameV80'},'Allocator guard coverage missing')
 contracts=json.loads((stage/'contracts.json').read_text())
 require(set(contracts)=={'77','80'} and all(re.fullmatch('[a-f0-9]{64}',v) for v in contracts.values()) and contracts!=retained['peer_contracts'],'Scene work peer contract missing')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'scene-load-work-v1',b'scene loading used its ',b'owned-vehicle-menu-v1',b'local-all-views-v2',b'batched-detail-v2',b'Could not capture custom-course records from the owned guest: ',b'collection=94 local-only v1',b'complete-family selection',b'custom vehicle submitted'):
   require(marker in data,'Missing linked repair/retained feature: '+repr(marker))
 return {'schema':1,'version':VERSION,'swaps':rows,'peer_contracts':contracts,'allocator_guard_inputs':guards,'retained_online_menu_audit':retained,
  'binary_sha256':sha(binary) if binary else None,'validation':'Compile/link and static source/binary/archive checks only. No game, mod, test executable or AppImage run. Owner must confirm the reported modded race finishes loading.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();report=inspect(a.stage,a.binary)
 if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 else:print(json.dumps(report,indent=2))
