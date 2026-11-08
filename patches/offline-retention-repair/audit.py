"""Recompose retention and retain all existing race/online/security audits."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys,types
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-stability-playtest.3'


def need(ok,why):
 if not ok:raise ValueError(why)
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m


def inspect(build,binary,baseline,accepted):
 stage=build/'generated/offline-retention-repair'
 rows,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
 need(rows==json.loads((stage/'swaps.json').read_text()),'Offline retention source receipt differs')
 for name,data in outputs.items():need((stage/name).read_bytes()==data,'Stale offline retention output: '+name)
 def compiled(target):
  project=build/(target+'.vcxproj')
  if project.exists():return {Path(x).resolve() for x in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
  return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
   if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
 sources=compiled('DKRLegacyImportLibrary')
 need((stage/'online_mod_profile.cpp').resolve() in sources and (stage/'offline_keep.cpp').resolve() in sources,'Both retention units must be compiled')
 need(Path(rows[0]['path']).resolve() not in sources,'Superseded profile source is still compiled')
 need((stage/'runtime_ui.cpp').resolve() in compiled('DKRPortGame'),'Final lobby layout source is not compiled')
 need(Path(rows[1]['path']).resolve() not in compiled('DKRPortGame'),'Superseded launcher source is still compiled')
 ui=outputs['runtime_online_ui.inl'].decode();sidebar=outputs['lobby_sidebar.inl'].decode()
 need(ui.count('DrawOlLobbyFilters(inner);')==2,'Wide and narrow filter placements are required')
 need('g_ol_page_width > 1000.F' in ui and 'g_ol_page_width <= 1000.F' in ui,'Responsive filter placement missing')
 need(ui.index('DrawOlSidebarReadiness(width, frame);')<ui.index('DrawOlFeature(width, "Online guide"'),'Readiness must precede the guide')
 for marker in ('browse-join-role','directory-source','local-profile','SaveSettings();','set_online_input_profile',
  'g_mods_page.entry_section = kModsSectionMagic','g_page_navigation_request = target','std::max(1.F, box.Inner() - 40.F)'):
  need(marker in sidebar,'Sidebar action or width guard missing: '+marker)
 helper=outputs['offline_keep.cpp'].decode();profile=outputs['online_mod_profile.cpp'].decode()
 for marker in ('verify_keep_asset(source,characters,stop)', 'verify_keep_asset(at->second,characters,stop)',
  'receipt[key]=entries', 'same_keep_content', 'sha256(bytes)!=hash', 'selected.insert(key)',
  'std::lock_guard lease(offline_keep_mutex)', 'copy_tree(copy.source/storage', 'std::filesystem::space(root)',
  'check_storage(copy.target,true)', 'if(!same_tree(copy.staged,copy.target,stop))', '[online-mods][keep-legacy]'):
  need(marker in helper,'Offline retention safeguard missing: '+marker)
 need('TrackCatalog::validate_retained_merge(root,library)' not in profile,'Whole incoming libraries still block retention')
 need('retain_legacy_sources(sources,root,stop)' in profile,'Validated profile selection is not retained')
 for marker in ('const auto sources=profile_sources(profile)', 'codes.import(', 'pack_track_lab(at->second,stop)!=data',
  'write_new_file(staged/".online-keep-state",Bytes{\'0\'})'):
  need(marker in profile,'Existing profile/code/course guard missing: '+marker)
 saved={n:sys.modules.get(n) for n in ('pipeline','transforms')}
 try:
  previous=ROOT/'patches/race-release-repair'
  # The race audit imports its pipeline by module name; its transforms remain
  # exactly the prior source, with no changes to the peer build identifiers.
  sys.modules['transforms']=load('keep_race_transforms',previous/'transforms.py')
  sys.modules['pipeline']=load('keep_race_pipeline_ready',previous/'pipeline.py')
  code=(previous/'audit.py').read_text()
  anchor="source = (directory / 'audit.py').read_text()"
  need(code.count(anchor)==1,'Retained race audit loader changed')
  code=code.replace(anchor,anchor+'''
        old_binding="need(Path(row['new']).resolve() in compiled, 'New stability source is not compiled')"
        need(source.count(old_binding)==1, 'Retained stability binding guard changed')
        source=source.replace(old_binding,"need(resolve_binding(row['target'],Path(row['new']).resolve()) in compiled, 'New stability source is not compiled')",1)
''',1)
  prior=types.ModuleType('keep_retained_race_audit');prior.__file__=str(previous/'audit.py')
  exec(compile(code,prior.__file__,'exec'),prior.__dict__);prior.VERSION=VERSION
  retained=prior.inspect(build,binary,baseline,accepted)
 finally:
  for name,value in saved.items():
   if value is None:sys.modules.pop(name,None)
   else:sys.modules[name]=value
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION,'[online-mods][keep-legacy]','The selected mod differs from its preparation receipt.',
   'An offline mod asset needs repair: '):need(marker.encode() in data,'Linked retention behavior missing: '+marker)
 ledger=b''.join(path.name.encode()+b'\0'+path.read_bytes() for path in [AUTH/'pipeline.py',AUTH/'ReleaseOverlay.cmake',*sorted((AUTH/'native').iterdir())])
 return {'schema':1,'version':VERSION,'offline_keep_contract':hashlib.sha256(ledger).hexdigest(),
  'lobby_layout_contract':hashlib.sha256((AUTH/'lobby_layout.py').read_bytes()+(AUTH/'native/lobby_sidebar.inl').read_bytes()).hexdigest(),
  'rollback_build_ids':retained['rollback_build_ids'],'release_contract':retained['release_contract'],
  'unchanged_authored_files':retained['unchanged_authored_files'],'swaps':rows,
  'outputs':{k:pipeline.sha(v) for k,v in outputs.items()},'retained':retained,
  'validation':'Exact source recomposition, compiled bindings, linked features and all retained race/online audits. Synthetic filesystem tests and manual multiplayer acceptance recorded separately.'}


if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
 a=p.parse_args();result=inspect(a.build,a.binary,a.baseline,a.accepted)
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
 print('Offline retention, prior race-start fix and every retained online layer verified.')
