cmake_minimum_required(VERSION 3.24)

project(DKRRuntimePreparation LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" DKR_NETWORK_RELEASE_VERSION
     LIMIT_COUNT 1)
if(DKR_NETWORK_RELEASE_VERSION STREQUAL "")
    set(DKR_NETWORK_RELEASE_VERSION "development")
endif()

set(DKR_RELEASE_VERSION "" CACHE STRING "Public DKR-R release version")
if(DKR_RELEASE_VERSION STREQUAL "")
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" DKR_RELEASE_VERSION
         LIMIT_COUNT 1)
endif()
if(DKR_RELEASE_VERSION STREQUAL "")
    set(DKR_RELEASE_VERSION "development")
endif()
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" DKR_RELEASE_VERSION_NUMERIC
       "${DKR_RELEASE_VERSION}")
if(DKR_RELEASE_VERSION_NUMERIC STREQUAL "")
    set(DKR_RELEASE_VERSION_NUMERIC "1.0.4")
endif()
string(REGEX MATCH "-rc([0-9]+)$" DKR_RELEASE_RC_MATCH
       "${DKR_RELEASE_VERSION}")
if(DKR_RELEASE_RC_MATCH)
    set(DKR_RELEASE_BUILD_NUMBER "${CMAKE_MATCH_1}")
