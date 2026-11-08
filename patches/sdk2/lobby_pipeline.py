"""Development-lobby admission, consent and immutable save routing."""
FILES=('src/game/netplay/netplay_types.hpp','src/game/netplay/netplay_types.cpp',
       'src/game/netplay/netplay_protocol.cpp','src/game/netplay/direct_session.cpp',
       'src/game/runtime_ui.cpp','src/game/runtime_online_ui.inl','src/game/runtime_online_mods_ui.inl',
       'src/game/runtime_code_mods.cpp','src/game/game_main.cpp','src/game/game_registration.cpp',
       'src/game/runtime_legacy_mods.cpp','src/game/runtime_legacy_mods.hpp',
       'src/game/runtime_save_routing.hpp','src/game/runtime_save_routing.cpp',
       'tests/replay_probe/owned_game.hpp','src/game/netplay/experimental_pump.hpp',
       'src/game/netplay/experimental_pump.cpp')


def transform(name,text,once):
    def one(a,b):
        nonlocal text
        text=once(text,a,b)
    if name.endswith('netplay_types.hpp'):
        one('kProtocolVersion = 50U;', 'kProtocolVersion = 51U;')
        one('    bool record_replay = true;', '    bool record_replay = true;\n    bool development = false;')
        one('    constexpr bool occupied(std::size_t slot) const {','    bool development = false;\n\n    constexpr bool occupied(std::size_t slot) const {')
    elif name.endswith('netplay_types.cpp'):
        one('        descriptor.host_control > HostControlPolicy::EveryAssignedPort ||', '''        (descriptor.development&&descriptor.synchronization!=SynchronizationMode::ExperimentalRollback) ||
        descriptor.host_control > HostControlPolicy::EveryAssignedPort ||''')
        one('    append(static_cast<std::uint8_t>(descriptor.host_control), 1U);', '    append(static_cast<std::uint8_t>(descriptor.host_control), 1U);\n    append(descriptor.development,1U);')
        one('    return rules.maximum_players >= 2U', '    return (!rules.development||rules.synchronization==SynchronizationMode::ExperimentalRollback)&&rules.maximum_players >= 2U')
    elif name.endswith('netplay_protocol.cpp'):
        one('    rule_flags |= payload.rules.record_replay ? 4U : 0U;', '    rule_flags |= payload.rules.record_replay ? 4U : 0U;\n    rule_flags |= payload.rules.development ? 8U : 0U;')
        one('(rule_flags & ~7U)', '(rule_flags & ~15U)')
        one('    payload.rules.record_replay = (rule_flags & 4U) != 0U;', '    payload.rules.record_replay = (rule_flags & 4U) != 0U;\n    payload.rules.development=(rule_flags&8U)!=0U;')
        one('    out.reserve(39U);','    out.reserve(40U);')
        one('    out.push_back(static_cast<std::uint8_t>(payload.descriptor.host_control));', '    out.push_back(static_cast<std::uint8_t>(payload.descriptor.host_control));\n    out.push_back(payload.descriptor.development?1U:0U);')
        one('bytes.size() != 39U', 'bytes.size() != 40U')
        one('    if (!Take64(bytes, cursor, payload.descriptor_hash) ||', '''    if(bytes[cursor]>1){error="Invalid development-lobby flag.";return false;}
    payload.descriptor.development=bytes[cursor++]!=0;
    if (!Take64(bytes, cursor, payload.descriptor_hash) ||''')
    elif name.endswith('direct_session.cpp'):
        one('    descriptor.host_control = room.rules.host_control;', '    descriptor.host_control = room.rules.host_control;\n    descriptor.development=room.rules.development;')
    elif name.endswith('runtime_ui.cpp'):
        one('#include "runtime_ui.hpp"','#include "runtime_ui.hpp"\n#include <bit>')
        one('bool g_online_record_replay = true;', 'bool g_online_record_replay = true;\nbool g_online_development=false;\nbool g_online_development_consent=false;\nstd::uint64_t g_online_development_room=0;')
        one('bool g_online_development=false;', 'bool g_online_development=false;\nint g_sdk2_tools_key=SDL_SCANCODE_F2;\nbool g_sdk2_key_capture=false,g_sdk2_tools_chord=true;\nstd::map<SDL_JoystickID,unsigned> g_sdk2_chord_held;')
        one('        output << "online_record_replay="', '        output << "online_development=" << (g_online_development?1:0) << \'\\n\';\n        output << "online_record_replay="')
        # Keep this out of the historic deeply nested numeric else-if chain:
        # one more branch exceeds MSVC's block nesting limit.
        one('        if(key=="online_local_view") {', '        if(key=="online_development"){g_online_development=value=="1";continue;}\n        if(key=="online_local_view") {')
        one('        if(key=="online_development")', '        if(key=="sdk2_tools_key"){int scancode=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),scancode);if(parsed.ec==std::errc{}&&scancode>0&&scancode<SDL_NUM_SCANCODES)g_sdk2_tools_key=scancode;continue;}\n        if(key=="sdk2_tools_chord"){g_sdk2_tools_chord=value=="1";continue;}\n        if(key=="online_development")')
        one('        output << "online_development="', '        output << "sdk2_tools_key=" << g_sdk2_tools_key << \'\\n\';\n        output << "sdk2_tools_chord=" << (g_sdk2_tools_chord?1:0) << \'\\n\';\n        output << "online_development="')
    elif name.endswith('runtime_online_ui.inl'):
        one('        if (changed) SaveSettings();\n    });\n}\n\n// ------------------------------------------------------------ online profile', '''        PaddockGap(16.0F);
        changed |= OlSwitchRow("##development-lobby", "Development lobby",
            "Allows host tool commands. All racers must agree before readying. Progress is temporary.",
            &g_online_development,inner,CurrentOnlineSynchronization()!=SynchronizationMode::ExperimentalRollback);
        if (changed) SaveSettings();
    });
}

// ------------------------------------------------------------ online profile''')
        one('    const SessionView& view = frame.view;\n    DirectSession& online = session();\n    const int maximum', '''    const SessionView& view = frame.view;
    DirectSession& online = session();
    const auto room_key=stable_hash(view.room.room_id);
    if(g_online_development_room!=room_key){g_online_development_room=room_key;g_online_development_consent=false;}
    if(view.room.rules.development) {
        OlParagraph("DEVELOPMENT LOBBY - The host can change gameplay with development tools. Progress is temporary.",14.0F,kOlError,width,true);
        ImGui::Checkbox("I agree to development commands for this lobby",&g_online_development_consent);
    }
    const int maximum''')
        one('                               local != nullptr;','                               local != nullptr && (!view.room.rules.development||g_online_development_consent);')
    elif name.endswith('runtime_online_mods_ui.inl'):
        one('    request.rules.record_replay=g_online_record_replay;', '    request.rules.record_replay=g_online_record_replay;\n    request.rules.development=g_online_development&&request.rules.synchronization==SynchronizationMode::ExperimentalRollback;')
    elif name.endswith('runtime_code_mods.cpp'):
        one('void reset(){ordinary.store(nullptr);','void reset(){dkr::sdk2::end();ordinary.store(nullptr);')
        one('    std::uint64_t result=0;\n    if(bound->world->dispatch(', '''    // The ordinary game worker yields through its existing scheduler at a
    // retired main-loop boundary. UI/audio/VI workers are never blocked here.
    if(!leaving&&bound->world->sdk2_frame_starts_at(address))for(;;) {
        const int status=bound->world->sdk2_dispatch_pulse();if(status<0)stop(bound->world->error());if(status)break;
        ultramodern::wait_for_external_message_timed(ram,16);ultramodern::check_running_queue(ram);
    }
    std::uint64_t result=0;
    if(bound->world->dispatch(''')
        one('    legacy::set_trophy_menu(', '    next->world->sdk2_current_course([]{return legacy::current_course();});\n    if(next->world->sdk2_wants_assets())next->world->sdk2_assets(legacy::asset_inventory());\n    legacy::set_trophy_menu(')
    elif name.endswith('runtime_legacy_mods.hpp'):
        one('std::string selected_trophy();', 'std::string selected_trophy();\nstd::string current_course();')
        one('std::string current_course();','std::string current_course();\nstd::string asset_inventory();')
        one('    std::string selected_trophy()const;', '    std::string selected_trophy()const;\n    std::string current_course()const;')
        one('    std::string current_course()const;','    std::string current_course()const;\n    std::string asset_inventory()const;')
    elif name.endswith('runtime_legacy_mods.cpp'):
        one('#include "runtime_legacy_mods.hpp"','#include "runtime_legacy_mods.hpp"\n#include "mods/magic_code_package.hpp"')
        one('std::string selected_trophy() {', 'std::string current_course(){const auto active=session.load();return active?active->current_content():std::string{};}\nstd::string selected_trophy() {')
        one('std::string current_course(){', '#include "mods/sdk2_asset_inventory.inl"\nstd::string asset_inventory(){return sdk2_asset_inventory(session.load());}\nstd::string current_course(){')
        one('std::string ModWorld::selected_trophy()const {','std::string ModWorld::current_course()const {return state_->session.load()->current_content();}\nstd::string ModWorld::selected_trophy()const {')
        one('std::string ModWorld::current_course()const {','std::string ModWorld::asset_inventory()const {return sdk2_asset_inventory(state_->session.load());}\nstd::string ModWorld::current_course()const {')
    elif name.endswith('game_registration.cpp'):
        start=text.index('    } else if(const auto mods=dkr::runtime::legacy::prepared_launch();')
        end=text.index('    dkr::runtime::code_mods::boot(rdram,recomp::mem_size);',start)
        # prepare_runtime_save_context is the only route selector. init_saving
        # has already loaded that leased path before this guest entrypoint.
        # Re-selecting PreparedModLaunch::save_subfolder here overwrote the
        # SDK 2 development decision and tripped the immutable-route guard.
        text=text[:start]+'''    } else {
        // runtime_save_context_ready() verified the actual loaded EEPROM
        // against its immutable launch owner above. Keep that exact route:
        // ordinary, isolated mod-set, or isolated development save.
        std::fprintf(stderr,"[save][boot] frozen offline EEPROM verified; no mutable route switch\\n");
    }
'''+text[end:]
    elif name.endswith('game_main.cpp'):
        one('            selecting_save_owner = true;', '''            const bool development_code=code_set&&std::any_of(code_set->ordered.begin(),code_set->ordered.end(),[](const auto& p){return p->development;});
              const bool development_session=online_save.active?(online_save.launch_descriptor&&online_save.launch_descriptor->development):development_code;
              if(development_code&&online_save.active&&!development_session)throw dkr::mods::Error("Enable Development lobby before using development code mods online.");
              const auto developer_folder=development_session?std::filesystem::path("development")/(code_set?code_set->identity.substr(0,24):"online-"+std::to_string(online_save.launch_descriptor->match_id)):prepared_mods->save_subfolder;
              const auto developer_paks=development_session?config_directory/"saves"/developer_folder/"paks":prepared_mods->pak_directory;
              selecting_save_owner = true;''')
        one('                    prepared_mods->save_subfolder, prepared_mods->pak_directory,','                    developer_folder, developer_paks,')
        one('                    online_save.online_save_hash, rom_error))', '                    online_save.online_save_hash, rom_error,development_session))')
    elif name.endswith('runtime_save_routing.hpp'):
        one('std::uint64_t match_id, std::uint64_t expected_hash, std::string& error);','std::uint64_t match_id, std::uint64_t expected_hash, std::string& error,bool development=false);')
    elif name.endswith('runtime_save_routing.cpp'):
        one('bool online = false, host = false, loaded = false, incomplete = false;', 'bool online = false, host = false, loaded = false, incomplete = false, development = false;')
        one('bool online, bool host, std::uint64_t match, std::uint64_t hash, std::string& error) {', 'bool online, bool host, std::uint64_t match, std::uint64_t hash, std::string& error,bool development) {')
        one('    g_context.online = online; g_context.host = host;', '    g_context.development=development;\n    if(development)g_context.paks=storage::absolute(paks);\n    g_context.online = online; g_context.host = host;')
        one('    return g_context.lease && g_context.loaded && g_context.failure.empty();', '''    if(!g_context.lease||!g_context.loaded||!g_context.failure.empty())return false;
    // Verify what init_saving actually loaded, not a second proposed route.
    // On mismatch retain the scoped failure for the launcher's recovery UI.
    // Never fall back to a user's ordinary save or release the existing lease.
    if(storage::absolute(ultramodern::get_save_file_path())!=g_context.path) {
        g_context.failure="The loaded EEPROM does not match its frozen launch save owner.";
        std::fprintf(stderr,"[save][boot] loaded EEPROM differs from its frozen save owner\\n");
        return false;
    }
    return true;''')
        one('    std::string error;\n    if (!storage::write_atomic(path, bytes, ValidRuntimeImage, error)) {', '''    // Online development starts from the authenticated seed, but never writes
    // experimental progress into its normal durable profile. The owned writer
    // follows the same policy and returns the original seed to the lobby.
    if(g_context.development&&g_context.online){g_context.incomplete=false;return true;}
    std::string error;
    if (!storage::write_atomic(path, bytes, ValidRuntimeImage, error)) {''')
    elif name.endswith('owned_game.hpp'):
        one('    std::uint64_t code_seed=0x444B52434F444531ULL;', '    std::uint64_t code_seed=0x444B52434F444531ULL;\n    bool development=false;')
    elif name.endswith('experimental_pump.hpp'):
        one('    const PumpView& view() const', '    void command_source(std::function<dkr::sdk2::Command()> source){commands_=std::move(source);}\n    const PumpView& view() const')
        one('    PumpView view_{};', '    PumpView view_{};\n    std::function<dkr::sdk2::Command()> commands_;')
    elif name.endswith('experimental_pump.cpp'):
        one('session_.sample_local(sample(),true)', 'session_.sample_local(sample(),true,commands_?commands_():dkr::sdk2::Command{})')
    else:raise ValueError('Unreviewed development-lobby source: '+name)
    return text
