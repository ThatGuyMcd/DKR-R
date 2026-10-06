cmake_minimum_required(VERSION 3.24)
project(DKRGuestReplayProbe LANGUAGES C CXX)
set(DKR_PROBE_GENERATED "" CACHE PATH "Separate Patch Pipeline probe output")
if(NOT EXISTS "${DKR_PROBE_GENERATED}/manifest.json")
    message(FATAL_ERROR "Generate an isolated probe payload first")
endif()
file(READ "${DKR_PROBE_GENERATED}/manifest.json" manifest)
string(JSON full_scenes ERROR_VARIABLE full_scenes_error GET "${manifest}" full_scenes)
if(full_scenes_error OR NOT full_scenes)
    set(full_scenes 0)
else()
    set(full_scenes 1)
endif()
string(JSON audio_services ERROR_VARIABLE audio_services_error GET "${manifest}" audio_services)
if(audio_services_error OR NOT audio_services)
    set(audio_services 0)
else()
    set(audio_services 1)
endif()
string(JSON revision GET "${manifest}" revision)
string(JSON symbol_prefix ERROR_VARIABLE symbol_prefix_error GET "${manifest}" symbol_prefix)
if(symbol_prefix_error OR NOT symbol_prefix)
    set(symbol_isolation 0)
else()
    if(NOT symbol_prefix STREQUAL "dkr_experimental_v${revision}_")
        message(FATAL_ERROR "Unreviewed private guest C symbol prefix")
    endif()
    set(symbol_isolation 1)
endif()
string(JSON audio_symbol_isolation ERROR_VARIABLE audio_isolation_error GET "${manifest}" audio_symbol_isolation)
if(audio_isolation_error OR NOT audio_symbol_isolation)
    set(audio_symbol_isolation 0)
else()
    set(audio_symbol_isolation 1)
endif()
string(JSON authored_cpu ERROR_VARIABLE authored_cpu_error GET "${manifest}" authored_cpu)
if(authored_cpu_error OR NOT authored_cpu)
    set(authored_cpu 0)
else()
    set(authored_cpu 1)
endif()
string(JSON input_services ERROR_VARIABLE input_services_error GET "${manifest}" input_services)
if(input_services_error OR NOT input_services)
    set(input_services 0)
else()
    set(input_services 1)
endif()
string(JSON scene_cuts ERROR_VARIABLE scene_cuts_error GET "${manifest}" scene_cuts)
if(scene_cuts_error OR NOT scene_cuts)
    set(scene_cuts 0)
else()
    set(scene_cuts 1)
endif()
file(GLOB guest_sources CONFIGURE_DEPENDS "${DKR_PROBE_GENERATED}/guest_*.c")
add_executable(DKRGuestReplayProbe probe_bridge.c probe_native.c probe_eeprom.cpp probe_policies.cpp probe_presentation.c probe_main.cpp probe_component.c probe_driver.cpp probe_save.c probe_save.cpp probe_audio.c probe_audio_events.c probe_audio_rsp.cpp probe_audio.cpp probe_input.c probe_input.cpp ${guest_sources}
    probe_pak.c probe_pak.cpp probe_pak_tests.cpp probe_pak_retail.c probe_pak_retail.cpp
    probe_magic.c probe_magic.cpp probe_magic_tests.cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_magic_codes.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/dkr_save_codec.cpp"
    "${DKR_PROBE_GENERATED}/blocked_imports.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/runtime_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_rollback.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_checkpoint_hash.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/local_scenery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_present_gate.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_owner_inputs.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_timeline.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_epoch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_network.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_pump.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_eeprom.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_pak.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/secure_channel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/datagram_socket.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/session_transport.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../../extern/monocypher/src/monocypher.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/replay_qualification.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/rollback_state_store.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/rollback_ring.cpp")
