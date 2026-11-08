"""Review exact source deltas, retained contracts and packages; never run a target."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,subprocess,sys,zipfile
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.7'

def need(ok,message):
 if not ok:raise ValueError(message)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def sha(path):return pipeline.sha(path.read_bytes())
def build_bindings(stage,rows):
 build=stage.parent.parent;result=[]
 for row in rows:
  new,old=Path(row['new']),Path(row['path']);project=build/(row['target']+'.vcxproj')
  if project.is_file():
   sources={Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
   need(new.resolve() in sources and old.resolve() not in sources,'Compiler source binding is stale: '+new.name)
  else:
   graph=(build/'build.ninja').read_text()
   lines=[line for line in graph.splitlines() if line.startswith('build CMakeFiles/'+row['target']+'.dir/')]
   need(any(' '+new.as_posix()+' ' in line+' ' for line in lines),'Ninja source binding is stale: '+new.name)
   need(not any(' '+old.as_posix()+' ' in line+' ' for line in lines),'Ninja still compiles the superseded source: '+old.name)
  result.append({'target':row['target'],'source':new.name})
 return result
def baseline_report(path,pins):
 rows=json.loads(path.read_text());changed=set(pipeline.git('diff','--name-only',pins['base'],pins['upstream']).decode().splitlines())
 need(len(changed)==21,'Pinned upstream file inventory changed')
 kept=0;accounted=[]
 with zipfile.ZipFile(path.parent/'AUTHORED-SOURCE-BEFORE.zip') as archive:
  for row in rows:
   name=row['path'];current=ROOT/name
   if name not in changed:
    need(sha(current)==row['sha256'],'Pre-existing file changed outside upstream: '+name);kept+=1
   else:
    original=archive.read(name)
    need(pipeline.sha(original)==row['sha256'],'Recovery archive member changed: '+name)
    need(pipeline.canonical(original)==pipeline.canonical(pipeline.git('show',pins['base']+':'+name)),'Unexpected pre-sync base: '+name)
    expected=pipeline.canonical(pipeline.git('show',pins['upstream']+':'+name))
    need(pipeline.canonical(current.read_bytes())==expected,'Merged source differs from reviewed upstream: '+name)
    accounted.append({'path':name,'before_sha256':row['sha256'],'upstream_sha256':pipeline.sha(expected)})
 need({r['path'] for r in accounted}==changed,'Some upstream changes were not accounted against the recovery checkpoint')
 return {'unchanged_files':kept,'exact_upstream_changes':accounted,'total_files':len(rows)}
def inspect(stage,binary=None,baseline=None,previous=None):
 pins,up=pipeline.upstream()
 rows=json.loads((stage/'inputs.json').read_text());swaps,includes,outputs=pipeline.compose(rows,stage,up)
 need(json.loads((stage/'swaps.json').read_text())==swaps,'Source replacement receipts changed')
 need(json.loads((stage/'includes.json').read_text())==includes,'Include replacement receipts changed')
 need(json.loads((stage/'upstream.json').read_text())==pins,'Staged upstream pins changed')
 for path,data in outputs.items():
  need(not path.resolve().is_relative_to(ROOT),'Sync output escaped external staging')
  need(path.read_bytes()==data,'Unexpected staged source modification: '+path.name)
 # All original lobby, vehicle, Rollback, invite and spectator checks still run.
 base=load('upstream_retained_lobby',ROOT/'patches/lobby-restoration/audit.py');base.VERSION=VERSION
 retained=base.inspect(stage.parent/'lobby-restoration',binary,None)
 detail=load('upstream_retained_detail',ROOT/'patches/detail-records-repair/audit.py');detail.VERSION=VERSION
 retained_detail=detail.inspect(stage.parent/'detail-records-repair',binary)
 comparison={}
 if previous:
  old=json.loads(previous.read_text())['audit']
  select=lambda x:{'lobby':x['lobby_restoration_contract'],'store':x['retained']['vehicle_store_contract'],
    'rollback':x['retained']['retained']['rollback_policy_contract'],
    'invitations':x['retained']['retained']['invitations']['invite_contract'],
    'spectators':x['retained']['retained']['invitations']['spectators']['contract']['spectator_runtime']}
  comparison=select(retained);need(comparison==select(old) and all(comparison.values()),'An accepted Playtest 6 feature contract changed')
 tracks=(stage/'custom_tracks.cpp').read_text();graphics=(stage/'graphics.inl').read_text()
 need('store_track_locator.inl' in tracks and 'store_track_adapter.inl' in tracks,'Accepted Store track adapter was replaced')
 need('locate_unpacked_track(temp_root, track_dir, wrapped_hd_pack, &extract_error)' in tracks,'Accepted wrapped ZIP handling was lost')
 for marker in ('valid_folder_id(probe.id)','folder_name + ".partial"','folder_name + ".previous"','rename(previous, destination, ignored)'):
  need(marker in tracks,'Upstream track import safeguard missing: '+marker)
 for marker in ('matching_preset(config, enh::anisotropy_level(), low_load)','preset_choices.push_back({"Custom", {}, true})',
                'highest-detail characters and vehicles in every view','SettingsRaceButton("APPLY PRESET", preset == custom)'):
  need(marker in graphics,'Preset fix or accepted detail UI was lost: '+marker)
 # The final UI changes only its graphics/header include paths; all original
 # online/lobby/include content is already checked through the retained audit.
 ui=(stage/'runtime_ui.cpp').read_text()
 online=stage.parent/'lobby-restoration/runtime_online_ui.inl'
 need(online.as_posix() in ui,'Final UI no longer uses the accepted restored lobby')
 dispatch=(stage/'magic_code_dispatch.h').read_text();code=(stage/'runtime_code_mods.cpp').read_text()
 need('#define DKR_CODE_TLS __thread' in dispatch and 'extern "C" {__thread int dkr_code_callbacks_enabled=0;' in code,'Darwin TLS declaration/definition do not match')
 need(code.splitlines()[0]=='#include "'+(stage/'magic_code_dispatch.h').as_posix()+'"','Old frozen TLS declarations can override the updated header')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b"That track's manifest id can't be used as a folder name.",b'Could not replace the installed track: ',b'APPLY PRESET'):
   need(marker in data,'Linked upstream fix/version missing: '+repr(marker))
 contract=hashlib.sha256()
 for p in sorted([AUTH/'ReleaseOverlay.cmake',AUTH/'pins.json',AUTH/'pipeline.py',AUTH/'transforms.py'],key=lambda p:p.name):
  contract.update(p.name.encode());contract.update(pipeline.canonical(p.read_bytes()))
 return {'schema':1,'version':VERSION,'upstream':pins,'sync_contract':contract.hexdigest(),'swaps':swaps,'includes':includes,
  'compiler_source_bindings':build_bindings(stage,swaps),
  'retained':retained,'retained_detail':retained_detail,'playtest6_contracts_unchanged':comparison,
  'baseline':baseline_report(baseline,pins) if baseline else None,
  'validation':'Compilation/linking and static source/binary/archive checks only. No game, mod, test executable or AppImage was launched.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','previous','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
