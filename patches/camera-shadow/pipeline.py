"""Add private Camera++ shadow sidecars in NEW Patch Pipeline output."""
from pathlib import Path
import argparse
import json
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
AUTH = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'scripts'))
from camera_plus_pipeline import digest, new_output, replace, verify as verify_camera
sys.path.insert(0, str(ROOT / 'patches/launcher-polish'))
from pipeline import inspect as verify_ui


def authored():
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(AUTH.iterdir()) if p.is_file()}


def transform(name, text):
    if name == 'camera_rt64.hpp':
        text = replace(text, '#include "camera_kernel.h"', '#include "camera_kernel.h"\n#include "camera_shadow_kernel.h"')
        text = replace(text, '    void before_command(std::uint32_t physical);', '''    struct ShadowMarker {
        std::uint32_t address=0,identity=0;
        std::uint64_t history=0;
        std::uint8_t batches=0;
        bool begin=false;
        presentation::RigidShadowOwnerPolicy policy{};
        presentation::ShadowOwnerMotionSample motion{};
    };
    std::span<const ShadowMarker> shadows_at(std::uint32_t physical)const;
    void before_command(std::uint32_t physical);''')
        return replace(text, '    dkr_camera_draw_result draw_{};', '''    struct ShadowOwner {
        std::uint32_t object,header,asset,kind,view,continuity;
        std::uint64_t history;
    };
    void capture_shadows(const Range&);
    std::vector<ShadowOwner> shadow_owners_;
    std::vector<ShadowMarker> shadow_markers_;
    std::array<std::uint32_t,4> shadow_continuity_{};
    std::array<std::uint64_t,4> shadow_cut_frame_{{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX}};
    dkr_camera_shadows shadows_{};
    dkr_camera_draw_result draw_{};''')
    if name == 'camera_rt64.cpp':
        text = replace(text, '#include <algorithm>', '#include <algorithm>\n#include <atomic>')
        text = replace(text, 'session_.reset();active_=next;failed_views_=0;',
                       'session_.reset();active_=next;failed_views_=0;shadow_owners_.clear();')
        text = replace(text, 'if(failed_scene_!=stamp.scene){failed_scene_=stamp.scene;failed_views_=0;}',
                       'if(failed_scene_!=stamp.scene){failed_scene_=stamp.scene;failed_views_=0;shadow_owners_.clear();shadow_continuity_={};shadow_cut_frame_.fill(UINT64_MAX);}')
        text = replace(text, '        scene_=stamp.scene;full_screen_=full_screen;aspect_=stamp.aspect;', '''        for(const auto& range:ranges_)if(range.pose.cut) {
            const auto frame=(std::uint64_t(stamp.epoch)<<32)|stamp.frame;
            if(shadow_cut_frame_[range.view]!=frame) {
                shadow_cut_frame_[range.view]=frame;++shadow_continuity_[range.view];
            }
        }
        scene_=stamp.scene;full_screen_=full_screen;aspect_=stamp.aspect;''')
        text = replace(text, '    const bool okay=active_->revision==rom::Revision::UsV77?',
                       '    bool okay=active_->revision==rom::Revision::UsV77?')
        for revision in (77, 80):
            text = replace(text, f'dkr_camera_v{revision}_draw(decoder_.data(),decoder_.size(),&request,&draw_)',
                           f'dkr_camera_v{revision}_shadow_draw(decoder_.data(),decoder_.size(),&request,&draw_,&shadows_)')
        text = replace(text, '    if(!okay) {', '''    if(okay)try {capture_shadows(*range);}catch(const std::exception& e) {
        report(e.what());okay=false;draw_.failure="Camera shadow metadata was rejected.";
    }
    if(!okay) {''')
        text = replace(text, 'void RenderPass::finish()noexcept {',
                       (AUTH/'shadow_pass.inl').read_text()+'\nvoid RenderPass::finish()noexcept {\n    shadow_markers_.clear();')
        return text
    if name == 'f3ddkr_rt64.cpp':
        text = replace(text, '    camera::RenderPass* camera=nullptr;',
                       '    camera::RenderPass* camera=nullptr;\n    std::optional<camera::RenderPass::ShadowMarker> camera_shadow;')
        text = replace(text, '''    const auto rigid_policy =
        dkr::runtime::presentation::shadow_owner_rigid_policy(
            data.shadow_history_key);
    const auto owner_motion =
        dkr::runtime::presentation::shadow_owner_motion_sample(
            data.shadow_history_key);''', '''    const auto rigid_policy = data.camera_shadow ? data.camera_shadow->policy :
        dkr::runtime::presentation::shadow_owner_rigid_policy(data.shadow_history_key);
    const auto owner_motion = data.camera_shadow ? data.camera_shadow->motion :
        dkr::runtime::presentation::shadow_owner_motion_sample(data.shadow_history_key);''')
        text = replace(text, '    const std::uint8_t shadow_aspect = ActiveAspectMode(',
                       '    const std::uint8_t shadow_aspect = data.camera_shadow ? G_EX_ASPECT_ADJUST : ActiveAspectMode(')
        text = replace(text, '            interpolate_page_vertices);',
                       '            interpolate_page_vertices, 0U, data.camera_shadow.has_value());')
        text = replace(text, '        shadow_aspect);',
                       '        shadow_aspect, false, 0U, data.camera_shadow.has_value());')
        text = replace(text, '''    const auto markers =
        dkr::runtime::presentation::active_presentation_markers(''', '''    if(auto* camera=active_->data_->camera) {
        for(const auto& marker:camera->shadows_at(std::uint32_t(command-rdram_begin))) {
            auto& data=*active_->data_;
            if(marker.begin) {
                data.camera_shadow=marker;
                ApplyPresentationGroup(state,kPresentationGroupShadowMode,0,marker.batches,PresentationMarkerKind::Geometry);
            } else {
                ApplyPresentationGroup(state,0,0,0,PresentationMarkerKind::Geometry);
                data.camera_shadow.reset();
            }
        }
    }
    const auto markers =
        dkr::runtime::presentation::active_presentation_markers(''')
        text = replace(text, '''        SelectInterpolationGroup(
            *state->rsp, active_group.identity,''', '''        const bool camera_visual=data.camera_matrices[std::min(data.selected_matrix,2U)];
        SelectInterpolationGroup(
            *state->rsp, active_group.identity,''')
        text = replace(text, '''            ActiveAspectMode(data.interpolation_groups,
                             data.matrix_aspect_override_active,
                             data.matrix_aspect_override), false,
            active_group.procedural_water ? active_group.water_scroll_tag : 0U);''', '''            camera_visual ? G_EX_ASPECT_ADJUST : ActiveAspectMode(data.interpolation_groups,
                             data.matrix_aspect_override_active,
                             data.matrix_aspect_override), false,
            active_group.procedural_water ? active_group.water_scroll_tag : 0U,camera_visual);''')
        text = replace(text, '''    const std::uint32_t scoped_identity =
        dkr::runtime::interpolation::scope_identity_uses_selected_matrix(''', '''    const std::uint32_t scoped_identity = mode==kPresentationGroupShadowMode&&data.camera_shadow
        ? data.camera_shadow->identity :
        dkr::runtime::interpolation::scope_identity_uses_selected_matrix(''')
        text = replace(text, '''            data.shadow_history_key =
                presentation_token != 0U && scene_generation != 0U''', '''            data.shadow_history_key = data.camera_shadow ? data.camera_shadow->history :
                presentation_token != 0U && scene_generation != 0U''')
        return text
    if name == 'rt64_renderer.cpp':
        return text  # Recompile against the new private RenderPass layout.
    raise ValueError('Unreviewed Camera shadow input')


