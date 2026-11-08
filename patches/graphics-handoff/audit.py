"""Recompose both handoff units and retain the full render/memory/online audit."""
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
    if target=='ultramodern':project=build/'n64-modern-runtime/ultramodern/ultramodern.vcxproj'
    if project.is_file():return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
    return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
        if re.match(r'build (?:[^ ]*/)?CMakeFiles/'+target+r'[.]dir/',line) if (m:=re.search(r': [^ ]+ (\S+)',line))}
def inspect(build,binary,baseline,accepted):
    stage=build/'generated/graphics-handoff';rows,files=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
    need(rows==json.loads((stage/'swaps.json').read_text()),'Handoff receipt differs')
    for name,data in files.items():need((stage/name).read_bytes()==data,'Stale handoff file: '+name)
    for row in rows:
        sources=compiled(build,row['target'])
        need(Path(row['new']).resolve() in sources and Path(row['path']).resolve() not in sources,'Handoff compiler binding differs')
    need((stage/'producer_snapshot.cpp').resolve() in compiled(build,'DKRPortGame'),'Shared mailbox implementation is not linked')
    identity=files['DKRPortGame/presentation_identity.cpp'].decode()
    body=identity[identity.index('extern "C" void dkr_presentation_task_submitted('):]
    need(body.index('external_side_effects_allowed()')<body.index('graphics_handoff::capture(')<body.index('modern_presentation_enabled()'),'Capture suppression/entry ordering differs')
    hooks=[]
    for rev in (77,80):
        hits=[]
        for path in compiled(build,'DKRRecompiledFuncsV'+str(rev)):
            if path.suffix!='.c':continue
            text=path.read_text()
            marker='static void dkr_code_body_gfxtask_run_xbus(uint8_t* rdram, recomp_context* ctx) {'
            if marker not in text:continue
            fn=text.split(marker,1)[1].split(';}',1)[0]
            need(fn.count('dkr_presentation_task_submitted(rdram, ctx);')==1,'Producer callback coverage differs')
            need(fn.index('dkr_presentation_task_submitted(rdram, ctx);')<fn.index('osSendMesg_recomp(rdram, ctx);'),'Producer capture occurs after scheduling')
            for token in ('ctx->r11 = SUB32(ctx->r5, ctx->r4);','ctx->r12 = S32(SIGNED(ctx->r11) >> 3);','MEM_W(0X44, ctx->r7) = ctx->r12;'):
                need(token in fn,'Native task command-count ABI changed: '+token)
            hits.append({'revision':rev,'path':str(path),'sha256':pipeline.sha(path.read_bytes())})
        need(len(hits)==1,'Missing or duplicate final producer hook');hooks+=hits
    old=next(Path(r['path']).read_text() for r in rows if r['target']=='ultramodern')
    events=files['ultramodern/events.cpp'].decode()
    for token in ('sp_complete();','dp_complete();','host_tasks::registry.mark(', 'action_queue.enqueue(', 'sp_task_queue.enqueue('):
        need(old.count(token)==events.count(token),'Interrupt/queue lifecycle changed: '+token)
    need(events.count('graphics_handoff::discard(')==1 and events.count('graphics_handoff::reset();')==2,'Suppressed/reset ownership missing')
    need(events.index('graphics_handoff::take(')<events.index('std::memcpy(rdram_snapshot.get(), rdram, RDRAMSnapshotSize)'),'Fallback precedes transfer')
    saved=sys.modules.get('pipeline')
    try:
        prior_pipeline=load('handoff_render_pipeline',ROOT/'patches/render-stability/pipeline.py');prior_pipeline.VERSION=pipeline.VERSION
        sys.modules['pipeline']=prior_pipeline
        prior=load('handoff_render_audit',ROOT/'patches/render-stability/audit.py')
        retained=prior.inspect(build,binary,baseline,accepted)
    finally:
        if saved is None:sys.modules.pop('pipeline',None)
        else:sys.modules['pipeline']=saved
    if binary:
        need(binary.stat().st_mtime_ns>=max((stage/name).stat().st_mtime_ns for name in files),'Binary predates handoff inputs')
        data=binary.read_bytes()
        for marker in (pipeline.VERSION,'[render][producer-snapshot]','[render][handoff-summary]','original SP/DP completion retained'):
            need(marker.encode() in data,'Linked handoff missing: '+marker)
    return {'schema':1,'version':pipeline.VERSION,'swaps':rows,'producer_hooks':hooks,
        'rollback_build_ids':retained['rollback_build_ids'],'retained':retained,
        'guest_code_changed':False,'protected_sources_edited':False,
        'validation':'Exact composition; actual producer hooks before the scheduler message; two compiler replacements and one shared implementation; retained complete render/memory/online audit. No game execution.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();result=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Producer snapshot ownership and retained render/memory/online chain verified.')
