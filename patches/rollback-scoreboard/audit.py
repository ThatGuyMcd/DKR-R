"""Static source/contract/binding/archive checks. No gameplay or test execution."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.13'
def need(ok,message):
 if not ok:raise ValueError(message)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def contract():
 h=hashlib.sha256()
 for p in sorted([AUTH/'ReleaseOverlay.cmake',AUTH/'pipeline.py',AUTH/'transforms.py',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if p.is_file():h.update(p.name.encode());h.update(p.read_bytes().replace(b'\r\n',b'\n'))
 return h.hexdigest()
def inspect(stage,binary=None,baseline=None,previous=None):
 swaps,includes,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
 need(swaps==json.loads((stage/'swaps.json').read_text()),'Scoreboard source receipts differ')
 need(includes==json.loads((stage/'includes.json').read_text()),'Scoreboard include receipts differ')
 for name,data in outputs.items():
  need(not (stage/name).resolve().is_relative_to(ROOT),'Output escaped external build')
  need((stage/name).read_bytes()==data,'Unexpected scoreboard staged content: '+name)
 # Follow the existing explicit replacement graph to the actual compiled unit.
 # Retained audits still recompose and inspect every historical source; their
 # binding assertions check the final replacement, not superseded build copies.
 graph={}
 for receipt in stage.parent.glob('*/swaps.json'):
  for r in json.loads(receipt.read_text()):
   if isinstance(r,dict) and all(k in r for k in ('target','path','new')):graph[(r['target'],Path(r['path']).resolve())]=Path(r['new']).resolve()
 checked=[];sources={}
 def bindings(old_stage,rows):
  result=[];build=stage.parent.parent
  for r in rows:
   target=r['target'];path=Path(r['new']).resolve();seen=set()
   while (target,path) in graph:
    need(path not in seen,'Compiler substitution cycle');seen.add(path);path=graph[(target,path)]
   if target not in sources:
    project=build/(target+'.vcxproj')
    if project.is_file():sources[target]={Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
    else:
     lines=(build/'build.ninja').read_text().splitlines();sources[target]={Path(m[1]).resolve() for line in lines if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
   need(path in sources[target],'Final compiler binding missing: '+str(path))
   for old in seen:need(old not in sources[target],'Superseded source still compiled: '+str(old))
   result.append({'target':target,'source':str(path)})
  checked.extend(result);return result
 def hook(module):
  for name in ('bindings','build_bindings'):
   if hasattr(module,name):setattr(module,name,bindings)
  if hasattr(module,'load'):
   original=module.load
   def child(name,path):
    loaded=original(name,path)
    if Path(path).name=='audit.py':hook(loaded)
    return loaded
   module.load=child
  return module
 saved={n:sys.modules.get(n) for n in ('pipeline','transforms')}
 try:
  sys.modules['transforms']=load('scoreboard_departure_transforms',ROOT/'patches/party-departure-repair/transforms.py')
  sys.modules['pipeline']=load('scoreboard_departure_pipeline',ROOT/'patches/party-departure-repair/pipeline.py')
  prior=hook(load('scoreboard_departure_audit',ROOT/'patches/party-departure-repair/audit.py'))
 finally:
  for n,v in saved.items():
   if v is None:sys.modules.pop(n,None)
   else:sys.modules[n]=v
 retained=prior.inspect(stage.parent/'party-departure-repair')
 if previous:
  old=json.loads(previous.read_text())['audit']
  need(retained['party_departure_contract']==old['party_departure_contract'],'Accepted departure handling changed')
  need(retained['retained']['checkpoint_clock_contract']==old['retained']['checkpoint_clock_contract'],'Accepted checkpoint clock changed')
 need(checked,'Retained compiler bindings were not inspected')
 final_bindings=bindings(stage,swaps)
 for r in swaps:
  need(not any(p.lower() in ('extern','recompiledfuncs','recompiledpatches') for p in Path(r['path']).parts),'Forbidden source replacement')
 core=outputs['rollback_check.cpp'].decode();wire=outputs['scoreboard_wire.inl'].decode();ui=outputs['report_ui.inl'].decode();score=outputs['assessment.hpp'].decode();ctx=outputs['owner_context.inl'].decode()
 for marker in ('b[4]!=2','MaximumReportRows','count!=c.members.size()','get(b,72,8)!=c.roster','b[29]!=ScoringVersion',
                'host!=(member->id==c.host_member)','spectator!=member->spectator','s.report_parts[index]!=b',
                'get(part,20,8)==r.id','for(unsigned i=0;i<count;++i)if(s.report_parts[i].empty())return true;',
                'ReportPacketsPerPulse=12','std::chrono::milliseconds(500)','std::chrono::milliseconds(200)'):
  need(marker in core+wire,'Shared-report guard missing: '+marker)
 for marker in ('1000000000/30','s.measure_at+std::chrono::seconds(6)','s.drain_at+std::chrono::seconds(2)',
                'if(sender!=c.host_id)return','!same(s.context,c)||!c.idle','s.client_seen[b[7]][packet.sequence]',
                'CheckRegistry& check_registry()','s.retry_until=now+std::chrono::seconds(3)'):
  need(marker in core,'Retained diagnostic safeguard missing: '+marker)
 for forbidden in ('receive_input(', 'world.', 'controller_sample', 'run_runtime(', 'memcpy('):need(forbidden not in core+wire,'Diagnostic can mutate game: '+forbidden)
 for marker in ('35*timing+35*reliability+20*consistency+10*latency','MinimumRacerReplies=90,MinimumSpectatorReplies=60',
                'if(!r.host&&!r.spectator)','if(headroom<0)score=std::min(score,4U)',
                'Retest when quiet','No remote racers','r.host?race_assessment(v)'):
  need(marker in score,'Assessment safeguard missing: '+marker)
 need('c.members.push_back' in ctx and 'c.local_member' in ctx and 'c.host_member' in ctx,'Authenticated full roster missing')
 for marker in ('ImGui::BeginChild("##rollback-scoreboard-body"','ImGui::EndChild();PaddockGap(8.F)',
                'ImGui::Selectable("##participant-details"','ImGuiKey_GamepadFaceRight','state.focus=state.expanded',
                'ImGui::SetKeyboardFocusHere()','ImGui::SetItemDefaultFocus()','PaddockFocusRing',
                'SPECTATORS - PLAYBACK TIMING','RACERS - CONNECTION TO HOST','NOT RATED',
                'Nothing has been changed automatically.','checkpoint download and catch-up speed are not benchmarked.'):
  need(marker in ui,'Scoreboard UX safeguard missing: '+marker)
 need(ui.count('ImGui::BeginChild(')==1,'Nested scrolling introduced')
 need('if (BeginOlScoreboard(kResults)) {' in outputs['runtime_online_ui.inl'].decode(),'Results modal not wired')
 count=0
 if baseline:
  for r in json.loads(baseline.read_text()):
   need(pipeline.sha((ROOT/r['path']).read_bytes())==r['sha256'],'Pre-existing authored source changed: '+r['path']);count+=1
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'ROLLBACK CONNECTION CHECK',b'[rollback-check][scoreboard]',b'SPECTATORS - PLAYBACK TIMING',
                 b'RACERS - CONNECTION TO HOST',b'Retest when quiet',b'[lobby][leave] application close notice',
                 b'[lobby][countdown] five seconds',b'[party][checkpoint] first chunk received',b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE'):
   need(marker in data,'Linked scoreboard/retained feature missing: '+repr(marker))
 return {'schema':1,'version':VERSION,'scoreboard_contract':contract(),'scoring_policy':1,'diagnostic_protocol':2,
  'unchanged_authored_files':count,'swaps':swaps,'includes':includes,'compiler_bindings':final_bindings,'retained':retained,
  'validation':'Compilation/linking and static source/binary/archive checks only. No game, mod, test executable or AppImage was run. Live network/controller acceptance remains with the user.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('stage','binary','baseline','previous','output'):p.add_argument('--'+n,type=Path,required=n=='stage')
 a=p.parse_args();r=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 else:print(json.dumps(r,indent=2))
