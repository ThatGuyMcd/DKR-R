"""Verify narrow resource composition, retained fixes and new canonical peer IDs."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def need(ok,why):
    if not ok:raise ValueError(why)
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def compiled(build,target):
    p=build/(target+'.vcxproj')
    if p.exists():return {Path(v).resolve() for v in re.findall(r'<ClCompile Include="([^"]+)"',p.read_text(encoding='utf-8-sig'))}
    return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
        if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
def inspect(build,binary,baseline,accepted):
    stage=build/'generated/memory-stability'
    rows,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
    need(rows==json.loads((stage/'swaps.json').read_text()),'Memory source receipts changed')
    for name,data in outputs.items():need((stage/name).read_bytes()==data,'Stale memory output: '+name)
    for row in rows:
        sources=compiled(build,row['target'])
        need(Path(row['new']).resolve() in sources,'Memory replacement is not compiled')
        need(Path(row['path']).resolve() not in sources,'Superseded unit still compiled')
    # The retained audits already follow the complete recorded compiler graph.
    # Their old final IDs remain valid intermediate derivations. Verify their
    # source/binding contracts without asking the new binary to contain old IDs.
    # Check the final binary's new IDs and retained critical guards separately.
    saved={n:sys.modules.get(n) for n in ('pipeline','transforms','lobby_layout')}
    previous=ROOT/'patches/spectator-admission-repair'
    try:
        sys.modules['transforms']=load('memory_old_transforms',previous/'transforms.py')
        sys.modules['pipeline']=load('memory_old_pipeline',previous/'pipeline.py')
        prior=load('memory_retained_audit',previous/'audit.py');prior.VERSION=pipeline.VERSION
        retained=prior.inspect(build,None,baseline,accepted)
    finally:
        for name,value in saved.items():
            if value is None:sys.modules.pop(name,None)
            else:sys.modules[name]=value
    ledger='guarded-primary-heap-v1\n'
    for name in ('ReleaseOverlay.cmake','pipeline.py','native/primary_heap.h'):
        ledger+=name+':'+pipeline.sha((AUTH/name).read_bytes())+'\n'
    need((stage/'ledger.txt').read_text()==ledger,'Canonical memory policy ledger changed')
    contracts={k:pipeline.sha((v+'\n'+ledger).encode()) for k,v in retained['rollback_build_ids'].items()}
    need(contracts==json.loads((stage/'contracts.json').read_text()),'Memory peer identity derivation differs')
    # Extracted fixtures must come from ACTUAL final normal allocator inputs.
    fixtures=json.loads((stage/'allocator-inputs.json').read_text())
    need(len(fixtures)==26,'Missing allocator regression closure')
    for r in fixtures:
        need(Path(r['input']).resolve() in compiled(build,'DKRRecompiledFuncsV'+str(r['revision'])),'Fixture is not a final allocator input')
        need(pipeline.sha(Path(r['input']).read_bytes())==r['input_sha256'],'Allocator fixture input changed')
        name=r['name'];text=(stage/('allocator'+str(r['revision'])+'.c')).read_text()
        match=re.search(r'RECOMP_FUNC void '+name+r'\(uint8_t\* rdram, recomp_context\* ctx\) \{',text)
        need(match is not None,'Extracted allocator body missing')
        start=match.end();depth=1
        for tok in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\{|\}',text[start:]):
            if tok[0]=='{':depth+=1
            if tok[0]=='}':depth-=1
            if not depth:break
        need(not depth and pipeline.sha(text[start:start+tok.start()].encode())==r['body_sha256'],'Extracted allocator body changed')
        need((stage/('allocator'+str(r['revision'])+'.c')).resolve() in compiled(build,'DKRMemoryStability'+str(r['revision'])+'Checks'),'Allocator regression is not bound to extracted code')
    for rev in (77,80):
        need((stage/f'owned-entry{rev}.c').resolve() in compiled(build,f'DKRMemoryOwned{rev}Checks'),'Owned regression is not bound to exact entry prefix')
    if binary:
        data=binary.read_bytes()
        markers=(pipeline.VERSION,'[memory][primary-pool]','[hud][heap]',
            '[spectator][music] checkpoint-ready','[spectator][admission] failed','[spectator][playback] first-frame',
            'Spectator playback could not recover after five failures:',
            'Custom music was not prepared at a confirmed loading boundary.',
            '[online-mods][keep-legacy]','The selected mod differs from its preparation receipt.',
            'An offline mod asset needs repair: ','[party][release-receive] accepted',
            '[party][release-receive] ignored=duplicate','[party][release-receive] ignored=stale','[party][scene] prepared',
            'COPY JOIN DIAGNOSTICS','RETRY JOIN','[lobby-start][bootstrap]',
            'No mod bytes arrived after five repair attempts.','Host download slot ready',
            'audio-queued-to-SP-worker','Invalid bootstrap task address.',
            'Not enough guest memory for a required game resource.',*contracts.values())
        for marker in markers:need(marker.encode() in data,'Linked memory/retained guard missing: '+marker)
    return {'schema':1,'version':pipeline.VERSION,'swaps':rows,'rollback_build_ids':contracts,
        'retained':retained,'allocator_fixtures':fixtures,'protected_sources_edited':False,
        'validation':'Exact composition and final compiler bindings; complete retained source audits; final linked guards and new peer IDs. Synthetic allocator and retained regression execution recorded separately. Real game acceptance pending.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();r=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print('Memory composition, retained source chain and final peer identities verified.')
