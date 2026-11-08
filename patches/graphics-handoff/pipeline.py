"""Add producer-owned graphics snapshots above the accepted render repair."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.10-render-playtest.2'
NAMES={('DKRPortGame','presentation_identity.cpp'),('ultramodern','events.cpp')}
def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Graphics handoff anchor changed: '+old[:120])
    return text.replace(old,new,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def compose(rows,out):
    if len(rows)!=2 or {(r['target'],Path(r['path']).name) for r in rows}!=NAMES:raise ValueError('Handoff source closure changed')
    files={}
    for name in ('producer_snapshot.hpp','producer_snapshot.cpp'):
        text=(AUTH/'native'/name).read_text()
        text=text.replace('#include "graphics_snapshot_pool.hpp"','#include "'+(ROOT/'runtime-recomp/src/game/graphics_snapshot_pool.hpp').as_posix()+'"')
        text=text.replace('#include "performance_trace.hpp"','#include "'+(ROOT/'runtime-recomp/src/game/performance_trace.hpp').as_posix()+'"')
        files[name]=text.encode()
    swaps=[]
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
        text='#include "'+(out/'producer_snapshot.hpp').as_posix()+'"\n'+text
        if source.name=='presentation_identity.cpp':
            text='#include "'+(ROOT/'runtime-recomp/src/game/live_fov/runtime.hpp').as_posix()+'"\n'+text
            text=once(text,'extern "C" void dkr_presentation_frame_begin(std::uint8_t* rdram,\n                                              recomp_context*) {',
                'extern "C" void dkr_presentation_frame_begin(std::uint8_t* rdram,\n                                              recomp_context* context) {\n    dkr::runtime::live_fov::refresh(rdram,context);')
            # This existing hook runs at gfxtask_run_xbus ENTRY, before its
            # osSendMesg can wake the scheduler. Capture regardless of Modern
            # presentation; historical replay keeps its existing suppression.
            anchor='    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {\n        return;\n    }\n    SubmittedFrame frame{};'
            text=once(text,anchor,
                '    dkr::runtime::graphics_handoff::capture(rdram,static_cast<std::uint32_t>(context->r4),\n'
                '        static_cast<std::uint32_t>(context->r5));\n'+anchor)
        else:
            text=once(text,'static RdramSnapshotPool rdram_snapshot_pool;','// Pool ownership is shared with the earlier producer handoff.')
            old='''        auto rdram_snapshot = rdram_snapshot_pool.acquire();
        {
            dkr::runtime::performance_trace::Scope measure(
                dkr::runtime::performance_trace::Region::SnapshotCopy, RDRAMSnapshotSize);
            std::memcpy(rdram_snapshot.get(), rdram, RDRAMSnapshotSize);
        }'''
            new='''        auto rdram_snapshot = dkr::runtime::graphics_handoff::take(
            rdram,static_cast<std::uint32_t>(task->t.data_ptr),task->t.data_size);
        if (!rdram_snapshot) {
            rdram_snapshot = dkr::runtime::graphics_handoff::acquire();
            dkr::runtime::performance_trace::Scope measure(
                dkr::runtime::performance_trace::Region::SnapshotCopy, RDRAMSnapshotSize);
            std::memcpy(rdram_snapshot.get(), rdram, RDRAMSnapshotSize);
        }'''
            text=once(text,old,new)
            text=once(text,'        if (!presentation_allowed) {',
                '        if (!presentation_allowed) {\n'
                '            dkr::runtime::graphics_handoff::discard(rdram,\n'
                '                static_cast<std::uint32_t>(task->t.data_ptr),task->t.data_size);')
            text=once(text,'static void reset_event_state() {','static void reset_event_state() {\n    dkr::runtime::graphics_handoff::reset();')
            anchor='    while (events_context.action_queue.try_dequeue(action)) {}'
            # Reset and shutdown both drain the action queue. At shutdown all
            # producers have stopped; active leases retain their pool owner.
            if text.count(anchor)!=2:raise ValueError('Event drain closure changed')
            at=text.rfind(anchor);text=text[:at]+text[at:].replace(anchor,anchor+'\n    dkr::runtime::graphics_handoff::reset();',1)
        def include(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',include,text,flags=re.M)
        name=row['target']+'/'+source.name;files[name]=text.encode()
        swaps.append({**row,'new':str(out/name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Generated handoff files must remain outside checkout')
    rows,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    print('Producer-to-scheduler snapshot ownership staged; guest code and interrupt lifecycle unchanged.')
