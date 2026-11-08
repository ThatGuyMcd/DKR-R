"""Compose bounded setup and owned-menu repairs above the accepted v1.0.11 chain."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.11-online-playtest.2'
NAMES={('DKRExperimentalOwnedGameV77','probe_native.c'),('DKRExperimentalOwnedGameV80','probe_native.c'),
 ('DKRUpdateInstall','update_process.cpp'),('DKRUpdateClient','update_service.cpp'),('DKRPortGame','game_main.cpp'),('DKRPortGame','party_runtime.cpp'),
 ('DKRNetplayCore','direct_session.cpp'),('DKRNetplayCore','quick_join_transport.cpp'),('DKRNetplayCore','experimental_rollback.cpp'),
 ('DKRNetplayCore','experimental_lobby_admission.cpp'),('DKRNetplayCore','experimental_network.cpp'),('DKRPortGame','experimental_runtime.cpp'),
 ('DKRNetplayCore','online_mod_admission.cpp'),
 ('DKRLegacyModCore','online_mod_cache.cpp'),('DKRLegacyImportLibrary','online_mod_sync.cpp'),
 ('DKRLegacyModCore','online_mod_transfer.cpp'),('DKRLegacyImportLibrary','online_mod_profile.cpp'),
 ('DKRLegacyModLaunch','online_mod_runtime.cpp'),('DKRLegacyModWorker','legacy_mod_worker.cpp')}
def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Online performance anchor differs: '+old[:140])
    return text.replace(old,new,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def function(text,marker):
    start=text.index(marker);brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

def beta3(name,text):
    """Extend the existing checked test-build composition; no new layer.

    Source modules live in runtime-recomp. These adapters also reach the
    frozen capsule consumers until the approved source fold is complete.
    """
    game=ROOT/'runtime-recomp/src/game'
    include=lambda n:'#include "'+(game/n).as_posix()+'"\n'
    # These first-party definitions already contain the repair when their
    # direct source is compiled. Retained capsule aliases still need the same
    # declaration/body below, through this existing compatibility boundary.
    repaired={
        'game_payload.hpp':game/'game_payload.hpp',
        'game_payload_v77.cpp':game/'game_payload_v77.cpp',
        'game_payload_v80.cpp':game/'game_payload_v80.cpp',
        'legacy_character_menu.hpp':game/'mods/legacy_character_menu.hpp',
        'legacy_character_menu_render.hpp':game/'mods/legacy_character_menu_render.hpp',
        'legacy_character_stage.hpp':game/'mods/legacy_character_stage.hpp',
        'legacy_character_menu_render.cpp':game/'mods/legacy_character_menu_render.cpp',
        'legacy_character_stage.cpp':game/'mods/legacy_character_stage.cpp',
        'runtime_legacy_mods.cpp':game/'runtime_legacy_mods.cpp',
        'update_process.hpp':game/'updates/update_process.hpp',
    }
    if name in repaired and text==repaired[name].read_text():return text
    if name=='game_payload.hpp':
        text=once(text,'    RecompiledEntrypoint menu_render_reset;',
            '    RecompiledEntrypoint menu_render_reset;\n    RecompiledEntrypoint menu_text_width;\n    RecompiledEntrypoint menu_view_matrix;\n    RecompiledEntrypoint menu_projection;')
    elif name in ('game_payload_v77.cpp','game_payload_v80.cpp'):
        text=once(text,'        .menu_render_reset = rendermode_reset,',
            '        .menu_render_reset = rendermode_reset,\n        .menu_text_width = get_text_width,\n        .menu_view_matrix = get_camera_matrix,\n        .menu_projection = get_projection_matrix_s16,')
    elif name=='legacy_character_menu.hpp':
        text=include('mods/legacy_character_label_layout.hpp')+text
        text=once(text,'    std::array<unsigned,4> native{};',
            '    std::array<std::optional<CharacterLabelWorld>,CustomStageSlots> label_anchors{};\n    std::array<unsigned,4> native{};')
    elif name=='legacy_character_menu_render.hpp':
        text=once(text,'    OriginalPiHandler texture=nullptr,font=nullptr,colour=nullptr,background=nullptr,text=nullptr,reset=nullptr;',
            '    OriginalPiHandler texture=nullptr,font=nullptr,colour=nullptr,background=nullptr,text=nullptr,reset=nullptr;\n    OriginalPiHandler text_width=nullptr,view_matrix=nullptr,projection=nullptr;')
    elif name=='legacy_character_stage.hpp':
        text=once(text,'    bool redirect_spawn(std::span<std::uint8_t>,recomp_context&);',
            '    bool redirect_spawn(std::span<std::uint8_t>,recomp_context&);\n    void label_anchors(std::span<std::uint8_t>,CharacterMenuView&)const;')
    elif name=='legacy_character_menu_render.cpp':
        marker='void CharacterMenuRenderer::draw_hint('
        text=once(text,function(text,marker),function((game/'mods'/name).read_text(),marker))
    elif name=='legacy_character_stage.cpp':
        marker='bool CharacterStage::redirect_spawn('
        text=once(text,marker,function((game/'mods'/name).read_text(),'void CharacterStage::label_anchors(')+'\n'+marker)
    elif name=='runtime_legacy_mods.cpp':
        text=once(text,'p->menu_texture_draw,p->menu_font,p->menu_colour,p->menu_background,p->menu_text,p->menu_render_reset};',
            'p->menu_texture_draw,p->menu_font,p->menu_colour,p->menu_background,p->menu_text,p->menu_render_reset,\n                p->menu_text_width,p->menu_view_matrix,p->menu_projection};\n            state->stage.label_anchors({rdram,legacy::guest_memory_size()},view);')
    elif name=='update_process.hpp':
        text=once(text,'bool inherit_update_environment = false);','bool inherit_update_environment = false, bool system_desktop_libraries = false);')
        text=once(text,'    int wait(unsigned timeout_ms);','    int wait(unsigned timeout_ms);\n    void terminate();')
    elif name=='update_process.cpp':
        current=(game/'updates/update_process.cpp').read_text()
        if 'void Child::terminate(' not in text:
            text=once(text,function(text,'Child::Child('),function(current,'Child::Child('))
            text=once(text,'bool parent_alive(',function(current,'void Child::terminate(')+'\n'+'bool parent_alive(')
    elif name=='legacy_mod_launch.hpp':
        text=include('library_ui/startup_progress.hpp')+text
        text=once(text,'struct PreparedModLaunch {','struct PreparedModLaunch {\n    bool content_enabled=true;')
        text=once(text,'using LaunchProgress=std::function<void(const char*)>;',
            'using LaunchProgress=std::function<void(const char*)>;\nusing DetailedLaunchProgress=std::function<void(const char*,dkr::library_ui::StartupProgress)>;')
        text=once(text,'const LaunchProgress& progress={},std::stop_token stop={});',
            'const LaunchProgress& progress={},std::stop_token stop={},const DetailedLaunchProgress& detailed={});')
        text=once(text,'struct ModLaunchView {','struct ModLaunchView {\n    dkr::library_ui::StartupProgress progress;')
    elif name=='legacy_mod_launch.cpp':
        text=include('session_content/policy.hpp')+text
        text=once(text,'const LaunchProgress& progress,std::stop_token stop) {',
            'const LaunchProgress& progress,std::stop_token stop,const DetailedLaunchProgress& detailed) {')
        text=once(text,'auto report=[&](const char* message){if(stop.stop_requested())throw Error("Game launch cancelled.");if(progress)progress(message);};',
            'auto report=[&](const char* message,std::size_t completed=0,std::size_t total=0){if(stop.stop_requested())throw Error("Game launch cancelled.");if(detailed)detailed(message,{completed,total});if(progress)progress(message);};')
        text=once(text,'    for(auto& track:tracks) {\n        report("Preparing custom Track Select previews");',
            '    std::size_t preview_index=0;\n    for(auto& track:tracks) {\n        report("Preparing custom Track Select previews",preview_index,tracks.size());')
        text=once(text,'        session->admit(std::move(track));',
            '        session->admit(std::move(track));\n        report("Preparing custom Track Select previews",++preview_index,tracks.size());')
        text=once(text,'prepare_mod_launch(config,rom,online,[&](const char* stage){std::lock_guard lock(mutex_);state_.stage=stage;},stop);',
            'prepare_mod_launch(config,rom,online,{},stop,[&](const char* stage,dkr::library_ui::StartupProgress value){std::lock_guard lock(mutex_);state_.stage=stage;state_.progress=value;});')
        text=once(text,'if(state_.modal && !stop.stop_requested())state_.prepared=std::move(prepared);',
            'if(state_.modal && !stop.stop_requested()){state_.prepared=std::move(prepared);state_.progress={1,1};}')
        text=once(text,'    auto out=std::make_shared<PreparedModLaunch>();',
            '    auto out=std::make_shared<PreparedModLaunch>();\n    out->content_enabled=dkr::session_content::preparing();\n    if(!online&&!out->content_enabled)return out;')
        text=once(text,'[this,config=std::move(config),rom=std::move(rom),online](std::stop_token stop) {',
            '[this,config=std::move(config),rom=std::move(rom),online,enabled=dkr::session_content::next_enabled.load()](std::stop_token stop) {\n            dkr::session_content::Preparation selection(enabled);')
    elif name=='game_main.cpp':
        text=include('crash_reports/native_context.hpp')+text
        current=(game/name).read_text()
        for marker in ('LONG WINAPI RuntimeCrashFilter(','void RuntimeSignalHandler(','void InstallRuntimeSignalHandlers('):
            text=once(text,function(text,marker),function(current,marker))
        if 'dkr::crash_reports::refresh_native_context(true);' not in text:
            text=once(text,'                 DKR_RELEASE_VERSION);\n    return true;',
                '                 DKR_RELEASE_VERSION);\n    dkr::crash_reports::refresh_native_context(true);\n    return true;')
        text=include('crash_reports/reporting.hpp')+text
        text=once(text,'    dkr::runtime::startup_performance::mark("persistent-log-ready");',
            '    dkr::runtime::startup_performance::mark("persistent-log-ready");\n    dkr::crash_reports::start(config_directory,DKR_RELEASE_VERSION);')
        text=include('session_content/policy.hpp')+text
        text=once(text,'        std::optional<std::uint32_t> online_magic_codes;',
            '        const bool online_content=dkr::runtime::netplay::session().active();\n        const bool content_enabled=online_content&&dkr::runtime::netplay::session().view().host?dkr::session_content::host_enabled.load():prepared_mods?prepared_mods->content_enabled:dkr::session_content::next_enabled.load();\n        dkr::crash_reports::Gameplay quiet_reports;\n        dkr::session_content::Session content_session(content_enabled,online_content);\n        dkr::session_content::Preparation content_preparation(content_enabled);\n        std::fprintf(stderr,"[mods][session] enabled=%u online=%u; selections preserved\\n",unsigned(content_enabled),unsigned(online_content));\n        std::optional<std::uint32_t> online_magic_codes;')
        text=text.replace('const auto startup = dkr::runtime::ui::run_startup_screen(',
            'dkr::session_content::end();\n        const auto startup = dkr::runtime::ui::run_startup_screen(')
        text=once(text,'            if(prepared_mods->session && dkr::runtime::netplay::session().active())',
            '            bool modified=!prepared_mods->fingerprint.empty()||dkr::runtime::magic_codes::launch_mask()!=0||dkr::runtime::custom_tracks::enabled_count()!=0;\n            for(const auto& pack:dkr::runtime::texture_packs::snapshot())modified|=pack.enabled&&pack.compatible;\n            dkr::session_content::running_modded=content_enabled&&modified;\n            if(prepared_mods->session && dkr::runtime::netplay::session().active())')
    elif name=='runtime_magic_codes.cpp':
        text=include('session_content/policy.hpp')+text
        text=once(text,'online_mask.value_or(selected_mask())','online_mask.value_or(dkr::session_content::running_enabled.load()?selected_mask():0U)')
    elif name=='runtime_texture_packs.cpp':
        text=include('session_content/policy.hpp')+text
        text=once(text,'void apply_pending(RT64::Application& application, bool modern_profile) {',
            'void apply_pending(RT64::Application& application, bool modern_profile) {\n    modern_profile=modern_profile&&dkr::session_content::textures_allowed();')
    elif name=='custom_tracks.cpp':
        text=include('session_content/policy.hpp')+text
        for marker,result in [('std::string armed_track_id() {','{}'),('bool auto_boot_enabled() {','false'),
                ('bool consume_auto_boot() {','false'),('std::int32_t track_override() {','kNoTrackOverride'),
                ('bool owns_level_id(std::int32_t level_id) {','false')]:
            text=once(text,marker,marker+'\n    if(!dkr::session_content::local_assets_allowed())return '+result+';')
        marker='    return assemble_table(section,retail_table,g_tracks,g_sections,g_resolved_level_ids,table_words);'
        text=once(text,marker,'    if(!dkr::session_content::local_assets_allowed()) {\n        const std::vector<Track> vanilla;\n        return assemble_table(section,retail_table,vanilla,g_sections,g_resolved_level_ids,table_words);\n    }\n'+marker)
        text=once(text,'    const SectionState& state = g_sections[section_slot(section)];',
            '    if(!dkr::session_content::local_assets_allowed())return nullptr;\n    const SectionState& state = g_sections[section_slot(section)];')
        text=once(text,'    const SectionState& source = g_sections[section_slot(from)];',
            '    if(!dkr::session_content::local_assets_allowed())return -1;\n    const SectionState& source = g_sections[section_slot(from)];')
    elif name=='rt64_renderer.cpp':
        text=include('crash_reports/native_context.hpp')+text
        if '[boot][graphics] requested-api=' not in text:
            text=once(text,'    dkr::runtime::texture_packs::renderer_started();',
                function((game/name).read_text(),'if(application_->device)')+'\n    dkr::runtime::texture_packs::renderer_started();')
        if 'dkr::crash_reports::refresh_native_context();' not in text:
            text=once(text,'    dkr::runtime::startup_performance::report("renderer-ready",',
                '    dkr::crash_reports::refresh_native_context();\n    dkr::runtime::startup_performance::report("renderer-ready",')
        text='#include <tuple>\n'+include('display_settings/policy.hpp')+text
        text=once(text,'void ApplyConfig(RT64::Application& application,',include('display_settings/renderer.inl')+'\nvoid ApplyConfig(RT64::Application& application,')
        text=once(text,'    application.userConfig.downsampleMultiplier = std::max(config.ds_option, 1);',
            '    application.userConfig.downsampleMultiplier = std::max(config.ds_option, 1);\n    ApplyDisplayRequest(application,false);')
        text=once(text,'    const bool resolution_or_aspect_changed =','    bool resolution_or_aspect_changed =')
        text=once(text,'    ApplyConfig(*application_, new_config);',
            '    const auto previous_display=std::tuple{application_->userConfig.aspectTarget,application_->userConfig.resolutionMultiplier,application_->userConfig.downsampleMultiplier};\n    ApplyConfig(*application_, new_config);\n    resolution_or_aspect_changed|=previous_display!=std::tuple{application_->userConfig.aspectTarget,application_->userConfig.resolutionMultiplier,application_->userConfig.downsampleMultiplier};')
        text=once(text,'    dkr::runtime::telemetry::record_graphics_task();','    ApplyDisplayRequest(*application_,true);\n    dkr::runtime::telemetry::record_graphics_task();')
        text=once(text,'    const bool keep_awake = render_power::gpu_keep_awake.load();','    ApplyDisplayRequest(*application_,true);\n    const bool keep_awake = render_power::gpu_keep_awake.load();')
        text=once(text,'    reap_owned();\n    // The render loop leaves', '    ApplyDisplayRequest(*application_,true);\n    reap_owned();\n    // The render loop leaves')
        start=text.index('    const bool expanded=enhancements::modern_presentation_enabled() &&')
        end=text.index('    const auto read_vertex=',start)
        text=text[:start]+'''    int width=0,height=0;
    if(auto* window=static_cast<SDL_Window*>(platform::sdl_window()))SDL_GetWindowSize(window,&width,&height);
    const float local_aspect=modern?dkr::display::view_aspect(std::max(1,width),std::max(1,height)):4.F/3.F;
    const bool expanded=modern&&std::abs(local_aspect-4.F/3.F)>0.00001F;
    const float cover=expanded?(std::max)(1.F,local_aspect/(4.F/3.F)):1.F;
'''+text[end:]
        text=text.replace('cover*4.F/3.F','local_aspect').replace('cover*4.0F/3.0F','local_aspect')
        text=text.replace('hud::decode::Scope hud_decode(hud_mode,cover*4.F/3.F', 'hud::decode::Scope hud_decode(hud_mode,local_aspect')
    elif name=='runtime_hud_layout.cpp':
        text=include('display_settings/policy.hpp')+text
        text=once(text,'static_cast<float>(state.viewport_width) / state.viewport_height : 4.0F/3.0F;',
            'dkr::display::view_aspect(state.viewport_width,state.viewport_height) : 4.0F/3.0F;')
    elif name=='runtime_enhancements.cpp':
        text=include('display_settings/policy.hpp')+text
        text=once(text,'static_cast<float>(width) / static_cast<float>(height), layout,',
            'dkr::display::view_aspect(width,height), layout,')
    elif name=='owner_pump.inl':
        text=once(text,'a.watching?party->enabled&&spectators+pending_spectators<8:',
            'a.watching?party->enabled&&party->allow_spectators&&spectators+pending_spectators<8:')
        text=once(text,'{"maxSpectators",party->enabled?8:0}',
            '{"maxSpectators",party->enabled?(party->allow_spectators?8:spectators):0}')
        text=once(text,'{"canSpectateRunning",party->enabled&&online->running_spectators}',
            '{"canSpectateRunning",party->enabled&&party->allow_spectators&&online->running_spectators}')
    elif name=='netplay_types.hpp':
        if 'struct PackedInput {' not in text:return text
        start=text.index('struct PackedInput {');end=text.index('using FrameInputs',start)
        text=text[:start]+text[end:]
        text=include('analog_driving/packed_input.hpp')+text
        text=re.sub(r'kProtocolVersion = \d+U;', 'kProtocolVersion = 52U;',text,count=1)
    elif name=='online_input_broker.hpp':
        if 'std::uint8_t accelerator=' in text:return text
        text=once(text,'float stick_x, float stick_y, bool blocked);',
            'float stick_x, float stick_y, bool blocked, std::uint8_t accelerator=0,std::uint8_t brake=0,std::uint8_t driving_flags=0);')
    elif name=='online_input_broker.cpp':
        text=once(text,'float stick_y, bool blocked) {','float stick_y, bool blocked,std::uint8_t accelerator,std::uint8_t brake,std::uint8_t driving_flags) {')
        text=once(text,'pack_input_axis(stick_y)};', 'pack_input_axis(stick_y),accelerator,brake,driving_flags};\n    if(blocked||!valid_input(local_.input))local_.input={};')
    elif name=='runtime_input.hpp':
        text=once(text,'    CameraFirstPerson, CameraXrRecenter,\n    Count,','    CameraFirstPerson, CameraXrRecenter,\n    AnalogueAccelerator, AnalogueBrake,\n    Count,')
        text=once(text,'    std::uint32_t camera_actions=0;','    std::uint32_t camera_actions=0;\n    std::uint8_t accelerator=0,brake=0,driving_flags=0;')
    elif name=='runtime_input.cpp':
        text=include('analog_driving/poll.hpp')+'#if DKR_RUNTIME_HAS_RT64\n'+include('analog_driving/raw_axes.hpp')+'#endif\nnamespace driving=dkr::runtime::driving;\n'+text
        text=once(text,'    {SDL_SCANCODE_HOME,dkr::runtime::input::kUnbound},',
            '    {SDL_SCANCODE_HOME,dkr::runtime::input::kUnbound},\n    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_TRIGGERRIGHT*2+1},\n    {dkr::runtime::input::kUnbound,kAxisSourceBase+SDL_CONTROLLER_AXIS_TRIGGERLEFT*2+1},')
        text=once(text,'"camera_zoom_in","camera_zoom_out","camera_first_person","camera_xr_recenter",',
            '"camera_zoom_in","camera_zoom_out","camera_first_person","camera_xr_recenter",\n    "analogue_accelerator","analogue_brake",')
        text=once(text,'    "Camera zoom in","Camera zoom out","First-person camera","Centre headset",',
            '    "Camera zoom in","Camera zoom out","First-person camera","Centre headset",\n    "Analogue accelerator","Analogue brake",')
        block='''    if (blocked) {
        // Keep sampling Controller 1 while the overlay is open so calibration
        // and both live preview bars remain truthful. Gameplay receives a
        // neutral sample until the overlay closes.
        return state;
    }
'''
        text=once(text,block,'');text=once(text,'    if (blocked) return state;\n','')
        pattern=r'(    const auto press = \[&\]\(Action action, std::uint16_t mask,\s*float threshold = 0.5F\) \{)'
        text,count=re.subn(pattern,lambda m:include('analog_driving/poll.inl')+m[1]+'''
        if(analogue_enabled&&(action==Action::A||action==Action::B)){if(action==Action::A?pedals.accelerate:pedals.braking)state.buttons|=mask;return;}
        if(analogue_enabled&&action==Action::Z){if(raw_value(action,true)>threshold)state.buttons|=mask;return;}''',text)
        if count!=2:raise ValueError('Both SDL input owners must consume analogue controls')
    elif name=='runtime_platform.cpp':
        text=include('look_behind/policy.hpp')+include('analog_driving/input.hpp')+text
        text=once(text,'{state.camera_actions,state.orbit_x,state.orbit_y,0,\n                gameplay_blocked||!dkr::runtime::camera::owns_controls(unsigned(player)),true});',
            '{dkr::runtime::camera::owns_controls(unsigned(player))?state.camera_actions:(state.camera_actions&1U),state.orbit_x,state.orbit_y,0,\n                gameplay_blocked||(!dkr::runtime::camera::owns_controls(unsigned(player))&&!dkr::runtime::look_behind::enabled.load()),true});\n        dkr::runtime::driving::publish(unsigned(player),{state.accelerator,state.brake,state.driving_flags});')
        text=once(text,'if(state.accelerate)state.buttons|=0x8000U;', 'if(state.accelerate&&!(state.driving_flags&1U))state.buttons|=0x8000U;')
        text=once(text,'                gameplay_blocked);','                gameplay_blocked,state.accelerator,state.brake,state.driving_flags);')
        # Consumers need all canonical channels; the original public 3-value
        # helper remains ABI-compatible for its other callers.
        text=once(text,'        ClearPublishedControllerInput(false);',
            '        for(unsigned p=0;p<4;++p)dkr::runtime::driving::publish(p,{});\n        ClearPublishedControllerInput(false);')
        marker='dkr::runtime::platform::player_input_preview(std::size_t player)'
        original=function(text,marker);preview=original
        preview=once(preview,'        if (device != nullptr) {',
            '        (void)dkr::runtime::input::poll_snapshot(player,device?&device->snapshot:nullptr,nullptr,\n            dkr::runtime::input::keyboard_player()==int(player),true,false,false);\n        if (device != nullptr) {')
        preview=once(preview,'    SDL_GameController* controller = g_controllers[player];',
            '    SDL_GameController* controller = g_controllers[player];\n    (void)dkr::runtime::input::poll(player,controller,nullptr,\n        dkr::runtime::input::keyboard_player()==int(player),true,false,false);')
        text=once(text,original,preview)
    elif name=='runtime_ui.cpp':
        text=include('file_picker/client.hpp')+text
        text=include('display_settings/policy.hpp')+include('session_content/policy.hpp')+include('analog_driving/settings.hpp')+include('look_behind/policy.hpp')+text
        text=once(text,'        output << "modern_resolution="', '        dkr::display::save(output);\n        output << "session_mods=" << unsigned(dkr::session_content::next_enabled.load()) << \'\\n\';\n        output << "modern_resolution="')
        text=once(text,'            const int number = std::stoi(value);','            const int number = std::stoi(value);\n            if(dkr::display::load(key,number))continue;\n            if(key=="session_mods"){dkr::session_content::next_enabled=number!=0;continue;}')
        if 'migrated_rollback_backend' in text:
            text=once(text,'    if (migrated_rollback_backend) SaveSettings();','    migrated |= migrated_rollback_backend; // Persist only after every setting owner has loaded.')
        text=once(text,'    profile = resolved_profile;\n    dkr::runtime::enhancements::set_presentation_profile(profile);','    if(!dkr::display::loaded)migrated=true;\n    dkr::display::migrate(int(g_modern_resolution),int(g_modern_aspect),g_modern_downsample);\n    profile = resolved_profile;\n    dkr::runtime::enhancements::set_presentation_profile(profile);')
        text=once(text,'    std::string room,name;','    std::string room,name;\n    bool content_enabled=dkr::session_content::next_enabled.load();')
        original=function(text,'bool FinishOnlineLobby(')
        updated=once(original,'    auto manifest=request.manifest;manifest.mod_manifest_hash=mod_digest;',
            '    auto manifest=request.manifest;\n    if(!request.content_enabled) {\n        if(!mod_digest.empty()){g_online_action_status="Vanilla lobby cannot admit a mod profile.";return false;}\n        manifest.magic_codes_hash=0;manifest.gameplay_settings_hash=stable_hash("0:retail-simulation-30");\n    }\n    manifest.mod_manifest_hash=mod_digest;')
        text=once(text,original,once(updated,'    return true;','    dkr::session_content::host_enabled=request.content_enabled;\n    return true;'))
        text=once(text,'        output << "modern_camera="','        dkr::runtime::driving::save(output);\n        output << "standalone_look_behind=" << dkr::runtime::look_behind::enabled.load() << \'\\n\';\n        output << "modern_camera="')
        text=once(text,'            if (key == "modern_camera") {','            if(dkr::runtime::driving::load(key,number))continue;\n            if(key=="standalone_look_behind"){dkr::runtime::look_behind::enabled=number!=0;continue;}\n            if (key == "modern_camera") {')
        text,count=re.subn(r'(#include "[^"\n]*(?:runtime_graphics_ui|graphics)[.]inl")',r'void DrawCrtOverlaySection(float);\n\1',text,count=1)
        if count!=1:raise ValueError('The selected Graphics owner must declare the shared CRT controls')
    elif name=='runtime_controls_ui.inl':
        text=once(text,'void DrawControlsPage(float available_width, bool live) {',include('analog_driving/controls_ui.inl')+'void DrawControlsPage(float available_width, bool live) {')
        text=once(text,'        {"Modern camera", {18, 19, -1, -1, -1, -1}},','        {"Camera", {18, 19, -1, -1, -1, -1}},')
        text=once(text,'            if (index < 0) continue;', '            if (index < 0) continue;\n            if(index==18&&!dkr::runtime::look_behind::enabled.load())continue;')
        text=once(text,'        DrawControlsBindings(state, width, commit);','        DrawControlsBindings(state, width, commit);\n        DrawAnalogueDrivingControls(width);')
    elif name=='runtime_online_mods_ui.inl':
        text=once(text,'    OnlineHostRequest request;', '    OnlineHostRequest request;\n    request.content_enabled=dkr::session_content::next_enabled.load();')
        text=once(text,'    std::vector<std::filesystem::path> labs;',
            '    if(!request.content_enabled){\n        request.manifest.magic_codes_hash=0;\n        request.manifest.gameplay_settings_hash=stable_hash("0:retail-simulation-30");\n        g_online_mod_notice.clear();\n        if(g_online_mod_sync)g_online_mod_sync->cancel();\n        return FinishOnlineLobby(request,{},0);\n    }\n    std::vector<std::filesystem::path> labs;')
    elif name in ('runtime_graphics_ui.inl','graphics.inl'):
        text=include('display_settings/ui.inl')+text
        text=once(text,'    bool changed = false;\n    const bool owned_camera_settings_deferred',
            '    bool changed = false;\n    if(!live){const auto& io=ImGui::GetIO();dkr::display::output_width=unsigned(std::max(1.F,io.DisplaySize.x*io.DisplayFramebufferScale.x));dkr::display::output_height=unsigned(std::max(1.F,io.DisplaySize.y*io.DisplayFramebufferScale.y));}\n    const bool owned_camera_settings_deferred')
        start=text.index('            changed |= SettingsDropdown("resolution", "Resolution", &resolution,')
        end=text.index('            fields.End();',text.index('SettingsDropdown("aspect",',start))+len('            fields.End();')
        text=text[:start]+'''            if(DrawDisplayChoices(fields.column,true)){aspect=dkr::display::preference.aspect==dkr::display::Aspect::Original?0:1;changed=true;}
            fields.End();
            fields.Begin();
            if(DrawDisplayChoices(fields.column,false)){resolution=2;downsample=1;changed=true;}
            fields.End();'''+text[end:]
        start=text.index('        renderer.Begin();\n        changed |= SettingsRange("##downsample"')
        end=text.index('        renderer.End();',start)+len('        renderer.End();')
        text=text[:start]+text[end:]
        if 'graphics_presets::apply_preset(config, chosen);' in text:
            text=once(text,'            graphics_presets::apply_preset(config, chosen);',
                '            graphics_presets::apply_preset(config, chosen);\n            dkr::display::loaded=false;dkr::display::migrate(int(config.res_option),int(config.ar_option),config.ds_option);')
            text=once(text,'const auto applied = enh::modern_presentation_enabled()',
                'const auto applied = enh::modern_presentation_enabled()&&dkr::display::legacy_matches(int(config.res_option),int(config.ar_option),config.ds_option)')
        else:
            text=once(text,'                static_cast<dkr::runtime::mobile_graphics::Preset>(preset));',
                '                static_cast<dkr::runtime::mobile_graphics::Preset>(preset));\n            dkr::display::loaded=false;dkr::display::migrate(int(config.res_option),int(config.ar_option),config.ds_option);')
        anchor='            SettingsHelp("ModernCamera++ is available as an optional mod in Mods/Hacks.",inner);'
        text=once(text,anchor,anchor+'''
            bool rear=dkr::runtime::look_behind::enabled.load();
            if(SettingsCheck("standalone-look-behind","Look behind",&rear,inner,
                "Hold your Look back binding while racing. Works offline and online, without ModernCamera++.")) {
                dkr::runtime::look_behind::enabled=rear;SaveSettings();
            }
            PaddockGap(12.F);''')
        text=once(text,'live&&camera_provider&&SettingsButton(', 'live&&camera_provider&&camera_provider->editor&&SettingsButton(')
        start=text.index('            bool crt = g_crt_enabled;');end=text.index('\n            }',start)+14
        text=text[:start]+'            DrawCrtOverlaySection(inner);'+text[end:]
    elif name=='camera_runtime.cpp':
        text=once(text,'            if(!development_tools)return;','            (void)development_tools; // Standalone local rear view is always available.')
        text=once(text,'}catch(const std::exception& e){report(e.what());}', '''}catch(const std::exception& e){
        auto next=std::make_shared<Activation>();next->revision=revision;next->online=online;
        next->settings=std::make_shared<SettingsChannel>();std::array<Json,4> defaults;for(auto& v:defaults)v=Json::object();next->settings->publish(defaults);
        current.store(std::move(next));report(e.what());
    }''')
    elif name=='camera_session.cpp':
        text=include('look_behind/policy.hpp')+text
        text=once(text,'if(local_camera.cut&&!local_camera.enabled)', 'if(local_camera.valid&&local_camera.cut&&!local_camera.enabled)')
        text=once(text,'        bool driving=false;','        bool driving=false,rear_requested=false;')
        text=once(text,'        if(!driving)return batch;', '        if(!driving){rear_active_={};return batch;}')
        anchor='            inputs[owner.view]=actions_.consume(owner.physical_profile,frame.dt);'
        text=once(text,anchor,anchor+'\n            rear_requested|=look_behind::enabled.load()&&!inputs[owner.view].blocked&&(inputs[owner.view].held&DKR_VIEW_LOOK_BACK);')
        text=once(text,'        if(frame.capabilities&DKR_VIEW_STATIC_COLLISION) {', '        if((rear_requested||(program_&&!program_->quarantined()))&&(frame.capabilities&DKR_VIEW_STATIC_COLLISION)) {')
        anchor='            if(!program_||program_->quarantined()){dkr::sdk2::debug_frame(*state,{},unsigned(owners.size()));continue;}'
        rear='''            const bool was_rear=rear_active_[owner.view];rear_active_[owner.view]=false;
            if(look_behind::enabled.load()&&!input.blocked&&(input.held&DKR_VIEW_LOOK_BACK)) {
                DkrViewPose pose{};
                if(look_behind::pose(*state,pose)) {
                    pose.cut=!was_rear||(state->flags&DKR_VIEW_CUT);rear_active_[owner.view]=true;
                    if(collision_) {
                        DkrViewQuery cast{};cast.api=1;cast.size=sizeof(cast);cast.view=owner.view;cast.exclude_racer=state->racer_id;cast.radius=12;
                        for(unsigned i=0;i<3;++i){cast.origin[i]=state->position[i];cast.target[i]=pose.position[i];}cast.origin[1]+=35;
                        const auto hit=query(cast);if(hit.status==DKR_VIEW_QUERY_HIT)for(unsigned i=0;i<3;++i)pose.position[i]=hit.position[i]+hit.normal[i]*12;
                    }
                    batch.poses[owner.view]=pose;continue;
                }
            }
            if(was_rear){state->flags|=DKR_VIEW_CUT;batch.snapshots[owner.view]=state;}
            if(!look_behind::enabled.load())input.held&=~DKR_VIEW_LOOK_BACK;
'''
        text=once(text,anchor,rear+anchor)
        text=once(text,'                batch.poses={};batch.diagnostic=program_->error();return batch;', '                batch.diagnostic=program_->error();continue;')
        text=once(text,'        batch.diagnostic=scene_error_;', '        if(batch.diagnostic.empty())batch.diagnostic=scene_error_;')
    elif name=='camera_session.hpp':
        text=once(text,'    bool scene_attempted_=false;', '    std::array<bool,4> rear_active_{};\n    bool scene_attempted_=false;')
    elif name=='camera_rt64.cpp':
        text=once(text,'    presented(range->profile,true);', '    presented(range->profile,session_&&session_->active());')
    elif name=='magic_code_game_world.cpp':
        text=include('analog_driving/input.hpp')+text
        text=once(text,'    p.modules.emplace("game-hooks",1);', '    p.modules.emplace("analogue-driving",1);p.services.insert({"dkr2_driving_input","dkr2_driving_apply"});\n    p.modules.emplace("game-hooks",1);')
    elif name=='sdk2_world.inl':
        text=once(text,'void GameWorld::sdk2_boundary(unsigned event) {', 'void GameWorld::sdk2_boundary(unsigned event) {\n    if(event==DKR_CODE_EVENT_FRAME)dkr::runtime::driving::begin_frame(online_);')
        anchor='    if(owner.package->api!=2)return result(-5);'
        text=once(text,anchor,anchor+'\n'+include('analog_driving/sdk_service.inl'))
    elif name=='runtime_code_mods.cpp':
        text=include('analog_driving/native.hpp')+text
        text+='''
extern "C" std::uint32_t dkr_analogue_driving(std::uint8_t* ram,recomp_context*,unsigned channel,std::uint32_t racer,std::uint32_t bits) {
    if(!dkr_code_callbacks_enabled)return bits;
    return dkr::runtime::driving::apply(ram,channel,racer,bits);
}
'''
    elif name=='netplay_lobby.hpp':
        if 'host_timing_member.inl' not in text:
            text=once(text,'class Lobby final {\npublic:',
                'class Lobby final {\npublic:\n'+include('online_stability/host_timing_member.inl'))
    elif name=='experimental_rollback.hpp':
        text=include('online_stability/snapshot_history.hpp')+text
        text=include('online_stability/live_prediction.hpp')+text
        text=include('netplay/experimental_checkpoint_hash.hpp')+text
        text=once(text,'std::unique_ptr<RollbackStateStore> checkpoints_;','std::unique_ptr<dkr::online_stability::SnapshotHistory<RollbackStateStore>> checkpoints_;')
        text=once(text,'    bool correcting() const;', '''    bool correcting() const;
    unsigned prediction_window()const{return configuration_.prediction_window;}
    bool set_prediction_window(unsigned value,std::string& error) {
        return dkr::online_stability::update_prediction_window(active(),value,contract_.state_bytes,
            configuration_.prediction_window,configuration_.checkpoint_budget_bytes,checkpoints_.get(),statistics_.checkpoint_bytes,
            std::max(statistics_.next_frame,replay_goal_),error);
    }
    // Copy only the verified retained state at the confirmed frontier. Never
    // rewind the live world or expose a speculative checkpoint to observers.
    bool copy_confirmed_checkpoint(std::span<std::uint8_t> destination) const {
        if(!active()||correcting()||statistics_.confirmed_frames>=statistics_.next_frame)return false;
        std::uint64_t expected=0;
        return checkpoints_->load(statistics_.confirmed_frames,destination,&expected)&&checkpoint_hash(destination)==expected;
    }''')
    elif name=='experimental_rollback.cpp':
        text=once(text,'Step Driver::step(bool allow_advance) {', '''Step Driver::step(bool allow_advance) {
    if(const auto requested=dkr::online_stability::requested_prediction_window();requested&&*requested!=prediction_window()) {
        std::string detail;if(!set_prediction_window(*requested,detail)){fail(detail);return Step::Failed;}
    }''')
        text=once(text,'if (statistics_.next_frame - statistics_.confirmed_frames >= configuration_.prediction_window)',
            'if (statistics_.next_frame >= replay_goal_ && statistics_.next_frame - statistics_.confirmed_frames >= configuration_.prediction_window)')
        text=once(text,'auto checkpoints = std::make_unique<RollbackStateStore>(\n            contract.state_bytes, configuration.prediction_window + 8U, 4U, 2U, true);',
            'auto checkpoints = std::make_unique<dkr::online_stability::SnapshotHistory<RollbackStateStore>>(\n            contract.state_bytes, configuration.prediction_window,\n            configuration.checkpoint_budget_bytes-contract.state_bytes);')
        text=once(text,'    std::string detail;\n    {performance::Scope timing(performance::Stage::Capture);',
            '    std::string detail;\n    auto target=checkpoints_->staging(frame);\n    if(target.empty())target=scratch_;\n    {performance::Scope timing(performance::Stage::Capture);')
        text=once(text,'simulation_.capture(scratch_, detail)','simulation_.capture(target, detail)')
        text=once(text,'hash=checkpoint_hash(scratch_);','hash=checkpoint_hash(target);')
        text=once(text,'checkpoints_->save(frame, scratch_, hash)','checkpoints_->save(frame, target, hash)')
        text=once(text,'        configuration_ = configuration;',
            '        std::fprintf(stderr,"[rollback][checkpoints] direct=%u state-bytes=%zu retained-bytes=%zu budget=%zu\\n",unsigned(checkpoints->direct()),contract.state_bytes,checkpoints->allocated_bytes()+scratch.capacity(),configuration.checkpoint_budget_bytes);\n        configuration_ = configuration;')
    elif name=='probe_driver.cpp':
        text=include('analog_driving/native.hpp')+text
        anchor='    bool tick(std::uint32_t frame, const FrameInputs& inputs, TickOutput& output, std::string& error) override {'
        text=once(text,anchor,anchor+'''
        std::array<dkr::runtime::driving::Pedals,4> driving_inputs{};
        for(unsigned p=0;p<4;++p)driving_inputs[p]={inputs[p].accelerator,inputs[p].brake,inputs[p].driving_flags};
        dkr::runtime::driving::InputScope driving_scope(driving_inputs);''')
        anchor='                auto& world=*static_cast<Component*>(owner);'
        text=once(text,anchor,anchor+'''
                if(std::strcmp(name,"dkr_analogue_driving")==0) {
                    if(count!=3||!args||!result)return -1;
                    *result=dkr::runtime::driving::apply(ram,unsigned(args[0]),std::uint32_t(args[1]),std::uint32_t(args[2]));return 1;
                }''')
    elif name=='replay_recorder.cpp':
        text=once(text,'    std::uint32_t version = 1U;', '    std::uint32_t version = 2U;')
        text=once(text,'static_cast<std::uint32_t>(frames_.size()), 1U,', 'static_cast<std::uint32_t>(frames_.size()), 2U,')
    elif name=='netplay_protocol.cpp':
        text=text.replace('batch.inputs.size() * 8U','batch.inputs.size() * 11U').replace('static_cast<std::size_t>(count) * 8U','static_cast<std::size_t>(count) * 11U')
        text=once(text,'    if (bytes.size() < 18U) {','    if (bytes.size() < 21U) {')
        text=text.replace('batch.commits.size() * 44U','batch.commits.size() * 56U').replace('static_cast<std::size_t>(bytes[0]) * 44U','static_cast<std::size_t>(bytes[0]) * 56U')
        text=re.sub(r'(?m)^(\s*)out.push_back\(static_cast<std::uint8_t>\(input.stick_y\)\);',lambda m:m[0]+'\n'+m[1]+'if(!valid_input(input))return {};\n'+m[1]+'out.push_back(input.accelerator);out.push_back(input.brake);out.push_back(input.driving_flags);',text)
        text=re.sub(r'(?m)^(\s*)input.stick_y = static_cast<std::int8_t>\(bytes\[cursor\+\+\]\);',lambda m:m[0]+'\n'+m[1]+'input.accelerator=bytes[cursor++];input.brake=bytes[cursor++];input.driving_flags=bytes[cursor++];\n'+m[1]+'if(!valid_input(input)){error="Invalid analogue input.";return false;}',text)
        text=once(text,'        add(static_cast<std::uint8_t>(input.stick_y), 1U);','        add(static_cast<std::uint8_t>(input.stick_y), 1U);\n        add(input.accelerator,1U);add(input.brake,1U);add(input.driving_flags,1U);')
    elif name=='experimental_owner_inputs.hpp':
        text=text.replace('53+68*kOwnerInputBatch','53+71*kOwnerInputBatch')
    elif name=='experimental_owner_inputs.cpp':
        text=text.replace("'K','R','X',3", "'K','R','X',4").replace("'K','R','X',2", "'K','R','X',4")
        text=text.replace('bytes[4] != 3','bytes[4] != 4').replace('bytes[4] != 2','bytes[4] != 4')
        at=text.index('void put(')
        text=once(text[:at],'    return true;\n}', '    for(unsigned i=0;i<p.count;++i)if(!valid_input(p.inputs[i]))return false;\n    return true;\n}')+text[at:]
        text=once(text,'        result.push_back(std::bit_cast<std::uint8_t>(p.inputs[i].stick_y));', '        result.push_back(std::bit_cast<std::uint8_t>(p.inputs[i].stick_y));\n        result.push_back(p.inputs[i].accelerator);result.push_back(p.inputs[i].brake);result.push_back(p.inputs[i].driving_flags);')
        if 'unsigned cursor=kHeader;' in text:
            text=text.replace('bytes.size()-cursor<4','bytes.size()-cursor<7')
            text=once(text,'std::bit_cast<std::int8_t>(bytes[cursor+3])};cursor+=4;', 'std::bit_cast<std::int8_t>(bytes[cursor+3]),bytes[cursor+4],bytes[cursor+5],bytes[cursor+6]};cursor+=7;')
        else:
            text=text.replace('p.count * 4','p.count * 7').replace('bytes[7] * 4U','bytes[7] * 7U').replace('i * 4, 2','i * 7, 2').replace('i * 4 + 2','i * 7 + 2').replace('i * 4 + 3','i * 7 + 3')
            text=once(text,'std::bit_cast<std::int8_t>(bytes[kHeader + i * 7 + 3])};', 'std::bit_cast<std::int8_t>(bytes[kHeader + i * 7 + 3]),bytes[kHeader+i*7+4],bytes[kHeader+i*7+5],bytes[kHeader+i*7+6]};')
    elif name in ('experimental_runtime.cpp','experimental_race_frontend.cpp'):
        text='#include "netplay/online_input_broker.hpp"\n'+text
        text=once(text,'std::uint32_t pack(PackedInput p) {return p.buttons|(std::uint32_t(std::uint8_t(p.stick_x))<<16)|(std::uint32_t(std::uint8_t(p.stick_y))<<24);}', 'std::uint64_t pack(PackedInput p) {return packed_input_bits(p);}')
        text=once(text,'PackedInput unpack(std::uint32_t p) {return {std::uint16_t(p),std::int8_t(p>>16),std::int8_t(p>>24)};}', 'PackedInput unpack(std::uint64_t p) {return input_from_bits(p);}')
        text=text.replace('std::atomic<std::uint32_t> input{0};','std::atomic<std::uint64_t> input{0};').replace('std::atomic<std::uint32_t> input=0,frames=0,confirmed=0,corrections=0;', 'std::atomic<std::uint64_t> input=0;std::atomic<std::uint32_t> frames=0,confirmed=0,corrections=0;')
        # All runtime sampling paths use the broker's COMPLETE local sample.
        text=re.sub(r'(\w+)\.buttons,\1\.stick_x,\1\.stick_y\}',r'\1.buttons,\1.stick_x,\1.stick_y,\1.accelerator,\1.brake,\1.driving_flags}',text)
        text=text.replace('PackedInput{buttons,std::int8_t(std::clamp(x,-1.0F,1.0F)*80),std::int8_t(std::clamp(y,-1.0F,1.0F)*80)}','online_input_broker().local_sample().input')
    return text
def storage_guards(name,text):
    anchors={
        'legacy_import_library.cpp':['        thread_=std::jthread([this,source=std::move(source),roms=std::move(roms),scan](std::stop_token stop) {'],
        'legacy_track_catalog.cpp':['        thread_=std::jthread([this,action,previous,id=std::move(id),roms=std::move(roms)](std::stop_token stop) {'],
        'magic_code_library.cpp':['struct Lease {'],
        'vehicle_library.cpp':['struct Lock {'],
        'online_mod_sync.cpp':['        revision=std::move(revision),ai,prepare=std::move(prepare)](std::stop_token stop) mutable {','    try {thread_=std::jthread([this,settings=std::move(settings),prepare=std::move(prepare)](std::stop_token stop) mutable {'],
        'online_mod_cache.cpp':['bool cached_payload(const std::filesystem::path& directory,const Payload& expected) {'],
        'runtime_texture_packs.cpp':['                    const TrackPackOwner* owner) {'],
        'custom_tracks.cpp':['             InstallOutcome* outcome) {'],
        'update_service.cpp':['    worker = std::jthread([this, action = std::move(action)](std::stop_token stop) {'],
    }
    if name not in anchors and name!='online_mod_profile.cpp':return text
    text='#include "'+(ROOT/'runtime-recomp/src/game/storage/maintenance.hpp').as_posix()+'"\n'+text
    for anchor in anchors.get(name,[]):
        guard='OnlineActivity' if name=='online_mod_sync.cpp' else 'Activity'
        replacement=anchor+'\n    dkr::storage::'+guard+' storage_activity;'
        if not re.search(re.escape(anchor)+r'\s*dkr::storage::'+guard+r' storage_activity;',text):text=once(text,anchor,replacement)
    if name=='online_mod_profile.cpp':
        anchor='try{validate_profile(*out);'
        replacement=anchor+'dkr::storage::used(out->directory);'
        if replacement not in text:text=once(text,anchor,replacement)
        anchor='        } else if(cached) {'
        replacement=anchor+'\n            dkr::storage::used(source);'
        if replacement not in text:text=once(text,anchor,replacement)
    if name=='online_mod_cache.cpp':
        start=text.index('bool cached_payload(');end=text.index('void cache_bundle(',start)
        part=text[start:end]
        if 'dkr::storage::used(file);' not in part:part=once(part,'    return true;','    dkr::storage::used(file);\n    return true;')
        text=text[:start]+part+text[end:]
    return text

def compose(rows,out):
    if not NAMES.issubset({(r['target'],Path(r['path']).name) for r in rows}):
        raise ValueError('Online performance source closure differs')
    files={p.name:p.read_bytes() for p in (AUTH/'native').glob('*')};swaps=[]
    header=lambda n:'#include "'+(out/n).as_posix()+'"\n'
    # Quoted capsule headers are transitive compiler inputs. Redirect the
    # complete affected include closure, so no TU retains the four-byte ABI.
    headers={}; seeds={'netplay_types.hpp','online_input_broker.hpp','runtime_input.hpp',
        'runtime_controls_ui.inl','runtime_graphics_ui.inl','graphics.inl','sdk2_world.inl','experimental_owner_inputs.hpp','experimental_rollback.hpp','netplay_lobby.hpp','owner_pump.inl','camera_session.hpp','legacy_mod_launch.hpp','runtime_online_mods_ui.inl',
        'game_payload.hpp','legacy_character_menu.hpp','legacy_character_menu_render.hpp','legacy_character_stage.hpp','update_process.hpp'}
    pattern=re.compile(r'^\s*#include "([^"\n]+)"',re.M)
    receipts=[]
    core_search=[]
    for row in rows:
        if row['target']=='DKRNetplayCore':
            core_search.extend(Path(p) for p in row.get('includes',[]) if '$<' not in p and Path(p).is_dir())
            core_search.append(Path(row['path']).parent)
    core_search=list(dict.fromkeys(core_search))
    core_types=next((p for base in core_search for p in (base/'netplay_types.hpp',base/'netplay/netplay_types.hpp') if p.is_file()),None)
    if core_types is None:raise ValueError('The compiled netplay owner must supply the canonical input types')
    core_types=core_types.resolve()
    core_lobby=next((p.resolve() for base in core_search for p in (base/'netplay_lobby.hpp',base/'netplay/netplay_lobby.hpp') if p.is_file()),None)
    def dependencies(text,origin,search):
        found={}
        for match in pattern.finditer(text):
            name=Path(match[1]);candidates=[name] if name.is_absolute() else [origin.parent/name,*[p/name for p in search]]
            path=next((p.resolve() for p in candidates if p.is_file()),None)
            if path and path.name=='netplay_types.hpp':path=core_types
            if path and path.name=='netplay_lobby.hpp' and core_lobby:path=core_lobby
            if path is None or path.suffix not in ('.hpp','.h','.inl'):continue
            if any(p.lower() in ('extern','recompiledfuncs','recompiledpatches','recompiledrsp','thirdparty','contrib') for p in path.parts):continue
            found[match[1]]=path
            if path in headers:continue
            raw=path.read_bytes();content=raw.decode().replace('\r\n','\n')
            cooked=beta3(path.name,content) if path.name in seeds else content
            dest=out/'includes'/sha(path.as_posix().encode())[:16]/path.name
            headers[path]={'raw':raw,'text':cooked,'changed':cooked!=content,'dest':dest,'deps':{}}
            headers[path]['deps']=dependencies(cooked,path,search)
        return found
    # DKRNetplayCore's public include roots reach its linked consumers via
    # CMake's link interface, not each consumer's INCLUDE_DIRECTORIES. Keep
    # that same fallback context when following unqualified netplay headers.
    sources={};test_bodies={}
    for row in rows:
        path=Path(row['path']);search=[Path(p) for p in row.get('includes',[]) if '$<' not in p and Path(p).is_dir()]+core_search
        content=path.read_text(encoding='utf-8').replace('\r\n','\n')
        if path.name in ('experimental_runtime.cpp','experimental_race_frontend.cpp'):
            content='#include "netplay/online_input_broker.hpp"\n'+content
        sources[str(path)]=dependencies(content,path,search)
        if row.get('test_source'):
            if row['target']!='DKRRaceReleaseBeforeChecks':raise ValueError('Unexpected test-only body context')
            before=Path(row['test_source']).resolve()
            if not before.is_file():raise ValueError('Negative-control implementation missing')
            body=before.read_text(encoding='utf-8').replace('\r\n','\n')
            test_bodies[row['target']]={'path':before,'text':body,'deps':dependencies(body,before,search)}
    changed=True
    while changed:
        changed=False
        for value in headers.values():
            if not value['changed'] and any(headers[p]['changed'] for p in value['deps'].values()):
                value['changed']=True;changed=True
    def remap(text,deps):
        def replace(m):
            p=deps.get(m[1]);return '#include "'+headers[p]['dest'].as_posix()+'"' if p and headers[p]['changed'] else m[0]
        return pattern.sub(replace,text)
    for path,value in headers.items():
        if not value['changed']:continue
        content=remap(value['text'],value['deps'])
        # Rebase unchanged adjacent includes after relocating this header.
        def adjacent(m):
            p=Path(m[1]);p=p if p.is_absolute() else path.parent/p
            return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
        content=pattern.sub(adjacent,content)
        # Capsule roles contain byte-identical aliases of shared headers.
        # Preserve their single-definition identity after rebasing each role.
        guard='DKR_BETA3_SHARED_HEADER_'+sha(value['raw']).upper()
        content='#ifndef '+guard+'\n#define '+guard+'\n'+content+'\n#endif\n'
        files[value['dest'].relative_to(out).as_posix()]=content.encode()
        receipts.append({'path':str(path),'new':str(value['dest']),'input_sha256':sha(value['raw']),'output_sha256':sha(content.encode())})
    files['header_receipts.json']=(json.dumps(receipts,indent=2)+'\n').encode()
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n');name=source.name
        required=(row['target'],name) in NAMES
        if name=='probe_native.c' and required:
            text=header('menu_notifications.h')+text
            text=once(text,'DKR_PROBE_HAS_FULL_SCENES && event<=17','DKR_PROBE_HAS_FULL_SCENES && dkr_menu_notification(event)')
            text=text.replace('complete 18-event reviewed','reviewed stock and vehicle-notification')
            marker='    dkr_probe_block(name);'
            start=text.index('int dkr_probe_native_fields(')
            pre=text[:start];tail=text[start:]
            tail=once(tail,marker,'    if(strcmp(name,"dkr_legacy_track_menu")==0)fprintf(stderr,"[rollback][menu-refused] event=%u full-scenes=%u offline=%u canonical=%u\\n",event,unsigned_placeholder,offline,canonical_presentation);\n'+marker)
            tail=tail.replace('unsigned_placeholder','(unsigned)DKR_PROBE_HAS_FULL_SCENES')
            text=pre+tail
            files[row['target']+'/menu_fixture.c']=(header('menu_notifications.h')+(AUTH/'tests/menu_fixture_prefix.c').read_text()+
                function(text,'int dkr_probe_native_fields(')+'\n'+(AUTH/'tests/menu_fixture_main.c').read_text()).encode()
            files[row['target']+'/menu_before_fixture.c']=((AUTH/'tests/menu_fixture_prefix.c').read_text()+
                function(raw.decode(),'int dkr_probe_native_fields(')+'\n'+(AUTH/'tests/menu_fixture_main.c').read_text()).encode()
        elif name=='update_process.cpp' and required:
            text=once(text,'command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW','command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW')
        elif name=='update_service.cpp' and required:
            text=once(text,'Service *service() {', '''void shutdown() {
    // Explicitly join while process-wide TLS providers are still alive.
    // cancel() remains non-blocking for ordinary launcher interactions.
    owner.reset();
}
Service *service() {''')
        elif name=='game_main.cpp':
            text='#include "vehicle_lighting/check.hpp"\n'+text
            text=once(text,'        while(std::chrono::steady_clock::now()<deadline&&!accepted) {',
                '        while(std::chrono::steady_clock::now()<deadline&&!accepted) {\n            dkr::runtime::vehicles::lighting::pump_owned_check();')
            text='#include "online_stability/process_services.hpp"\n'+header('runtime_log.hpp')+text
            text=once(text,'    std::unique_ptr<dkr::updates::InstallLock> application_update_lock;',
                '    std::unique_ptr<dkr::updates::InstallLock> application_update_lock;\n    dkr::online_stability::ProcessServices process_services([]{dkr::crash_reports::shutdown();dkr::online_stability::retire_store_http();dkr::online::shutdown_account();});')
            text=once(text,'const std::filesystem::path current = log_directory / "runtime.log";\n    if (!dkr::runtime::support::history::rotate(log_directory)) return false;',
                'const bool rotated=dkr::runtime::support::history::rotate(log_directory);\n'
                '    const std::filesystem::path current = rotated ? log_directory / "runtime.log" : dkr::online_performance::fallback_log(log_directory);\n    if(current.empty())return false;')
            text=once(text,'    std::setvbuf(stderr, nullptr, _IONBF, 0);\n#if defined(__ANDROID__) || defined(DKR_IOS)',
                '    dkr::online_performance::protect_log(stderr);\n    std::setvbuf(stderr, nullptr, _IONBF, 0);\n'
                '    if(!rotated){dkr::runtime::support::history::prune(log_directory/"history");std::fprintf(stderr,"[boot][logging] current log busy; separate instance log=%s\\n",current.string().c_str());}\n#if defined(__ANDROID__) || defined(DKR_IOS)')
        elif name=='quick_join_transport.cpp':
            text='#include <cstdio>\n#include "online_presentation/test_receive_delay.hpp"\n#include "online_stability/live_send_capacity.hpp"\n'+text
            text=once(text,'        realtime.reliability.maxRetransmits = 0U;',
                '        dkr::online_stability::configure_live_reliability(realtime.reliability);')
            text=once(text,'            last_heartbeat_ = now;', '''            last_heartbeat_ = now;
            struct SdkRow {
                std::shared_ptr<rtc::PeerConnection> connection;
                std::array<std::shared_ptr<rtc::DataChannel>,5> channels;
            };
            std::vector<SdkRow> sdk_rows;
            {
                std::scoped_lock lock(mutex_);
                for(const auto& [identifier,peer]:peers_)if(peer->current.load())
                    sdk_rows.push_back({peer->connection,{peer->control_channel,peer->authority_channel,
                        peer->realtime_channel,peer->checkpoint_channel,peer->replica_channel}});
            }
            for(const auto& row:sdk_rows)if(row.connection) {
                const auto rtt=row.connection->rtt();std::array<std::size_t,5> queued{};
                for(unsigned n=0;n<queued.size();++n)if(row.channels[n])queued[n]=row.channels[n]->bufferedAmount();
                std::fprintf(stderr,"[online][sdk] role=%s sctp-rtt-ms=%lld bytes-sent=%zu bytes-received=%zu queued-control=%zu queued-authority=%zu queued-live=%zu queued-checkpoint=%zu queued-replica=%zu\\n",
                    host_?"host":"client",static_cast<long long>(rtt?rtt->count():-1),row.connection->bytesSent(),row.connection->bytesReceived(),
                    queued[0],queued[1],queued[2],queued[3],queued[4]);
            }''')
            text=once(text,'        std::vector<std::uint8_t> bytes;\n    };\n\n    void release_packet_bytes_locked',
                '        std::vector<std::uint8_t> bytes;\n        std::chrono::steady_clock::time_point test_ready=dkr::online_presentation::test_receive_delay::ready_at();\n    };\n\n    void release_packet_bytes_locked')
            text=once(text,'            if (!candidate->empty()) { queue = candidate; break; }',
                '            if (!candidate->empty()&&dkr::online_presentation::test_receive_delay::ready(candidate->front().test_ready)) { queue = candidate; break; }')
            text=header('setup_policy.hpp')+text
            text=once(text,'if (!quick_join_admit(traffic, buffered, aggregate, bytes.size() + 1U))',
                'if (!(quick_join_admit(traffic, buffered, aggregate, bytes.size() + 1U) ||\n'
                '              dkr::online_performance::additional_capacity(traffic,dkr::online_performance::setup_bulk,buffered,aggregate,bytes.size()+1U)) ||\n'
                '              !dkr::online_stability::fresh_input_capacity(traffic,buffered,bytes.size()+1U))')
        elif name=='direct_session.cpp':
            text='#include "netplay/session_chat/chat.hpp"\n#include "netplay/session_chat/settings.hpp"\n#include "netplay/peer_relay/transport.hpp"\n#include "online_stability/qualification_delivery.hpp"\n'+text
            text=text.replace('make_udp_session_transport()', 'dkr::relay::wrap(this,make_udp_session_transport())')
            text=text.replace('make_quick_join_session_transport(true, {}, error)', 'dkr::relay::wrap(this,make_quick_join_session_transport(true, {}, error))')
            text=text.replace('make_quick_join_session_transport(false, invite, error)', 'dkr::relay::wrap(this,make_quick_join_session_transport(false, invite, error))')
            text=header('setup_policy.hpp')+text
            text=once(text,'DirectSession::DirectSession()',
                'namespace {\nvoid retire_directory_room(const void* session) {\n'
                '    dkr::online::native::reset(session);\n'
                '    dkr::chat::reset(session);\n'
                '    auto bridge = dkr::online::bridge(session);\n'
                '    std::lock_guard lock(bridge->mutex);\n'
                '    bridge->commands.clear();\n    bridge->responses.clear();\n}\n} // namespace\n\n'
                'DirectSession::DirectSession()')
            text=once(text,'    state_ = ConnectionState::Offline;\n    is_host_ = false;',
                '    state_ = ConnectionState::Offline;\n    is_host_ = false;\n'
                '    // A retired transport will not pump the directory owner again.\n'
                '    retire_directory_room(this);')
            text=once(text,'    state_ = ConnectionState::Failed;\n    status_ = message;',
                '    state_ = ConnectionState::Failed;\n    retire_directory_room(this);\n    status_ = message;')
            text=once(text,'            const DatagramSendStatus result = transport_->send_status(',
                '            dkr::online_performance::SetupBulk setup(packet.type==protocol::MessageType::ModData);\n'
                '            const DatagramSendStatus result = transport_->send_status(')
            text=once(text,'                    const auto sent=transport_->send_status(destination,encrypted,item.traffic,ignored);',
                '                    dkr::online_performance::SetupBulk setup(item.traffic==TransportTrafficClass::Checkpoint && !party->running && !party->stopping && state_==ConnectionState::Loading);\n'
                '                    const auto sent=transport_->send_status(destination,encrypted,item.traffic,ignored);')
            text=once(text,'        (local_slot_ >= room_view_.players.size() ||\n         !room_view_.players[local_slot_].loaded) &&\n',
                '        ((launch_descriptor_&&launch_descriptor_->synchronization==SynchronizationMode::ExperimentalRollback)||\n         local_slot_ >= room_view_.players.size() || !room_view_.players[local_slot_].loaded) &&\n')
            marker='    } else if (packet.header.type == protocol::MessageType::Loaded || packet.header.type==protocol::MessageType::ExperimentalLoaded) {'
            text=once(text,marker,marker+'''
        if(packet.header.type==protocol::MessageType::ExperimentalLoaded&&state_==ConnectionState::Running&&launch_descriptor_) {
            protocol::ExperimentalLoadedPayload retry;
            if(protocol::decode_experimental_loaded(packet.payload,retry)&&retry.launch_hash==launch_descriptor_hash(*launch_descriptor_)&&
               retry.loaded.player_slot==peer->slot&&bootstrap_hash_present_[peer->slot]&&runtime_save_present_[peer->slot]&&
               retry.loaded.bootstrap_hash==bootstrap_hashes_[peer->slot]&&retry.loaded.online_save_generation==runtime_save_generations_[peer->slot]&&
               retry.loaded.online_save_hash==runtime_save_hashes_[peer->slot]) {
                send_with_key(peer->address,peer->key,protocol::MessageType::Start,protocol::encode_start({1U,*launch_descriptor_,launch_descriptor_hash(*launch_descriptor_)}));
                send_with_key(peer->address,peer->key,protocol::MessageType::LobbyState,protocol::encode_lobby_state(lobby_payload()));
            }
            return;
        }''')
        elif name=='online_mod_admission.cpp':
            text='#include "online_stability/transfer_window.hpp"\n'+text
            text=once(text,'MaxModQueue=512*1024','MaxModQueue=dkr::online_stability::MaximumTransferWindow+128*1024')
        elif name=='experimental_lobby_admission.cpp':
            text=once(text,'            if(!host_&&local_ready_&&source.storage[3]==0)release_seen_=true;\n','')
        elif name=='experimental_network.cpp':
            text='#include "online_stability/flow_trace.hpp"\n#include "online_stability/live_delay.hpp"\n'+text
            text=once(text,'plain.push_back(std::uint8_t(kind));','plain.push_back(std::uint8_t(kind==0&&traffic==TransportTrafficClass::Authoritative?3:kind));')
            text=once(text,'plain[5]>2','plain[5]>3')
            text=once(text,'if(plain[5]==0) {','if(plain[5]==0||plain[5]==3) {\n        dkr::online_stability::ArrivalScope arrival_scope;')
            text=once(text,'Network::~Network() {','Network::~Network() {\n    dkr::online_stability::flow::forget(this);')
            text=once(text,'bool Network::fail(std::string message) {','bool Network::fail(std::string message) { dkr::online_stability::flow::dump(this);')
            text=once(text,'        transport_.service();','        dkr::online_stability::flow::pulse(this,configuration_.match_id,session_.epoch(),session_.local_owner(),now);\n        transport_.service();')
            text=once(text,'        const auto result=session_.receive_input(std::uint8_t(peer),payload);', '''        const auto result=session_.receive_input(std::uint8_t(peer),payload);
        if(result==SessionInputResult::Accepted)if(const auto input=decode_owner_inputs(payload);input&&input->count)
            dkr::online_stability::batch_arrival(input->owner,input->first_frame+input->count-1,plain[5]==3);
        if(result==SessionInputResult::Accepted||result==SessionInputResult::Duplicate)
            if(const auto input=decode_owner_inputs(payload))dkr::online_stability::flow::received(this,peer,*input,result==SessionInputResult::Duplicate);''')
            text=once(text,'                if(!packet || (repair && !packet->count))continue;', '                if(!packet || (repair && !packet->count))continue;\n                dkr::online_stability::flow::offer(this,peer,*packet,repair!=0);')
            text=once(text,'                if(result==DatagramSendStatus::Sent) {', '''                if(packet.input_key)dkr::online_stability::flow::sent(this,peer,*packet.input_key,result==DatagramSendStatus::Sent);
                if(result==DatagramSendStatus::Sent) {''')
        elif name=='experimental_runtime.cpp':
            text='#include "vehicle_lighting/check.hpp"\n'+text
            text='#include "online_stability/response_trace.hpp"\n'+text
            text=once(text,'                        if(!world->publish_current(session.epoch(),stats.next_frame-1,message))throw std::runtime_error(message);',
                '                        if(!world->publish_current(session.epoch(),stats.next_frame-1,message))throw std::runtime_error(message);\n                        dkr::online_stability::response::reached(session.epoch(),stats.next_frame-1,dkr::online_stability::response::Stage::Publish);')
            text='#include "online_stability/startup_transport.hpp"\n#include "online_stability/flow_trace.hpp"\n'+text
            text=once(text,'            bool loaded=false;', '''            using StartupRoute=dkr::online_stability::StartupTransport<SessionTransport,TransportTrafficClass,LobbyAdmission,LobbyRoute>;
            StartupRoute startup_route(admission,route);
            std::unique_ptr<Session> prepared_session;
            std::unique_ptr<Network> prepared_network;
            const auto prepare_network=[&] {
                if(prepared_network)return;
                prepared_session=std::make_unique<Session>(*world);
                const auto budget=std::max<std::size_t>(128U*1024U*1024U,contract.state_bytes*(std::size_t(launch->rollback_window)+2));
                if(!prepared_session->start({{epoch,launch->player_count,launch->rollback_window,budget},local,launch->input_delay_frames},message))throw std::runtime_error(message);
                NetworkConfiguration config;if(!route.configuration(admission.incarnation(),config,message))throw std::runtime_error(message);
                prepared_network=std::make_unique<Network>(*prepared_session,startup_route);
                if(!prepared_network->start(config,message))throw std::runtime_error(message);
            };
            bool loaded=false;''')
            text=once(text,'                if(!admission.service_admission())throw std::runtime_error(admission.error());', '''                if(prepared_network) {
                    if(!prepared_network->service())throw std::runtime_error(prepared_network->error());
                } else if(!admission.service_admission())throw std::runtime_error(admission.error());
                if(initial.host||admission.prepared())prepare_network();''')
            text=once(text,'                Session session(*world);','                prepare_network();auto& session=*prepared_session;')
            start=text.index('                const auto budget=std::max<std::size_t>',text.index('                prepare_network();auto& session='))
            end=text.index('                pump.command_source',start)
            text=text[:start]+'''                simulation_started=true;
                auto& network=*prepared_network;Pump pump(session,network);
'''+text[end:]
            text=once(text,'                        throw std::runtime_error("Experimental peers made no confirmed progress for 90 seconds.',
                '                        dkr::online_stability::flow::dump(&network);\n                        throw std::runtime_error("Experimental peers made no confirmed progress for 90 seconds.')
        elif name=='online_mod_cache.cpp':
            text=header('setup_policy.hpp')+text
            text=once(text,'if(size_-flushed_>=64*1024)flush();','if(size_-flushed_>=dkr::online_performance::DurableBatch)flush();')
        elif name=='online_mod_transfer.cpp':
            text='#include "online_stability/transfer_window.hpp"\n'+text
            text=once(text,'    const bool chunk=operation==Operation::ManifestChunk || operation==Operation::PayloadChunk;', '''    const bool chunk=operation==Operation::ManifestChunk || operation==Operation::PayloadChunk;
    const bool window=(operation==Operation::PayloadRequest||operation==Operation::PayloadCredit||operation==Operation::ConsentAck)&&!value.bytes.empty();
    if(window)(void)dkr::online_stability::requested_window(value.bytes);''')
            text=once(text,'(!chunk && !value.bytes.empty())','(!chunk && !window && !value.bytes.empty())')
            text=text.replace('transfer_window_bytes(chunk)', '(message.payload.empty()?transfer_window_bytes(chunk):(dkr::online_stability::requested_window(message.bytes)/chunk)*chunk)')
            text=once(text,'chunk.offset-request.offset>=TransferWindowBytes', 'chunk.offset-request.offset>=(request.bytes.empty()?TransferWindowBytes:dkr::online_stability::MaximumTransferWindow)')
            text=once(text,'chunk.bytes.size()>TransferWindowBytes-bytes_', 'bytes_>(request.bytes.empty()?TransferWindowBytes:dkr::online_stability::MaximumTransferWindow)||chunk.bytes.size()>(request.bytes.empty()?TransferWindowBytes:dkr::online_stability::MaximumTransferWindow)-bytes_')
        elif name=='legacy_mod_worker.cpp':
            text='#include "online_stability/preparation_policy.hpp"\n'+text
            text=once(text,'// Two sequential items, one verified ROM source set, the SAME','// Up to four sequential items, one verified ROM source set, the SAME')
            text=once(text,'if(count!=2)throw Error("Invalid bounded online preparation batch.");','if(count<2||count>dkr::online_stability::MaximumPrepareBatch)throw Error("Invalid bounded online preparation batch.");')
        elif name=='online_mod_profile.cpp':
            text='#include "online_stability/preparation_policy.hpp"\n#include "online_stability/catalogue_cache.hpp"\n'+text
            text=once(text,'using nlohmann::json;', 'using nlohmann::json;\nusing CatalogueScope=dkr::online_stability::CatalogueScope<TrackCatalog,TrackCatalogItem>;')
            if text.count('TrackCatalog::installed_items(')!=3:raise ValueError('Preparation catalogue surface changed')
            text=text.replace('TrackCatalog::installed_items(', 'CatalogueScope::installed(')
            text=once(text,'    auto out=std::make_shared<Profile>();out->manifest=canonical(input);',
                '    CatalogueScope catalogue_scope;\n    auto out=std::make_shared<Profile>();out->manifest=canonical(input);')
            if 'Rebuilding this damaged cache from verified files' not in text:raise ValueError('Retained cache repair is missing')
            text=once(text,'            validate_profile(reusable);','            cached=profile_sources(reusable);')
            text=once(text,'        append_sources(sources,profile_sources(reusable));','        append_sources(sources,cached?*cached:profile_sources(reusable));')
            text=once(text,'            std::optional<Manifest> second;', '''            const auto batch_limit=dkr::online_stability::preparation_batch(std::filesystem::space(root).available,dkr::online_stability::available_memory());
            std::vector<Manifest> items{subset};''')
            text=once(text,'                second=std::move(next);break;', '                items.push_back(std::move(next));if(items.size()>=batch_limit)break;')
            text=once(text,'            if(second) {','            if(items.size()>1) {')
            text=once(text,'                const std::array<Manifest,2> items{subset,*second};\n','')
            text=once(text,'{"count",2},{"roms",json::array()}', '{"count",items.size()},{"roms",json::array()}')
            text=once(text,'Preparing two missing mods with shared verified ROM sources','Preparing '+ '"+std::to_string(items.size())+"' +' missing mods with shared verified ROM sources')
            text=once(text,'"[online-mods][batch] items=2 complete=%u outcome=%u elapsed-ms=%lld\\n",completed', '"[online-mods][batch] items=%zu complete=%u outcome=%u elapsed-ms=%lld\\n",items.size(),completed')
            start=text.index('        if(const auto at=existing.find(track.id);at!=existing.end()) {')
            end=text.index('        if(data.size()>std::uint64_t(1024*MiB)-bytes)',start)
            text=text[:start]+'''        bool alternate=false;
        if(const auto at=existing.find(track.id);at!=existing.end()) {
            if(pack_track_lab(at->second,stop)==data)continue;
            alternate=true;
        }
        auto target=tracks/course.filename();
        if(alternate||std::filesystem::exists(target)) {
            ensure_directory(tracks/"online-variants");
            target=dkr::online_stability::archived_course(tracks,sha256(data));
            if(std::filesystem::exists(target)) {
                check_storage(target,true);
                if(pack_track_lab(target,stop)!=data)throw Error("The archived offline course needs repair. Existing files were not replaced.");
                continue;
            }
            std::fprintf(stderr,"[online-mods][offline-variant] course=%s preserved-installed=1 archived-inactive=1\\n",track.id.c_str());
        }
'''+text[end:]
        elif name=='online_mod_sync.cpp':
            text='#include "online_stability/mod_progress.hpp"\n#include "online_stability/transfer_window.hpp"\n'+text
            text=once(text,'Message{Operation::ConsentAck,message.manifest}', 'Message{Operation::ConsentAck,message.manifest,{},0,0,dkr::online_stability::window_bytes(dkr::online_stability::MaximumTransferWindow)}')
            text=once(text,'    Download download;', '    dkr::online_stability::TransferWindow transfer;\n    Download download;')
            text=once(text,'                consent_sent=true;continue;', '                transfer.chunk_budget(link_.chunk_budget());transfer.negotiate(message.bytes);consent_sent=true;continue;')
            text=text.replace('download.request(link_.chunk_budget())','transfer.request(download.request(link_.chunk_budget()))')
            text=once(text,'request.offset-requested_offset>=transfer_window_bytes(link_.chunk_budget())/2','request.offset-requested_offset>=(request.payload.empty()?transfer_window_bytes(link_.chunk_budget())/2:transfer.stride())')
            text=once(text,'            if(first || gap || refill) {', '            if(first || gap || refill) {\n                transfer.credit(first,gap);request=transfer.request(std::move(request));')
            text=once(text,'file->append(chunk->bytes);download.persisted(chunk->bytes.size());',
                'file->append(chunk->bytes);download.persisted(chunk->bytes.size());transfer.observed(chunk->bytes.size());')
            text=text.replace('retry=Clock::now()+std::chrono::seconds(1);', 'retry=Clock::now()+transfer.retry_delay();')
            text=once(text,'for(const auto& [id,peer]:peers)state_.peers.push_back(peer.progress);}',
                'for(const auto& [id,peer]:peers)state_.peers.push_back(peer.progress);dkr::online_stability::publish_mod_progress(&link_,state_.peers);}')
            # Keep the bounded bursts, active-slot fairness and SDK backpressure.
            # Drain a full receive batch immediately instead of sleeping after it.
            text=once(text,'        pause(stop,std::chrono::milliseconds(2));\n    }\n    if(saw_offer)',
                '        if(drained>32)std::this_thread::yield();else pause(stop,std::chrono::milliseconds(2));\n    }\n    if(saw_offer)')
            text=once(text,'const auto download_began=Clock::now();','const auto download_began=Clock::now();Clock::time_point payload_began{};')
            text=once(text,'download.consent();progress(', 'download.consent();payload_began=Clock::now();progress(')
            text=once(text,'cached-bytes=%llu gap-retries=%llu refill-credits=%llu elapsed-ms=%lld',
                'cached-bytes=%llu gap-retries=%llu refill-credits=%llu setup-ms=%lld payload-phase-ms=%lld')
            text=once(text,'(Clock::now()-download_began).count()));',
                '(Clock::now()-download_began).count()),\n                static_cast<long long>(payload_began.time_since_epoch().count()?std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-payload_began).count():0));')
            text=once(text,'            {std::lock_guard lock(mutex_);state_.phase=SyncPhase::Preparing;',
                '            std::fprintf(stderr,"[online-mods][window] peak-bytes=%zu max-inbox-bytes=%zu\\n",transfer.peak(),dkr::online_stability::MaximumTransferWindow);\n            {std::lock_guard lock(mutex_);state_.phase=SyncPhase::Preparing;')
        elif name=='online_mod_runtime.cpp':
            text=header('setup_policy.hpp')+text
            text=text.replace('resource_cache_budget(','dkr::online_performance::resource_budget(')
            text=once(text,'std::weak_ptr<const RuntimeResources> live_resource_cache;',
                'std::weak_ptr<const RuntimeResources> live_resource_cache;\nstd::uint64_t retained_bytes=0;')
            text=once(text,'        if(auto reusable=live_resource_cache.lock();',
                '        if(resource_cache && retained_bytes>retention_budget())resource_cache.reset();\n        if(auto reusable=live_resource_cache.lock();')
            text=once(text,'if(cache_bytes<=budget)resource_cache=out;',
                'retained_bytes=cache_bytes;if(cache_bytes<=budget)resource_cache=out;')
            text=once(text,'    cancelled(stop);const auto sources=profile_sources(*profile);check_storage(rom,false);',
                '    auto phase_at=began;\n    const auto phase=[&](const char* name){const auto now=std::chrono::steady_clock::now();std::fprintf(stderr,"[online-mods][prepare-phase] phase=%s elapsed-ms=%lld\\n",name,static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now-phase_at).count()));phase_at=now;};\n'
                '    cancelled(stop);if(identity(profile->manifest)!=profile->digest)throw Error("Online resource identity differs from its manifest.");check_storage(rom,false);phase("profile-identity");')
            text=once(text,'const auto key=out->profile_->digest+":"+sha256(bytes)+":selected-music-v1";','const auto key=out->profile_->digest+":"+sha256(bytes)+":selected-music-v1:"+std::string(version);')
            # The exact immutable resources already passed deep validation and
            # own all bytes. A hit never loads changed disk content or shares a
            # mutable world. A miss keeps the complete source and asset checks.
            text=once(text,'    out->stock_=AssetBank::stock(std::move(bytes));',
                '    phase("rom-verification");const auto sources=profile_sources(*out->profile_);phase("profile-verification");out->stock_=AssetBank::stock(std::move(bytes));')
            text=once(text,'    auto characters=TrackCatalog::load_selected_characters(sources.characters,out->stock_);',
                '    phase("stock-and-portable-code");auto characters=TrackCatalog::load_selected_characters(sources.characters,out->stock_);phase("character-artifacts");')
            text=once(text,'    cancelled(stop);out->tracks_=TrackCatalog::load_selected(sources.tracks,out->stock_);',
                '    phase("character-namespace");cancelled(stop);out->tracks_=TrackCatalog::load_selected(sources.tracks,out->stock_);phase("legacy-courses");')
            text=once(text,'    auto music=std::make_shared<MusicLibrary>();',
                '    phase("native-course-namespace");auto music=std::make_shared<MusicLibrary>();')
            text=once(text,'    std::vector<std::shared_ptr<const vehicles::Package>> cars;',
                '    phase("courses-characters-music");\n    std::vector<std::shared_ptr<const vehicles::Package>> cars;')
            text=once(text,'    const auto budget=retention_budget();','    phase("vehicles-and-world");\n    const auto budget=retention_budget();')
        elif name=='party_runtime.cpp':
            # Immutable input has its own reliable/unordered channel. Epoch,
            # lease, immutability and bounded repair history remain unchanged.
            count=len(re.findall(r'Msg::Input,body.b,Lane::Control',text))
            if count!=4:raise ValueError('Party input send surface differs')
            text=text.replace('Msg::Input,body.b,Lane::Control','Msg::Input,body.b,Lane::Authoritative')
            text=once(text,'            if(d.accepted&&chunks<4)if(const auto chunk=d.sender.next_chunk()) {',
                '            const unsigned total_budget=phase==Phase::Admission?16:4;\n'
                '            for(unsigned peer_chunks=0;d.accepted&&chunks<total_budget&&peer_chunks<(phase==Phase::Admission?4U:1U);++peer_chunks) {\n'
                '                const auto chunk=d.sender.next_chunk();if(!chunk)break;')
            text=once(text,'if(party_send(key,id,w.b,Lane::Checkpoint)){d.sender.sent();d.progress=now;++chunks;}',
                'if(party_send(key,id,w.b,Lane::Checkpoint)){d.sender.sent();d.progress=now;++chunks;}else break;')
            text=once(text,'    std::string text="Preparing party play",callback_error;',
                '    Clock::time_point performance_at{};std::uint64_t step_us=0,max_step_us=0;unsigned steps=0,stalls=0;\n'
                '    std::string text="Preparing party play",callback_error;')
            text=once(text,'            const auto step=driver->step(advance);',
                '            const auto step_began=Clock::now();const auto step=driver->step(advance);\n'
                '            const auto cost=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-step_began).count());live_delay.work(cost,step==ex::Step::Replayed,next().frame?next().frame-1:0,step==ex::Step::Replayed||step==ex::Step::Advanced);step_us+=cost;max_step_us=std::max(max_step_us,cost);++steps;if(advance&&step!=ex::Step::Advanced)++stalls;')
            text=once(text,'            (void)stats;',
                '            if(now>=performance_at){performance_at=now+std::chrono::seconds(5);\n'
                '                std::fprintf(stderr,"[party][performance] epoch=%llu next=%u confirmed=%u delay=%u window=%u rollbacks=%u replayed=%u largest=%u predictions=%u step-avg-us=%llu step-max-us=%llu blocked-pulses=%u\\n",static_cast<unsigned long long>(cursor.epoch),after.frame,confirmed(),unsigned(live_delay.lead()),unsigned(live_delay.prediction_window()),stats.rollbacks,stats.replayed_frames,stats.largest_rollback,stats.predicted_frames,static_cast<unsigned long long>(steps?step_us/steps:0),static_cast<unsigned long long>(max_step_us),stalls);\n'
                '                steps=stalls=0;step_us=max_step_us=0;}')
        text=storage_guards(name,beta3(name,text))
        text=remap(text,sources[str(source)])
        if row['target'] in test_bodies:
            before=test_bodies[row['target']]
            body=remap(before['text'],before['deps'])
            def before_relative(m):
                path=Path(m[1]);path=path if path.is_absolute() else before['path'].parent/path
                return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
            body=pattern.sub(before_relative,body)
            body_name='test-only/'+row['target']+'.inl'
            files[body_name]=body.encode()
            text=once(text,'#include DKR_RACE_RELEASE_SOURCE','#include "'+(out/body_name).as_posix()+'"')
        if not required and text==raw.decode().replace('\r\n','\n'):continue
        def relative(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',relative,text,flags=re.M)
        filename=row['target']+'/'+source.name;files[filename]=text.encode()
        swaps.append({**row,'new':str(out/filename),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
    fixture_key='DKRRaceReleaseChecks/runtime_tests.cpp'
    if fixture_key not in files:raise ValueError('The extended party fixture requires the compiled input-header context')
    fixture=files[fixture_key].decode()
    fixture=once(fixture,'}\n}\nint main() {',(AUTH/'tests/party_checks.inl').read_text()+'\n}\n}\nint main() {')
    fixture=once(fixture,'        racer_transitions();','        input_lane_and_reordering();initial_checkpoint_burst();racer_transitions();')
    files['party_checks.cpp']=fixture.encode()
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Performance staging must remain outside checkout')
    rows,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    print('Online performance layer composed; previous protected inputs untouched.')
