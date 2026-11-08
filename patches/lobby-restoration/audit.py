"""Lobby restoration source/link/package receipts only; never execute a target."""
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
VERSION='1.0.9-spectator-playtest.6'
INC=re.compile(r'^#include "([^"\n]+)"',re.M)

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def need(ok,message):
 if not ok:raise ValueError(message)
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def between(text,start,end):
 a=text.index(start);return text[a:text.index(end,a)]
def adjacent(text,source):
 def one(m):
  p=source.parent/m[1]
  return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
 return INC.sub(one,text)

def inspect(stage,binary=None,baseline=None):
 base=load('lobby_retained_store_audit',ROOT/'patches/store-vehicles/audit.py');base.VERSION=VERSION
 retained=base.inspect(stage.parent/'store-vehicles',binary,baseline)
 rows=json.loads((stage/'swaps.json').read_text());inls=json.loads((stage/'includes.json').read_text())
 need(len(rows)==2 and {(r['target'],Path(r['new']).name) for r in rows}=={
  ('DKRPortGame','runtime_ui.cpp'),('DKRNetplayCore','direct_session.cpp')},'Unexpected lobby source replacements')
 need(len(inls)==1 and Path(inls[0]['new']).name=='runtime_online_ui.inl','Unexpected online UI include replacement')
 for r in rows+inls:
  old,new=Path(r['path']),Path(r['new'])
  need(not new.resolve().is_relative_to(ROOT),'Lobby output escaped the external build stage')
  need(sha(old)==r['input_sha256'] and sha(new)==r['output_sha256'],'Lobby source receipt changed: '+new.name)
  need(not any(p.lower() in ('extern','recompiledfuncs','recompiledpatches') for p in old.parts),'Forbidden runtime/dependency replacement')
 transform=load('lobby_authored_transforms',AUTH/'transforms.py')
 before=Path(inls[0]['path']).read_text();after=Path(inls[0]['new']).read_text()
 need(transform.online(before,stage)==after,'Online UI is not the exact reviewed transform')
 for r in rows:
  source=Path(r['path']);text=source.read_text()
  if source.name=='direct_session.cpp':expected=transform.direct(text,stage)
  else:
   matches=[m for m in INC.finditer(text) if Path(m[1]).name=='runtime_online_ui.inl']
   need(len(matches)==1,'Runtime UI source has ambiguous online include')
   m=matches[0]
   expected='#include "'+(stage/'lobby_adapter.hpp').as_posix()+'"\n'+text[:m.start()]+'#include "'+(stage/'runtime_online_ui.inl').as_posix()+'"'+text[m.end():]
  need(adjacent(expected,source)==Path(r['new']).read_text(),'Unreviewed runtime implementation change: '+source.name)
 preserved=[]
 for start,end in (
  ('void DrawOlPlate(', 'struct OlMetaItem'),
  ('void DrawOlCountdown(', 'void DrawOlLobby('),
  ('void DrawOlAvatar(', 'struct OlTileLook'),
  ('void DrawOlGuideCard(', 'void DrawOlProfileCard('),
  ('void DrawOlProfileCard(', 'struct OlRoom'),
  ('void DrawOlTabs(', 'void DrawOlCard('),
  ('void DrawOlStartBar(', 'void DrawOlCountdown('),
  ('void DrawOlHostSettings(', 'void DrawOlPrivacy(')):
  old,new=between(before,start,end),between(after,start,end)
  need(old==new,'Original lobby component unexpectedly changed: '+start)
  preserved.append({'component':start.split('(')[0].removeprefix('void '),'sha256':hashlib.sha256(new.encode()).hexdigest()})
 for marker in ('DrawPageHeading("ONLINE")','DrawRomSelect("online-rom"','DrawOlMain(main_width, frame);',
                'DrawOlProfileCard(340.0F, frame, false);','DrawOlGuideCard(340.0F, frame);',
                'DrawOlModals(frame);','DrawOlPartyDialogs(frame);','DrawOnlineGuideModal();',
                'DrawInviteInbox(std::max(available_width,1.F));',
                'if (view.host) tabs.push_back({OlSection::Friends, "Invite friends"});'):
  need(marker in after,'Shared lobby shell feature lost: '+marker)
 for marker in ('void DrawPartyPage(', 'party_tabs', 'if(dkr::runtime::spectators::party_enabled(&online)&&in_lobby)'):
  need(marker not in after,'Replacement lobby bypass remains: '+marker)
 need('const int columns = width < 660.F ? 1 : 2;' in after,'Card breakpoint does not use local panel width')
 need('OlPartyJoinSpace(frame,pending)' in after and 'OlPartyActive()?frame.has_space:' in after,'Invites/admission still use four-player capacity')
 context=(stage/'lobby_context.inl').read_text();controls=(stage/'lobby_controls.inl').read_text()
 for marker in ('view.local_slot=255','g_ol_party.link.party.controls[port].member',
                'g_ol_party.pending_watch.find(pending.request_id)', 'if(OlPartyLive())return false;',
                'metric.index-row.index', 'test.last_sample>2.', 'test.generation!=g_ol_party.link.party.generation'):
  need(marker in context,'Role/identity/measurement safeguard missing: '+marker)
 for marker in ('"SPECTATORS "','"Test session connection","Invite friends"',
                'ports.push_back(p)','ports[std::size_t(selected)]','"PLAYER SWAP REQUESTS"',
                'party_approve_swap(&online,port,incoming)','party_request_swap(&online,port,incoming)',
                '"BACK TO MY RACE"','"RETURN TO LOBBY"', '"WAITING FOR HOST"',
                'g_ol_member_generation!=g_ol_party.link.party.generation',
                'dialog_room!=g_ol_party.room','ImGui::CloseCurrentPopup();ImGui::EndPopup();',
                'No game or renderer performance score is inferred.', 'gameplay packet loss.'):
  need(marker in controls,'Approved lobby addition/guard missing: '+marker)
 adapter=(stage/'lobby_adapter.cpp').read_text();removal=(stage/'remove_member.inl').read_text()
 for marker in ('p->seed.size()==512&&p->save_generation!=0','p->party.snapshot().generation!=seen.link.party.generation',
                'id==p->local','p->party.snapshot().handover','removals[session]={seen.room,seen.link.party.generation,id}'):
  need(marker in adapter,'Adapter readiness/removal safeguard missing: '+marker)
 for marker in ('!is_host_','request.room!=party->room_id','request.generation!=party->party.snapshot().generation',
                'request.member==party->local','party->party.snapshot().handover',
                'send_with_key(address,key,','flush_outbound_locked();','transport_->release_peer_route(address);',
                'party->stop_requested=true;'):
  need(marker in removal,'Owner-side removal safeguard missing: '+marker)
 need(removal.index('flush_outbound_locked();')<removal.index('transport_->release_peer_route(address);'),'Removal releases the authenticated route before sending')
 contract=hashlib.sha256();authored=[]
 for p in sorted([AUTH/'pipeline.py',AUTH/'transforms.py',AUTH/'ReleaseOverlay.cmake',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if not p.is_file():continue
  authored.append({'name':p.name,'sha256':sha(p)})
  contract.update(p.name.encode());contract.update(p.read_bytes().replace(b'\r\n',b'\n'))
  if p.parent.name=='native':
   text=(stage/p.name).read_text()
   if p.name=='lobby_adapter.hpp':text=re.sub(r'#include "[^"\n]+party_link.hpp"','#include "party_link.hpp"',text)
   need(text==p.read_text(),'Staged lobby adapter differs from authored source: '+p.name)
 if binary:
  data=binary.read_bytes()
  for marker in (b'SPECTATORS ',b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE',
                 b'The lobby changed before removal. No player was removed.',
                 b'No game or renderer performance score is inferred.',
                 b'The connection check was interrupted.'):
   need(marker in data,'Linked lobby feature marker missing: '+repr(marker))
 return {'schema':1,'version':VERSION,'lobby_restoration_contract':contract.hexdigest(),'authored':authored,
  'preserved_original_components':preserved,'swaps':rows,'includes':inls,'retained':retained,
  'validation':'Compilation/linking and static source/binary/archive inspection only. No game, mod, test executable or AppImage was launched.'}

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
