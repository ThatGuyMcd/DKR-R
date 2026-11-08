"""Read-only source and linked-byte inspection. No target execution."""
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
VERSION='1.0.9-detail-playtest.3'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,why):
 if not ok:raise ValueError(why)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def inspect(stage,binary=None):
 retained=load('retained_records_detail',ROOT/'patches/detail-records-repair/audit.py').inspect(stage.parent/'detail-records-repair')
 rows=json.loads((stage/'swaps.json').read_text())
 require(len(rows)==5,'Expected one host, two owned drivers and two owned menu units')
 require(json.loads((stage/'coverage.json').read_text())=={'runtime':1,'driver':2,'77':1,'80':1},'Missing revision coverage')
 for row in rows:
  old,new=Path(row['path']),Path(row['new'])
  require(sha(old)==row['input_sha256'],'Input changed: '+str(old))
  require(sha(new)==row['output_sha256'],'Output changed: '+str(new))
  require(not new.resolve().is_relative_to(ROOT),'Stage entered checkout')
  text=new.read_text()
  if old.name=='runtime_legacy_mods.cpp':
   for marker in ('thread_local OwnedVehicleDrawCallbacks owned_vehicle_draw_callbacks','WorldScope scope(*state_)','!state_->private_owner||!state_->vehicles','state_->vehicles->draw({ram,guest_memory_size()},*ctx,state_->calls,callbacks.frame,callbacks.text_width)','~Binding(){owned_vehicle_draw_callbacks=before;}','owned-vehicle-menu-v1'):
    require(marker in text,'Missing owned drawing boundary: '+marker)
   require(text.index('WorldScope scope(*state_)')<text.index('state_->vehicles->draw({ram,guest_memory_size()}'),'World scope installed after drawing')
  elif old.name=='probe_driver.cpp':
   for marker in ('#include "owned_vehicle_calls.inl"','!result||count||!world.mods_','legacy::draw_owned_vehicle_menu(*world.mods_','dkr_owned_vehicle_menu::frame,dkr_owned_vehicle_menu::text_width'):
    require(marker in text,'Missing private callback binding: '+marker)
  else:
   require('dkr_probe_mod_dispatch("dkr_owned_vehicle_menu_draw",rdram,&vc,0,0,0,0,&vehicle_draw_result)' in text,'Owned menu not dispatched')
   require('dkr_vehicle_menu_draw(rdram,&vc,menu_element_render,get_text_width);' not in text,'Owned menu still uses ordinary world')
   require('owned-vehicle-menu-draw-unavailable' in text and 'dkr_legacy_track_menu(rdram,&vc,103U,0,0U)' in text,'Picker gating/failure boundary lost')
 for p in (AUTH/'native').iterdir():require(sha(p)==sha(stage/p.name),'New native bridge not staged')
 callbacks=(stage/'owned_vehicle_calls.inl').read_text()
 require('namespace {' in callbacks and callbacks.count('static void ')==2,'Revision callback linkage is not private')
 require(callbacks.count('dkr_probe_run_native(')==2 and callbacks.count('if(!result.completed)throw')==2,'Guest fault fence missing')
 contracts=json.loads((stage/'contracts.json').read_text())
 require(set(contracts)=={'77','80'} and all(re.fullmatch('[a-f0-9]{64}',v) for v in contracts.values()),'Invalid peer contracts')
 require(contracts!=retained['peer_contracts'],'Online contract not advanced')
 # Offline guest rendering must remain byte-for-byte identical to its admitted
 # vehicle layer. The new overlay replaces owned targets only.
 for revision in ('77','80'):
  normal=stage.parent/'custom-vehicles'/('DKRRecompiledFuncsV'+revision)
  for row in json.loads((normal/'swaps.json').read_text()):
   require(sha(Path(row['new']))==row['output_sha256'],'Ordinary generated guest changed')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'owned-vehicle-menu-v1',b'owned-vehicle-menu-draw-unavailable',b'Owned vehicle frame callback failed.',b'Owned vehicle text callback failed.',b'local-all-views-v2',b'batched-detail-v2',b'Could not capture custom-course records from the owned guest: ',b'collection=94 local-only v1',b'complete-family selection',b'custom vehicle submitted'):
   require(marker in data,'Missing linked repair/retained feature: '+repr(marker))
 return {'schema':1,'version':VERSION,'swaps':rows,'peer_contracts':contracts,'retained_records_detail_audit':retained,
  'binary_sha256':sha(binary) if binary else None,'validation':'Compile/link and read-only source/binary/archive inspections only. No game, mod, test executable or AppImage run. Online visual acceptance remains with owner.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();report=inspect(a.stage,a.binary)
 if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 else:print(json.dumps(report,indent=2))
