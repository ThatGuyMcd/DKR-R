"""Reviewed first-party transformations; only used in a NEW copied stage."""
from pathlib import Path

TRANSFORMS={
 'src/game/game_main.cpp','src/game/camera_obstruction.cpp',
 'src/game/runtime_input.hpp','src/game/runtime_input.cpp','src/game/runtime_platform.cpp',
 'src/game/runtime_ui.cpp','src/game/runtime_graphics_ui.inl','src/game/runtime_camera_tuning_ui.inl',
 'src/game/runtime_code_mods_ui.inl','src/game/runtime_controls_ui.inl',
 'src/game/presentation_marker_policy.hpp','src/game/presentation_identity.hpp','src/game/presentation_identity.cpp',
 'src/game/runtime_stubs.cpp','src/game/netplay/experimental_draw_events.h',
 'tests/replay_probe/probe_native.c','src/game/online_view_rt64.cpp',
 'src/game/f3ddkr_rt64.hpp','src/game/f3ddkr_rt64.cpp',
 'src/game/rt64_renderer.hpp','src/game/rt64_renderer.cpp',
}

def online_view_policy(text,replace):
    def one(old,new):
        nonlocal text
        text=replace(text,old,new,1)
    one('#include "online_view_rt64.hpp"','#include "online_view_rt64.hpp"\n#include "online_stability/battle_view.hpp"')
    one('    std::uint32_t scene=0,framebuffer=0;','    std::uint32_t scene=0,framebuffer=0,race_type=0;')
    one('auto& p=*impl_;p.plan=online_view::plan(session,events);p.count=0;p.view=4;',
        'auto& p=*impl_;const auto eligibility=dkr::online_stability::view_eligibility(session.owner,session.players,events);\n'
        '    p.race_type=eligibility.race_type;\n'
        '    p.plan=dkr::online_stability::focus_plan(online_view::plan(session,events),eligibility,session.owner,\n'
        '        requested_layout.load(std::memory_order_relaxed)==Layout::MyRacer,requested_labels.load(std::memory_order_relaxed),requested_label_size.load(std::memory_order_relaxed));p.count=0;p.view=4;')
    one('hud::groups::Scenario::Race,false,false);',
        'hud::groups::scenario_for(int(p.race_type),false),p.plan.layout==1,p.phase==Impl::Phase::Shared);')
    return text