FILES=('camera_rt64.cpp','camera_rt64.hpp','f3ddkr_rt64.cpp','rt64_renderer.cpp')


def expected(camera,kernels):
    result={f'native/src/game/{name}':transform(name,(camera/'src/game'/name).read_text()) for name in FILES}
    result['native/src/game/camera_shadow_kernel.h']=(AUTH/'camera_shadow_kernel.h').read_text()
    for rev,path in kernels.items():
        hooks=(path/'camera_scene_hooks.c').read_text()
        for suffix in ('begin','end'):
            hooks=replace(hooks,f'unsigned dkr_camera_v{rev}_dkr_shadow_interpolation_{suffix}(uint8_t* ram,recomp_context* ctx,...)'
                          '{(void)ram;(void)ctx;return 0;}\n','')
        result[f'kernel-v{rev}/camera_scene_hooks.c']=hooks
        result[f'kernel-v{rev}/camera_shadow_kernel.c']=(AUTH/'camera_shadow_kernel.c').read_text()
    return result


def inspect(output):
    receipt=json.loads((output/'shadow-receipt.json').read_text())
    camera=ROOT/receipt['camera'];ui=ROOT/receipt['ui']
    kernels={int(rev):ROOT/name for rev,name in receipt['kernels'].items()}
    verify_camera(camera);verify_ui(ui)
    sys.path.insert(0,str(ROOT/'patches/camera-plus'))
    from scene_kernel_pipeline import verify as verify_kernel
    for path in kernels.values():verify_kernel(path)
    if receipt['authored']!=authored():raise ValueError('Shadow recipe changed; generate a fresh stage')
    for name,text in expected(camera,kernels).items():
        path=output/name
        if path.is_symlink() or path.read_text()!=text or digest(path)!=receipt['files'][name]:
            raise ValueError('Shadow source is not its reviewed transformation: '+name)
    actual={p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file() and p.name!='shadow-receipt.json'}
    if actual!=set(receipt['files']):raise ValueError('Unexpected shadow stage files')
    print('Camera++ shadow sidecars inspected; original camera, launcher and kernels preserved')


def stage(camera,ui,kernels,output):
    verify_camera(camera);verify_ui(ui)
    output=new_output(output,camera,ui,*kernels.values())
    files=expected(camera,kernels)
    for name,text in files.items():
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,newline='\n')
    receipt={'schema':1,'camera':camera.relative_to(ROOT).as_posix(),'ui':ui.relative_to(ROOT).as_posix(),
             'kernels':{rev:path.relative_to(ROOT).as_posix() for rev,path in kernels.items()},'authored':authored(),
             'files':{name:digest(output/name) for name in files}}
    (output/'shadow-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    inspect(output)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('stage','verify'))
    p.add_argument('--camera',type=Path);p.add_argument('--ui',type=Path)
    p.add_argument('--kernel-v77',type=Path);p.add_argument('--kernel-v80',type=Path)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.mode=='verify':inspect(a.output.resolve(strict=True))
    else:stage(a.camera.resolve(strict=True),a.ui.resolve(strict=True),
               {77:a.kernel_v77.resolve(strict=True),80:a.kernel_v80.resolve(strict=True)},a.output)