else()
    set(DKR_RELEASE_BUILD_NUMBER "1")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(DKRPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Root of the DKR Port repository")
option(DKR_RUNTIME_BUILD_GENERATED "Compile N64Recomp-generated DKR CPU functions" ON)
option(DKR_RUNTIME_BUILD_RT64 "Also configure and compile the pinned RT64 renderer" OFF)
option(DKR_LEGACY_QUALIFICATION "Build private real-runtime legacy scene qualification hooks (not for packaging)" OFF)
option(DKR_REPLAY_QUALIFICATION "Private real-game replay fixtures; never package" OFF)
option(DKR_EXPERIMENTAL_RENDER_QUALIFICATION "PRIVATE generation-retirement GPU probe; never package" OFF)
option(DKR_EXPERIMENTAL_RACE_TEST "Opt-in owned rollback backend in the existing launcher (x64)" OFF)
if(DKR_EXPERIMENTAL_RACE_TEST AND (ANDROID OR APPLE OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$"))
    message(FATAL_ERROR "The first experimental race test is Windows/Linux x64 only")
endif()
set(DKR_PATCH_POLICY_V77
    "${CMAKE_CURRENT_LIST_DIR}/dkr.us.v77.recomp-policy.json")
set(DKR_PATCH_POLICY_V80
    "${CMAKE_CURRENT_LIST_DIR}/dkr.us.v80.recomp-policy.json")
foreach(DKR_PATCH_POLICY_FILE IN ITEMS
        "${DKR_PATCH_POLICY_V77}" "${DKR_PATCH_POLICY_V80}")
    if(NOT EXISTS "${DKR_PATCH_POLICY_FILE}")
        message(FATAL_ERROR "Missing Patch Pipeline policy: ${DKR_PATCH_POLICY_FILE}")
    endif()
endforeach()
file(SHA256 "${DKR_PATCH_POLICY_V77}" DKR_PATCH_POLICY_V77_SHA256)
file(SHA256 "${DKR_PATCH_POLICY_V80}" DKR_PATCH_POLICY_V80_SHA256)
set(DKR_PATCH_POLICY_SHA256
    "v77:${DKR_PATCH_POLICY_V77_SHA256}|v80:${DKR_PATCH_POLICY_V80_SHA256}")
set(DKR_TEST_ROM_V77 "" CACHE FILEPATH "Optional local US v1.0 ROM for revision tests")
set(DKR_TEST_ROM_V80_Z64 "" CACHE FILEPATH "Optional local US Rev A z64 ROM for revision tests")
set(DKR_TEST_ROM_V80_SWAPPED "" CACHE FILEPATH "Optional local US Rev A byte-swapped ROM for revision tests")
get_filename_component(DKRPORT_ROOT "${DKRPORT_ROOT}" ABSOLUTE)
set(DKR_MODERN_RUNTIME_SOURCE "${DKRPORT_ROOT}/extern/n64-modern-runtime")
set(DKR_RECOMP_SOURCE "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp")
set(DKR_RT64_SOURCE "${DKRPORT_ROOT}/extern/rt64")
set(DKR_GEKKONET_SOURCE "${DKRPORT_ROOT}/extern/gekkonet")
set(DKR_MONOCYPHER_SOURCE "${DKRPORT_ROOT}/extern/monocypher")
set(DKR_LIBDATACHANNEL_SOURCE "${DKRPORT_ROOT}/extern/libdatachannel")
set(DKR_MBEDTLS_SOURCE "${DKRPORT_ROOT}/extern/mbedtls")
set(DKR_SDL3_SOURCE "${DKRPORT_ROOT}/extern/sdl3")
if(ANDROID)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/AndroidPlatform.cmake")
endif()
option(DKR_RUNTIME_BUILD_SDL3_INPUT_HOST
    "Build the isolated SDL3 native controller and sensor host" ON)
set(DKR_GENERATED_SOURCE_V77 "${CMAKE_CURRENT_LIST_DIR}/RecompiledFuncs" CACHE PATH
    "N64Recomp-generated CPU source for DKR US v1.0")
set(DKR_GENERATED_SOURCE_V80 "" CACHE PATH
    "N64Recomp-generated CPU source for DKR US Rev A/v1.1")
set(DKR_GENERATED_RSP_SOURCE "${CMAKE_CURRENT_LIST_DIR}/RecompiledRSP" CACHE PATH
    "RSPRecomp-generated source for the selected DKR revision")
set(DKR_GENERATED_FILE_COUNT 0)

if(DKR_RUNTIME_BUILD_GENERATED)
    if(DKR_GENERATED_SOURCE_V80 STREQUAL "")
        message(FATAL_ERROR
            "DKR_GENERATED_SOURCE_V80 must name the generated US Rev A/v1.1 "
            "CPU source directory. Both revision payloads are linked into the "
            "same DKR-R executable.")
    endif()
    get_filename_component(DKR_GENERATED_SOURCE_V77_ABSOLUTE
        "${DKR_GENERATED_SOURCE_V77}" ABSOLUTE)
    get_filename_component(DKR_GENERATED_SOURCE_V80_ABSOLUTE
        "${DKR_GENERATED_SOURCE_V80}" ABSOLUTE)
    if(DKR_GENERATED_SOURCE_V77_ABSOLUTE STREQUAL
       DKR_GENERATED_SOURCE_V80_ABSOLUTE)
        message(FATAL_ERROR
            "The v1.0 and Rev A CPU payloads must come from different generated "
            "source directories; protected generated output is never rewritten.")
    endif()
endif()

if(NOT EXISTS "${DKR_MODERN_RUNTIME_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR
        "N64ModernRuntime has not been prepared at ${DKR_MODERN_RUNTIME_SOURCE}. "
        "Run Build-DKR-Runtime.cmd first.")
endif()
if(NOT EXISTS "${DKR_RECOMP_SOURCE}/include/recomp.h")
    message(FATAL_ERROR
        "The N64Recomp submodule pinned by N64ModernRuntime is missing at ${DKR_RECOMP_SOURCE}. "
        "Run Build-DKR-Runtime.cmd first.")
endif()

# N64ModernRuntime exports the ultramodern and librecomp targets used by
# production recompilation projects. It carries GPL-3.0 licensing; see the
# runtime preparation notice before distributing a linked executable.
add_subdirectory("${DKR_MODERN_RUNTIME_SOURCE}" "${CMAKE_BINARY_DIR}/n64-modern-runtime")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/PerformancePatches.cmake")
if(MSVC)
    # The pinned dependencies enable multi-processor compilation internally.
    # Serialize writes to each target PDB from the project boundary so clean
    # release builds remain reliable without modifying either submodule.
    foreach(DKR_PDB_SAFE_TARGET IN ITEMS N64Recomp ultramodern librecomp)
        if(TARGET ${DKR_PDB_SAFE_TARGET})
            target_compile_options(${DKR_PDB_SAFE_TARGET} PRIVATE /FS)
        endif()
    endforeach()
endif()

if(NOT EXISTS "${DKR_GEKKONET_SOURCE}/GekkoLib/CMakeLists.txt")
    message(FATAL_ERROR
        "GekkoNet has not been prepared at ${DKR_GEKKONET_SOURCE}. "
        "Run Build-DKR-Runtime.cmd first.")
endif()
set(NO_ASIO_BUILD ON CACHE BOOL "DKR-R supplies its own authenticated transport" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "Do not build dependency documentation" FORCE)
add_subdirectory("${DKR_GEKKONET_SOURCE}" "${CMAKE_BINARY_DIR}/gekkonet")
if(MSVC)
    # DKR-R and N64ModernRuntime use the DLL CRT. GekkoNet's upstream static
    # target defaults to /MT, which is unsafe once rollback pulls its objects
    # into the final executable. Align the target at the project boundary;
    # never modify the pinned dependency itself.
    set_property(TARGET GekkoNet PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set(CMAKE_CXX_STANDARD 20)

if(NOT EXISTS "${DKR_MONOCYPHER_SOURCE}/src/monocypher.c")
    message(FATAL_ERROR
        "Monocypher has not been prepared at ${DKR_MONOCYPHER_SOURCE}. "
        "Run Build-DKR-Runtime.cmd first.")
endif()
add_library(DKRNetplayCrypto STATIC
    "${DKR_MONOCYPHER_SOURCE}/src/monocypher.c"
)

option(DKR_NETPLAY_WEBRTC "Build the optional five-character Quick Join transport" ON)
if(DKR_NETPLAY_WEBRTC)
    if(NOT EXISTS "${DKR_LIBDATACHANNEL_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "libdatachannel has not been prepared at ${DKR_LIBDATACHANNEL_SOURCE}. "
            "Run the pinned dependency preparation step first, or configure "
            "DKR_NETPLAY_WEBRTC=OFF for a UDP-only developer build.")
    endif()
    if(NOT EXISTS "${DKR_MBEDTLS_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "Mbed TLS has not been prepared at ${DKR_MBEDTLS_SOURCE}. "
            "Run the pinned dependency preparation step first.")
    endif()
    set(DKR_SAVED_SKIP_INSTALL_RULES "${CMAKE_SKIP_INSTALL_RULES}")
    set(CMAKE_SKIP_INSTALL_RULES ON)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "Do not build Mbed TLS programs" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "Do not build Mbed TLS tests" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "Dependency warnings are not fatal" FORCE)
    set(MBEDTLS_USER_CONFIG_FILE
        "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay/mbedtls_dkrr_user_config.h"
        CACHE FILEPATH "DKR-R Mbed TLS protocol extensions" FORCE)
    add_subdirectory("${DKR_MBEDTLS_SOURCE}"
                     "${CMAKE_BINARY_DIR}/mbedtls" EXCLUDE_FROM_ALL)
    if(NOT TARGET MbedTLS::MbedTLS)
        add_library(MbedTLS::MbedTLS ALIAS mbedtls)
    endif()
    if(NOT TARGET MbedTLS::MbedCrypto)
        add_library(MbedTLS::MbedCrypto ALIAS mbedcrypto)
    endif()
    if(NOT TARGET MbedTLS::MbedX509)
        add_library(MbedTLS::MbedX509 ALIAS mbedx509)
    endif()
    set(NO_MEDIA ON CACHE BOOL "DKR-R uses WebRTC data channels only" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "Do not build libdatachannel examples" FORCE)
    set(NO_TESTS ON CACHE BOOL "Do not build libdatachannel dependency tests" FORCE)
    set(USE_NICE OFF CACHE BOOL "Use libjuice ICE backend" FORCE)
    set(USE_MBEDTLS ON CACHE BOOL "Use the pinned static Mbed TLS provider" FORCE)
    add_subdirectory("${DKR_LIBDATACHANNEL_SOURCE}"
                     "${CMAKE_BINARY_DIR}/libdatachannel")
    if(UNIX AND TARGET juice)
        # libjuice compiles its POSIX implementation as strict C11. glibc
        # consequently hides the standard recursive-mutex and getnameinfo
        # declarations unless a feature profile is selected. Apply that
        # requirement at our dependency boundary so the pinned source remains
        # untouched and Windows/macOS builds are unaffected.
        target_compile_definitions(juice PRIVATE _DEFAULT_SOURCE)
    endif()
    set(CMAKE_SKIP_INSTALL_RULES "${DKR_SAVED_SKIP_INSTALL_RULES}")
    unset(DKR_SAVED_SKIP_INSTALL_RULES)
endif()
target_include_directories(DKRNetplayCrypto PUBLIC
    "${DKR_MONOCYPHER_SOURCE}/src"
)

add_library(DKRNetplayCore STATIC
    src/game/netplay/authoritative_state.cpp
    src/game/netplay/authoritative_state_codec.cpp
    src/game/determinism_state_hash.cpp
    src/game/netplay/datagram_socket.cpp
    src/game/netplay/direct_session.cpp
    src/game/netplay/failure_recorder.cpp
    src/game/netplay/friend_service.cpp
    src/game/netplay/gekko_direct_adapter.cpp
    src/game/netplay/lan_discovery.cpp
    src/game/netplay/netplay_lobby.cpp
    src/game/netplay/online_input_broker.cpp
    src/game/netplay/netplay_protocol.cpp
    src/game/netplay/netplay_timeline.cpp
    src/game/netplay/netplay_types.cpp
    src/game/netplay/network_interfaces.cpp
    src/game/netplay/port_mapping.cpp
    src/game/netplay/replay_recorder.cpp
    src/game/netplay/quick_join_transport.cpp
    src/game/netplay/rollback_ring.cpp
    src/game/netplay/rollback_simulation_state.cpp
    src/game/netplay/rollback_state_store.cpp
    src/game/netplay/experimental_rollback.cpp
    src/game/netplay/experimental_checkpoint_hash.cpp
    src/game/netplay/experimental_owner_inputs.cpp
    src/game/netplay/experimental_timeline.cpp
    src/game/netplay/experimental_epoch.cpp
    src/game/netplay/experimental_session.cpp
    src/game/netplay/experimental_network.cpp
    src/game/netplay/experimental_launch.cpp
    src/game/netplay/experimental_lobby_admission.cpp
    src/game/netplay/experimental_pump.cpp
    src/game/netplay/experimental_eeprom.cpp
    src/game/netplay/experimental_presentation.cpp
    src/game/netplay/local_scenery.cpp
    src/game/netplay/experimental_present_gate.cpp
    src/game/netplay/replay_qualification.cpp
    src/game/netplay/runtime_state.cpp
    src/game/netplay/secure_channel.cpp
    src/game/netplay/session_transport.cpp
    src/game/netplay/windows_firewall.cpp
)
target_include_directories(DKRNetplayCore PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src/game"
    "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay"
    "${DKR_RECOMP_SOURCE}/include"
)
target_include_directories(DKRNetplayCore PRIVATE "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
target_link_libraries(DKRNetplayCore PUBLIC GekkoNet DKRNetplayCrypto)
if(DKR_NETPLAY_WEBRTC)
    target_link_libraries(DKRNetplayCore PUBLIC datachannel)
    target_include_directories(DKRNetplayCore PRIVATE
        "${DKR_LIBDATACHANNEL_SOURCE}/deps/json/include")
    target_compile_definitions(DKRNetplayCore PUBLIC DKR_NETPLAY_WEBRTC=1)
else()
    target_compile_definitions(DKRNetplayCore PUBLIC DKR_NETPLAY_WEBRTC=0)
endif()
if(WIN32)
    target_link_libraries(DKRNetplayCore PUBLIC ws2_32 bcrypt iphlpapi ole32 oleaut32 shell32)
endif()

if(DKR_RUNTIME_BUILD_RT64 AND DKR_RUNTIME_BUILD_SDL3_INPUT_HOST)
    if(NOT EXISTS "${DKR_SDL3_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "SDL3 input support requires the pinned extern/sdl3 checkout. "
            "Run the dependency preparation.")
    endif()
    set(SDL_SHARED ON CACHE BOOL "Build the SDL3 runtime library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Do not build a duplicate static SDL3" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL3 test support" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Disable SDL3 tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL3 examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Do not install embedded SDL3" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "Do not create SDL3 uninstall target" FORCE)
    add_subdirectory("${DKR_SDL3_SOURCE}" "${CMAKE_BINARY_DIR}/sdl3"
                     EXCLUDE_FROM_ALL)
endif()

if(DKR_RUNTIME_BUILD_RT64 AND DKR_RUNTIME_BUILD_SDL3_INPUT_HOST)
    add_executable(DKRSdl3InputHost
        src/input/sdl3_input_host.cpp
    )
    target_include_directories(DKRSdl3InputHost PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    target_compile_definitions(DKRSdl3InputHost PRIVATE NOMINMAX)
    target_link_libraries(DKRSdl3InputHost PRIVATE SDL3::SDL3-shared)
    set_target_properties(DKRSdl3InputHost PROPERTIES
        OUTPUT_NAME "DKR-R-InputHost"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/libexec/dkr-r"
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(DKRSdl3InputHost PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper}
                "${CMAKE_BINARY_DIR}/bin/${config}/libexec/dkr-r"
        )
    endforeach()
    if(WIN32)
        target_link_libraries(DKRSdl3InputHost PRIVATE ws2_32)
        add_custom_command(TARGET DKRSdl3InputHost POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3-shared>"
                "$<TARGET_FILE_DIR:DKRSdl3InputHost>/SDL3.dll"
            VERBATIM
        )
    else()
        add_custom_command(TARGET DKRSdl3InputHost POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_SONAME_FILE:SDL3-shared>"
                "$<TARGET_FILE_DIR:DKRSdl3InputHost>/$<TARGET_SONAME_FILE_NAME:SDL3-shared>"
            VERBATIM
        )
    endif()
endif()

option(DKR_ANDROID_RENDER_QUALIFICATION "Private desktop qualification of Android render-pass stages; never package" OFF)
if(DKR_RUNTIME_BUILD_RT64)
    if(NOT EXISTS "${DKR_RT64_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "RT64 has not been prepared at ${DKR_RT64_SOURCE}. "
            "Run Prepare-DKR-Runtime.cmd first.")
    endif()
    set(RT64_STATIC TRUE CACHE BOOL "Build RT64 as a static runtime dependency" FORCE)
    if(ANDROID)
        set(RT64_SDL_WINDOW_VULKAN FALSE CACHE BOOL "Android uses ANativeWindow" FORCE)
    else()
        set(RT64_SDL_WINDOW_VULKAN TRUE CACHE BOOL "Allow SDL/Vulkan window integration" FORCE)
    endif()
    add_compile_definitions(HLSL_CPU)
    add_subdirectory("${DKR_RT64_SOURCE}" "${CMAKE_BINARY_DIR}/rt64")
    if(ANDROID)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set_property(TARGET file_to_c PROPERTY CROSSCOMPILING_EMULATOR
            "${Python3_EXECUTABLE};${DKRPORT_ROOT}/scripts/run_host_shader_tool.py;${DKR_HOST_FILE_TO_C}")
        target_sources(nfd PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/android/android_file_dialog.cpp")
        target_link_libraries(nfd PRIVATE SDL2::SDL2)
        get_target_property(DKR_RT64_SOURCES rt64 SOURCES)
        list(FILTER DKR_RT64_SOURCES EXCLUDE REGEX "(^|/)rt64_application_window\\.cpp$")
        set_property(TARGET rt64 PROPERTY SOURCES "${DKR_RT64_SOURCES}")
        target_sources(rt64 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/android/android_rt64_window.cpp")
        target_link_libraries(rt64 SDL2::SDL2 android log)
        include("${CMAKE_CURRENT_LIST_DIR}/cmake/android/RendererPatches.cmake")
    endif()
    # Android's RGBA patch and the resource budget compose on the same staged
    # application source. Desktop starts from the unchanged dependency source.
    if(ANDROID)
        set(DKR_RESOURCE_PATCH_SOURCE "${CMAKE_BINARY_DIR}/generated/android-renderer")
        dkr_stage_patch(plume "${DKR_RT64_SOURCE}" android-vulkan-surface
            "${DKRPORT_ROOT}/patches/android/vulkan-surface-capabilities.patch"
            src/contrib/plume/plume_vulkan.cpp src/contrib/plume/plume_vulkan.h)
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-vulkan-surface" android-vulkan-descriptors
            "${DKRPORT_ROOT}/patches/android/vulkan-descriptor-safety.patch"
            src/contrib/plume/plume_vulkan.cpp)
        dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" android-buffer-lifecycle
            "${DKRPORT_ROOT}/patches/android/buffer-worker-lifecycle.patch"
            src/render/rt64_buffer_uploader.cpp)
        dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/android-buffer-lifecycle" android-small-upload
            "${DKRPORT_ROOT}/patches/android/buffer-small-upload.patch" src/render/rt64_buffer_uploader.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/android-small-upload/src/render/rt64_buffer_uploader.cpp"
            TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES "${DKR_RT64_SOURCE}/src/render;${DKRPORT_ROOT}/runtime-recomp/src/game")
        dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" android-background-priority
            "${DKRPORT_ROOT}/patches/android/background-shader-priority.patch" src/common/rt64_thread.cpp)
        dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" android-texture-memory
            "${DKRPORT_ROOT}/patches/android/texture-memory-pressure.patch" src/render/rt64_texture_cache.cpp)
        target_include_directories(rt64 BEFORE PRIVATE
            "${CMAKE_BINARY_DIR}/generated/android-vulkan-surface/src/contrib/plume")
        # Every composed plume source must see the same staged class layout.
        target_include_directories(plume BEFORE PRIVATE
            "${CMAKE_BINARY_DIR}/generated/android-vulkan-surface/src/contrib/plume")
    else()
        set(DKR_RESOURCE_PATCH_SOURCE "${DKR_RT64_SOURCE}")
    endif()
    dkr_stage_patch(rt64 "${DKR_RESOURCE_PATCH_SOURCE}" performance-renderer
        "${DKRPORT_ROOT}/patches/performance/renderer-workers.patch" src/hle/rt64_application.cpp)
    if(ANDROID)
        dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/performance-renderer" android-transfer-qualification
            "${DKRPORT_ROOT}/patches/android/renderer-transfer-qualification.patch" src/hle/rt64_application.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/android-transfer-qualification/src/hle/rt64_application.cpp"
            TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES
            "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game"
            COMPILE_OPTIONS "-include;render_resource_policy.hpp")
    endif()
    dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" performance-pacing
        "${DKRPORT_ROOT}/patches/performance/presentation-wait.patch" src/common/rt64_timer.cpp)
    dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" performance-measurements
        "${DKRPORT_ROOT}/patches/performance/renderer-measurements.patch"
        src/hle/rt64_state.cpp src/hle/rt64_framebuffer.cpp src/render/rt64_render_worker.cpp)
    if(ANDROID)
        set(DKR_VULKAN_MEASURE_SOURCE "${CMAKE_BINARY_DIR}/generated/android-vulkan-descriptors")
    else()
        set(DKR_VULKAN_MEASURE_SOURCE "${DKR_RT64_SOURCE}")
    endif()
    dkr_stage_patch(plume "${DKR_VULKAN_MEASURE_SOURCE}" performance-vulkan-measurements
        "${DKRPORT_ROOT}/patches/performance/vulkan-measurements.patch" src/contrib/plume/plume_vulkan.cpp)
    if(ANDROID)
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/performance-vulkan-measurements" android-mapped-memory
            "${DKRPORT_ROOT}/patches/android/vulkan-mapped-memory.patch" src/contrib/plume/plume_vulkan.cpp)
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-mapped-memory" android-resource-failures
            "${DKRPORT_ROOT}/patches/android/vulkan-resource-failures.patch" src/contrib/plume/plume_vulkan.cpp)
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-resource-failures" android-capability-preflight
            "${DKRPORT_ROOT}/patches/android/vulkan-capability-preflight.patch" src/contrib/plume/plume_vulkan.cpp)
        configure_file("${CMAKE_BINARY_DIR}/generated/android-vulkan-surface/src/contrib/plume/plume_vulkan.h"
            "${CMAKE_BINARY_DIR}/generated/android-capability-preflight/src/contrib/plume/plume_vulkan.h" COPYONLY)
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-capability-preflight" android-surface-recovery
            "${DKRPORT_ROOT}/patches/android/vulkan-surface-recovery.patch"
            src/contrib/plume/plume_vulkan.cpp src/contrib/plume/plume_vulkan.h)
        target_include_directories(rt64 BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-surface-recovery/src/contrib/plume")
        target_include_directories(plume BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-surface-recovery/src/contrib/plume")
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-surface-recovery" android-copy-contract
            "${DKRPORT_ROOT}/patches/android/vulkan-copy-contract.patch" src/contrib/plume/plume_vulkan.cpp)
        configure_file("${CMAKE_BINARY_DIR}/generated/android-surface-recovery/src/contrib/plume/plume_vulkan.h"
            "${CMAKE_BINARY_DIR}/generated/android-copy-contract/src/contrib/plume/plume_vulkan.h" COPYONLY)
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-copy-contract" android-pipeline-cache
            "${DKRPORT_ROOT}/patches/android/vulkan-pipeline-cache.patch"
            src/contrib/plume/plume_vulkan.cpp src/contrib/plume/plume_vulkan.h)
        target_include_directories(rt64 BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-pipeline-cache/src/contrib/plume")
        target_include_directories(plume BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-pipeline-cache/src/contrib/plume")
    endif()
    if(ANDROID OR DKR_ANDROID_RENDER_QUALIFICATION)
        if(ANDROID)
            set(DKR_PASS_PATCH_SOURCE "${CMAKE_BINARY_DIR}/generated/android-pipeline-cache")
            set(DKR_PASS_HEADER_SOURCE "${DKR_PASS_PATCH_SOURCE}/src/contrib/plume")
        else()
            set(DKR_PASS_PATCH_SOURCE "${CMAKE_BINARY_DIR}/generated/performance-vulkan-measurements")
            set(DKR_PASS_HEADER_SOURCE "${DKR_RT64_SOURCE}/src/contrib/plume")
        endif()
        dkr_stage_patch(plume "${DKR_PASS_PATCH_SOURCE}" android-pass-retention
            "${DKRPORT_ROOT}/patches/android/vulkan-pass-retention.patch" src/contrib/plume/plume_vulkan.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/android-pass-retention/src/contrib/plume/plume_vulkan.cpp"
            TARGET_DIRECTORY plume PROPERTIES INCLUDE_DIRECTORIES
            "${DKR_PASS_HEADER_SOURCE};${DKR_RT64_SOURCE}/src/contrib/plume;${CMAKE_CURRENT_LIST_DIR}/src/game")
        configure_file("${DKR_PASS_HEADER_SOURCE}/plume_vulkan.h"
            "${CMAKE_BINARY_DIR}/generated/android-pass-retention/src/contrib/plume/plume_vulkan.h" COPYONLY)
        dkr_stage_patch(plume "${CMAKE_BINARY_DIR}/generated/android-pass-retention" android-read-barriers
            "${DKRPORT_ROOT}/patches/android/vulkan-read-barriers.patch"
            src/contrib/plume/plume_vulkan.cpp src/contrib/plume/plume_vulkan.h)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/android-read-barriers/src/contrib/plume/plume_vulkan.cpp"
            TARGET_DIRECTORY plume PROPERTIES INCLUDE_DIRECTORIES
            "${DKR_RT64_SOURCE}/src/contrib/plume;${CMAKE_CURRENT_LIST_DIR}/src/game")
        target_include_directories(plume BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-read-barriers/src/contrib/plume"
            "${CMAKE_CURRENT_LIST_DIR}/src/game")
        target_include_directories(rt64 BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-read-barriers/src/contrib/plume"
            "${CMAKE_CURRENT_LIST_DIR}/src/game")
        dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" android-depth-pass-retention
            "${DKRPORT_ROOT}/patches/android/render-pass-retention.patch" src/render/rt64_framebuffer_renderer.cpp)
    endif()
    dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" performance-present-wait
        "${DKRPORT_ROOT}/patches/performance/present-queue-wait.patch" src/hle/rt64_present_queue.cpp)
    if(ANDROID)
        dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/performance-present-wait" android-present-ownership
            "${DKRPORT_ROOT}/patches/android/present-surface-ownership.patch" src/hle/rt64_present_queue.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/android-present-ownership/src/hle/rt64_present_queue.cpp"
            TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game")
    endif()
    if(DKR_EXPERIMENTAL_RENDER_QUALIFICATION OR DKR_EXPERIMENTAL_RACE_TEST)
        if(ANDROID)
            message(FATAL_ERROR "Private rollback GPU qualification is desktop only")
        endif()
        # OFF for distributable builds. Isolated copies, not vendor edits.
        dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/performance-present-wait" experimental-present-queue
            "${DKRPORT_ROOT}/patches/netplay/experimental-present-queue.patch" src/hle/rt64_present_queue.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/experimental-present-queue/src/hle/rt64_present_queue.cpp"
            TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES
            "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game")
        dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" experimental-present-state
            "${DKRPORT_ROOT}/patches/netplay/experimental-present-state.patch" src/hle/rt64_state.cpp)
    endif()
    dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" performance-workload-wait
        "${DKRPORT_ROOT}/patches/performance/workload-queue-wait.patch" src/hle/rt64_workload_queue.cpp)
    dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/performance-workload-wait" water-renderer-history
        "${DKRPORT_ROOT}/patches/performance/water-renderer-history.patch" src/hle/rt64_workload_queue.cpp)
    set_source_files_properties("${CMAKE_BINARY_DIR}/generated/water-renderer-history/src/hle/rt64_workload_queue.cpp"
        TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES
        "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game")
    if(DKR_EXPERIMENTAL_RENDER_QUALIFICATION OR DKR_EXPERIMENTAL_RACE_TEST)
        dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/water-renderer-history" experimental-workload-history
            "${DKRPORT_ROOT}/patches/netplay/experimental-workload-history.patch" src/hle/rt64_workload_queue.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/experimental-workload-history/src/hle/rt64_workload_queue.cpp"
            TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES
            "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game")
    endif()
    if(ANDROID)
        dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/water-renderer-history" android-render-idle-priority
            "${DKRPORT_ROOT}/patches/android/render-idle-priority.patch" src/hle/rt64_workload_queue.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/android-render-idle-priority/src/hle/rt64_workload_queue.cpp"
            TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES
            "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game")
    endif()
    if(ANDROID OR DKR_ANDROID_RENDER_QUALIFICATION)
        if(ANDROID)
            set(DKR_STAGE_TIMING_SOURCE "${CMAKE_BINARY_DIR}/generated/android-render-idle-priority")
        else()
            set(DKR_STAGE_TIMING_SOURCE "${CMAKE_BINARY_DIR}/generated/water-renderer-history")
        endif()
        dkr_stage_patch(rt64 "${DKR_STAGE_TIMING_SOURCE}" android-render-stage-timing
            "${DKRPORT_ROOT}/patches/android/render-stage-timing.patch" src/hle/rt64_workload_queue.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/android-render-stage-timing/src/hle/rt64_workload_queue.cpp"
            TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES
            "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game")
    endif()
    dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" performance-shader-wait
        "${DKRPORT_ROOT}/patches/performance/shader-cache-wait.patch" src/render/rt64_raster_shader_cache.cpp)
    dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" water-scroll-interpolation
        "${DKRPORT_ROOT}/patches/performance/water-scroll-interpolation.patch" src/hle/rt64_game_frame.cpp)
    set_source_files_properties("${CMAKE_BINARY_DIR}/generated/performance-renderer/src/hle/rt64_application.cpp"
        TARGET_DIRECTORY rt64 PROPERTIES INCLUDE_DIRECTORIES
        "${DKR_RT64_SOURCE}/src/hle;${CMAKE_CURRENT_LIST_DIR}/src/game")
    # Forced include applies only to this isolated translation unit.
    if(MSVC)
        set_property(SOURCE "${CMAKE_BINARY_DIR}/generated/performance-renderer/src/hle/rt64_application.cpp"
            TARGET_DIRECTORY rt64 APPEND PROPERTY COMPILE_OPTIONS /FIrender_resource_policy.hpp)
    else()
        set_property(SOURCE "${CMAKE_BINARY_DIR}/generated/performance-renderer/src/hle/rt64_application.cpp"
            TARGET_DIRECTORY rt64 APPEND PROPERTY COMPILE_OPTIONS -include render_resource_policy.hpp)
    endif()
    if(MSVC AND TARGET rt64)
        # RT64 enables /MP internally. Ensure all compiler workers serialize
        # writes to the target PDB so clean RelWithDebInfo builds are reliable.
        target_compile_options(rt64 PRIVATE /FS)
    endif()