target_compile_features(DKRGuestReplayProbe PRIVATE c_std_11 cxx_std_20)
string(JSON mod_services ERROR_VARIABLE mod_services_error GET "${manifest}" owned_mod_services)
if(NOT mod_services_error AND mod_services)
    get_filename_component(DKRPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    include("${DKRPORT_ROOT}/runtime-recomp/cmake/LegacyMods.cmake")
    target_sources(DKRGuestReplayProbe PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../../src/game/runtime_legacy_mods.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../native_mod_world_test_boundaries.cpp")
    target_link_libraries(DKRGuestReplayProbe PRIVATE DKRLegacyGuestIO DKRLegacyModLaunch)
    target_include_directories(DKRGuestReplayProbe PRIVATE
        "${DKRPORT_ROOT}/extern/n64-modern-runtime/librecomp/include"
        "${DKRPORT_ROOT}/extern/n64-modern-runtime/ultramodern/include"
        "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty/concurrentqueue")
    target_compile_definitions(DKRGuestReplayProbe PRIVATE DKR_PROBE_HAS_MOD_SERVICES=1)
    if(MSVC)
        target_compile_options(DKRGuestReplayProbe PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/EHs;/EHc->")
    endif()
endif()
if(symbol_isolation)
    target_sources(DKRGuestReplayProbe PRIVATE probe_link_canary.c)
endif()
if(audio_symbol_isolation)
    target_sources(DKRGuestReplayProbe PRIVATE probe_audio_link_canary.cpp)
endif()
if(audio_services)
    target_sources(DKRGuestReplayProbe PRIVATE "${DKR_PROBE_GENERATED}/private_audio_rsp.cpp")
    if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(DKRGuestReplayProbe PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-msse4.1>)
    endif()
endif()
target_compile_definitions(DKRGuestReplayProbe PRIVATE DKR_PROBE_REVISION=${revision} DKR_PROBE_HAS_SCENE_CUTS=${scene_cuts} DKR_PROBE_HAS_AUDIO=${audio_services} DKR_PROBE_HAS_INPUT=${input_services} DKR_PROBE_HAS_AUTHORED_CPU=${authored_cpu} DKR_PROBE_HAS_FULL_SCENES=${full_scenes} DKR_PROBE_HAS_SYMBOL_ISOLATION=${symbol_isolation} DKR_PROBE_HAS_AUDIO_ISOLATION=${audio_symbol_isolation})
target_include_directories(DKRGuestReplayProbe PRIVATE "${DKR_PROBE_GENERATED}"
    "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../src/game"
    "${CMAKE_CURRENT_LIST_DIR}/../../../extern/monocypher/src"
    "${CMAKE_CURRENT_LIST_DIR}/../../../extern/n64-modern-runtime/thirdparty")
# Legacy native ABI headers must never take precedence over the generated
# checked CPU/RSP headers. Link canaries intentionally fail on that mistake.
target_include_directories(DKRGuestReplayProbe BEFORE PRIVATE "${DKR_PROBE_GENERATED}")
if(MSVC)
    target_compile_options(DKRGuestReplayProbe PRIVATE /UNDEBUG /fp:strict)
else()
    target_compile_options(DKRGuestReplayProbe PRIVATE -UNDEBUG -fno-strict-aliasing
        $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
    target_link_libraries(DKRGuestReplayProbe PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(DKRGuestReplayProbe PRIVATE ws2_32 bcrypt)
endif()
enable_testing()
find_package(Threads REQUIRED)
target_link_libraries(DKRGuestReplayProbe PRIVATE Threads::Threads)
add_executable(DKRExperimentalPresentationOwnershipTests
    "${CMAKE_CURRENT_LIST_DIR}/../experimental_presentation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/renderer_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_present_gate.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_checkpoint_hash.cpp")
target_compile_features(DKRExperimentalPresentationOwnershipTests PRIVATE cxx_std_20)
target_include_directories(DKRExperimentalPresentationOwnershipTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/game"
    "${CMAKE_CURRENT_LIST_DIR}/../../../extern/n64-modern-runtime/thirdparty")
target_link_libraries(DKRExperimentalPresentationOwnershipTests PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(DKRExperimentalPresentationOwnershipTests PRIVATE /UNDEBUG)
else()
    target_compile_options(DKRExperimentalPresentationOwnershipTests PRIVATE -UNDEBUG)
endif()
add_test(NAME ExperimentalPresentationOwnership COMMAND DKRExperimentalPresentationOwnershipTests)
add_executable(DKRExperimentalMagicCodeOwnershipTests
    "${CMAKE_CURRENT_LIST_DIR}/../experimental_magic_code_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_magic_codes.cpp")
target_compile_features(DKRExperimentalMagicCodeOwnershipTests PRIVATE cxx_std_20)
target_include_directories(DKRExperimentalMagicCodeOwnershipTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/game")
if(MSVC)
    target_compile_options(DKRExperimentalMagicCodeOwnershipTests PRIVATE /UNDEBUG)
else()
    target_compile_options(DKRExperimentalMagicCodeOwnershipTests PRIVATE -UNDEBUG)
endif()
add_test(NAME ExperimentalMagicCodeOwnership COMMAND DKRExperimentalMagicCodeOwnershipTests)
add_executable(DKRExperimentalPakOwnershipTests
    "${CMAKE_CURRENT_LIST_DIR}/../experimental_pak_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_pak.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_rollback.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_checkpoint_hash.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/replay_qualification.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/rollback_state_store.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/rollback_ring.cpp")
target_compile_features(DKRExperimentalPakOwnershipTests PRIVATE cxx_std_20)
target_include_directories(DKRExperimentalPakOwnershipTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/game"
    "${CMAKE_CURRENT_LIST_DIR}/../../../extern/n64-modern-runtime/thirdparty")
if(MSVC)
    target_compile_options(DKRExperimentalPakOwnershipTests PRIVATE /UNDEBUG)
else()
    target_compile_options(DKRExperimentalPakOwnershipTests PRIVATE -UNDEBUG)
endif()
add_test(NAME ExperimentalPakOwnership COMMAND DKRExperimentalPakOwnershipTests)
add_test(NAME RealGuestProbeClosure COMMAND DKRGuestReplayProbe --self-test)
set_tests_properties(RealGuestProbeClosure PROPERTIES TIMEOUT 20)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ReplayProbePipelineControlFlow COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_generator.py")
set_tests_properties(ReplayProbePipelineControlFlow PROPERTIES TIMEOUT 20)

add_executable(DKRExperimentalProbeDriverTests
    "${CMAKE_CURRENT_LIST_DIR}/../experimental_rollback_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_rollback.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/experimental_checkpoint_hash.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/replay_qualification.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/rollback_state_store.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/rollback_ring.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/netplay_protocol.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game/netplay/netplay_types.cpp")
target_compile_features(DKRExperimentalProbeDriverTests PRIVATE cxx_std_20)
target_include_directories(DKRExperimentalProbeDriverTests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../src/game"
    "${CMAKE_CURRENT_LIST_DIR}/../../../extern/n64-modern-runtime/thirdparty")
if(MSVC)
    target_compile_options(DKRExperimentalProbeDriverTests PRIVATE /UNDEBUG)
else()
    target_compile_options(DKRExperimentalProbeDriverTests PRIVATE -UNDEBUG)
endif()
add_test(NAME ExperimentalProbeDriver COMMAND DKRExperimentalProbeDriverTests)
