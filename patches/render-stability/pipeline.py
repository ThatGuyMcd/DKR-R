"""Compose presentation-only repair copies above the accepted memory layer."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.10-render-playtest.1'
NAMES={('DKRPortGame','f3ddkr_rt64.cpp'),('DKRPortGame','rt64_renderer.cpp'),('DKRPortGame','online_view_rt64.cpp'),
       ('DKRNetplayCore','experimental_present_gate.cpp'),('DKRPortGame','experimental_present_gate.cpp'),
       ('rt64','rt64_texture_cache.cpp'),('rt64','rt64_framebuffer_renderer.cpp'),
       ('rt64','rt64_state.cpp'),('rt64','rt64_workload_queue.cpp'),('rt64','rt64_present_queue.cpp')}
def sha(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Render repair anchor changed: '+old[:120])
    return text.replace(old,new,1)
def compose(rows,out):
    if len(rows)!=len(NAMES) or {(r['target'],Path(r['path']).name) for r in rows}!=NAMES:raise ValueError('Render source closure differs')
    bridge=next(r for r in rows if Path(r['path']).name=='f3ddkr_rt64.cpp')
    search=[Path(p) for p in bridge['includes']]
    original_water=next((p/'water_residency_rt64.hpp' for p in search if (p/'water_residency_rt64.hpp').is_file()),None)
    if not original_water:raise ValueError('Accepted water header not resolved')
    search=[AUTH/'native',original_water.parent,*search]
    def resolve(text):
        def include(m):
            if m[1]=='enhanced_water_rt64.hpp':
                styled=next((p/'styled_water_rt64.hpp' for p in search if (p/'styled_water_rt64.hpp').is_file()),None)
                if styled:return '#include "'+styled.resolve().as_posix()+'"'
            # RT64's transitive interface selects its staged ABI headers.
            # Do not bypass that include order with a checkout header.
            if m[1].startswith(('render/','hle/')):return m[0]
            candidates=[p/m[1] for p in search]
            path=next((p for p in candidates if p.is_file()),None)
            return '#include "'+path.resolve().as_posix()+'"' if path else m[0]
        return re.sub(r'^#include "([^"\n]+)"',include,text,flags=re.M)
    files={}
    for name in ('liquid_replacement_gate.hpp','water_residency_rt64.hpp','render_provenance.hpp'):
        files[name]=resolve((AUTH/'native'/name).read_text()).encode()
    swaps=[]
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n');name=source.name
        if name=='experimental_present_gate.cpp':
            text='#include "'+(ROOT/'runtime-recomp/src/game/online_presentation/frame_policy.hpp').as_posix()+'"\n#include "'+(ROOT/'runtime-recomp/src/game/online_stability/battle_view.hpp').as_posix()+'"\n'+text
            anchor='    return owner_->condition.wait_for(lock,timeout,[&]{return owner_->worker_waiting;});\n}\n}'
            text=once(text,anchor,anchor[:-1]+'#include "'+(ROOT/'runtime-recomp/src/game/online_presentation/queue_picture.inl').as_posix()+'"\n}')
        elif name=='online_view_rt64.cpp':
            # The capsule still selects the older consumer. Apply the already
            # authored camera/fullscreen integration to THIS compiler input.
            # Do not modify the capsule or silently leave the policy unbound.
            integration=ROOT/'patches/camera-plus/integration_pipeline.py'
            spec=importlib.util.spec_from_file_location('render_camera_integration',integration)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            def replace(content,old,new,count=1):
                if count!=1:raise ValueError('Unexpected fullscreen integration count')
                return once(content,old,new)
            text=module.online_view_policy(text,replace)
        elif name=='rt64_present_queue.cpp':
            text='#include "'+(ROOT/'runtime-recomp/src/game/mod_store/overlay_completion.hpp').as_posix()+'"\n'+text
            text=text.replace('ext.presentGraphicsWorker->wait();',
                'ext.presentGraphicsWorker->wait();\n                    dkr::store::overlay::completed();')
            text='#include "'+(ROOT/'runtime-recomp/src/game/online_presentation/frame_policy.hpp').as_posix()+'"\n#include "'+(ROOT/'runtime-recomp/src/game/online_presentation/test_receive_delay.hpp').as_posix()+'"\n'+text
            anchor='            if ((presentFb != nullptr) && (viFb != nullptr)) {'
            text=once(text,anchor,'''            const bool ownedImage=dkr::runtime::netplay::experimental::performance::observed_present;
            bool missingImage=presentFb==nullptr||viFb==nullptr||(ownedImage&&present.colorImageAddresses.empty());
            const bool simulateMissing=dkr::online_presentation::test_fixture::missing_gpu_target.load(std::memory_order_relaxed);
            if(ownedImage&&!viewRDRAM&&!present.debuggerFramebuffer.view&&(missingImage||simulateMissing)&&
                dkr::runtime::netplay::experimental::present_gate::requires_gpu_picture(&present,present.presentId,present.workloadId)) {
                if(!missingImage&&dkr::online_presentation::test_fixture::missing_gpu_target.exchange(false,std::memory_order_relaxed)) {
                    missingImage=true;
                    dkr::online_presentation::count(dkr::online_presentation::Event::ForcedMissingTarget);
                }
                if(missingImage) {
                    // A racing snapshot contains GPU display lists, not a
                    // completed native RAM picture. Retain the last completed
                    // image; use the existing skipped-present completion path.
                    dkr::online_presentation::count(dkr::online_presentation::Event::PictureHeld);
                    if(presentFb==nullptr||viFb==nullptr)dkr::online_presentation::count(dkr::online_presentation::Event::MissingFramebuffer);
                    if(present.colorImageAddresses.empty())dkr::online_presentation::count(dkr::online_presentation::Event::MissingCapturedTarget);
                    skipInterpolation();notifyPresentId(present);return;
                }
            }
'''+anchor)
            text=once(text,'const std::vector<uint32_t> &colorImageAddresses = present.colorImageAddresses.empty() ? ext.sharedResources->colorImageAddressVector : present.colorImageAddresses;',
                'const std::vector<uint32_t> &colorImageAddresses = (ownedImage || !present.colorImageAddresses.empty()) ? present.colorImageAddresses : ext.sharedResources->colorImageAddressVector;\n                if(ownedImage)presentFb->interpolationEnabled=false;')
            text=once(text,'if (!present.debuggerFramebuffer.view && (presentationMode == EnhancementConfiguration::Presentation::Mode::SkipBuffering)) {',
                'if (!ownedImage && !present.debuggerFramebuffer.view && (presentationMode == EnhancementConfiguration::Presentation::Mode::SkipBuffering)) {')
            text=once(text,'                    dkr::runtime::netplay::experimental::performance::presented();',
                '                    dkr::runtime::netplay::experimental::performance::presented();\n                    dkr::online_presentation::presented(dkr::runtime::netplay::experimental::performance::observed_present,present.paused,present.screenVI.visible());')
            text=once(text,'                uint32_t fbAddress = present.screenVI.fbAddress();',
                '                if(dkr::runtime::netplay::experimental::performance::observed_present)dkr::online_presentation::count(dkr::online_presentation::Event::NativeFallback);\n                uint32_t fbAddress = present.screenVI.fbAddress();')
            text=once(text,'            if ((presentFb != nullptr) && (viFb != nullptr)) {',
                '            if ((presentFb != nullptr) && (viFb != nullptr)) {\n                if(dkr::runtime::netplay::experimental::performance::observed_present)dkr::online_presentation::count(dkr::online_presentation::Event::RenderTarget);')
        elif name=='f3ddkr_rt64.cpp':
            text='#include "'+(out/'render_provenance.hpp').as_posix()+'"\n'+text
            text=once(text,'void ReportMalformedInput(RT64::State* state,RT64::DisplayList* command,std::uint64_t task) {',
                'void ReportMalformedInput(RT64::State* state,RT64::DisplayList* command,std::uint64_t task,std::uint32_t payload=0,std::uint32_t bytes=0) {')
            text=once(text,'        static_cast<unsigned long long>(task),unsigned(address),command->w0,command->w1);',
                '        static_cast<unsigned long long>(task),unsigned(address),command->w0,command->w1);\n'
                '    const auto word=[&](std::uint32_t p) {std::uint32_t value=0;p&=0x1FFFFFFFU;\n'
                '        if(p<=0x7FFFFCU)std::memcpy(&value,state->RDRAM+p,4);return value;};\n'
                '    dkr::runtime::render_provenance::report(task,unsigned(address),payload,bytes,\n'
                '        dkr::runtime::presentation::task_scene_generation(),\n'
                '        word(dkr::runtime::revision_addresses::CurrentMapId),word(dkr::runtime::revision_addresses::CurrentMenuId));')
            text=once(text,'            ReportMalformedInput(state,*display_list,data.task_count);',
                '            ReportMalformedInput(state,*display_list,data.task_count,source,count*16U);')
            text=once(text,'                             address, i, count, commands[i].w0, commands[i].w1);\n            }\n            RejectTask(display_list);',
                '                             address, i, count, commands[i].w0, commands[i].w1);\n            }\n'
                '            ReportMalformedInput(state,*display_list,data.task_count,address,count*8U);\n            RejectTask(display_list);')
            text=once(text,'    handler(state, display_list);\n    auto& data=*active_->data_;',
                '#if defined(DKR_ENHANCED_WATER)\n    if(opcode==kFullSyncOpcode) {\n'
                '        auto& workload=state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor];\n'
                '        if(active_->data_->water_registry)water::residency().publish(&workload,workload.submissionFrame);\n'
                '        else water::streaming::publish(&workload,workload.submissionFrame,{});\n'
                '    }\n#endif\n    handler(state, display_list);\n    auto& data=*active_->data_;')
        elif name=='rt64_renderer.cpp':
            text='#include "'+(out/'render_provenance.hpp').as_posix()+'"\n'+text
            # Keep the leased submission pristine even for a stock frame with
            # no optional camera/car/detail pass. Reuse the existing arena.
            text=once(text,'    if(camera_requested||cars_requested||detail_requested) {',
                '    { // render repair: pristine submission, reusable private decode arena')
            text=once(text,'                                         decode_bytes);',
                '                                         decode_bytes);\n'
                '    render_provenance::Scope provenance({rdram_snapshot,0x800000},\n'
                '        {decode_bytes,decode_bytes==rdram_snapshot?0x800000U:camera_decoder_.size()},task->t.data_ptr);\n'
                '    static std::atomic_flag provenance_notice=ATOMIC_FLAG_INIT;\n'
                '    if(!provenance_notice.test_and_set(std::memory_order_relaxed))\n'
                '        std::fprintf(stderr,"[render][decode-isolation] pristine submission; reusable private workspace; 8 MiB copy per source task\\n");')
        elif name=='rt64_texture_cache.cpp':
            text=once(text,'            dkr::runtime::water::streaming::changed(this);',
                '            dkr::runtime::water::streaming::evicted(this);')
        elif name in ('rt64_state.cpp','rt64_workload_queue.cpp'):
            text='#include "'+(out/'liquid_replacement_gate.hpp').as_posix()+'"\n'+text
            # Both normal queued rendering and synchronous framebuffer paths
            # must select the publication for this exact workload slot/frame.
            anchor='framebufferRenderer->updateTextureCache(ext.textureCache);'
            if text.count(anchor)!=1:raise ValueError('Texture capture call changed: '+name)
            text=text.replace(anchor,'{\n                    dkr::runtime::water::streaming::WorkloadScope liquid_scope(&workload,workload.submissionFrame);\n                    '+anchor+'\n                }',1)
        # Redirect every old gate/residency include, including absolute paths.
        for header in ('liquid_replacement_gate.hpp','water_residency_rt64.hpp'):
            text=re.sub(r'#include "[^"\n]*'+header.replace('.','\\.')+'"',
                lambda m:'#include "'+(out/header).as_posix()+'"',text)
        def relative(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',relative,text,flags=re.M)
        new=row['target']+'/'+name;files[new]=text.encode()
        swaps.append({**row,'new':str(out/new),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
    # The staged residency header must use the staged gate, not the auth copy.
    files['water_residency_rt64.hpp']=files['water_residency_rt64.hpp'].replace(
        (AUTH/'native/liquid_replacement_gate.hpp').as_posix().encode(),(out/'liquid_replacement_gate.hpp').as_posix().encode())
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Generated render repair must remain outside the checkout')
    swaps,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(swaps,indent=2)+'\n').encode())
    print('Render selection, eviction recovery and bounded fault provenance staged; guest and online state unchanged.')
