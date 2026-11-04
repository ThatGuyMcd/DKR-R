"""Verify the ten final compiler substitutions and the entire retained chain."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def need(ok,message):
    if not ok:raise ValueError(message)
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def compiled(build,target):
    if (build/(target+'.vcxproj')).is_file():
        project=build/(target+'.vcxproj')
        if target=='rt64':project=build/'rt64/rt64.vcxproj'
        return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
    if target=='rt64' and (build/'rt64/rt64.vcxproj').is_file():
        return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',(build/'rt64/rt64.vcxproj').read_text(encoding='utf-8-sig'))}
    prefix='build '+('rt64/CMakeFiles/' if target=='rt64' else 'CMakeFiles/')+target+'.dir/'
    return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
        if line.startswith(prefix) if (m:=re.search(r': [^ ]+ (\S+)',line))}
def inspect(build,binary,baseline,accepted):
    stage=build/'generated/render-stability';rows,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
    need(rows==json.loads((stage/'swaps.json').read_text()),'Render source receipt differs')
    for name,data in outputs.items():need((stage/name).read_bytes()==data,'Stale render output: '+name)
    for row in rows:
        sources=compiled(build,row['target'])
        need(Path(row['new']).resolve() in sources,'Final render source is not compiled: '+row['new'])
        need(Path(row['path']).resolve() not in sources,'Superseded render source is still compiled')
        original=Path(row['path']).read_text();current=Path(row['new']).read_text()
        for token in ('->wait(', '->waitForGPUUploads(', '->spInterrupt(', '->dpInterrupt('):
            need(original.count(token)==current.count(token),'GPU/interrupt lifecycle changed: '+token)
    saved={name:sys.modules.get(name) for name in ('pipeline','transforms','lobby_layout')}
    try:
        old=ROOT/'patches/memory-stability'
        sys.modules['pipeline']=load('render_retained_memory_pipeline',old/'pipeline.py')
        prior=load('render_retained_memory_audit',old/'audit.py')
        prior.pipeline.VERSION=pipeline.VERSION
        retained=prior.inspect(build,binary,baseline,accepted)
    finally:
        for name,value in saved.items():
            if value is None:sys.modules.pop(name,None)
            else:sys.modules[name]=value
    bridge=outputs['DKRPortGame/f3ddkr_rt64.cpp'].decode()
    view=outputs['DKRPortGame/online_view_rt64.cpp'].decode()
    picture=outputs['DKRNetplayCore/experimental_present_gate.cpp'].decode()
    need('online_presentation/queue_picture.inl' in picture,'Immutable racing-picture classifier is not compiled')
    need(outputs['DKRNetplayCore/experimental_present_gate.cpp']==outputs['DKRPortGame/experimental_present_gate.cpp'],
        'Library and direct present-gate implementations differ')
    present=outputs['rt64/rt64_present_queue.cpp'].decode()
    for marker in ('requires_gpu_picture(&present,present.presentId,present.workloadId)',
        'skipInterpolation();notifyPresentId(present);return;',
        '(ownedImage || !present.colorImageAddresses.empty())',
        'if (!ownedImage && !present.debuggerFramebuffer.view'):
        need(marker in present,'Owned-picture recovery or exact-workload selection missing: '+marker)
    for marker in ('dkr::online_stability::view_eligibility','dkr::online_stability::focus_plan',
        'e.kind==DKR_OWNED_CAMERA_WORLD_END)continue;','hud::groups::scenario_for(int(p.race_type),false)'):
        need(marker in view,'Final fullscreen consumer is missing current policy: '+marker)
    for marker in ('kMaxDKRVertices = 32','RejectTask(display_list)','source_end > kRDRAMSize','IsSafeCountedOpcode(opcode)',
        'source,count*16U','address,count*8U','water::residency().publish(&workload,workload.submissionFrame)'):
        need(marker in bridge,'Render guard or publication missing: '+marker)
    for name in ('rt64_state.cpp','rt64_workload_queue.cpp'):
        need(outputs['rt64/'+name].decode().count('WorkloadScope liquid_scope(&workload,workload.submissionFrame)')==1,'Missing workload identity binding')
    if binary:
        need(binary.stat().st_mtime_ns>=max((stage/name).stat().st_mtime_ns for name in outputs),
             'Binary predates the final staged render inputs')
        data=binary.read_bytes()
        for marker in (pipeline.VERSION,'immutable workload families','[water][reload]','[water][coverage]',
            '[render][provenance]','[render][decode-isolation]','checked-bytes-match-submission','payload-changed-after-submission'):
            need(marker.encode() in data,'Linked render repair missing: '+marker)
    return {'schema':1,'version':pipeline.VERSION,'swaps':rows,'rollback_build_ids':retained['rollback_build_ids'],
        'retained':retained,'presentation_only':True,'protected_sources_edited':False,
        'validation':'Exact recomposition, ten actual compiler bindings, identical direct/library present gates, immutable racing-picture recovery, current fullscreen consumer binding, retained full memory/online audit chain, GPU/interrupt call preservation and linked markers. Synthetic execution and manual gameplay acceptance are recorded separately.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();result=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Render repair and retained memory/online chain verified; no game execution.')
