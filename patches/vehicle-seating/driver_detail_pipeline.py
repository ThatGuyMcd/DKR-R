"""Compose driver-detail and finish-tint fixes into NEW build-local copies.

Never run targets or alter source inputs, protected payloads or dependencies.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'patches/custom-vehicles'))
from pipeline import once,put
from guest_pipeline import body


def local_model(text):
    text=once(text,'#include "local_model_rt64.hpp"','#include "local_model_rt64.hpp"\n#include "vehicle_rt64.hpp"')
    text=once(text,'    std::array<Corner,3> corners{};','    std::array<Corner,3> corners{};\n    unsigned source_triangle=0;')
    text=once(text,'                    Step step;step.kind=Step::Triangle;',
        '                    Step step;step.source_triangle=(source-packet.model.triangles)/16+tri;step.kind=Step::Triangle;')
    text=once(text,'        const Packet* packet=nullptr;',
        '        const Packet* packet=nullptr;\n        const mods::vehicles::ModelProfile* driver_mask=nullptr;')
    anchor='            packet=p.build(o.e);if(!packet || packet->triangles>32768U-p.submitted_triangles)continue;'
    text=once(text,anchor,anchor+'''
            if(const auto* cars=vehicles::active_render_pass();!o.extra&&cars)
                driver_mask=cars->detail_profile(o.e.parameters[1],o.e.parameters[2]);
            if(driver_mask)for(const auto& step:packet->steps)
                if(step.kind==Step::Triangle&&step.source_triangle>=driver_mask->hide.size())
                    throw std::runtime_error("high-detail driver mask exceeds its own model");''')
    anchor='            rsp.textureState.on=std::uint8_t(step.w1);'
    text=once(text,anchor,'''            // LOD0 mask is checked before any GPU draw. Retain the driver,
            // remove only its reviewed chassis, and leave attachments alone.
            if(driver_mask&&driver_mask->hide[step.source_triangle])continue;
'''+anchor)
    text=once(text,'        state.flush();o.replaced=true;++p.draws;p.submitted_triangles+=packet->triangles;',
        '''        state.flush();o.replaced=true;++p.draws;p.submitted_triangles+=packet->triangles;
        if(!o.extra&&p.logged++<16)std::fprintf(stderr,"[graphics][local-detail] highest-loaded driver instance=%08X object=%08X car-mask=%u triangles=%u; independent-driver-detail-v1\\n",
            o.e.parameters[2],o.e.parameters[1],unsigned(driver_mask!=nullptr),packet->triangles);''')
    return text


def visual_kernel(text):
    # C00000-C0FFFF is immediate vertex scratch; D00000+ holds labels, car
    # textures and model textures. The old E arena overlapped car uploads.
    # C10000-CFFFFF is disjoint from every persistent texture bank and camera.
    values={'0xE00000':'0xC10000','0xEF0000':'0xD00000','0xE00100':'0xC10100',
        '0xE00200':'0xC10200','0xE00300':'0xC10300','0xE10000':'0xC20000',
        '0xE20000':'0xC30000','0xE30000':'0xC40000','0xE40000':'0xC50000',
        '0xE50000':'0xC60000','0xE90000':'0xCA0000','0xEE0000':'0xCF0000'}
    for old,new in values.items():
        if old not in text:raise ValueError('Visual arena anchor missing: '+old)
        text=text.replace(old,new)
    return text


def bridge(text):
    text=once(text,'            if (kind == PresentationMarkerKind::BackgroundAspect) {',
        '''            if (kind == PresentationMarkerKind::BackgroundAspect ||
                (kind==PresentationMarkerKind::TrackSelectLensFlare&&presentation_token==1U)) {''')
    text=once(text,'''                data.background_fill_stretch_active =
                    presentation_token <= 2U ? presentation_token : 0U;''',
        '''                data.background_fill_stretch_active =
                    presentation_token <= 2U ? presentation_token : 0U; // postrace-lens-window-v1''')
    return text


def stubs(text):
    anchor='''    if (!dkr::runtime::enhancements::preserve_track_select_lens_flare_tint('''
    text=once(text,anchor,'''    const bool postrace_lens=PostraceFullViewScope(rdram,ExpandedCoverScale());
    if (!postrace_lens && !dkr::runtime::enhancements::preserve_track_select_lens_flare_tint(''')
    # Replace only the tint begin callback's marker token; its matching end is
    # unchanged, and the frame gate advances only after a submitted wood draw.
    begin=text.index('extern "C" void dkr_track_select_lens_flare_tint_begin(')
    end=text.index('extern "C" void dkr_track_select_lens_flare_tint_end(',begin)
    part=once(text[begin:end],'rdram, context->r17, PresentationGroupMode::StaticAuto, 0U,',
        'rdram, context->r17, PresentationGroupMode::StaticAuto,\n            postrace_lens&&!g_postrace_frame_gate.contracted_frame_submitted?1U:0U,')
    return text[:begin]+part+text[end:]


def owned_native(text):
    anchor='''        lens_scope=dkr_probe_lens_scope(b,MEM_W(0,guest_address(a[6])));
        if(lens_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_LENS_BEGIN,0);'''
    return once(text,anchor,'''        const int postrace=dkr_probe_postrace_scope(MEM_W(0,guest_address(a[1])),
            (int8_t)MEM_B(0,guest_address(a[2])),MEM_W(0,guest_address(a[3])),
            MEM_W(0,guest_address(a[4])),MEM_W(0,guest_address(a[5])));
        lens_scope=postrace||dkr_probe_lens_scope(b,MEM_W(0,guest_address(a[6])));
        if(lens_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_LENS_BEGIN,postrace&&!state.postrace_contracted?1U:0U);''')


def enhancements(text):
    begin=text.index('extern "C" void dkr_apply_maximum_racer_detail(')
    end=text.index('extern "C" void ',begin+len('extern "C" void '))
    part=once(text[begin:end],'        context->r8 = context->r4;', '''        const auto authored_lod=static_cast<unsigned>(context->r8);
        context->r8 = context->r4;
        // Bounded evidence of the native selector's actual first resident LOD.
        // No extra guest reads, allocation, animation tick or model mutation.
        static std::atomic<unsigned> detail_reports{0};
        if(behaviour==1U && detail_reports.load(std::memory_order_relaxed)<32U &&
            detail_reports.fetch_add(1,std::memory_order_relaxed)<32U)
            std::fprintf(stderr,"[graphics][driver-detail] object=%08X authored-lod=%u selected-lod=%u; native-lod-selector-v2\\n",
                static_cast<unsigned>(context->r16),authored_lod,static_cast<unsigned>(context->r8));''')
    return text[:begin]+part+text[end:]


def primary_guest(text):
    span=body(text,'track_setup_racers')
    if span is None:return text,False
    start,end=span;part=text[start:end]
    # Both reviewed revisions share this primary (not ghost) spawn ABI.
    # fp/r30 is vehicle; a3/r7 is a Settings racer-row POINTER, not vehicle.
    # Require the native register lineage, not just presence of our new hook.
    for anchor in (
        '// 0x8000D634: slti        $at, $fp, 0x5\n    ctx->r1 = SIGNED(ctx->r30) < 0X5 ? 1 : 0;',
        '// 0x8000D644: sll         $t8, $fp, 2\n    ctx->r24 = S32(ctx->r30 << 2);',
        '// 0x8000D658: addu        $a3, $s7, $t6\n    ctx->r7 = ADD32(ctx->r23, ctx->r14);',
        '// 0x8000D780: or          $a1, $s1, $zero\n    ctx->r5 = ctx->r17 | 0;',
    ):
        if part.count(anchor)!=1:raise ValueError('Primary vehicle register ABI changed: '+anchor)
    # The AI code-mod hook has already selected the actual character here.
    site=part.index('// 0x8000D788: jal')
    before,tail=part[:site],part[site:]
    tail=replace_primary_call(tail)
    return text[:start]+before+tail+text[end:],True


def replace_primary_call(tail):
    call=tail.index('    spawn_object(rdram, ctx);')
    return tail[:call]+'''    // Identical resident models on all peers; graphics stays local.
    if((uint32_t)ctx->r30<=2U)ctx->r5&=~(gpr)12U; // primary-driver-lod-residency-v2: fp is vehicle, a3 is Settings row
'''+tail[call:]


def sha(data):return hashlib.sha256(data).hexdigest()


def main(a):
    if a.output.resolve().is_relative_to(ROOT):raise ValueError('Stage outside the checkout')
    rows=json.loads(a.inputs.read_text());swaps=[];guest_counts={}
    transforms={'local_model_rt64.cpp':local_model,'visual_model_kernel.c':visual_kernel,
        'f3ddkr_rt64.cpp':bridge,'runtime_stubs.cpp':stubs,'probe_native.c':owned_native,
        'runtime_enhancements.cpp':enhancements}
    for row in rows:
        old=Path(row['path']);original=old.read_bytes();text=original.decode();name=old.name
        if row.get('guest'):
            text,changed=primary_guest(text)
            guest_counts[row['target']]=guest_counts.get(row['target'],0)+int(changed)
            if not changed:continue
        else:text=transforms[name](text)
        target=a.output/row['target']/name
        put(target,text.encode())
        swaps.append({**row,'new':str(target.resolve()),'input_sha256':sha(original),'output_sha256':sha(text.encode())})
    if len(guest_counts)!=4 or any(n!=1 for n in guest_counts.values()):raise ValueError('Primary driver hook count: '+repr(guest_counts))
    put(a.output/'swaps.json',(json.dumps(swaps,indent=2)+'\n').encode())
    print('Driver residency, matched high-driver masks, disjoint visual arena and postrace lens scope staged; no target execution.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    main(p.parse_args())
