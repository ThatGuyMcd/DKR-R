"""Static Rollback policy, migration and packaging receipts. No target execution."""
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
VERSION='1.0.9-spectator-playtest.4'

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def require(ok,message):
 if not ok: raise ValueError(message)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def inspect(stage,binary=None,baseline=None):
 base=load('rollback_invite_audit',ROOT/'patches/invite-handoff/audit.py');base.VERSION=VERSION
 prior=base.inspect(stage.parent/'invite-handoff',binary,baseline)
 rows=json.loads((stage/'swaps.json').read_text())
 includes=json.loads((stage/'includes.json').read_text())
 require({'runtime_ui.cpp','direct_session.cpp','netplay_types.cpp','netplay_protocol.cpp'}.issubset({Path(r['new']).name for r in rows}), 'Missing Rollback enforcement sources')
 for row in rows+includes:
  old,new=Path(row['path']),Path(row['new'])
  require(not new.resolve().is_relative_to(ROOT),'Rollback output escaped the isolated stage')
  require(sha(old)==row['input_sha256'] and sha(new)==row['output_sha256'],'Rollback source receipt changed: '+new.name)
 for row in rows:
  require(row['target'] in ('DKRNetplayCore','DKRPortGame'),'Unexpected rollback policy target')
  require(not any(p.lower() in ('extern','recompiledfuncs','recompiledpatches','recompiledrsp') for p in Path(row['path']).parts),'Forbidden dependency or recompiled source replacement')
  if Path(row['new']).name in ('experimental_rollback.cpp','experimental_session.cpp','experimental_lobby_admission.cpp','experimental_runtime.cpp','party_runtime.cpp','game_main.cpp'):
   def logic(path):
    text=path.read_text().replace('\r\n','\n')
    text=re.sub(r'^#include[^\n]*\n','',text,flags=re.M)
    return re.sub(r'"(?:\\.|[^"\\\n])*"','"<prose>"',text)
   require(logic(Path(row['path']))==logic(Path(row['new'])),'Label-only change modified runtime/rollback logic: '+Path(row['new']).name)
 ui=next(Path(r['new']).read_text() for r in rows if Path(r['new']).name=='runtime_ui.cpp')
 require('#include "gui/rt64_inspector.h"' in ui and '#include "hle/rt64_application.h"' in ui,
         'Renderer header selection was bypassed instead of preserving the existing compiler include order')
 require('return dkr::runtime::netplay::rollback_policy::mode;' in ui,'UI can still choose the legacy backend')
 require('migrated_rollback_backend' in ui and 'if (migrated_rollback_backend) SaveSettings();' in ui,'Legacy settings are not canonicalized')
 require('!has_rollback_optin_setting' in ui and '!has_automatic_backend_setting' in ui,'Incomplete legacy profiles are not persisted canonically')
 for field in ('maximum_players','prediction_window','automatic_delay','input_delay','record_replay','development','spectators'):
  require('rollback_policy::'+field in ui,'Default not using shared profile: '+field)
 host=(AUTH/'native/host_settings.inl').read_text()
 online=next(Path(r['new']).read_text() for r in includes if Path(r['new']).name=='runtime_online_ui.inl')
 require(host in online,'Host settings are not the reviewed fixed-mode layout')
 for old in ('Use Host Prediction','Strict Input Sync','"synchronization", "Online mode"','Auto or Experimental Rollback'):
  require(old not in online,'Removed sync control remains: '+old)
 for marker in ('"##spectators"','"##development-lobby"','RequestLobbyInvitation(invite.invite_id,invite.friend_identity);'):
  require(marker in online,'Retained host/invitation control is missing: '+marker)
 types=next(Path(r['new']).read_text() for r in rows if Path(r['new']).name=='netplay_types.cpp')
 protocol=next(Path(r['new']).read_text() for r in rows if Path(r['new']).name=='netplay_protocol.cpp')
 require('supported(descriptor.synchronization)' in types and 'supported(rules.synchronization)' in types,'Rules and starts do not enforce engine 2')
 require('supported(payload.rules.synchronization)' in protocol and 'supported(payload.descriptor.synchronization)' in protocol,'Incoming lobby/start parsing can admit obsolete modes')
 direct=next(Path(r['new']).read_text() for r in rows if Path(r['new']).name=='direct_session.cpp')
 require('rollback_policy::admission_error(rules.synchronization, owned_backend_available())' in direct,'Hosting bypasses the concrete engine/adapter check')
 source=(AUTH/'native/rollback_policy.hpp').read_text()
 staged=(stage/'rollback_policy.hpp').read_text()
 require(re.sub(r'#include "[^"\n]+netplay_types.hpp"','#include "netplay_types.hpp"',staged)==source,'Staged policy differs from authored policy')
 require('mode = SynchronizationMode::ExperimentalRollback' in source and 'static_assert(static_cast<unsigned>(mode) == 2)' in source,'Wire mode was renumbered')
 require('bool development = false' in source and 'bool record_replay = false' in source,'Public default enables developer/replay mode')
 native=[];contract=hashlib.sha256()
 for path in sorted([AUTH/'pipeline.py',AUTH/'ReleaseOverlay.cmake',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if not path.is_file():continue
  native.append({'name':path.name,'sha256':sha(path)})
  contract.update(path.name.encode());contract.update(path.read_bytes().replace(b'\r\n',b'\n'))
 if binary:
  data=binary.read_bytes()
  for value in (b'Online mode: Rollback',b'This lobby uses a sync mode that is no longer supported.',b'Progress is temporary; achievements are paused.'):
   require(value in data,'Linked Rollback policy marker missing: '+repr(value))
  for value in (b'Use Host Prediction',b'Strict Input Sync',b'ONLINE EXPERIMENTAL'):
   require(value not in data,'Removed sync UI remains linked: '+repr(value))
 return {'schema':1,'version':VERSION,'rollback_policy_contract':contract.hexdigest(),'defaults':{
  'spectators':True,'host_control':'Host until character select','maximum_players':4,'mode':'Rollback','wire_mode':2,
  'prediction_window':14,'automatic_input_delay':False,'input_delay':6,'record_replay':False,'development':False},
  'authored_sources':native,'swaps':rows,'includes':includes,'invitations':prior,
  'validation':'Compilation/linking and static source/binary/archive inspection only. No game, mod, test executable or AppImage was launched.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path)
 a=p.parse_args();r=inspect(a.stage,a.binary,a.baseline)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 else:print(json.dumps(r,indent=2))
