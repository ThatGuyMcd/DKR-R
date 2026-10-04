# Consumes NEW Patch Pipeline output only; no submodule/generated input edits.
# Excluded from default builds. The opt-in experimental main-runtime target
# explicitly links this isolated closure; it never interposes native symbols.
function(dkr_add_experimental_owned_game target generated)
    get_filename_component(generated "${generated}" REALPATH)
    if(NOT EXISTS "${generated}/manifest.json")
        message(FATAL_ERROR "Experimental owned game requires new Patch Pipeline output")
    endif()
    file(READ "${generated}/manifest.json" manifest)
    string(JSON revision GET "${manifest}" revision)
    string(JSON full_scenes ERROR_VARIABLE full_scenes_error GET "${manifest}" full_scenes)
    if(full_scenes_error OR NOT full_scenes)
        set(full_scenes 0)
    else()
        set(full_scenes 1)
    endif()
    if(full_scenes AND target STREQUAL "DKRExperimentalOwnedGame")
        string(JSON local_world ERROR_VARIABLE local_world_error GET "${manifest}" local_world_observations)
        if(local_world_error OR NOT local_world)
            message(FATAL_ERROR "Owned runtime requires checked local-world observations. Regenerate into a NEW Patch Pipeline output directory; do not edit existing generated payloads.")
        endif()
    endif()
    string(JSON prefix GET "${manifest}" symbol_prefix)
    if(NOT revision MATCHES "^(77|80)$" OR NOT prefix STREQUAL "dkr_experimental_v${revision}_")
        message(FATAL_ERROR "Experimental owned game requires reviewed revision-specific guest/import symbols")
    endif()
    foreach(field scene_cuts audio_services input_services authored_cpu audio_symbol_isolation)
        string(JSON value GET "${manifest}" ${field})
        if(NOT value)
            message(FATAL_ERROR "Experimental owned game requires ${field}; no stub/unsafe fallback")
        endif()
    endforeach()
    foreach(header isolated_symbols.h isolated_audio_symbols.h private_audio_rsp.cpp)
        if(NOT EXISTS "${generated}/${header}")
            message(FATAL_ERROR "Missing experimental owned payload ${header}")
        endif()
    endforeach()
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(probe "${root}/tests/replay_probe")
    file(GLOB guest CONFIGURE_DEPENDS "${generated}/guest_*.c")
    add_library(${target} STATIC EXCLUDE_FROM_ALL
        "${probe}/probe_bridge.c" "${probe}/probe_native.c" "${probe}/probe_eeprom.cpp"
        "${probe}/probe_policies.cpp" "${probe}/probe_presentation.c" "${probe}/probe_component.c" "${probe}/probe_driver.cpp"
        "${probe}/probe_audio.c" "${probe}/probe_audio_events.c" "${probe}/probe_audio_rsp.cpp"
        "${probe}/probe_input.c" "${probe}/probe_input.cpp" "${probe}/probe_pak.c" "${probe}/probe_pak.cpp"
        "${probe}/probe_magic.c" "${probe}/probe_magic.cpp"
        "${root}/src/game/netplay/experimental_magic_codes.cpp"
        "${root}/src/game/netplay/experimental_pak.cpp"
        "${root}/src/game/dkr_save_codec.cpp"
        "${generated}/blocked_imports.c" "${generated}/private_audio_rsp.cpp" ${guest})
    target_compile_features(${target} PRIVATE c_std_11 PUBLIC cxx_std_20)
    target_include_directories(${target} BEFORE PRIVATE "${generated}" "${probe}")
    target_include_directories(${target} PUBLIC "${probe}")
    target_compile_definitions(${target} PRIVATE DKR_PROBE_REVISION=${revision}
        DKR_PROBE_HAS_SCENE_CUTS=1 DKR_PROBE_HAS_AUDIO=1 DKR_PROBE_HAS_INPUT=1
        DKR_PROBE_HAS_AUTHORED_CPU=1 DKR_PROBE_HAS_FULL_SCENES=${full_scenes}
        DKR_PROBE_HAS_SYMBOL_ISOLATION=1 DKR_PROBE_HAS_AUDIO_ISOLATION=1)
    target_link_libraries(${target} PUBLIC DKRNetplayCore)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG /fp:strict)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG -fno-strict-aliasing
            $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-msse4.1>)
        endif()
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()