def transform(name,text,replace,auth):
    def one(old,new,count=1):
        nonlocal text
        text=replace(text,old,new,count)
    if name=='src/game/game_main.cpp':
        one('#include "runtime_ui.hpp"','#include "runtime_ui.hpp"\n#include "camera_runtime.hpp"')
        one('if(code_set&&online_save.active&&','if(code_set&&!code_set->ordered.empty()&&online_save.active&&')
        one('            dkr::runtime::code_mods::begin(code_set,registered_revision,',
            '            dkr::runtime::camera::begin(config_directory,DKR_RELEASE_VERSION,registered_revision,online_save.active);\n            dkr::runtime::code_mods::begin(code_set,registered_revision,')
        one('dkr::runtime::code_mods::reset();','dkr::runtime::code_mods::reset();\n        dkr::runtime::camera::reset();',3)
    elif name=='src/game/camera_obstruction.cpp':
        one('#include "modern_camera.hpp"','')
        start=text.index('    /* The local Modern camera')
        end=text.index('\n}',start)
        text=text[:start]+text[end:]
        start=text.index('extern "C" void dkr_modern_camera_restore')
        text=text[:start]+'''// Retained generated-hook ABI. Camera++ never changes live camera RAM.
extern "C" void dkr_modern_camera_restore(std::uint8_t*, recomp_context*) {}
'''
    elif name=='src/game/runtime_input.hpp':
        one('    Accelerate,\n    Count,','''    Accelerate,
    OrbitUp, OrbitDown, OrbitLeft, OrbitRight,
    CameraRecenter, CameraCycle, CameraZoomIn, CameraZoomOut,
    CameraFirstPerson, CameraXrRecenter,
    AnalogueAccelerator, AnalogueBrake,
    Count,''')
        one('    bool accelerate = false;','    bool accelerate = false;\n    float orbit_x=0,orbit_y=0;\n    std::uint32_t camera_actions=0;\n    std::uint8_t accelerator=0,brake=0,driving_flags=0;')
    elif name=='src/game/runtime_input.cpp':
        one('#include "runtime_input.hpp"','#include "runtime_input.hpp"\n#include "analog_driving/poll.hpp"\n#if DKR_RUNTIME_HAS_RT64\n#include "analog_driving/raw_axes.hpp"\n#endif\nnamespace driving=dkr::runtime::driving;')
        one('    {dkr::runtime::input::kUnbound, kAxisSourceBase + SDL_CONTROLLER_AXIS_TRIGGERRIGHT * 2 + 1},',
            '''    {dkr::runtime::input::kUnbound, kAxisSourceBase + SDL_CONTROLLER_AXIS_TRIGGERRIGHT * 2 + 1},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTY*2},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTY*2+1},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTX*2},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_RIGHTX*2+1},
    {SDL_SCANCODE_R,SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {SDL_SCANCODE_C,SDL_CONTROLLER_BUTTON_Y},
    {SDL_SCANCODE_PAGEUP,dkr::runtime::input::kUnbound},
    {SDL_SCANCODE_PAGEDOWN,dkr::runtime::input::kUnbound},
    {SDL_SCANCODE_F6,dkr::runtime::input::kUnbound},
    {SDL_SCANCODE_HOME,dkr::runtime::input::kUnbound},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_TRIGGERRIGHT*2+1},
    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_TRIGGERLEFT*2+1},''')
        one('"c_up", "c_down", "c_left", "c_right", "look_back", "accelerate",',
            '"c_up", "c_down", "c_left", "c_right", "look_back", "accelerate",\n    "camera_up","camera_down","camera_left","camera_right","camera_recenter","camera_cycle",\n    "camera_zoom_in","camera_zoom_out","camera_first_person","camera_xr_recenter",\n    "analogue_accelerator","analogue_brake",')
        one('    "Look back", "Accelerate",','''    "Look back", "Accelerate",
    "Camera up","Camera down","Camera left","Camera right","Centre camera","Cycle camera",
    "Camera zoom in","Camera zoom out","First-person camera","Centre headset",
    "Analogue accelerator","Analogue brake",''')
        for anchor in ('    const auto press = [&](Action action, std::uint16_t mask, float threshold = 0.5F) {',
                       '    const auto press = [&](Action action, std::uint16_t mask,\n                           float threshold = 0.5F) {'):
            one(anchor,'#include "analog_driving/poll.inl"\n'+anchor+'\n        if(analogue_enabled&&(action==Action::A||action==Action::B)){if(action==Action::A?pedals.accelerate:pedals.braking)state.buttons|=mask;return;}\n        if(analogue_enabled&&action==Action::Z){if(raw_value(action,true)>threshold)state.buttons|=mask;return;}')
        one('''    if (blocked) {
        // Keep sampling Controller 1 while the overlay is open so calibration
        // and both live preview bars remain truthful. Gameplay receives a
        // neutral sample until the overlay closes.
        return state;
    }
''','')
        one('    if (blocked) return state;\n','')
        one('    state.look_back = value(Action::LookBack) > 0.5F;', '''    state.look_back = value(Action::LookBack) > 0.5F;
    state.orbit_x=std::clamp(value(Action::OrbitLeft)-value(Action::OrbitRight),-1.F,1.F);
    state.orbit_y=std::clamp(value(Action::OrbitDown)-value(Action::OrbitUp),-1.F,1.F);
    const Action camera_keys[]={Action::LookBack,Action::CameraRecenter,Action::CameraCycle,
        Action::CameraZoomIn,Action::CameraZoomOut,Action::CameraFirstPerson,Action::CameraXrRecenter};
    for(unsigned i=0;i<7;++i)if(value(camera_keys[i])>0.5F)state.camera_actions|=1U<<i;''',2)
    elif name=='src/game/runtime_platform.cpp':
        one('#include "modern_camera.hpp"','#include "camera_runtime.hpp"\n#include "look_behind/policy.hpp"\n#include "analog_driving/input.hpp"')
        one('''    if (g_input_backend_switch_in_progress.load(std::memory_order_acquire)) {
        ClearPublishedControllerInput(false);''','''    if (g_input_backend_switch_in_progress.load(std::memory_order_acquire)) {
        for(unsigned profile=0;profile<4;++profile)dkr::runtime::camera::actions().publish(profile,{});
        ClearPublishedControllerInput(false);''')
        one('const bool blocked = dkr::runtime::ui::overlay_visible();',
            'const bool blocked = dkr::runtime::ui::overlay_visible() || dkr::runtime::camera::editor_open.load();')
        one('''        dkr::runtime::modern_camera::publish_input(
            player, state.camera_x, state.camera_y, state.look_back,
            online_routing);''','''        dkr::runtime::camera::actions().publish(unsigned(player),
            {dkr::runtime::camera::owns_controls(unsigned(player))?state.camera_actions:(state.camera_actions&1U),state.orbit_x,state.orbit_y,0,
                gameplay_blocked||(!dkr::runtime::camera::owns_controls(unsigned(player))&&!dkr::runtime::look_behind::enabled.load()),true});
        dkr::runtime::driving::publish(unsigned(player),{state.accelerator,state.brake,state.driving_flags});
        if(dkr::runtime::camera::owns_controls(unsigned(player))) {
            // Remove only C buttons whose binding is actually shared with a
            // camera direction. Explicit independent C bindings remain usable.
            using A=dkr::runtime::input::Action;
            const A camera[]={A::OrbitUp,A::OrbitDown,A::OrbitLeft,A::OrbitRight};
            const A native[]={A::CUp,A::CDown,A::CLeft,A::CRight};
            const unsigned bits[]={8,4,2,1};
            for(unsigned c=0;c<4;++c)for(unsigned n=0;n<4;++n) {
                const int cb=dkr::runtime::input::controller_binding(player,camera[c]);
                const int nb=dkr::runtime::input::controller_binding(player,native[n]);
                const int ck=dkr::runtime::input::keyboard_binding(player,camera[c]);
                const int nk=dkr::runtime::input::keyboard_binding(player,native[n]);
                if((cb>=0&&cb==nb)||(ck>=0&&ck==nk))state.buttons&=std::uint16_t(~bits[n]);
            }
            if(state.accelerate&&!(state.driving_flags&1U))state.buttons|=0x8000U;
        }''')
        start=text.index('            if (dkr::runtime::modern_camera::owns_controls(player))')
        end=text.index('            g_buttons[player].store',start)
        text=text[:start]+text[end:]
        one('    dkr::runtime::modern_camera::set_online_routing(enabled);','')
        one('                gameplay_blocked);','                gameplay_blocked,state.accelerator,state.brake,state.driving_flags);')
    elif name=='src/game/runtime_ui.cpp':
        one('#include "modern_camera.hpp"','#include "modern_camera.hpp"\n#include "camera_runtime.hpp"\n#include "look_behind/policy.hpp"\n#include "analog_driving/settings.hpp"')
        one('        output << "modern_camera="','        dkr::runtime::driving::save(output);\n        output << "standalone_look_behind=" << dkr::runtime::look_behind::enabled.load() << \'\\n\';\n        output << "modern_camera="')
        one('            if (key == "modern_camera") {','            if(dkr::runtime::driving::load(key,number))continue;\n            if(key=="standalone_look_behind"){dkr::runtime::look_behind::enabled=number!=0;continue;}\n            if (key == "modern_camera") {')
        one('#include "runtime_graphics_ui.inl"','void DrawCrtOverlaySection(float);\n#include "runtime_graphics_ui.inl"')
        one('    if (HandleInputCaptureEvent(event)) {','''    if(CameraTuningVisible()&&!g_online_code_keyboard_visible.load()&&event->type==SDL_KEYDOWN&&event->key.repeat==0&&
       event->key.keysym.scancode==SDL_SCANCODE_ESCAPE) {
        dkr::runtime::camera::editor_open.store(false);return true;
    }
    if (HandleInputCaptureEvent(event)) {''')
        one('''    if (show_overlay) {
        dkr::runtime::platform::update_ui_gamepad_navigation();
    }''','''    if (show_overlay || show_camera_tuning) {
        dkr::runtime::platform::update_ui_gamepad_navigation();
    }''')
    elif name=='src/game/runtime_camera_tuning_ui.inl':
        return (auth/'camera_editor_ui.inl').read_text()
    elif name=='src/game/runtime_controls_ui.inl':
        one('void DrawControlsPage(float available_width, bool live) {','#include "analog_driving/controls_ui.inl"\nvoid DrawControlsPage(float available_width, bool live) {')
        one('        {"Modern camera", {18, 19, -1, -1, -1, -1}},','        {"Camera", {18, 19, -1, -1, -1, -1}},')
        one('            if (index < 0) continue;','            if (index < 0) continue;\n            if(index==18&&!dkr::runtime::look_behind::enabled.load())continue;')
        one('        DrawControlsBindings(state, width, commit);','        DrawControlsBindings(state, width, commit);\n        DrawAnalogueDrivingControls(width);')
    elif name=='src/game/runtime_code_mods_ui.inl':
        one('card.info=item.kept_online?"Kept from online":"Gameplay mod";',
            'card.info=item.presentation?"Local camera mod":item.kept_online?"Kept from online":"Gameplay mod";')
    elif name=='src/game/runtime_graphics_ui.inl':
        start=text.index('            namespace cam = dkr::runtime::modern_camera;')
        closing='            ImGui::EndDisabled();'
        end=text.index(closing,start)
        text=text[:start]+'''            ImGui::EndDisabled();
            const auto camera_provider=dkr::runtime::camera::activation();
            SettingsHelp("ModernCamera++ is available as an optional mod in Mods/Hacks.",inner);
            bool rear=dkr::runtime::look_behind::enabled.load();
            if(SettingsCheck("standalone-look-behind","Look behind",&rear,inner,
                "Hold your Look back binding while racing. Works without ModernCamera++.")) {
                dkr::runtime::look_behind::enabled=rear;SaveSettings();
            }
            PaddockGap(12.F);
            if(live&&camera_provider&&camera_provider->editor&&SettingsButton("Camera++ settings (F7)",inner)) {
                OpenCameraTuning();g_overlay_visible.store(false,std::memory_order_release);
            }
'''+text[end+len(closing):]
        one('''            bool crt = g_crt_enabled;
            if (SettingsCheck("##crt", "Enable CRT filter", &crt, inner,
                              "Adds a retro screen effect. Choose a filter in Textures.")) {
                g_crt_enabled = crt;
                SaveSettings();
            }''','            DrawCrtOverlaySection(inner);')
    elif name=='src/game/presentation_marker_policy.hpp':
        one('    TrackSelectPreview,','    TrackSelectPreview,\n    CameraWorldBegin, CameraWorldEnd,')
        one('    if (variant > 31U) return false;','    if (variant > 31U) return false;\n    if(kind==PresentationMarkerKind::CameraWorldBegin||kind==PresentationMarkerKind::CameraWorldEnd)return mode==0&&variant==0;')
    elif name=='src/game/presentation_identity.hpp':
        one('#include <span>','#include <span>\n#include <vector>')
        one('struct OwnedMatrixBinding {','std::vector<LocatedPresentationMarker> camera_world_boundaries();\nstruct OwnedMatrixBinding {')
        # This header already uses vector for other presentation helpers.
    elif name=='src/game/presentation_identity.cpp':
        anchor='extern "C" void dkr_presentation_scene_begin'
        insert='''std::vector<dkr::runtime::presentation::LocatedPresentationMarker>
dkr::runtime::presentation::camera_world_boundaries() {
    std::vector<LocatedPresentationMarker> out;
    if(g_active_owned_task)return out;
    for(const auto& [address,list]:g_active_marker_map)for(const auto& marker:list)
        if(marker.kind==PresentationMarkerKind::CameraWorldBegin||marker.kind==PresentationMarkerKind::CameraWorldEnd)
            out.push_back({address,marker});
    return out;
}

'''
        one(anchor,insert+anchor)
    elif name=='src/game/runtime_stubs.cpp':
        for function,kind in [('dkr_split_screen_viewport_fill','CameraWorldBegin'),('dkr_split_screen_world_aspect_end','CameraWorldEnd')]:
            start=text.index('extern "C" void '+function)
            end=text.index('\n#else',start)
            fragment=text[start:end]
            old='''    if (rdram == nullptr || context == nullptr) {
        return;
    }'''
            new=old+'''
    AppendPresentationGroupCommand(rdram,context->r17,PresentationGroupMode::World,
        std::uint16_t(MEM_W(0,RdramAddress(dkr::runtime::revision_addresses::ActiveCameraId))&3U),
        0U,PresentationMarkerKind::'''+kind+''');'''
            text=text[:start]+replace(fragment,old,new)+text[end:]
    elif name=='src/game/netplay/experimental_draw_events.h':
        one('DKR_OWNED_LOCAL_MODEL,DKR_OWNED_LOCAL_OBJECT','DKR_OWNED_LOCAL_MODEL,DKR_OWNED_LOCAL_OBJECT,DKR_OWNED_CAMERA_WORLD_END')
        one('e->kind>DKR_OWNED_LOCAL_OBJECT','e->kind>DKR_OWNED_CAMERA_WORLD_END')
        one('        case DKR_OWNED_ONLINE_HUD:', '        case DKR_OWNED_CAMERA_WORLD_END:\n            if(e->token>3)return 0;break;\n        case DKR_OWNED_ONLINE_HUD:')
    elif name=='tests/replay_probe/probe_native.c':
        one('''    if(strcmp(name,"dkr_split_screen_world_aspect_end")==0) {
        dkr_probe_online_racers''','''    if(strcmp(name,"dkr_split_screen_world_aspect_end")==0) {
        uint32_t camera_addresses[8];dkr_probe_presentation_addresses(camera_addresses);
        draw_marker(rdram,ctx->r17,DKR_OWNED_CAMERA_WORLD_END,
            (uint32_t)MEM_W(0,guest_address(camera_addresses[7]))&3U);
        dkr_probe_online_racers''')
    elif name=='src/game/online_view_rt64.cpp':
        one('e.kind==DKR_OWNED_LOCAL_OBJECT)continue;', 'e.kind==DKR_OWNED_LOCAL_OBJECT || e.kind==DKR_OWNED_CAMERA_WORLD_END)continue;')
        text=online_view_policy(text,replace)
    elif name=='src/game/f3ddkr_rt64.hpp':
        one('namespace online_view {class Pass;}','namespace online_view {class Pass;}\nnamespace camera {class RenderPass;}')
        one('LocalModelRT64Pass* models=nullptr);','LocalModelRT64Pass* models=nullptr,camera::RenderPass* camera=nullptr);')
    elif name=='src/game/f3ddkr_rt64.cpp':
        one('#include "f3ddkr_rt64.hpp"','#include "f3ddkr_rt64.hpp"\n#include "camera_rt64.hpp"')
        one('constexpr std::uint32_t kRDRAMSize = 0x00800000U;',
            'thread_local std::uint32_t kRDRAMSize = 0x00800000U; // widened only inside an authorized private camera decode')
        one('struct dkr::runtime::F3DDKRRT64Bridge::StateData {','struct dkr::runtime::F3DDKRRT64Bridge::StateData {\n    camera::RenderPass* camera=nullptr;\n    std::array<bool,3> camera_matrices{};')
        one('LocalModelRT64Pass* models) {','LocalModelRT64Pass* models,camera::RenderPass* camera) {')
        one('    active_ = this;','''    struct CameraDecodeScope {
        camera::RenderPass* pass;
        ~CameraDecodeScope(){if(pass)pass->finish();kRDRAMSize=0x800000U;}
    } camera_scope{camera};
    kRDRAMSize=camera&&camera->enabled()?0x1000000U:0x800000U;
    active_ = this;''')
        one('    fresh.local_scenery=scenery;','    fresh.camera=camera;\n    fresh.local_scenery=scenery;')
        one('    if (data.task_rejected) return false;','''    if (data.task_rejected) return false;
    if(data.camera) {
        const auto base=reinterpret_cast<std::uintptr_t>(state->RDRAM),p=reinterpret_cast<std::uintptr_t>(command);
        if(p>=base&&p-base<=kRDRAMSize-8)data.camera->before_command(std::uint32_t(p-base));
    }''')
        one('        const auto& marker = markers.markers[index];', '''        const auto& marker = markers.markers[index];
        if(marker.kind==PresentationMarkerKind::CameraWorldBegin||marker.kind==PresentationMarkerKind::CameraWorldEnd)continue;
        if(active_->data_->camera&&active_->data_->camera->replaced_command(std::uint32_t(command-rdram_begin))&&
           marker.kind!=PresentationMarkerKind::SplitViewport&&marker.kind!=PresentationMarkerKind::HudPass&&
           marker.kind!=PresentationMarkerKind::HudWidget&&marker.kind!=PresentationMarkerKind::HudRect)continue;''')
        one('    data.interpolation_groups.load_matrix(\n        index, matrix_group.identity,',
            '''    data.camera_matrices[index]=data.camera&&data.camera->matrix(address,matrix_group);
    data.interpolation_groups.load_matrix(
        index, matrix_group.identity,''')
        for function,end_function,index in [('Matrix','FillRect','index'),('MoveWord','SetTextureImage','data.selected_matrix')]:
            start=text.index('void dkr::runtime::F3DDKRRT64Bridge::'+function+'(')
            end=text.index('void dkr::runtime::F3DDKRRT64Bridge::'+end_function+'(',start)
            fragment=text[start:end]
            fragment=replace(fragment,'ActiveAspectMode(',f'data.camera_matrices[{index}]?G_EX_ASPECT_ADJUST:ActiveAspectMode(')
            fragment=replace(fragment,'active_group.procedural_water ? active_group.water_scroll_tag : 0U);',
                f'active_group.procedural_water ? active_group.water_scroll_tag : 0U,data.camera_matrices[{index}]);')
            text=text[:start]+fragment+text[end:]
    elif name=='src/game/rt64_renderer.hpp':
        one('struct OwnedDecodeScratch;','struct OwnedDecodeScratch;\nnamespace camera {class RenderPass;}')
        one('    F3DDKRRT64Bridge f3ddkr_;','    F3DDKRRT64Bridge f3ddkr_;\n    std::unique_ptr<camera::RenderPass> camera_pass_;\n    std::vector<std::uint8_t> camera_decoder_;')
        one('#include <deque>','#include <deque>\n#include <vector>')
    elif name=='src/game/rt64_renderer.cpp':
        one('#include "rt64_renderer.hpp"','#include "rt64_renderer.hpp"\n#include "camera_rt64.hpp"\n#include "revision_addresses.hpp"\n#include "netplay/online_input_broker.hpp"')
        one('''    RendererSnapshotScope snapshot_scope(application_->core.RDRAM,
                                         application_->state->RDRAM,
                                         rdram_snapshot);''','''    std::uint8_t* decode_bytes=rdram_snapshot;
    const bool camera_requested=bool(camera::activation());
    if(camera_requested) {
        if(camera_decoder_.size()!=DKR_CAMERA_KERNEL_BYTES)camera_decoder_.resize(DKR_CAMERA_KERNEL_BYTES);
        std::memcpy(camera_decoder_.data(),rdram_snapshot,0x800000);
        decode_bytes=camera_decoder_.data();
    }
    RendererSnapshotScope snapshot_scope(application_->core.RDRAM,
                                         application_->state->RDRAM,
                                         decode_bytes);''')
        one('''        rdram_snapshot, task->t.data_ptr);
    // DKR authors''','''        rdram_snapshot, task->t.data_ptr);
    if(!camera_pass_)camera_pass_=std::make_unique<camera::RenderPass>();
    camera::FrameStamp camera_frame{};
    camera_frame.scene=presentation::task_scene_generation();
    camera_frame.time=float(presentation::task_presentation_time());
    camera_frame.frame=std::uint32_t(std::max(0.F,camera_frame.time)*30.F);
    camera_frame.dt=1.F/30.F;
    int camera_width=0,camera_height=0;
    if(auto* w=static_cast<SDL_Window*>(platform::sdl_window()))SDL_GetWindowSize(w,&camera_width,&camera_height);
    if(application_->userConfig.aspectRatio==RT64::UserConfiguration::AspectRatio::Expand&&camera_width>0&&camera_height>0)
        camera_frame.aspect=float(camera_width)/camera_height;
    else if(application_->userConfig.aspectRatio==RT64::UserConfiguration::AspectRatio::Manual)
        camera_frame.aspect=float(application_->userConfig.aspectTarget);
    const auto camera_online=netplay::online_input_broker().view();
    camera_frame.online=camera_online.enabled;
    if(camera_requested)camera_pass_->prepare({rdram_snapshot,0x800000},camera_decoder_,camera_frame,{},
        camera_online.local_slot,unsigned(platform::online_input_profile()));
    // DKR authors''')
        one('f3ddkr_.process(*application_, *task);','f3ddkr_.process(*application_, *task,nullptr,nullptr,nullptr,camera_requested?camera_pass_.get():nullptr);',3)
        anchor='''        application_->state->setRefreshRate(30);
        {netplay::experimental::performance::Scope timing'''
        one(anchor,'''        if(!camera_pass_)camera_pass_=std::make_unique<camera::RenderPass>();
        camera::FrameStamp camera_frame{};camera_frame.scene=scene;
        camera_frame.epoch=std::uint32_t(lease->epoch());camera_frame.frame=d.frame;
        camera_frame.time=float(d.frame)/30.F;camera_frame.dt=1.F/30.F;
        camera_frame.aspect=cover*4.F/3.F;camera_frame.online=true;
        camera_pass_->prepare(lease->bytes(),workspace->bytes(),camera_frame,lease->draw_events(),
            online_view_session_.owner,unsigned(platform::online_input_profile()),owned_decode_scratch_->online.focused());
        application_->state->setRefreshRate(30);
        {netplay::experimental::performance::Scope timing''')
        one('''                 ,&owned_decode_scratch_->local_models
#endif
             );''','''                 ,&owned_decode_scratch_->local_models
#else
                 ,nullptr
#endif
                 ,camera_pass_.get());''')
    else:
        raise ValueError('Unknown integration target '+name)
    return text
