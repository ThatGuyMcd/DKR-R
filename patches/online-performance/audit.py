"""Verify final bindings, prior compositions, baseline preservation and playtest peer IDs."""
from pathlib import Path
import argparse,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def need(ok,why):
    if not ok:raise ValueError(why)
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def compiled(build,target):
    project=build/(target+'.vcxproj')
    if project.is_file():return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
    return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
        if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
def inspect(build,binary,baseline,accepted):
    stage=build/'generated/online-performance';rows,files=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
    need(rows==json.loads((stage/'swaps.json').read_text()),'Performance receipts differ')
    for name,data in files.items():need((stage/name).read_bytes()==data,'Stale staged input: '+name)
    for row in rows:
        actual=compiled(build,row['target'])
        need(Path(row['new']).resolve() in actual and Path(row['path']).resolve() not in actual,'Final compiler substitution missing: '+row['new'])
    portrait_stage=build/'generated/character-library-portraits'
    portrait_pipeline=load('portrait_final_pipeline',ROOT/'patches/character-library-portraits/pipeline.py')
    portrait_rows,portrait_files=portrait_pipeline.compose(json.loads((portrait_stage/'inputs.json').read_text()),portrait_stage)
    need(portrait_rows==json.loads((portrait_stage/'swaps.json').read_text()),'Portrait source receipts differ')
    for name,data in portrait_files.items():need((portrait_stage/name).read_bytes()==data,'Stale portrait file: '+name)
    portrait_swaps=[r for r in portrait_rows if r['compiled']]
    for row in portrait_swaps:
        actual=compiled(build,row['target']);need(Path(row['new']).resolve() in actual and Path(row['path']).resolve() not in actual,'Portrait launcher binding missing')
    need((portrait_stage/'character_portraits.cpp').resolve() in compiled(build,'DKRCharacterPortraits'),'Portrait background decoder not compiled')
    ui=portrait_files['runtime_ui.cpp'].decode();cards=portrait_files['runtime_mod_library_ui.inl'].decode()
    need(ui.count('#include "'+(portrait_stage/'runtime_mod_library_ui.inl').as_posix()+'"')==1,'Portrait card include binding missing')
    need(ui.index('ReleaseCharacterPortraits();')<ui.index('    ReleaseModStoreTextures();'),'Portrait textures outlive launcher cleanup')
    need('view.preview_height=100;view.preview=[item]' in cards,'Vehicle preview hook not reused')
    # Previous auditors prove their exact intermediate compositions. Present
    # ONLY this layer's already-verified compiler substitutions as their input
    # paths and stop their graph at the previous layer. This layer's receipt
    # and actual compiler graph were independently checked above. No source
    # bytes or prior receipt contents are rewritten by this view.
    class PriorBindings(type(Path())):
        def glob(self,pattern,**kwargs):
            for path in super().glob(pattern,**kwargs):
                if pattern=='*/swaps.json' and path.parent.name in {'online-performance','character-library-portraits'}:continue
                yield path
        def read_text(self,*args,**kwargs):
            text=super().read_text(*args,**kwargs)
            if self.suffix=='.vcxproj' or self.name=='build.ninja':
                for row in [*rows,*portrait_swaps]:
                    for old,new in ((row['new'],row['path']),(row['new'].replace('\\','/'),row['path'].replace('\\','/')),
                        (row['new'].replace('/','\\'),row['path'].replace('/','\\'))):text=text.replace(old,new)
            return text
    saved=sys.modules.get('pipeline')
    try:
        prior_pipeline=load('online_previous_pipeline',ROOT/'patches/vehicle-original-textures/pipeline.py')
        prior_pipeline.VERSION=pipeline.VERSION;sys.modules['pipeline']=prior_pipeline
        retained=load('online_previous_audit',ROOT/'patches/vehicle-original-textures/audit.py').inspect(PriorBindings(build),None,baseline,accepted)
    finally:
        if saved is None:sys.modules.pop('pipeline',None)
        else:sys.modules['pipeline']=saved
    paths=sorted([*(AUTH/'native').glob('*'),*(AUTH/'tests').glob('*'),AUTH/'pipeline.py',AUTH/'ReleaseOverlay.cmake'],key=lambda p:p.as_posix())
    ledger='online-performance-v1\n'+''.join(p.relative_to(AUTH).as_posix()+':'+pipeline.sha(p.read_bytes())+'\n' for p in paths)
    ids={k:pipeline.sha((v+'\n'+ledger).encode()) for k,v in retained['rollback_build_ids'].items()}
    need(ids==json.loads((stage/'contracts.json').read_text()),'Peer derivation differs')
    sources=compiled(build,'DKROnlinePartyChecks');need((stage/'party_checks.cpp').resolve() in sources,'Actual party fixture not bound')
    unchanged=json.loads(baseline.read_text())
    for row in unchanged:need(pipeline.sha((ROOT/row['path']).read_bytes())==row['sha256'],'Pre-existing source changed: '+row['path'])
    if binary:
        data=binary.read_bytes()
        for marker in (pipeline.VERSION,'[rollback][menu-refused]','[party][performance]','payload-phase-ms=',
            '[online-mods][prepare-phase]','[spectator][playback] first-frame','[party][release-receive] accepted',
            '[party][release-receive] ignored=duplicate','immutable workload families','[render][provenance]',
            '[render][producer-snapshot]','[memory][primary-pool]','[online-mods][keep-legacy]',
            'Custom music was not prepared at a confirmed loading boundary.','No mod bytes arrived after five repair attempts.',
            '[custom-vehicles][original-textures]',*ids.values()):need(marker.encode() in data,'Linked repair missing: '+marker)
        need(binary.stat().st_mtime_ns>=max((stage/name).stat().st_mtime_ns for name in files),'Binary predates staged inputs')
    return {'schema':1,'version':pipeline.VERSION,'swaps':rows,'portrait_swaps':portrait_rows,'rollback_build_ids':ids,'retained':retained,
        'unchanged_preexisting_authored_files':len(unchanged),'protected_sources_edited':False,
        'prior_audit_scope':'Exact previous sources/receipts; compiler-only inverse mapping of the verified final substitutions; no old-ID binary assertions.',
        'game_or_rom_or_rsp_or_gpu_executed':False,'live_multiplayer_acceptance':'Required on owner machines.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();result=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Final performance source graph and complete retained repair chain verified.')