endif()

if(DKR_RUNTIME_BUILD_GENERATED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/LegacyMods.cmake")
    foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(DKRLegacyModWorker PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper}
                "${CMAKE_BINARY_DIR}/bin/${config}/libexec/dkr-r")
    endforeach()
    set(DKR_V80_SYMBOL_MAP
        "${CMAKE_BINARY_DIR}/generated/dkr_v80_symbol_map.hpp")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/cmake/generate_revision_symbol_map.py"
                "${DKR_GENERATED_SOURCE_V80}"
                "dkr_v80_"
                "${DKR_V80_SYMBOL_MAP}"
        RESULT_VARIABLE DKR_SYMBOL_MAP_RESULT
        ERROR_VARIABLE DKR_SYMBOL_MAP_ERROR
    )
    if(NOT DKR_SYMBOL_MAP_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Could not namespace the Rev A CPU payload: ${DKR_SYMBOL_MAP_ERROR}")
    endif()

    # Each ROM revision contributes only its generated CPU payload. The
    # frontend, renderer, audio, input, netplay and settings runtime are linked
    # exactly once into DKRPortGame below. The alternate payload is namespaced
    # at compile time so protected generated output remains untouched.
    function(dkr_add_recompiled_payload TARGET_NAME REVISION SOURCE_DIR WRAPPER)
        set(FORCE_INCLUDE "${ARGV4}")
        file(GLOB_RECURSE GENERATED_C CONFIGURE_DEPENDS "${SOURCE_DIR}/*.c")
        file(GLOB_RECURSE GENERATED_CXX CONFIGURE_DEPENDS "${SOURCE_DIR}/*.cpp")
        set(GENERATED_FILES ${GENERATED_C} ${GENERATED_CXX})
        list(LENGTH GENERATED_FILES GENERATED_COUNT)
        if(GENERATED_COUNT EQUAL 0)
            message(FATAL_ERROR
                "No N64Recomp output was found under ${SOURCE_DIR} for US "
                "revision ${REVISION}.")
        endif()

        set(REQUIRED_INPUT_HOOKS
            "dkr_scheduler_keep_host_tasks"
            "dkr_netplay_prepare_controller_init"
            "dkr_netplay_resolve_authored_input_frame"
            "dkr_legacy_scene_begin"
            "dkr_legacy_asset_api"
            "dkr_legacy_pi_start_dma"
            "dkr_legacy_track_menu"
            "dkr_legacy_character_event"
            "dkr_legacy_character_menu")
        foreach(REQUIRED_INPUT_HOOK IN LISTS REQUIRED_INPUT_HOOKS)
            set(REQUIRED_INPUT_HOOK_FOUND FALSE)
            foreach(GENERATED_FILE IN LISTS GENERATED_FILES)
                file(STRINGS "${GENERATED_FILE}" GENERATED_HOOK_MATCH
                    REGEX "${REQUIRED_INPUT_HOOK}" LIMIT_COUNT 1)
                if(GENERATED_HOOK_MATCH)
                    set(REQUIRED_INPUT_HOOK_FOUND TRUE)
                    break()
                endif()
            endforeach()
            if(NOT REQUIRED_INPUT_HOOK_FOUND)
                message(FATAL_ERROR
                    "The US v${REVISION} generated payload is stale: required "
                    "Patch Pipeline hook ${REQUIRED_INPUT_HOOK} is missing under "
                    "${SOURCE_DIR}. Regenerate it from the matching policy before "
                    "building the unified runtime.")
            endif()
        endforeach()

        add_library(${TARGET_NAME} STATIC ${GENERATED_FILES} "${WRAPPER}")
        target_include_directories(${TARGET_NAME} PRIVATE
            "${DKRPORT_ROOT}/include"
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "${DKR_RECOMP_SOURCE}/include"
            "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp/include"
            "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
            "${DKR_MODERN_RUNTIME_SOURCE}/librecomp/include"
            "${SOURCE_DIR}"
        )
        if(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE
                /FS
                /permissive-
                /wd4068
                /wd4101
                /wd4189
                /wd4244
                /wd4267
                /wd4702
            )
            if(NOT FORCE_INCLUDE STREQUAL "")
                target_compile_options(${TARGET_NAME} PRIVATE
                    "/FI${FORCE_INCLUDE}")
            endif()
        else()
            target_compile_options(${TARGET_NAME} PRIVATE
                -fno-strict-aliasing
                -Wno-unused-but-set-variable
                -Wno-unused-variable
                -Wno-implicit-function-declaration
            )
            if(NOT FORCE_INCLUDE STREQUAL "")
                target_compile_options(${TARGET_NAME} PRIVATE
                    -include "${FORCE_INCLUDE}")
            endif()
        endif()
        math(EXPR TOTAL_GENERATED_COUNT
            "${DKR_GENERATED_FILE_COUNT} + ${GENERATED_COUNT}")
        set(DKR_GENERATED_FILE_COUNT "${TOTAL_GENERATED_COUNT}" PARENT_SCOPE)
    endfunction()

    dkr_add_recompiled_payload(
        DKRRecompiledFuncsV77 77 "${DKR_GENERATED_SOURCE_V77}"
        "${CMAKE_CURRENT_LIST_DIR}/src/game/game_payload_v77.cpp")
    dkr_add_recompiled_payload(
        DKRRecompiledFuncsV80 80 "${DKR_GENERATED_SOURCE_V80}"
        "${CMAKE_CURRENT_LIST_DIR}/src/game/game_payload_v80.cpp"
        "${DKR_V80_SYMBOL_MAP}")
endif()

if(DKR_RUNTIME_BUILD_GENERATED)
    file(GLOB DKR_GENERATED_RSP CONFIGURE_DEPENDS "${DKR_GENERATED_RSP_SOURCE}/*.cpp")
    if(NOT DKR_GENERATED_RSP)
        message(FATAL_ERROR
            "No RSPRecomp output was found under ${DKR_GENERATED_RSP_SOURCE}. "
            "Run Generate-DKR-RSP.cmd first.")
    endif()
    set(DKR_GAME_SOURCES
        src/game/audio_equalizer.cpp
        # The policy's asset hooks call into these with or without RT64; the
        # table logic itself is DKRCustomTracksCore, via DKRLegacyModLaunch.
        src/game/custom_tracks_hooks.cpp
        src/game/dkr_save_codec.cpp
        src/game/game_payload.cpp
        src/game/game_main.cpp
        src/game/game_registration.cpp
        src/game/null_renderer.cpp
        src/game/presentation_identity.cpp
        src/game/water_profile.cpp
        src/game/rev_a_asset_mutex.cpp
        src/game/rom_revision.cpp
        src/game/renderer_snapshot.cpp
        src/game/runtime_enhancements.cpp
        src/game/runtime_hud_layout.cpp
        src/game/runtime_audio_controls.cpp
        src/game/runtime_input.cpp
        src/game/runtime_magic_codes.cpp
        src/game/runtime_legacy_mods.cpp
        src/game/runtime_netplay.cpp
        src/game/runtime_platform.cpp
        src/game/runtime_quick_restart.cpp
        src/game/sdl3_input_client.cpp
        src/game/save_manager.cpp
        src/game/runtime_save_routing.cpp
        src/game/runtime_stubs.cpp
        src/game/runtime_support.cpp
        src/game/runtime_telemetry.cpp
        src/game/startup_performance.cpp
        src/game/virtual_pak.cpp
        ${DKR_GENERATED_RSP}
    )
    if(APPLE)
        list(APPEND DKR_GAME_SOURCES src/game/runtime_metal_layer.mm)
    endif()
    if(DKR_LEGACY_QUALIFICATION)
        list(APPEND DKR_GAME_SOURCES src/game/legacy_runtime_qualification.cpp)
    endif()
    if(DKR_REPLAY_QUALIFICATION OR DKR_EXPERIMENTAL_RACE_TEST)
        list(APPEND DKR_GAME_SOURCES src/game/replay_probe_capture.cpp)
    endif()
    if(DKR_REPLAY_QUALIFICATION)
        set_property(SOURCE src/game/runtime_netplay.cpp src/game/custom_tracks_hooks.cpp
            APPEND PROPERTY COMPILE_DEFINITIONS DKR_REPLAY_QUALIFICATION=1)
    endif()
    if(DKR_EXPERIMENTAL_RACE_TEST)
        list(APPEND DKR_GAME_SOURCES src/game/netplay/experimental_runtime.cpp)
    endif()
    if(DKR_RUNTIME_BUILD_RT64)
        list(APPEND DKR_GAME_SOURCES
            src/game/hud_basic_settings.cpp
            src/game/f3ddkr_rt64.cpp
            src/game/local_scenery_rt64.cpp
            src/game/generated/jumpman_font.c
            src/game/generated/racing_banana_font.c
            src/game/generated/selawik_font.c
            src/game/rt64_renderer.cpp
            src/game/runtime_crt_overlay.cpp
            src/game/runtime_rice_texture_import.cpp
            src/game/runtime_texture_packs.cpp
            src/game/runtime_ui.cpp
            "${DKR_RT64_SOURCE}/src/contrib/imgui/backends/imgui_impl_sdlrenderer2.cpp"
        )
    endif()
    if(WIN32)
        list(APPEND DKR_GAME_SOURCES
            resources/dkr-r.rc
        )
        string(REPLACE "." "," DKR_RELEASE_RESOURCE_VERSION "${DKR_RELEASE_VERSION_NUMERIC}")
        set_property(SOURCE resources/dkr-r.rc APPEND PROPERTY COMPILE_DEFINITIONS
            "DKR_RELEASE_RESOURCE_VERSION=${DKR_RELEASE_RESOURCE_VERSION},${DKR_RELEASE_BUILD_NUMBER}")
    endif()
    if(ANDROID)
        add_library(DKRPortGame SHARED ${DKR_GAME_SOURCES} src/android/android_platform.cpp src/android/mobile_ui.cpp)
        target_link_libraries(DKRPortGame PRIVATE SDL2::SDL2 SDL2::SDL2main android log)
    else()
        add_executable(DKRPortGame ${DKR_GAME_SOURCES})
    endif()
    if(MSVC)
        # These new bridge callbacks hold leases across guest scheduler calls.
        # Scheduler termination must unwind them even through C-linkage entrypoints.
        set_property(SOURCE src/game/runtime_legacy_mods.cpp
            src/game/legacy_runtime_qualification.cpp APPEND PROPERTY
            COMPILE_OPTIONS /EHs /EHc-)
    endif()
    target_include_directories(DKRPortGame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
        "${DKR_RECOMP_SOURCE}/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/librecomp/include"
        "${DKR_GENERATED_RSP_SOURCE}"
        "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty"
    )
    target_compile_definitions(DKRPortGame PRIVATE
        DKR_RUNTIME_HAS_RT64=$<BOOL:${DKR_RUNTIME_BUILD_RT64}>
        DKR_LEGACY_QUALIFICATION=$<BOOL:${DKR_LEGACY_QUALIFICATION}>
        DKR_RUNTIME_HAS_SDL3_INPUT_HOST=$<BOOL:${DKR_RUNTIME_BUILD_SDL3_INPUT_HOST}>
        DKR_RELEASE_VERSION="${DKR_RELEASE_VERSION}"
        DKR_NETWORK_RELEASE_VERSION="${DKR_NETWORK_RELEASE_VERSION}"
        DKR_PATCH_POLICY_SHA256="${DKR_PATCH_POLICY_SHA256}"
        NOMINMAX
    )
    if(DKR_EXPERIMENTAL_RACE_TEST)
        target_compile_definitions(DKRPortGame PRIVATE DKR_EXPERIMENTAL_RACE_TEST=1)
    endif()
    if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        # librecomp's generated RSP vector implementation uses SSE4.1
        # intrinsics on x86-64. GCC and Clang require the target feature to be
        # enabled explicitly, while MSVC exposes these intrinsics by default.
        target_compile_options(DKRPortGame PRIVATE -msse4.1)
    endif()
    # Keep the generated payload archives before their shared runtime
    # providers. GNU ld scans static archives from left to right, so placing
    # librecomp first leaves the v1.1 payload's imported OS/compiler helpers
    # unresolved on Linux. MSVC is order-independent, and this ordering keeps
    # both platforms on the same single-runtime link graph.
    target_link_libraries(DKRPortGame PRIVATE
        DKRRecompiledFuncsV77
        DKRRecompiledFuncsV80
        ultramodern
        librecomp)
    target_link_libraries(DKRPortGame PRIVATE DKRNetplayCore)
    target_link_libraries(DKRPortGame PRIVATE DKRLegacyModLaunch DKRLegacyGuestIO)
    add_dependencies(DKRPortGame DKRLegacyModWorker)
    if(MSVC)
        target_compile_options(DKRPortGame PRIVATE /FS)
    endif()
    if(WIN32)
        target_link_libraries(DKRPortGame PRIVATE ws2_32)
    endif()
    if(DKR_RUNTIME_BUILD_RT64 AND TARGET rt64)
        target_include_directories(DKRPortGame PRIVATE
            "$<TARGET_PROPERTY:rt64,INCLUDE_DIRECTORIES>"
            "${DKR_RT64_SOURCE}/src"
            "${DKR_RT64_SOURCE}/src/include"
            "${DKR_RT64_SOURCE}/src/contrib"
            "${DKR_RT64_SOURCE}/src/contrib/hlslpp/include"
            "${DKR_RT64_SOURCE}/src/contrib/plume"
            "${DKR_RT64_SOURCE}/src/contrib/imgui"
            "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-core/src/api"
            "${DKR_RT64_SOURCE}/src/contrib/nativefiledialog-extended/src/include"
            "${CMAKE_BINARY_DIR}/src"
        )
        target_link_libraries(DKRPortGame PRIVATE rt64)
        if(APPLE)
            target_include_directories(DKRPortGame PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/metal-cpp"
            )
        endif()
        if(WIN32)
            target_include_directories(DKRPortGame PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include"
            )
            target_link_directories(DKRPortGame PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64"
            )
            target_link_libraries(DKRPortGame PRIVATE SDL2)
            add_custom_command(TARGET DKRPortGame POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRPortGame>/SDL2.dll"
                VERBATIM
            )
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(DKRPortGame PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(DKRPortGame PRIVATE ${SDL2_LIBRARIES})
        endif()
        if(WIN32)
            add_custom_command(TARGET DKRPortGame POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxcompiler.dll"
                    "$<TARGET_FILE_DIR:DKRPortGame>/dxcompiler.dll"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxil.dll"
                    "$<TARGET_FILE_DIR:DKRPortGame>/dxil.dll"
                VERBATIM
            )
        endif()
        add_custom_command(TARGET DKRPortGame POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Icons"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/../assets/ui/Icons/DKR-R-Logo.bmp"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Icons/DKR-R-Logo.bmp"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/../assets/ui/Icons/DKR-R-Spinning-Icon.png"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Icons/DKR-R-Spinning-Icon.png"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/../assets/ui/Icons/DKR-R-Short-Logo.png"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Icons/DKR-R-Short-Logo.png"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Backgrounds"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/../assets/ui/Backgrounds/DKR-R-Launcher-Background.png"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/ui/Backgrounds/DKR-R-Launcher-Background.png"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/filters"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_LIST_DIR}/../assets/filters"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/filters"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/controllers"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_LIST_DIR}/../assets/controllers/gamecontrollerdb.txt"
                "$<TARGET_FILE_DIR:DKRPortGame>/assets/controllers/gamecontrollerdb.txt"
            COMMENT "Staging the DKR-R launcher, graphics and controller assets"
            VERBATIM
        )
    endif()
    if(WIN32)
        target_link_libraries(DKRPortGame PRIVATE user32 shell32 dbghelp bcrypt dxgi)
    endif()
    set_target_properties(DKRPortGame PROPERTIES
        OUTPUT_NAME "DKR-R"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(APPLE)
        # CMake supplies the platform Info.plist and places the executable in
        # Contents/MacOS. RT64 selects its native Metal backend on Apple
        # platforms; the release packager adds artwork and notices before
        # applying an ad-hoc signature for local testing.
        set_target_properties(DKRPortGame PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "DKR-R"
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.thatguymcd.dkr-r"
            MACOSX_BUNDLE_INFO_STRING "Diddy Kong Racing - Recompiled"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${DKR_RELEASE_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${DKR_RELEASE_VERSION_NUMERIC}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${DKR_RELEASE_BUILD_NUMBER}"
            MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 ThatGuyMcd and DKR-R contributors"
        )
    endif()
    if(WIN32)
        set_target_properties(DKRPortGame PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(DKRPortGame PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}"
        )
    endforeach()
endif()

add_executable(DKRRuntimeProbe src/runtime_probe.cpp)

set(DKR_EXPERIMENTAL_OWNED_GENERATED "" CACHE PATH "NEW isolated owned CPU Patch Pipeline output; integration targets only")
if(DKR_EXPERIMENTAL_OWNED_GENERATED)
    include(cmake/ExperimentalOwnedGame.cmake)
    dkr_add_experimental_owned_game(DKRExperimentalOwnedGame "${DKR_EXPERIMENTAL_OWNED_GENERATED}")
    if(DKR_NETPLAY_WEBRTC)
        add_executable(DKRExperimentalOwnedQuickJoinTests EXCLUDE_FROM_ALL tests/experimental_owned_quick_join_tests.cpp)
        target_link_libraries(DKRExperimentalOwnedQuickJoinTests PRIVATE DKRExperimentalOwnedGame)
        target_include_directories(DKRExperimentalOwnedQuickJoinTests PRIVATE "${DKR_LIBDATACHANNEL_SOURCE}/deps/json/include")
        file(READ "${DKR_EXPERIMENTAL_OWNED_GENERATED}/manifest.json" DKR_OWNED_MANIFEST)
        string(JSON DKR_OWNED_REVISION GET "${DKR_OWNED_MANIFEST}" revision)
        string(JSON DKR_OWNED_FULL_SCENES ERROR_VARIABLE DKR_OWNED_FULL_SCENES_ERROR GET "${DKR_OWNED_MANIFEST}" full_scenes)
        if(DKR_OWNED_FULL_SCENES_ERROR OR NOT DKR_OWNED_FULL_SCENES)
            set(DKR_OWNED_FULL_SCENES 0)
        else()
            set(DKR_OWNED_FULL_SCENES 1)
        endif()
        if(DKR_EXPERIMENTAL_RACE_TEST AND DKR_OWNED_FULL_SCENES)
            set_property(SOURCE src/game/runtime_ui.cpp APPEND PROPERTY
                COMPILE_DEFINITIONS DKR_EXPERIMENTAL_FULL_SCENES=1)
        endif()
        if(DKR_EXPERIMENTAL_RACE_TEST AND NOT DKR_OWNED_REVISION EQUAL 77)
            message(FATAL_ERROR "The first distributable race test requires the qualified v77 owned payload")
        endif()
        if(DKR_EXPERIMENTAL_RACE_TEST AND NOT DKR_OWNED_FULL_SCENES)
            message(FATAL_ERROR "Integrated rollback requires the full-scene Patch Pipeline; race-only payloads cannot be shipped here")
        endif()
        target_compile_definitions(DKRExperimentalOwnedQuickJoinTests PRIVATE DKR_OWNED_TEST_REVISION=${DKR_OWNED_REVISION} DKR_OWNED_FULL_SCENES=${DKR_OWNED_FULL_SCENES})
        if(MSVC)
            target_compile_options(DKRExperimentalOwnedQuickJoinTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRExperimentalOwnedQuickJoinTests PRIVATE -UNDEBUG)
        endif()
    endif()
endif()

# Private real-GPU qualification only. It does not start the launcher, guest
# scheduler, live lobby, or save workers, and is never a release/package target.
# Reuse the reviewed DKR bridge and the exact staged RT64 build; do not create
# a substitute renderer or edit dependency/generated sources for this probe.
if(TARGET DKRPortGame AND (DKR_EXPERIMENTAL_RENDER_QUALIFICATION OR DKR_EXPERIMENTAL_RACE_TEST))
    # Private RT64 references arrive after the static core on ELF. An explicit
    # object keeps its retirement seam available without changing stable links.
    target_sources(DKRPortGame PRIVATE src/game/netplay/experimental_present_gate.cpp)
    # RT64's staged retirement seam is also used by renderer regression tests.
    # Propagate the narrow implementation, not the whole online core, to every
    # RT64 consumer so static-link order cannot leave unresolved gate symbols.
    target_sources(rt64 INTERFACE "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay/experimental_present_gate.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay/experimental_presentation.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay/experimental_checkpoint_hash.cpp")
    target_include_directories(rt64 INTERFACE "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
endif()
if(TARGET DKRPortGame AND DKR_RUNTIME_BUILD_RT64 AND NOT ANDROID AND NOT APPLE)
    set(DKR_PRESENTATION_PROBE_SOURCES ${DKR_GAME_SOURCES})
    list(REMOVE_ITEM DKR_PRESENTATION_PROBE_SOURCES src/game/game_main.cpp)
    add_executable(DKRExperimentalGpuProbe EXCLUDE_FROM_ALL
        tests/experimental_gpu_probe.cpp ${DKR_PRESENTATION_PROBE_SOURCES}
        src/game/netplay/experimental_checkpoint_hash.cpp)
    target_include_directories(DKRExperimentalGpuProbe PRIVATE
        "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>"
        "${CMAKE_CURRENT_LIST_DIR}/tests/replay_probe")
    target_compile_definitions(DKRExperimentalGpuProbe PRIVATE
        "$<TARGET_PROPERTY:DKRPortGame,COMPILE_DEFINITIONS>" SDL_MAIN_HANDLED=1)
    if(DKR_EXPERIMENTAL_RENDER_QUALIFICATION)
        target_compile_definitions(DKRExperimentalGpuProbe PRIVATE DKR_EXPERIMENTAL_RENDER_QUALIFICATION=1)
    endif()
    target_compile_options(DKRExperimentalGpuProbe PRIVATE
        "$<TARGET_PROPERTY:DKRPortGame,COMPILE_OPTIONS>")
    target_link_directories(DKRExperimentalGpuProbe PRIVATE
        "$<TARGET_PROPERTY:DKRPortGame,LINK_DIRECTORIES>")
    target_link_libraries(DKRExperimentalGpuProbe PRIVATE
        "$<TARGET_PROPERTY:DKRPortGame,LINK_LIBRARIES>")
    # PRIVATE interactive owner/renderer handoff qualification, never packaged.
    if(TARGET DKRExperimentalOwnedGame AND DKR_NETPLAY_WEBRTC AND (DKR_EXPERIMENTAL_RENDER_QUALIFICATION OR DKR_EXPERIMENTAL_RACE_TEST))
        if(DKR_EXPERIMENTAL_RACE_TEST)
            set(DKR_INTERACTIVE_TARGET DKRExperimentalRaceTest)
            set(DKR_INTERACTIVE_SOURCE src/game/netplay/experimental_race_frontend.cpp)
            set(DKR_PRIVATE_PRESENTATION_SOURCES ${DKR_PRESENTATION_PROBE_SOURCES})
            add_executable(${DKR_INTERACTIVE_TARGET} EXCLUDE_FROM_ALL ${DKR_INTERACTIVE_SOURCE} ${DKR_PRIVATE_PRESENTATION_SOURCES}
                src/game/netplay/experimental_process.cpp
                src/game/netplay/experimental_present_gate.cpp)
            set_target_properties(${DKR_INTERACTIVE_TARGET} PROPERTIES OUTPUT_NAME "DKR-R-RollbackTest"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
            foreach(config Debug Release RelWithDebInfo MinSizeRel)
                string(TOUPPER "${config}" config_upper)
                set_target_properties(${DKR_INTERACTIVE_TARGET} PROPERTIES
                    RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}")
            endforeach()
        else()
            set(DKR_INTERACTIVE_TARGET DKRExperimentalInteractiveProbe)
            set(DKR_INTERACTIVE_SOURCE tests/experimental_interactive_probe.cpp)
        add_executable(DKRExperimentalInteractiveProbe EXCLUDE_FROM_ALL
            tests/experimental_interactive_probe.cpp ${DKR_PRESENTATION_PROBE_SOURCES})
        endif()
        target_include_directories(${DKR_INTERACTIVE_TARGET} PRIVATE
            "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_compile_definitions(${DKR_INTERACTIVE_TARGET} PRIVATE
            "$<TARGET_PROPERTY:DKRPortGame,COMPILE_DEFINITIONS>"
            SDL_MAIN_HANDLED=1
            DKR_OWNED_TEST_REVISION=${DKR_OWNED_REVISION}
            DKR_OWNED_FULL_SCENES=${DKR_OWNED_FULL_SCENES})
        set(DKR_OWNED_ID_PARTS "")
        # Bind the negotiated identity to the COMPLETE linked owned closure,
        # not just the driver. Audio/input/Pak/policy changes must reject mixed
        # packages too. Content hashes (not absolute paths) remain cross-OS.
        file(GLOB DKR_OWNED_CLOSURE_ID_FILES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_LIST_DIR}/tests/replay_probe/*.c"
            "${CMAKE_CURRENT_LIST_DIR}/tests/replay_probe/*.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/tests/replay_probe/*.h"
            "${CMAKE_CURRENT_LIST_DIR}/tests/replay_probe/*.hpp"
            "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay/*.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay/*.h"
            "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay/*.hpp"
            "${DKR_EXPERIMENTAL_OWNED_GENERATED}/*.c"
            "${DKR_EXPERIMENTAL_OWNED_GENERATED}/*.cpp"
            "${DKR_EXPERIMENTAL_OWNED_GENERATED}/*.h")
        foreach(DKR_OWNED_ID_FILE
                "${DKR_EXPERIMENTAL_OWNED_GENERATED}/manifest.json"
                ${DKR_INTERACTIVE_SOURCE} tests/replay_probe/probe_driver.cpp
                tests/replay_probe/probe_bridge.h tests/replay_probe/probe_native.c
                tests/replay_probe/probe_presentation.c tests/replay_probe/probe_presentation.h
                tests/replay_probe/probe_policies.cpp
                tests/replay_probe/probe_audio_rsp.cpp src/game/netplay/runtime_state.hpp
                src/game/netplay/experimental_rollback.cpp src/game/netplay/experimental_launch.cpp
                src/game/netplay/experimental_timeline.cpp src/game/netplay/experimental_network.cpp
                src/game/netplay/experimental_session.cpp src/game/netplay/experimental_pump.cpp
                src/game/netplay/experimental_pcm.hpp
                src/game/save_manager.cpp src/game/save_manager.hpp
                ${DKR_OWNED_CLOSURE_ID_FILES})
            get_filename_component(DKR_OWNED_ID_FILE "${DKR_OWNED_ID_FILE}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
            file(SHA256 "${DKR_OWNED_ID_FILE}" DKR_OWNED_ID_PART)
            string(APPEND DKR_OWNED_ID_PARTS "${DKR_OWNED_ID_PART}|")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DKR_OWNED_ID_FILE}")
        endforeach()
        string(SHA256 DKR_OWNED_BUILD_ID "${DKR_OWNED_ID_PARTS}")
        message(STATUS "Owned rollback contract: ${DKR_OWNED_BUILD_ID}")
        if(DKR_EXPERIMENTAL_RACE_TEST)
            target_link_libraries(DKRPortGame PRIVATE DKRExperimentalOwnedGame)
            set_property(SOURCE src/game/netplay/experimental_runtime.cpp APPEND PROPERTY
                COMPILE_DEFINITIONS DKR_OWNED_BUILD_ID="${DKR_OWNED_BUILD_ID}")
        endif()
        # Only the frontend consumes this manifest. A protocol-identity change
        # must not recompile every unrelated launcher/renderer translation unit.
        set_property(SOURCE ${DKR_INTERACTIVE_SOURCE} APPEND PROPERTY
            COMPILE_DEFINITIONS DKR_OWNED_BUILD_ID="${DKR_OWNED_BUILD_ID}")
        target_compile_options(${DKR_INTERACTIVE_TARGET} PRIVATE
            "$<TARGET_PROPERTY:DKRPortGame,COMPILE_OPTIONS>")
        target_link_directories(${DKR_INTERACTIVE_TARGET} PRIVATE
            "$<TARGET_PROPERTY:DKRPortGame,LINK_DIRECTORIES>")
        target_link_libraries(${DKR_INTERACTIVE_TARGET} PRIVATE
            DKRExperimentalOwnedGame "$<TARGET_PROPERTY:DKRPortGame,LINK_LIBRARIES>")
    endif()
    if(WIN32)
        foreach(DKR_PROBE_DLL
            "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
            "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxcompiler.dll"
            "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxil.dll")
            add_custom_command(TARGET DKRExperimentalGpuProbe POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_PROBE_DLL}" "$<TARGET_FILE_DIR:DKRExperimentalGpuProbe>" VERBATIM)
            if(DKR_INTERACTIVE_TARGET AND TARGET ${DKR_INTERACTIVE_TARGET})
                add_custom_command(TARGET ${DKR_INTERACTIVE_TARGET} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${DKR_PROBE_DLL}" "$<TARGET_FILE_DIR:${DKR_INTERACTIVE_TARGET}>" VERBATIM)
            endif()
        endforeach()
    endif()
endif()

if(DKR_EXPERIMENTAL_RACE_TEST AND NOT TARGET DKRExperimentalRaceTest)
    message(FATAL_ERROR "Experimental race test requires RT64, WebRTC and the reviewed isolated owned Patch Pipeline output")
endif()
target_include_directories(DKRRuntimeProbe PRIVATE
    "${DKR_RECOMP_SOURCE}/include"
    "${DKR_MODERN_RUNTIME_SOURCE}/N64Recomp/include"
    "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
    "${DKR_MODERN_RUNTIME_SOURCE}/librecomp/include"
)
target_compile_definitions(DKRRuntimeProbe PRIVATE
    DKR_RUNTIME_GENERATED_FILE_COUNT=${DKR_GENERATED_FILE_COUNT}
    DKR_RUNTIME_HAS_RT64=$<BOOL:${DKR_RUNTIME_BUILD_RT64}>
)
target_link_libraries(DKRRuntimeProbe PRIVATE ultramodern librecomp)
if(DKR_RUNTIME_BUILD_RT64 AND TARGET rt64)
    target_link_libraries(DKRRuntimeProbe PRIVATE rt64)
    if(WIN32)
        # rt64 is static and its SDL dependency is not exported to consumers.
        # Keep the validation probe linkable for aggregate/release builds just
        # like the production game target above.
        target_link_directories(DKRRuntimeProbe PRIVATE
            "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64"
        )
        target_link_libraries(DKRRuntimeProbe PRIVATE SDL2)
    endif()
endif()

set_target_properties(DKRRuntimeProbe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(DKRRuntimeProbe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}"
    )
endforeach()

add_executable(DKRRomHash src/rom_hash.cpp)
target_include_directories(DKRRomHash PRIVATE
    "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty"
)
set_target_properties(DKRRomHash PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(DKRRomHash PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}"
    )
endforeach()

if(BUILD_TESTING)
    add_executable(DKRRenderIdlePolicyTests tests/render_idle_policy_tests.cpp)
    add_executable(DKRRenderPassPolicyTests tests/render_pass_policy_tests.cpp)
    target_compile_features(DKRRenderPassPolicyTests PRIVATE cxx_std_20)
    add_test(NAME DKRRenderPassPolicy COMMAND DKRRenderPassPolicyTests)
    set_tests_properties(DKRRenderPassPolicy PROPERTIES ENVIRONMENT "DKR_POWER_PROFILE=1")
    target_compile_features(DKRRenderIdlePolicyTests PRIVATE cxx_std_20)
    target_link_libraries(DKRRenderIdlePolicyTests PRIVATE Threads::Threads)
    add_test(NAME DKRRenderIdlePolicy COMMAND DKRRenderIdlePolicyTests)
    set_tests_properties(DKRRenderIdlePolicy PROPERTIES TIMEOUT 15)
    add_executable(DKRHostTaskLifetimeTests tests/host_task_lifetime_tests.cpp)
    target_compile_features(DKRHostTaskLifetimeTests PRIVATE cxx_std_20)
    target_link_libraries(DKRHostTaskLifetimeTests PRIVATE Threads::Threads)
    add_test(NAME DKRHostTaskLifetime COMMAND DKRHostTaskLifetimeTests)
    add_executable(DKRPerformancePolicyTests tests/performance_policy_tests.cpp)
    add_test(NAME DKRPerformancePolicy COMMAND DKRPerformancePolicyTests)
    set_tests_properties(DKRPerformancePolicy PROPERTIES ENVIRONMENT "DKR_POWER_PROFILE=1")
    if(DKR_RUNTIME_BUILD_RT64)
        # Explicit microbenchmarks, not pass/fail performance claims. Never run
        # during other builds or tests; driver/game qualification is separate.
        foreach(variant IN ITEMS Baseline Candidate)
            set(timer_source "${CMAKE_BINARY_DIR}/generated/performance-pacing/src/common/rt64_timer.cpp")
            add_executable(DKROptimisationBench${variant} EXCLUDE_FROM_ALL tests/optimisation_microbench.cpp "${timer_source}")
            if(variant STREQUAL "Baseline")
                target_compile_definitions(DKROptimisationBench${variant} PRIVATE DKR_PRESENTATION_LEGACY_WAIT=1)
            endif()
            target_include_directories(DKROptimisationBench${variant} PRIVATE
                "${DKR_RT64_SOURCE}/src" "${DKR_RT64_SOURCE}/src/common" "${CMAKE_CURRENT_LIST_DIR}/src/game")
            if(WIN32)
                target_link_libraries(DKROptimisationBench${variant} PRIVATE winmm)
            endif()
        endforeach()
    endif()
    add_executable(DKRMobileTouchPolicyTests tests/mobile_touch_policy_tests.cpp)
    target_compile_features(DKRMobileTouchPolicyTests PRIVATE cxx_std_20)
    add_test(NAME DKRMobileTouchPolicy COMMAND DKRMobileTouchPolicyTests)
    if(DKR_RUNTIME_BUILD_RT64 AND NOT ANDROID)
        add_executable(DKRMobileUiTests tests/mobile_ui_tests.cpp src/android/mobile_ui.cpp)
        target_compile_features(DKRMobileUiTests PRIVATE cxx_std_20)
        target_compile_definitions(DKRMobileUiTests PRIVATE DKR_MOBILE_UI_TEST=1 SDL_MAIN_HANDLED=1)
        target_include_directories(DKRMobileUiTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib" "${DKR_RT64_SOURCE}/src")
        target_link_libraries(DKRMobileUiTests PRIVATE rt64)
        if(WIN32)
            target_include_directories(DKRMobileUiTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include")
            target_link_directories(DKRMobileUiTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRMobileUiTests PRIVATE SDL2)
            add_custom_command(TARGET DKRMobileUiTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                "$<TARGET_FILE_DIR:DKRMobileUiTests>/SDL2.dll")
        else()
            target_include_directories(DKRMobileUiTests PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(DKRMobileUiTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        add_test(NAME DKRMobileUi COMMAND DKRMobileUiTests)
    endif()
    add_executable(DKRMobileGraphicsPresetTests tests/mobile_graphics_preset_tests.cpp)
    target_include_directories(DKRMobileGraphicsPresetTests PRIVATE
        "${DKR_MODERN_RUNTIME_SOURCE}/ultramodern/include"
        "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
    add_test(NAME DKRMobileGraphicsPreset COMMAND DKRMobileGraphicsPresetTests)
    add_executable(DKRAndroidStorageTests tests/android_storage_tests.cpp)
    add_executable(DKRAndroidCompatibilityTests tests/android_compatibility_tests.cpp)
    target_compile_features(DKRAndroidCompatibilityTests PRIVATE cxx_std_20)
    add_test(NAME DKRAndroidCompatibility COMMAND DKRAndroidCompatibilityTests)
    if(DKR_RUNTIME_BUILD_RT64)
        add_executable(DKRAndroidTextureCopyTests tests/android_texture_copy_tests.cpp)
        target_include_directories(DKRAndroidTextureCopyTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/plume")
        target_compile_definitions(DKRAndroidTextureCopyTests PRIVATE NOMINMAX)
        add_test(NAME DKRAndroidTextureCopy COMMAND DKRAndroidTextureCopyTests)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME DKRAndroidStartupContract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tests/android_startup_contract_tests.py")
        # Explicit GPU check: exercise the actual remaining Android startup
        # helper on desktop Vulkan, not a separate simplified implementation.
        add_executable(DKRAndroidTransferGpuTests EXCLUDE_FROM_ALL tests/android_transfer_gpu_tests.cpp)
        target_compile_features(DKRAndroidTransferGpuTests PRIVATE cxx_std_20)
        target_include_directories(DKRAndroidTransferGpuTests PRIVATE "${DKR_RT64_SOURCE}/src" "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_compile_definitions(DKRAndroidTransferGpuTests PRIVATE DKR_GRAPHICS_FAULT_TEST=1 NOMINMAX)
        target_link_libraries(DKRAndroidTransferGpuTests PRIVATE rt64)
        if(WIN32)
            target_link_directories(DKRAndroidTransferGpuTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRAndroidTransferGpuTests PRIVATE SDL2)
            add_custom_command(TARGET DKRAndroidTransferGpuTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRAndroidTransferGpuTests>/SDL2.dll")
        else()
            target_link_libraries(DKRAndroidTransferGpuTests PRIVATE ${SDL2_LIBRARIES})
        endif()
    endif()
    add_executable(DKRGraphicsResourceFailureTests tests/graphics_resource_failure_tests.cpp)
    add_executable(DKRAndroidPipelineCacheTests tests/android_pipeline_cache_tests.cpp)
    target_compile_features(DKRAndroidPipelineCacheTests PRIVATE cxx_std_20)
    add_test(NAME DKRAndroidPipelineCache COMMAND DKRAndroidPipelineCacheTests)
    if(TARGET rt64)
        add_executable(DKRAndroidRenderPassGpuTests EXCLUDE_FROM_ALL tests/android_render_pass_gpu_tests.cpp
            "${DKR_RT64_SOURCE}/src/contrib/plume/plume_vulkan.cpp" "${DKR_RT64_SOURCE}/src/contrib/plume/plume_vulkan.h")
        dkr_stage_patch(DKRAndroidRenderPassGpuTests "${DKR_RT64_SOURCE}" test-android-pass-retention
            "${DKRPORT_ROOT}/patches/android/vulkan-pass-retention.patch" src/contrib/plume/plume_vulkan.cpp)
        configure_file("${DKR_RT64_SOURCE}/src/contrib/plume/plume_vulkan.h"
            "${CMAKE_BINARY_DIR}/generated/test-android-pass-retention/src/contrib/plume/plume_vulkan.h" COPYONLY)
        dkr_stage_patch(DKRAndroidRenderPassGpuTests "${CMAKE_BINARY_DIR}/generated/test-android-pass-retention" test-android-read-barriers
            "${DKRPORT_ROOT}/patches/android/vulkan-read-barriers.patch"
            src/contrib/plume/plume_vulkan.cpp src/contrib/plume/plume_vulkan.h)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/test-android-read-barriers/src/contrib/plume/plume_vulkan.cpp"
            PROPERTIES INCLUDE_DIRECTORIES "${DKR_RT64_SOURCE}/src/contrib/plume;${CMAKE_CURRENT_LIST_DIR}/src/game")
        target_include_directories(DKRAndroidRenderPassGpuTests BEFORE PRIVATE
            "${CMAKE_BINARY_DIR}/generated/test-android-read-barriers/src/contrib/plume")
        target_compile_features(DKRAndroidRenderPassGpuTests PRIVATE cxx_std_20)
        target_compile_definitions(DKRAndroidRenderPassGpuTests PRIVATE NOMINMAX)
        target_include_directories(DKRAndroidRenderPassGpuTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/game")
        target_link_libraries(DKRAndroidRenderPassGpuTests PRIVATE plume)
        if(PLUME_SDL_VULKAN_ENABLED)
            target_link_libraries(DKRAndroidRenderPassGpuTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        add_executable(DKRAndroidUploadGpuTests EXCLUDE_FROM_ALL tests/android_upload_gpu_tests.cpp
            "${DKR_RT64_SOURCE}/src/render/rt64_buffer_uploader.cpp")
        dkr_stage_patch(DKRAndroidUploadGpuTests "${DKR_RT64_SOURCE}" test-android-buffer-lifecycle
            "${DKRPORT_ROOT}/patches/android/buffer-worker-lifecycle.patch" src/render/rt64_buffer_uploader.cpp)
        dkr_stage_patch(DKRAndroidUploadGpuTests "${CMAKE_BINARY_DIR}/generated/test-android-buffer-lifecycle" test-android-small-upload
            "${DKRPORT_ROOT}/patches/android/buffer-small-upload.patch" src/render/rt64_buffer_uploader.cpp)
        set_source_files_properties("${CMAKE_BINARY_DIR}/generated/test-android-small-upload/src/render/rt64_buffer_uploader.cpp"
            PROPERTIES INCLUDE_DIRECTORIES "${DKR_RT64_SOURCE}/src/render;${DKRPORT_ROOT}/runtime-recomp/src/game")
        target_compile_features(DKRAndroidUploadGpuTests PRIVATE cxx_std_20)
        target_compile_definitions(DKRAndroidUploadGpuTests PRIVATE NOMINMAX)
        target_include_directories(DKRAndroidUploadGpuTests PRIVATE "${DKR_RT64_SOURCE}/src"
            "${DKR_RT64_SOURCE}/src/contrib/plume" "${DKRPORT_ROOT}/runtime-recomp/src/game")
        target_link_libraries(DKRAndroidUploadGpuTests PRIVATE rt64)
        # Reference path for controlled upload microbenchmarks, never packaged.
        add_executable(DKRAndroidUploadBaselineTests EXCLUDE_FROM_ALL tests/android_upload_gpu_tests.cpp
            "${DKR_RT64_SOURCE}/src/render/rt64_buffer_uploader.cpp")
        dkr_stage_patch(DKRAndroidUploadBaselineTests "${DKR_RT64_SOURCE}" test-android-upload-baseline
            "${DKRPORT_ROOT}/patches/android/buffer-worker-lifecycle.patch" src/render/rt64_buffer_uploader.cpp)
        target_compile_features(DKRAndroidUploadBaselineTests PRIVATE cxx_std_20)
        target_compile_definitions(DKRAndroidUploadBaselineTests PRIVATE NOMINMAX)
        target_include_directories(DKRAndroidUploadBaselineTests PRIVATE "${DKR_RT64_SOURCE}/src"
            "${DKR_RT64_SOURCE}/src/contrib/plume")
        target_link_libraries(DKRAndroidUploadBaselineTests PRIVATE rt64)
        if(WIN32)
            target_link_directories(DKRAndroidUploadBaselineTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRAndroidUploadBaselineTests PRIVATE SDL2)
            target_link_directories(DKRAndroidUploadGpuTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRAndroidUploadGpuTests PRIVATE SDL2)
        else()
            target_link_libraries(DKRAndroidUploadBaselineTests PRIVATE ${SDL2_LIBRARIES})
            target_link_libraries(DKRAndroidUploadGpuTests PRIVATE ${SDL2_LIBRARIES})
        endif()
    endif()
    target_compile_features(DKRGraphicsResourceFailureTests PRIVATE cxx_std_20)
    target_compile_definitions(DKRGraphicsResourceFailureTests PRIVATE DKR_GRAPHICS_FAULT_TEST=1)
    add_test(NAME DKRGraphicsResourceFailure COMMAND DKRGraphicsResourceFailureTests)
    set_tests_properties(DKRGraphicsResourceFailure PROPERTIES TIMEOUT 5)
    target_compile_features(DKRAndroidStorageTests PRIVATE cxx_std_20)
    add_test(NAME DKRAndroidStorage COMMAND DKRAndroidStorageTests)
    foreach(snapshot_variant IN ITEMS Native AndroidCompatibility)
        add_executable(DKRAtomicSnapshot${snapshot_variant}Tests tests/atomic_snapshot_tests.cpp)
        target_include_directories(DKRAtomicSnapshot${snapshot_variant}Tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/game")
        target_compile_features(DKRAtomicSnapshot${snapshot_variant}Tests PRIVATE cxx_std_20)
        if(snapshot_variant STREQUAL "AndroidCompatibility")
            target_compile_definitions(DKRAtomicSnapshot${snapshot_variant}Tests PRIVATE DKR_TEST_ATOMIC_SNAPSHOT_FALLBACK=1)
        endif()
        add_test(NAME DKRAtomicSnapshot${snapshot_variant} COMMAND DKRAtomicSnapshot${snapshot_variant}Tests)
    endforeach()
    if(DKR_RUNTIME_BUILD_RT64 AND DKR_RUNTIME_BUILD_SDL3_INPUT_HOST)
        add_executable(DKRSdl3InputClientTests
            tests/sdl3_input_client_tests.cpp
            src/game/sdl3_input_client.cpp
        )
        target_include_directories(DKRSdl3InputClientTests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
        )
        target_compile_definitions(DKRSdl3InputClientTests PRIVATE NOMINMAX)
        if(WIN32)
            target_link_libraries(DKRSdl3InputClientTests PRIVATE ws2_32)
        endif()
        add_dependencies(DKRSdl3InputClientTests DKRSdl3InputHost)
        add_test(NAME DKRSdl3InputClient
            COMMAND DKRSdl3InputClientTests
                "$<TARGET_FILE:DKRSdl3InputHost>"
                "${CMAKE_CURRENT_LIST_DIR}/../assets/controllers/gamecontrollerdb.txt"
        )
        add_test(NAME DKRSdl3InputHostSelfTest
            COMMAND DKRSdl3InputHost --self-test --mappings
                "${CMAKE_CURRENT_LIST_DIR}/../assets/controllers/gamecontrollerdb.txt"
        )
    endif()

    add_executable(DKRNetplayCoreTests
        tests/netplay_core_tests.cpp
    )
    target_link_libraries(DKRNetplayCoreTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRNetplayCore COMMAND DKRNetplayCoreTests)

    add_executable(DKRQuickJoinTransportTests
        tests/quick_join_transport_tests.cpp
    )
    target_link_libraries(DKRQuickJoinTransportTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRQuickJoinTransport COMMAND DKRQuickJoinTransportTests)

    add_executable(DKRAuthoritativeStateCodecTests
        tests/authoritative_state_codec_tests.cpp
    )
    target_link_libraries(DKRAuthoritativeStateCodecTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRAuthoritativeStateCodec
             COMMAND DKRAuthoritativeStateCodecTests)

    add_executable(DKRRuntimeStateTests
        tests/runtime_state_tests.cpp
    )
    target_include_directories(DKRRuntimeStateTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game/netplay"
        "${DKR_RECOMP_SOURCE}/include"
    )
    target_link_libraries(DKRRuntimeStateTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRRuntimeState COMMAND DKRRuntimeStateTests)

    add_executable(DKROnlineWaitStateTests
        tests/online_wait_state_tests.cpp
    )
    target_include_directories(DKROnlineWaitStateTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKROnlineWaitState COMMAND DKROnlineWaitStateTests)

    add_executable(DKRRollbackStateStoreTests
        tests/rollback_state_store_tests.cpp
    )
    target_link_libraries(DKRRollbackStateStoreTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRRollbackStateStore COMMAND DKRRollbackStateStoreTests)

    foreach(DKR_TEST_REVISION IN ITEMS 77 80)
        add_executable(DKRRollbackSimulationState${DKR_TEST_REVISION}Tests
            tests/rollback_simulation_state_tests.cpp
            src/game/netplay/authoritative_state.cpp
            src/game/netplay/rollback_simulation_state.cpp
        )
        target_include_directories(DKRRollbackSimulationState${DKR_TEST_REVISION}Tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
        )
        target_compile_definitions(DKRRollbackSimulationState${DKR_TEST_REVISION}Tests PRIVATE
            DKR_TEST_REVISION=${DKR_TEST_REVISION}
        )
        add_test(NAME DKRRollbackSimulationState${DKR_TEST_REVISION}
                 COMMAND DKRRollbackSimulationState${DKR_TEST_REVISION}Tests)
    endforeach()

    add_executable(DKRRollbackDriverTests
        tests/rollback_driver_tests.cpp
    )
    target_link_libraries(DKRRollbackDriverTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRRollbackDriver COMMAND DKRRollbackDriverTests)

    add_executable(DKRExperimentalRollbackTests tests/experimental_rollback_tests.cpp)
    target_link_libraries(DKRExperimentalRollbackTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRExperimentalRollback COMMAND DKRExperimentalRollbackTests)
    add_executable(DKRLocalSceneryTests tests/local_scenery_tests.cpp)
    target_link_libraries(DKRLocalSceneryTests PRIVATE DKRNetplayCore)
    if(MSVC)
        target_compile_options(DKRLocalSceneryTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(DKRLocalSceneryTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME DKRLocalScenery COMMAND DKRLocalSceneryTests)
    if(DKR_EXPERIMENTAL_OWNED_GENERATED)
        add_executable(DKRCheckedMemoryTests
            tests/replay_probe/checked_memory_tests.cpp tests/replay_probe/probe_bridge.c)
        target_include_directories(DKRCheckedMemoryTests BEFORE PRIVATE
            "${DKR_EXPERIMENTAL_OWNED_GENERATED}" tests/replay_probe src/game)
        target_compile_features(DKRCheckedMemoryTests PRIVATE c_std_11 cxx_std_20)
        if(MSVC)
            target_compile_options(DKRCheckedMemoryTests PRIVATE /UNDEBUG /fp:strict)
        else()
            target_compile_options(DKRCheckedMemoryTests PRIVATE -UNDEBUG -fno-strict-aliasing)
            target_link_libraries(DKRCheckedMemoryTests PRIVATE m)
        endif()
        add_test(NAME DKRCheckedMemory COMMAND DKRCheckedMemoryTests)
    endif()
    add_executable(DKRPresentationOwnershipTests tests/experimental_presentation_tests.cpp src/game/renderer_snapshot.cpp)
    target_link_libraries(DKRPresentationOwnershipTests PRIVATE DKRNetplayCore)
    if(MSVC)
        target_compile_options(DKRPresentationOwnershipTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(DKRPresentationOwnershipTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME DKRPresentationOwnership COMMAND DKRPresentationOwnershipTests)
    add_executable(DKRExperimentalOwnerInputTests tests/experimental_owner_input_tests.cpp)
    target_link_libraries(DKRExperimentalOwnerInputTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRExperimentalOwnerInput COMMAND DKRExperimentalOwnerInputTests)
    add_executable(DKRExperimentalTimelineTests tests/experimental_timeline_tests.cpp)
    target_link_libraries(DKRExperimentalTimelineTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRExperimentalTimeline COMMAND DKRExperimentalTimelineTests)
    add_executable(DKRExperimentalEpochTests tests/experimental_epoch_tests.cpp)
    target_link_libraries(DKRExperimentalEpochTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRExperimentalEpoch COMMAND DKRExperimentalEpochTests)
    add_executable(DKRExperimentalSessionTests tests/experimental_session_tests.cpp)
    target_link_libraries(DKRExperimentalSessionTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRExperimentalSession COMMAND DKRExperimentalSessionTests)
    add_executable(DKRExperimentalNetworkTests tests/experimental_network_tests.cpp)
    target_link_libraries(DKRExperimentalNetworkTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRExperimentalNetwork COMMAND DKRExperimentalNetworkTests)
    add_executable(DKRExperimentalLaunchTests tests/experimental_launch_tests.cpp)
    target_link_libraries(DKRExperimentalLaunchTests PRIVATE DKRNetplayCore)
    add_executable(DKRExperimentalLobbyAdmissionTests tests/experimental_lobby_admission_tests.cpp)
    target_link_libraries(DKRExperimentalLobbyAdmissionTests PRIVATE DKRNetplayCore)
    if(MSVC)
        target_compile_options(DKRExperimentalLobbyAdmissionTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(DKRExperimentalLobbyAdmissionTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME DKRExperimentalLobbyAdmission COMMAND DKRExperimentalLobbyAdmissionTests)
    add_test(NAME DKRExperimentalLaunch COMMAND DKRExperimentalLaunchTests)
    add_executable(DKRExperimentalPcmTests tests/experimental_pcm_tests.cpp)
    target_link_libraries(DKRExperimentalPcmTests PRIVATE DKRNetplayCore)
    if(MSVC)
        target_compile_options(DKRExperimentalPcmTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(DKRExperimentalPcmTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME DKRExperimentalPcm COMMAND DKRExperimentalPcmTests)
    if(WIN32 OR (UNIX AND NOT ANDROID AND NOT APPLE))
        add_executable(DKRExperimentalProcessTests tests/experimental_process_tests.cpp src/game/netplay/experimental_process.cpp)
        target_include_directories(DKRExperimentalProcessTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/game")
        add_test(NAME DKRExperimentalProcess COMMAND DKRExperimentalProcessTests)
        set_tests_properties(DKRExperimentalProcess PROPERTIES TIMEOUT 15)
    endif()
    set_tests_properties(DKRExperimentalNetwork PROPERTIES TIMEOUT 60)
    add_executable(DKRExperimentalCheckpointHashTests tests/experimental_checkpoint_hash_tests.cpp)
    target_link_libraries(DKRExperimentalCheckpointHashTests PRIVATE DKRNetplayCore)
    target_include_directories(DKRExperimentalCheckpointHashTests PRIVATE "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
    add_test(NAME DKRExperimentalCheckpointHash COMMAND DKRExperimentalCheckpointHashTests)
    set_tests_properties(DKRExperimentalSession PROPERTIES TIMEOUT 60)
    add_executable(DKRExperimentalEepromTests tests/experimental_eeprom_tests.cpp)
    target_link_libraries(DKRExperimentalEepromTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRExperimentalEeprom COMMAND DKRExperimentalEepromTests)

    add_executable(DKRSecureChannelTests
        tests/secure_channel_tests.cpp
    )
    target_link_libraries(DKRSecureChannelTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRSecureChannel COMMAND DKRSecureChannelTests)

    add_executable(DKRFriendServiceTests
        tests/friend_service_tests.cpp
    )
    target_link_libraries(DKRFriendServiceTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRFriendService COMMAND DKRFriendServiceTests)
    if(DKR_NETPLAY_WEBRTC)
        add_executable(DKRFriendDeliveryTests tests/friend_delivery_tests.cpp)
        target_link_libraries(DKRFriendDeliveryTests PRIVATE DKRNetplayCore)
        target_include_directories(DKRFriendDeliveryTests PRIVATE "${DKR_LIBDATACHANNEL_SOURCE}/deps/json/include")
        if(MSVC)
            target_compile_options(DKRFriendDeliveryTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRFriendDeliveryTests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRFriendDelivery COMMAND DKRFriendDeliveryTests)
        set_tests_properties(DKRFriendDelivery PROPERTIES TIMEOUT 20)
        add_test(NAME DKRFriendLocalIntegration COMMAND DKRFriendDeliveryTests --integration)
        set_tests_properties(DKRFriendLocalIntegration PROPERTIES TIMEOUT 150)
        add_executable(DKRQuickJoinLifecycleTests tests/quick_join_lifecycle_tests.cpp)
        target_link_libraries(DKRQuickJoinLifecycleTests PRIVATE DKRNetplayCore)
        target_include_directories(DKRQuickJoinLifecycleTests PRIVATE "${DKR_LIBDATACHANNEL_SOURCE}/deps/json/include")
        if(MSVC)
            target_compile_options(DKRQuickJoinLifecycleTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRQuickJoinLifecycleTests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRQuickJoinLifecycle COMMAND DKRQuickJoinLifecycleTests)
        set_tests_properties(DKRQuickJoinLifecycle PROPERTIES TIMEOUT 90)
        add_executable(DKRExperimentalQuickJoinTests tests/experimental_network_tests.cpp)
        target_link_libraries(DKRExperimentalQuickJoinTests PRIVATE DKRNetplayCore)
        target_include_directories(DKRExperimentalQuickJoinTests PRIVATE "${DKR_LIBDATACHANNEL_SOURCE}/deps/json/include")
        target_compile_definitions(DKRExperimentalQuickJoinTests PRIVATE DKR_EXPERIMENTAL_QUICKJOIN_TEST=1)
        if(MSVC)
            target_compile_options(DKRExperimentalQuickJoinTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRExperimentalQuickJoinTests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRExperimentalQuickJoin COMMAND DKRExperimentalQuickJoinTests)
        set_tests_properties(DKRExperimentalQuickJoin PROPERTIES TIMEOUT 90)
    endif()

    add_executable(DKRDatagramSocketTests
        tests/datagram_socket_tests.cpp
    )
    target_link_libraries(DKRDatagramSocketTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRDatagramSocket COMMAND DKRDatagramSocketTests)

    add_executable(DKRDirectSessionTests
        tests/direct_session_tests.cpp
    )
    target_link_libraries(DKRDirectSessionTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRDirectSession COMMAND DKRDirectSessionTests)
    add_executable(DKRNetplayInputLatencyTests tests/netplay_input_latency_tests.cpp)
    target_link_libraries(DKRNetplayInputLatencyTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRNetplayInputLatency COMMAND DKRNetplayInputLatencyTests)

    add_executable(DKROnlineRosterPolicyTests
        tests/online_roster_policy_tests.cpp
    )
    target_include_directories(DKROnlineRosterPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKROnlineRosterPolicy COMMAND DKROnlineRosterPolicyTests)

    add_executable(DKROnlineInputPolicyTests
        tests/online_input_policy_tests.cpp
    )
    target_include_directories(DKROnlineInputPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKROnlineInputPolicy COMMAND DKROnlineInputPolicyTests)

    add_executable(DKROnlineInputBrokerTests
        tests/online_input_broker_tests.cpp
    )
    target_link_libraries(DKROnlineInputBrokerTests PRIVATE DKRNetplayCore)
    add_test(NAME DKROnlineInputBroker COMMAND DKROnlineInputBrokerTests)

    foreach(DKR_TEST_REVISION IN ITEMS 77 80)
        add_executable(DKRDeterminismStateHash${DKR_TEST_REVISION}Tests
            tests/determinism_state_hash_tests.cpp
            src/game/determinism_state_hash.cpp
            src/game/netplay/authoritative_state.cpp
        )
        target_include_directories(DKRDeterminismStateHash${DKR_TEST_REVISION}Tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
        )
        target_compile_definitions(DKRDeterminismStateHash${DKR_TEST_REVISION}Tests PRIVATE
            DKR_TEST_REVISION=${DKR_TEST_REVISION}
        )
        add_test(NAME DKRDeterminismStateHash${DKR_TEST_REVISION}
                 COMMAND DKRDeterminismStateHash${DKR_TEST_REVISION}Tests)
    endforeach()

    add_executable(DKRReplayRecorderTests
        tests/replay_recorder_tests.cpp
    )
    target_link_libraries(DKRReplayRecorderTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRReplayRecorder COMMAND DKRReplayRecorderTests)

    add_executable(DKRNetplayAdverseNetworkTests
        tests/netplay_adverse_network_tests.cpp
    )
    target_link_libraries(DKRNetplayAdverseNetworkTests PRIVATE DKRNetplayCore)
    add_test(NAME DKRNetplayAdverseNetwork COMMAND DKRNetplayAdverseNetworkTests)

    add_executable(DKRRomRevisionTests
        tests/rom_revision_tests.cpp
        src/game/rom_revision.cpp
        src/game/startup_performance.cpp
    )
    target_include_directories(DKRRomRevisionTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
        "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty"
    )
    if(DKR_TEST_ROM_V77 AND DKR_TEST_ROM_V80_Z64 AND DKR_TEST_ROM_V80_SWAPPED)
        add_test(NAME DKRRomRevision
            COMMAND DKRRomRevisionTests
                "${DKR_TEST_ROM_V80_Z64}"
                "${DKR_TEST_ROM_V80_SWAPPED}"
                "${DKR_TEST_ROM_V77}")
    endif()

    add_executable(DKRRevisionAddressesTests
        tests/revision_addresses_tests.cpp
    )
    target_include_directories(DKRRevisionAddressesTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRRevisionAddresses COMMAND DKRRevisionAddressesTests)

    add_executable(DKRVehicleContextPolicyTests
        tests/vehicle_context_policy_tests.cpp
    )
    target_include_directories(DKRVehicleContextPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRVehicleContextPolicy
             COMMAND DKRVehicleContextPolicyTests)

    add_executable(DKRSteeringWheelPolicyTests
        tests/steering_wheel_policy_tests.cpp
    )
    target_include_directories(DKRSteeringWheelPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRSteeringWheelPolicy
             COMMAND DKRSteeringWheelPolicyTests)

    add_executable(DKRPresentationPolicyTests
        tests/presentation_policy_tests.cpp
    )
    target_include_directories(DKRPresentationPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRPresentationPolicy COMMAND DKRPresentationPolicyTests)

    add_executable(DKRFinishPresentationTests tests/finish_presentation_tests.cpp)
    target_include_directories(DKRFinishPresentationTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/game")
    if(MSVC)
        target_compile_options(DKRFinishPresentationTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(DKRFinishPresentationTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME DKRFinishPresentation COMMAND DKRFinishPresentationTests)

    add_executable(DKRPresentationCounterPolicyTests
        tests/presentation_counter_policy_tests.cpp
    )
    target_include_directories(DKRPresentationCounterPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRPresentationCounterPolicy
             COMMAND DKRPresentationCounterPolicyTests)

    add_executable(DKRPresentationRandomPolicyTests
        tests/presentation_random_policy_tests.cpp
    )
    target_include_directories(DKRPresentationRandomPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRPresentationRandomPolicy
             COMMAND DKRPresentationRandomPolicyTests)

    add_executable(DKRModernCameraPolicyTests
        tests/modern_camera_policy_tests.cpp
    )
    target_include_directories(DKRModernCameraPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRModernCameraPolicy COMMAND DKRModernCameraPolicyTests)

    add_executable(DKRWidescreenPolicyTests
        tests/widescreen_policy_tests.cpp
    )
    target_include_directories(DKRWidescreenPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRWidescreenPolicy COMMAND DKRWidescreenPolicyTests)

    if(DKR_RUNTIME_BUILD_RT64 AND TARGET DKRPortGame)
        add_executable(DKRSplitScreenRT64Tests tests/split_screen_rt64_tests.cpp)
        target_include_directories(DKRSplitScreenRT64Tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_link_libraries(DKRSplitScreenRT64Tests PRIVATE rt64)
        target_compile_definitions(DKRSplitScreenRT64Tests PRIVATE NOMINMAX)
        if(WIN32)
            target_link_directories(DKRSplitScreenRT64Tests PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRSplitScreenRT64Tests PRIVATE SDL2)
            add_custom_command(TARGET DKRSplitScreenRT64Tests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRSplitScreenRT64Tests>/SDL2.dll")
        else()
            target_link_libraries(DKRSplitScreenRT64Tests PRIVATE ${SDL2_LIBRARIES})
        endif()
        if(MSVC)
            target_compile_options(DKRSplitScreenRT64Tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRSplitScreenRT64Tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRSplitScreenRT64 COMMAND DKRSplitScreenRT64Tests)

        add_executable(DKRWaterUVTests tests/water_uv_rt64_tests.cpp)
        target_include_directories(DKRWaterUVTests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_link_libraries(DKRWaterUVTests PRIVATE rt64)
        target_compile_definitions(DKRWaterUVTests PRIVATE NOMINMAX)
        if(WIN32)
            target_link_directories(DKRWaterUVTests PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRWaterUVTests PRIVATE SDL2)
            add_custom_command(TARGET DKRWaterUVTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRWaterUVTests>/SDL2.dll")
        else()
            target_link_libraries(DKRWaterUVTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        if(MSVC)
            target_compile_options(DKRWaterUVTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRWaterUVTests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRWaterUV COMMAND DKRWaterUVTests)

        add_executable(DKRLiveLodTests tests/live_lod_tests.cpp)
        add_executable(DKRHudGroupsTests tests/hud_group_layout_tests.cpp)
        target_include_directories(DKRHudGroupsTests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_link_libraries(DKRHudGroupsTests PRIVATE rt64)
        target_compile_definitions(DKRHudGroupsTests PRIVATE NOMINMAX)
        if(WIN32)
            target_link_directories(DKRHudGroupsTests PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRHudGroupsTests PRIVATE SDL2)
            add_custom_command(TARGET DKRHudGroupsTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRHudGroupsTests>/SDL2.dll")
        else()
            target_link_libraries(DKRHudGroupsTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        if(MSVC)
            target_compile_options(DKRHudGroupsTests PRIVATE /UNDEBUG)
        else()
            target_compile_options(DKRHudGroupsTests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRHudGroups COMMAND DKRHudGroupsTests)

        add_executable(DKRHudEditorTests tests/hud_basic_settings_tests.cpp
            src/game/generated/racing_banana_font.c src/game/generated/jumpman_font.c
            src/game/generated/selawik_font.c
            "${DKR_RT64_SOURCE}/src/contrib/imgui/backends/imgui_impl_sdlrenderer2.cpp")
        target_include_directories(DKRHudEditorTests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_link_libraries(DKRHudEditorTests PRIVATE rt64)
        target_compile_definitions(DKRHudEditorTests PRIVATE NOMINMAX)
        if(WIN32)
            target_link_directories(DKRHudEditorTests PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRHudEditorTests PRIVATE SDL2)
            add_custom_command(TARGET DKRHudEditorTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRHudEditorTests>/SDL2.dll")
        else()
            target_link_libraries(DKRHudEditorTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        if(MSVC)
            target_compile_options(DKRHudEditorTests PRIVATE /UNDEBUG)
            target_link_options(DKRHudEditorTests PRIVATE /STACK:262144)
        else()
            target_compile_options(DKRHudEditorTests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME DKRHudBasicSettings COMMAND DKRHudEditorTests)

        target_include_directories(DKRLiveLodTests PRIVATE
            "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_link_libraries(DKRLiveLodTests PRIVATE rt64)
        target_compile_definitions(DKRLiveLodTests PRIVATE NOMINMAX)
        if(WIN32)
            target_link_directories(DKRLiveLodTests PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRLiveLodTests PRIVATE SDL2)
            add_custom_command(TARGET DKRLiveLodTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRLiveLodTests>/SDL2.dll")
        else()
            target_link_libraries(DKRLiveLodTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        add_test(NAME DKRLiveLod COMMAND DKRLiveLodTests)
        add_executable(DKRGeneratedMipsTests tests/generated_mips_tests.cpp)
        target_include_directories(DKRGeneratedMipsTests PRIVATE "${DKR_RT64_SOURCE}/src/render")
        target_include_directories(DKRGeneratedMipsTests PRIVATE "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_compile_definitions(DKRGeneratedMipsTests PRIVATE NOMINMAX)
        target_link_libraries(DKRGeneratedMipsTests PRIVATE rt64)
        if(WIN32)
            target_link_directories(DKRGeneratedMipsTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRGeneratedMipsTests PRIVATE SDL2)
        else()
            target_link_libraries(DKRGeneratedMipsTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        add_test(NAME DKRGeneratedMips COMMAND DKRGeneratedMipsTests)
        add_executable(DKRMipmapLoadingTests tests/mipmap_loading_tests.cpp)
        target_include_directories(DKRMipmapLoadingTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src" "$<TARGET_PROPERTY:DKRPortGame,INCLUDE_DIRECTORIES>")
        target_compile_definitions(DKRMipmapLoadingTests PRIVATE NOMINMAX)
        target_link_libraries(DKRMipmapLoadingTests PRIVATE rt64)
        if(WIN32)
            target_link_directories(DKRMipmapLoadingTests PRIVATE "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRMipmapLoadingTests PRIVATE SDL2)
            foreach(mipmap_test DKRGeneratedMipsTests DKRMipmapLoadingTests)
                add_custom_command(TARGET ${mipmap_test} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                        "$<TARGET_FILE_DIR:${mipmap_test}>/SDL2.dll"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxcompiler.dll"
                        "$<TARGET_FILE_DIR:${mipmap_test}>/dxcompiler.dll"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxil.dll"
                        "$<TARGET_FILE_DIR:${mipmap_test}>/dxil.dll")
            endforeach()
        else()
            target_link_libraries(DKRMipmapLoadingTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        # Explicit GPU/window check, not a default headless CTest or package step.
        add_test(NAME DKRLiveLodPipeline
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/tests/live_lod_pipeline_tests.py")
    endif()

    add_executable(DKRHudLayoutPolicyTests
        tests/hud_layout_policy_tests.cpp
    )
    target_include_directories(DKRHudLayoutPolicyTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/game
    )
    add_test(NAME DKRHudLayoutPolicy COMMAND DKRHudLayoutPolicyTests)

    add_executable(DKRViPresentationPolicyTests
        tests/vi_presentation_policy_tests.cpp
    )
    target_include_directories(DKRViPresentationPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRViPresentationPolicy COMMAND DKRViPresentationPolicyTests)

    add_executable(DKRMagicCodePolicyTests
        tests/magic_code_policy_tests.cpp
    )
    target_include_directories(DKRMagicCodePolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRMagicCodePolicy COMMAND DKRMagicCodePolicyTests)

    add_executable(DKRMagicCodeRuntimeTests
        tests/magic_code_runtime_tests.cpp
        src/game/runtime_magic_codes.cpp
    )
    target_include_directories(DKRMagicCodeRuntimeTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
        "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp/include"
    )
    find_package(Threads REQUIRED)
    target_link_libraries(DKRMagicCodeRuntimeTests PRIVATE Threads::Threads)
    add_test(NAME DKRMagicCodeRuntime COMMAND DKRMagicCodeRuntimeTests)
    if(DKR_RUNTIME_BUILD_GENERATED)
        add_test(NAME DKRMagicCodePipeline
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/tests/magic_code_pipeline_tests.py"
                "${DKR_GENERATED_SOURCE_V77}" "${DKR_GENERATED_SOURCE_V80}")
    endif()

    add_executable(DKRQuickRestartPolicyTests
        tests/quick_restart_policy_tests.cpp
    )
    target_include_directories(DKRQuickRestartPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRQuickRestartPolicy COMMAND DKRQuickRestartPolicyTests)

    add_executable(DKRCharacterSelectAnimationPolicyTests
        tests/character_select_animation_policy_tests.cpp
    )
    target_include_directories(DKRCharacterSelectAnimationPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRCharacterSelectAnimationPolicy
             COMMAND DKRCharacterSelectAnimationPolicyTests)

    add_executable(DKRCharacterSelectMusicPolicyTests
        tests/character_select_music_policy_tests.cpp
    )
    target_include_directories(DKRCharacterSelectMusicPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRCharacterSelectMusicPolicy
             COMMAND DKRCharacterSelectMusicPolicyTests)

    add_executable(DKRMotionSteeringPolicyTests
        tests/motion_steering_policy_tests.cpp
    )
    target_include_directories(DKRMotionSteeringPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRMotionSteeringPolicy COMMAND DKRMotionSteeringPolicyTests)

    add_executable(DKRSaveManagerTests
        tests/save_manager_tests.cpp
        src/game/dkr_save_codec.cpp
        src/game/save_manager.cpp
    )
    target_include_directories(DKRSaveManagerTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRSaveManager COMMAND DKRSaveManagerTests)

    add_executable(DKRSaveCodecTests
        tests/dkr_save_codec_tests.cpp
        src/game/dkr_save_codec.cpp
    )
    target_include_directories(DKRSaveCodecTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRSaveCodec COMMAND DKRSaveCodecTests)

    add_executable(DKRAudioEqualizerTests
        tests/audio_equalizer_tests.cpp
        src/game/audio_equalizer.cpp
    )
    target_include_directories(DKRAudioEqualizerTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRAudioEqualizer COMMAND DKRAudioEqualizerTests)

    add_executable(DKRAudioMixPolicyTests
        tests/audio_mix_policy_tests.cpp
    )
    target_include_directories(DKRAudioMixPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRAudioMixPolicy COMMAND DKRAudioMixPolicyTests)

    add_executable(DKRCountdownTonePolicyTests
        tests/countdown_tone_policy_tests.cpp
    )
    target_include_directories(DKRCountdownTonePolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRCountdownTonePolicy COMMAND DKRCountdownTonePolicyTests)


    add_executable(DKRRendererSnapshotTests
        tests/renderer_snapshot_tests.cpp
        src/game/renderer_snapshot.cpp
    )
    target_include_directories(DKRRendererSnapshotTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRRendererSnapshot COMMAND DKRRendererSnapshotTests)

    add_executable(DKRPresentationIdentityTests
        tests/presentation_identity_tests.cpp
    )
    target_include_directories(DKRPresentationIdentityTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRPresentationIdentity COMMAND DKRPresentationIdentityTests)

    add_executable(DKRInterpolationStatePolicyTests
        tests/interpolation_state_policy_tests.cpp
    )
    target_include_directories(DKRInterpolationStatePolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRInterpolationStatePolicy
          COMMAND DKRInterpolationStatePolicyTests)

    if(DKR_RUNTIME_BUILD_RT64)
        add_executable(DKRCustomTracksTests
            tests/custom_tracks_tests.cpp
            src/game/custom_tracks.cpp
            "${DKR_RT64_SOURCE}/src/contrib/miniz/miniz.c"
        )
        target_include_directories(DKRCustomTracksTests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "${DKR_RT64_SOURCE}/src/contrib"
        )
        add_test(NAME DKRCustomTracks COMMAND DKRCustomTracksTests)
        add_executable(DKRCustomTracksRuntimeTests
            tests/custom_tracks_runtime_tests.cpp
            src/game/custom_tracks_hooks.cpp
            src/game/custom_tracks.cpp
            "${DKR_RT64_SOURCE}/src/contrib/miniz/miniz.c"
        )
        target_include_directories(DKRCustomTracksRuntimeTests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game"
            "${DKR_RT64_SOURCE}/src/contrib"
            "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp/include"
        )
        add_test(NAME DKRCustomTracksRuntime COMMAND DKRCustomTracksRuntimeTests)
        add_executable(DKRCustomTracksGuestTests
            tests/custom_tracks_guest_tests.cpp
            src/game/custom_tracks.cpp src/game/custom_tracks_hooks.cpp
            "${DKR_RT64_SOURCE}/src/contrib/miniz/miniz.c")
        target_include_directories(DKRCustomTracksGuestTests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src/game" "${DKR_RT64_SOURCE}/src/contrib"
            "${DKR_RECOMP_SOURCE}/include")
        add_test(NAME DKRCustomTracksGuest COMMAND DKRCustomTracksGuestTests)
    endif()
    add_executable(DKRPresentationMarkerPolicyTests
        tests/presentation_marker_policy_tests.cpp)
    target_include_directories(DKRPresentationMarkerPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game")
    add_test(NAME DKRPresentationMarkerPolicy COMMAND DKRPresentationMarkerPolicyTests)

      add_executable(DKRRiceTexturePackPolicyTests
          tests/rice_texture_pack_policy_tests.cpp
      )
      target_include_directories(DKRRiceTexturePackPolicyTests PRIVATE
          "${CMAKE_CURRENT_LIST_DIR}/src/game"
      )
      add_test(NAME DKRRiceTexturePackPolicy
          COMMAND DKRRiceTexturePackPolicyTests)

    add_executable(DKRTexturePackBrowserPolicyTests
        tests/texture_pack_browser_policy_tests.cpp
    )
    target_include_directories(DKRTexturePackBrowserPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRTexturePackBrowserPolicy
             COMMAND DKRTexturePackBrowserPolicyTests)

    add_executable(DKRLauncherRenderPolicyTests tests/launcher_render_policy_tests.cpp)
    target_include_directories(DKRLauncherRenderPolicyTests PRIVATE src/game)
    add_test(NAME DKRLauncherRenderPolicy COMMAND DKRLauncherRenderPolicyTests)
    if(DKR_RUNTIME_BUILD_RT64 AND TARGET rt64)
        add_executable(DKRLauncherSoftwarePixelsTests tests/launcher_software_pixels_tests.cpp)
        target_include_directories(DKRLauncherSoftwarePixelsTests PRIVATE src/game)
        if(WIN32)
            target_include_directories(DKRLauncherSoftwarePixelsTests PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include")
            target_link_directories(DKRLauncherSoftwarePixelsTests PRIVATE
                "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
            target_link_libraries(DKRLauncherSoftwarePixelsTests PRIVATE SDL2)
            add_custom_command(TARGET DKRLauncherSoftwarePixelsTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DKR_RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
                    "$<TARGET_FILE_DIR:DKRLauncherSoftwarePixelsTests>/SDL2.dll")
        else()
            target_include_directories(DKRLauncherSoftwarePixelsTests PRIVATE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(DKRLauncherSoftwarePixelsTests PRIVATE ${SDL2_LIBRARIES})
        endif()
        add_test(NAME DKRLauncherSoftwarePixels COMMAND DKRLauncherSoftwarePixelsTests)
    endif()

    add_executable(DKRUiNotificationPolicyTests
        tests/ui_notification_policy_tests.cpp
    )
    target_include_directories(DKRUiNotificationPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRUiNotificationPolicy
             COMMAND DKRUiNotificationPolicyTests)

    add_executable(DKRIntroTailPolicyTests
          tests/intro_tail_policy_tests.cpp
      )
      target_include_directories(DKRIntroTailPolicyTests PRIVATE
          "${CMAKE_CURRENT_LIST_DIR}/src/game"
      )
    add_test(NAME DKRIntroTailPolicy COMMAND DKRIntroTailPolicyTests)

    add_executable(DKRSchedulerEventPolicyTests
        tests/scheduler_event_policy_tests.cpp
    )
    target_include_directories(DKRSchedulerEventPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRSchedulerEventPolicy
             COMMAND DKRSchedulerEventPolicyTests)

    add_executable(DKRControllerAssignmentPolicyTests
        tests/controller_assignment_policy_tests.cpp
    )
    target_include_directories(DKRControllerAssignmentPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRControllerAssignmentPolicy
             COMMAND DKRControllerAssignmentPolicyTests)

    add_executable(DKRInputBackendPolicyTests
        tests/input_backend_policy_tests.cpp
    )
    target_include_directories(DKRInputBackendPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRInputBackendPolicy
             COMMAND DKRInputBackendPolicyTests)

    add_executable(DKRControllerMappingPolicyTests
        tests/controller_mapping_policy_tests.cpp
    )
    target_include_directories(DKRControllerMappingPolicyTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    add_test(NAME DKRControllerMappingPolicy
             COMMAND DKRControllerMappingPolicyTests)

    add_executable(DKRControllerDatabaseTests
        tests/controller_database_tests.cpp
    )
    add_test(NAME DKRControllerDatabase
             COMMAND DKRControllerDatabaseTests
                     "${CMAKE_CURRENT_LIST_DIR}/../assets/controllers/gamecontrollerdb.txt")

    add_executable(DKRInputProfileTests
        tests/input_profile_tests.cpp
        src/game/runtime_input.cpp
    )
    target_include_directories(DKRInputProfileTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src/game"
    )
    target_compile_definitions(DKRInputProfileTests PRIVATE
        DKR_RUNTIME_HAS_RT64=0
    )
    add_test(NAME DKRInputProfile COMMAND DKRInputProfileTests)

    # These executables intentionally use assert() as their zero-dependency
    # test primitive. RelWithDebInfo normally defines NDEBUG, which used to turn
    # every assertion into a no-op in the exact configuration shipped to users.
    set(DKR_ASSERT_TEST_TARGETS
        DKRExperimentalCheckpointHashTests
        DKRNetplayCoreTests
        DKRRollbackDriverTests
        DKRExperimentalRollbackTests
        DKRExperimentalOwnerInputTests
        DKRExperimentalTimelineTests
        DKRExperimentalEpochTests
        DKRExperimentalSessionTests
        DKRExperimentalNetworkTests
        DKRExperimentalLaunchTests
        DKRExperimentalEepromTests
        DKRRuntimeStateTests
        DKRSecureChannelTests
        DKRDatagramSocketTests
        DKRDirectSessionTests
        DKRNetplayInputLatencyTests
        DKROnlineRosterPolicyTests
        DKRReplayRecorderTests
        DKRNetplayAdverseNetworkTests
        DKRPresentationPolicyTests
        DKRPresentationRandomPolicyTests
        DKRModernCameraPolicyTests
        DKRWidescreenPolicyTests
        DKRHudLayoutPolicyTests
        DKRViPresentationPolicyTests
        DKRMagicCodePolicyTests
        DKRMagicCodeRuntimeTests
        DKRQuickRestartPolicyTests
        DKRCharacterSelectAnimationPolicyTests
        DKRCharacterSelectMusicPolicyTests
        DKRMotionSteeringPolicyTests
        DKRSaveManagerTests
        DKRSaveCodecTests
        DKRAudioEqualizerTests
        DKRAudioMixPolicyTests
        DKRCountdownTonePolicyTests
        DKRRendererSnapshotTests
        DKRPresentationIdentityTests
        DKRInterpolationStatePolicyTests
        DKRPresentationMarkerPolicyTests
        DKRRiceTexturePackPolicyTests
        DKRUiNotificationPolicyTests
        DKRIntroTailPolicyTests
        DKRControllerAssignmentPolicyTests
        DKRInputBackendPolicyTests
        DKRControllerMappingPolicyTests
        DKRControllerDatabaseTests
        DKRInputProfileTests
        DKROnlineInputPolicyTests
        DKRRomRevisionTests
        DKRRevisionAddressesTests
        DKRVehicleContextPolicyTests
        DKRSteeringWheelPolicyTests
        DKRPresentationCounterPolicyTests
        DKRDeterminismStateHash77Tests
        DKRDeterminismStateHash80Tests
    )
    if(DKR_RUNTIME_BUILD_RT64)
        list(APPEND DKR_ASSERT_TEST_TARGETS DKRCustomTracksTests DKRCustomTracksRuntimeTests DKRCustomTracksGuestTests)
    endif()
    foreach(DKR_ASSERT_TEST_TARGET IN LISTS DKR_ASSERT_TEST_TARGETS)
        if(MSVC)
            target_compile_options(${DKR_ASSERT_TEST_TARGET} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${DKR_ASSERT_TEST_TARGET} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()
