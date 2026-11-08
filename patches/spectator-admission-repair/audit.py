"""Verify observer admission and retain the complete playtest.3 audit chain."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, re, sys, types
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-stability-playtest.4'

def need(ok,why):
    if not ok:raise ValueError(why)
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def once(text,before,after):
    need(text.count(before)==1,'Retained audit anchor changed: '+before[:100]);return text.replace(before,after,1)

def inspect(build,binary,baseline,accepted):
    stage=build/'generated/spectator-admission-repair'
    rows,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
    need(rows==json.loads((stage/'swaps.json').read_text()),'Observer source receipts differ')
    for name,data in outputs.items():need((stage/name).read_bytes()==data,'Stale observer output: '+name)
    def compiled(target):
        project=build/(target+'.vcxproj')
        if project.exists():return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
        return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
            if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
    graph={}
    for receipt in stage.parent.glob('*/swaps.json'):
        for row in json.loads(receipt.read_text()):
            if isinstance(row,dict) and all(k in row for k in ('target','path','new')):
                graph[(row['target'],Path(row['path']).resolve())]=Path(row['new']).resolve()
    def resolve_binding(target,path):
        seen=set()
        while (target,path) in graph:
            need(path not in seen,'Compiler binding cycle');seen.add(path);path=graph[(target,path)]
        return path
    for row in rows:
        sources=compiled(row['target'])
        need(Path(row['new']).resolve() in sources,'Observer source is not compiled')
        need(Path(row['path']).resolve() not in sources,'Superseded observer input is still compiled')
    resident=outputs['DKRLegacyModLaunch/resident_music.cpp'].decode()
    for marker in ('if(!allow_decode || !preparation_depth)','activate(level,false)',
        'sha256_pcm_le(pcm->samples)!=r->proof','silent_preparation_depth?',
        'allow_decode || dkr::spectator_admission::preparing_music()',
        '(void)MusicState(sources_,revision80_).stage_checkpoint(bytes)', '[spectator][music] checkpoint-ready'):
        need(marker in resident,'Observer music safeguard missing: '+marker)
    stage_body=resident.split('State State::stage_checkpoint(View bytes)const {',1)[1]
    need(stage_body.index('MusicState(sources_,revision80_)')<stage_body.index('activate(level,false)'), 'Checkpoint fields must validate before decoder work')
    header=outputs['observer_music.hpp'].decode()
    canonical_music=re.search(r'^#include "([^"\n]*resident_music[.]hpp)"',header,re.M)[0]
    need('inline thread_local unsigned observer_music_depth' in header and '--observer_music_depth' in header,'Permission must be thread-local and balanced')
    for revision in (77,80):
        driver=outputs[f'DKRExperimentalOwnedGameV{revision}/probe_driver.cpp'].decode()
        need(canonical_music in driver,'Owned driver and preparation scope use different generated music headers')
        needle='if(admission) {\n                // ConfirmedAdmission'
        need(needle in driver,'Only verified observer staging may prepare music')
        need(driver.count('prepare_verified_music(')==1,'Observer music permission has extra call sites')
        need(driver.index('prepare_verified_music(')<driver.index('if (!state_.restore(ram_.data(),input.first(state_.snapshot_size()),frame))'), 'Music preparation occurs after guest RAM restore')
        need('} else {\n                staged_mods=mods_->stage_checkpoint(block.subspan(68,count));' in driver,'Ordinary rollback path changed')
        for marker in ('if(!restore_state(bytes,error,&staged))return false;', 'baseline.next.frame==UINT32_MAX',
                       'staged.magic.admit_confirmed','baseline.state->size()!=contract().state_bytes'):
            need(marker in driver,'Verified admission gate missing: '+marker)
    party=outputs['DKRPortGame/party_runtime.cpp'].decode()
    for marker in ('[spectator][admission] failed','[spectator][admission] ready','[spectator][playback] first-frame',
        'observer_retry.progressed()', 'observer_retry.failed(at.epoch)',
        'if(!retry){error="Spectator playback could not recover after five failures: "+error;return false;}',
        'activated_release=std::pair{at,controls}', 'at==activated_release->first', 'all_votes(frozen)'):
        need(marker in party,'Observer recovery or retained release behavior missing: '+marker)
    need(party.index('if(!simulating()) {\n            if(!receiving')<party.index('observer_retry.failed(at.epoch)'), 'Failure policy is outside observer branch')
    saved={n:sys.modules.get(n) for n in ('pipeline','transforms','lobby_layout')}
    try:
        previous=ROOT/'patches/offline-retention-repair'
        sys.modules['lobby_layout']=load('observer_old_layout',previous/'lobby_layout.py')
        sys.modules['pipeline']=load('observer_old_pipeline',previous/'pipeline.py')
        code=(previous/'audit.py').read_text()
        # Keep the old release's source/contract checks. Its compiler binding
        # follows this new recorded substitution, just as older layers do.
        anchor="  code=(previous/'audit.py').read_text()"
        code=once(code,anchor,anchor+'''
  old_binding="need(Path(rows[0]['new']).resolve() in compiled, 'Repaired runtime is not compiled')"
  need(code.count(old_binding)==1,'Retained release binding guard changed')
  code=code.replace(old_binding,"need(resolve_binding(rows[0]['target'],Path(rows[0]['new']).resolve()) in compiled, 'Repaired runtime is not compiled')",1)
''')
        anchor="  exec(compile(code,prior.__file__,'exec'),prior.__dict__);prior.VERSION=VERSION"
        code=once(code,anchor,"  prior.__dict__['resolve_binding']=resolve_binding\n"+anchor)
        prior=types.ModuleType('observer_retained_playtest3_audit');prior.__file__=str(previous/'audit.py')
        prior.__dict__['resolve_binding']=resolve_binding
        exec(compile(code,prior.__file__,'exec'),prior.__dict__);prior.VERSION=VERSION
        retained=prior.inspect(build,binary,baseline,accepted)
    finally:
        for name,value in saved.items():
            if value is None:sys.modules.pop(name,None)
            else:sys.modules[name]=value
    if binary:
        data=binary.read_bytes()
        for marker in (VERSION,'[spectator][music] checkpoint-ready','[spectator][admission] failed',
            '[spectator][playback] first-frame','Spectator playback could not recover after five failures:',
            'Custom music was not prepared at a confirmed loading boundary.'):
            need(marker.encode() in data,'Linked observer feature or guard missing: '+marker)
    ledger=b''.join(p.name.encode()+b'\0'+p.read_bytes() for p in [AUTH/'pipeline.py',AUTH/'transforms.py',AUTH/'ReleaseOverlay.cmake',*sorted((AUTH/'native').iterdir())])
    return {'schema':1,'version':VERSION,'spectator_admission_contract':hashlib.sha256(ledger).hexdigest(),
        'rollback_build_ids':retained['rollback_build_ids'],'offline_keep_contract':retained['offline_keep_contract'],
        'lobby_layout_contract':retained['lobby_layout_contract'],'release_contract':retained['release_contract'],
        'unchanged_authored_files':retained['unchanged_authored_files'],'swaps':rows,
        'outputs':{k:pipeline.sha(v) for k,v in outputs.items()},'retained':retained,
        'validation':'Exact source/compiled bindings, linked markers and full retained playtest.3 audit chain. Synthetic admission, handler, race and retention execution recorded separately; live multiplayer still needs playtesting.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();result=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Spectator preparation/recovery and all retained playtest.3 sources and contracts verified.')
