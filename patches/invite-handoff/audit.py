"""Compile/source/archive evidence only. No game or test executable is run."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.3'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,reason):
 if not ok:raise ValueError(reason)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def inspect(stage,binary=None,baseline=None):
 sys.path.insert(0,str(ROOT/'patches/spectators'))
 base=load('spectator_invite_audit',ROOT/'patches/spectators/audit.py');base.VERSION=VERSION
 kept=base.inspect(stage.parent/'spectators',binary,baseline)
 rows=json.loads((stage/'swaps.json').read_text(encoding='utf-8'))
 require(len(rows)==3 and {Path(r['new']).name for r in rows}=={'game_main.cpp','runtime_ui.cpp','runtime_platform.cpp'},'Unexpected invitation source replacements')
 for row in rows:
  old,new=Path(row['path']),Path(row['new'])
  require(row['target']=='DKRPortGame' and not new.resolve().is_relative_to(ROOT),'Invite source escaped isolated game stage')
  require(sha(old)==row['input_sha256'] and sha(new)==row['output_sha256'],'Invitation source receipt changed')
 main=(stage/'game_main.cpp').read_text(encoding='utf-8')
 require(main.index('runtime_thread.join();')<main.index('std::string invite_records_failure;')<main.index('invites::finish_return('),
         'Invitation join is not gated on completed runtime and records retirement')
 require('!invite_records_failure.empty()?invite_records_failure' in main and 'owned_failed?std::string(' in main,
         'Save/owned-runtime failure gates are missing')
 ui=(stage/'runtime_ui.cpp').read_text(encoding='utf-8')
 require('PumpInviteLauncher(rom_ready,online_manifest.has_value())' in ui,'Launcher is not resuming the pending invitation')
 require('if(dkr::runtime::invites::return_pending())return;' in ui,'Ordinary match-end UI can interrupt invitation handoff')
 online=(stage/'runtime_online_ui.inl').read_text(encoding='utf-8')
 require('RequestLobbyInvitation(invite.invite_id,invite.friend_identity);' in online,'Open Lobbies does not use shared invitation acceptance')
 require('g_online_party_join' not in ui and '"Spectator-capable lobby"' in online,'Host-only spectator policy was lost')
 records=[];h=hashlib.sha256()
 # Path ordering is case-insensitive on Windows but case-sensitive on Linux.
 # Hash the same filename order on both platforms, not the absolute Path order.
 for source in sorted(list((AUTH/'native').iterdir())+[AUTH/'pipeline.py',AUTH/'ReleaseOverlay.cmake'],key=lambda p:p.name):
  if not source.is_file():continue
  if source.parent.name=='native':require(sha(stage/source.name)==sha(source),'Native invite source not staged: '+source.name)
  records.append({'name':source.name,'sha256':sha(source)});h.update(source.name.encode());h.update(source.read_bytes().replace(b'\r\n',b'\n'))
 if binary:
  data=binary.read_bytes()
  for marker in (b'Leaving this lobby to accept an invitation.',b'Returning safely to the launcher...',b'The pending invitation was lost.',b'This invitation no longer has a valid admission token.'):
   require(marker in data,'Invitation linked marker missing: '+repr(marker))
 return {'schema':1,'version':VERSION,'invite_contract':h.hexdigest(),'invitation_sources':records,'swaps':rows,'spectators':kept,
         'validation':'Compilation/linking and static source/binary/archive inspection only. No game, mod, test executable or AppImage was launched.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path)
 a=p.parse_args();r=inspect(a.stage,a.binary,a.baseline)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
 else:print(json.dumps(r,indent=2))
