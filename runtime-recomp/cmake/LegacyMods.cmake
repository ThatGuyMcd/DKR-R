# Project-owned build boundary. Upstream sources are hash-pinned and never
# patched in place. The decoder has no shell/CLI/external-decompressor support.
include(FetchContent)
set(_dkr_xdelta_rev ff322e592383227b0d65ddfde7e0e5bbc504dc15) # v3.2.0
set(_dkr_xdelta_dir "${CMAKE_BINARY_DIR}/_deps/dkr-xdelta-3.2.0")
file(MAKE_DIRECTORY "${_dkr_xdelta_dir}")
function(dkr_xdelta_file name digest)
    set(path "${_dkr_xdelta_dir}/${name}")
    if(EXISTS "${path}")
        file(SHA256 "${path}" actual)
        string(TOLOWER "${digest}" digest)
        if(actual STREQUAL digest)
            return()
        endif()
    endif()
    file(DOWNLOAD
        "https://raw.githubusercontent.com/jmacd/xdelta/${_dkr_xdelta_rev}/xdelta3/${name}"
        "${path}" EXPECTED_HASH "SHA256=${digest}" TLS_VERIFY ON)
endfunction()
dkr_xdelta_file(xdelta3.c 7859a6c730de096f9d3a342d43de33df2d1ea7119e179d938a75aba8a52ea9ab)
dkr_xdelta_file(xdelta3.h 72371206cc4b7ee5a9c7f52c56e1eb92d204756db433a718caae2062097382f2)
dkr_xdelta_file(xdelta3-internal.h f6069453f85268923a63b0af2d539c8e6780b92bd8116fac0cc7ac1853709a12)
dkr_xdelta_file(xdelta3-list.h b42f583b58ae35bec6540abc4559a9b512ed9780cb07e866644e542e532bac77)
dkr_xdelta_file(xdelta3-hash.h 4c7cdd63911920af0b4eb6f4991ef72644eb5fc38c1a00ac51ae31385da830c8)
dkr_xdelta_file(xdelta3-cfgs.h 195986fd77604e48731d6c9df49ef1e4aa2e069522a57af9ac3b3b337e226214)
dkr_xdelta_file(xdelta3-decode.h a004e307019a64a72516545c4795ab7c0443db00b52fae8e64c9e02924f43eaa)
dkr_xdelta_file(xdelta3-second.h 54f29bb83094aaadadca79cea16b88aa2a1ca9a2f4926f1696effa59458e6df8)
dkr_xdelta_file(xdelta3-fgk.h eaa5461cb0593b2b2a8124f39ba1d9ded989b72f45b44f57a9838484bff8aacb)
dkr_xdelta_file(xdelta3-djw.h aad88a9e1c3dfeedc7b02dfdc496fd2836b4e653a1f79e4408987f45c5257f22)
dkr_xdelta_file(xdelta3-lzma.h 19bdc41f3f635afda6278b1e2981931cb9af3167ffd1f58125095418e91221b2)

# Configure liblzma in a child scope, keeping the application's other
# dependency/test/shared-library choices intact.
function(dkr_mods_lzma)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(XZ_NLS OFF)
    set(XZ_TOOL_XZ OFF)
    set(XZ_TOOL_XZDEC OFF)
    set(XZ_TOOL_LZMADEC OFF)
    set(XZ_TOOL_LZMAINFO OFF)
    set(XZ_TOOL_SCRIPTS OFF)
    set(XZ_TOOL_SYMLINKS OFF)
    set(XZ_DOC OFF)
    FetchContent_Declare(dkr_mods_xz
        URL https://codeload.github.com/tukaani-project/xz/tar.gz/refs/tags/v5.8.1
        URL_HASH SHA256=bdbc23fbf9098843357e71e49685724fda2c320c29cb1b25fd90505f14bb0b3d)
    FetchContent_MakeAvailable(dkr_mods_xz)
    set(_dkr_mod_lzma_api "${dkr_mods_xz_SOURCE_DIR}/src/liblzma/api" PARENT_SCOPE)
endfunction()
dkr_mods_lzma()
add_library(DKRLegacyDelta STATIC "${_dkr_xdelta_dir}/xdelta3.c"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods/legacy_mod_lzma_boundary.c")
set_property(SOURCE "${_dkr_xdelta_dir}/xdelta3.c" APPEND PROPERTY
    COMPILE_DEFINITIONS lzma_stream_decoder=dkr_mods_lzma_stream_decoder)
target_include_directories(DKRLegacyDelta PUBLIC "${_dkr_xdelta_dir}")
target_include_directories(DKRLegacyDelta PRIVATE
    "${_dkr_mod_lzma_api}")
target_compile_definitions(DKRLegacyDelta PUBLIC
    SIZEOF_SIZE_T=8 SIZEOF_UNSIGNED_LONG_LONG=8
    XD3_ENCODER=0 XD3_MAIN=0 SECONDARY_LZMA=1 SECONDARY_DJW=1 SECONDARY_FGK=0
    HAVE_LZMA_H=1 XD3_HARDMAXWINSIZE=16777216 NOMINMAX)
target_compile_definitions(DKRLegacyDelta PRIVATE LZMA_API_STATIC)
target_link_libraries(DKRLegacyDelta PUBLIC liblzma)

set(_dkr_mod_src "${DKRPORT_ROOT}/runtime-recomp/src/game/mods")
add_library(DKRLegacyModCore STATIC
    "${_dkr_mod_src}/online_mod_manifest.cpp"
    "${_dkr_mod_src}/online_mod_package.cpp"
    "${_dkr_mod_src}/online_mod_transfer.cpp"
    "${_dkr_mod_src}/online_mod_cache.cpp"
    "${_dkr_mod_src}/legacy_mod_format.cpp"
    "${_dkr_mod_src}/legacy_mod_import.cpp"
    "${_dkr_mod_src}/legacy_mod_geometry.cpp"
    "${_dkr_mod_src}/legacy_mod_dependencies.cpp"
    "${_dkr_mod_src}/legacy_asset_bank.cpp"
    "${_dkr_mod_src}/legacy_character_materialize.cpp"
    "${_dkr_mod_src}/legacy_character_audio.cpp"
    "${_dkr_mod_src}/legacy_character_artifact.cpp"
    "${_dkr_mod_src}/legacy_character_roster.cpp"
    "${_dkr_mod_src}/legacy_character_menu.cpp"
    "${_dkr_mod_src}/legacy_asset_directory.cpp"
    "${_dkr_mod_src}/legacy_asset_bus.cpp"
    "${_dkr_mod_src}/legacy_asset_io.cpp"
    "${_dkr_mod_src}/legacy_cache_namespace.cpp"
    "${_dkr_mod_src}/legacy_resident_assets.cpp"
    "${_dkr_mod_src}/legacy_audio_bank.cpp"
    "${_dkr_mod_src}/legacy_track_materialize.cpp"
    "${_dkr_mod_src}/legacy_track_artifact.cpp"
    "${_dkr_mod_src}/legacy_track_prepare_job.cpp"
    "${_dkr_mod_src}/legacy_track_menu.cpp"
    "${_dkr_mod_src}/legacy_track_menu_adapter.cpp"
    "${_dkr_mod_src}/legacy_runtime_session.cpp"
    "${_dkr_mod_src}/legacy_mod_stage.cpp")
target_compile_features(DKRLegacyModCore PUBLIC cxx_std_20)
target_include_directories(DKRLegacyModCore PUBLIC "${_dkr_mod_src}"
    "${DKRPORT_ROOT}/extern/rt64/src/contrib"
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/thirdparty")
target_compile_definitions(DKRLegacyModCore PRIVATE NOMINMAX)
target_link_libraries(DKRLegacyModCore PRIVATE DKRLegacyDelta)

# Reuse the exact pinned compression and hashing code already shipped with
# DKR-R. A standalone importer test does not need the renderer or networking.
add_library(DKRLegacyModMiniz STATIC
    "${DKRPORT_ROOT}/extern/rt64/src/contrib/miniz/miniz.c")
if(TARGET mbedcrypto)
    # Use the application's existing hashing provider and its ABI/configuration.
    set(_dkr_mod_hash mbedcrypto)
else()
    add_library(DKRLegacyModHash STATIC
        "${DKRPORT_ROOT}/extern/mbedtls/library/sha256.c"
        "${DKRPORT_ROOT}/extern/mbedtls/library/platform_util.c")
    target_include_directories(DKRLegacyModHash PRIVATE
        "${_dkr_mod_src}" "${DKRPORT_ROOT}/extern/mbedtls/include")
    target_compile_definitions(DKRLegacyModHash PRIVATE
        MBEDTLS_CONFIG_FILE="legacy_mod_hash_config.h")
    target_compile_definitions(DKRLegacyModCore PRIVATE
        MBEDTLS_CONFIG_FILE="legacy_mod_hash_config.h")
    set(_dkr_mod_hash DKRLegacyModHash)
endif()
target_include_directories(DKRLegacyModCore PRIVATE
    "${DKRPORT_ROOT}/extern/mbedtls/include")
target_link_libraries(DKRLegacyModCore PRIVATE ${_dkr_mod_hash})
if(TARGET rt64)
    # The game gets miniz from RT64's existing archive, exactly once. The child
    # worker gets a separate small provider, never the renderer or window stack.
    target_link_libraries(DKRLegacyModCore PRIVATE rt64)
else()
    target_link_libraries(DKRLegacyModCore PRIVATE DKRLegacyModMiniz)
endif()

add_library(DKRLegacyGuestIO STATIC "${_dkr_mod_src}/legacy_runtime_io.cpp"
    "${_dkr_mod_src}/legacy_runtime_character.cpp"
    "${_dkr_mod_src}/legacy_character_menu_render.cpp"
    "${_dkr_mod_src}/legacy_character_stage.cpp"
    "${_dkr_mod_src}/legacy_character_presentation.cpp"
    "${_dkr_mod_src}/legacy_guest_checkpoint.cpp"
    "${_dkr_mod_src}/legacy_runtime_assets.cpp")
target_include_directories(DKRLegacyGuestIO PUBLIC
    "${DKRPORT_ROOT}/extern/n64-modern-runtime/N64Recomp/include")
target_link_libraries(DKRLegacyGuestIO PUBLIC DKRLegacyModCore)
if(MSVC)
    target_compile_options(DKRLegacyGuestIO PRIVATE /EHs /EHc-)
endif()

add_library(DKRLegacyModProcess STATIC "${_dkr_mod_src}/legacy_mod_process.cpp")
target_include_directories(DKRLegacyModProcess PUBLIC "${_dkr_mod_src}")
target_compile_features(DKRLegacyModProcess PUBLIC cxx_std_20)
target_compile_definitions(DKRLegacyModProcess PRIVATE NOMINMAX)
add_executable(DKRLegacyModWorker "${_dkr_mod_src}/legacy_mod_worker.cpp")
set_target_properties(DKRLegacyModWorker PROPERTIES OUTPUT_NAME "DKR-R-ModWorker")
if(ANDROID)
    # Android only permits execution from the installed, read-only native
    # library directory. Package this PIE helper there using a .so filename.
    set_target_properties(DKRLegacyModWorker PROPERTIES
        OUTPUT_NAME "libDKR-R-ModWorker.so"
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    # Android's CMake platform suppresses automatic RPATH emission. This
    # executable is spawned independently of Java's loaded-library namespace.
    target_link_options(DKRLegacyModWorker PRIVATE "-Wl,-rpath,$ORIGIN")
endif()
add_library(DKRLegacyWorkerCore STATIC $<TARGET_OBJECTS:DKRLegacyModCore>)
target_include_directories(DKRLegacyWorkerCore PUBLIC
    "$<TARGET_PROPERTY:DKRLegacyModCore,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(DKRLegacyWorkerCore PRIVATE DKRLegacyDelta DKRLegacyModMiniz ${_dkr_mod_hash})
target_link_libraries(DKRLegacyModWorker PRIVATE DKRLegacyWorkerCore DKRLegacyModProcess)

add_library(DKRLegacyImportLibrary STATIC "${_dkr_mod_src}/legacy_import_library.cpp"
    "${_dkr_mod_src}/online_mod_bundle.cpp"
    "${_dkr_mod_src}/online_mod_profile.cpp"
    "${_dkr_mod_src}/online_mod_sync.cpp"
    "${_dkr_mod_src}/legacy_mod_library.cpp"
    "${_dkr_mod_src}/legacy_track_catalog.cpp")
target_link_libraries(DKRLegacyImportLibrary PUBLIC DKRLegacyModCore DKRLegacyModProcess)

add_library(DKRLegacyModLaunch STATIC "${_dkr_mod_src}/legacy_mod_launch.cpp"
    "${_dkr_mod_src}/online_mod_runtime.cpp"
    "${_dkr_mod_src}/online_mod_music.cpp"
    "${_dkr_mod_src}/../custom_music_decode.cpp")
target_include_directories(DKRLegacyModLaunch PRIVATE "${DKRPORT_ROOT}/runtime-recomp/third_party/dr_libs")
# .dkrmap table logic. A mod launch publishes the tracks' artwork into the
# character-augmented boot bank, so the launch owns this dependency.
# A track's native music sequence is validated at scan, so its checker
# (custom_music_sequence.cpp, no dependencies of its own) comes along.
add_library(DKRCustomTracksCore STATIC "${_dkr_mod_src}/../custom_tracks.cpp"
    "${_dkr_mod_src}/../custom_music_sequence.cpp")
target_include_directories(DKRCustomTracksCore PUBLIC "${_dkr_mod_src}/.."
    "${DKRPORT_ROOT}/extern/rt64/src/contrib")
target_compile_features(DKRCustomTracksCore PUBLIC cxx_std_20)
target_compile_definitions(DKRCustomTracksCore PRIVATE NOMINMAX)
if(TARGET rt64)
    # Same single miniz provider as DKRLegacyModCore inside the game.
    target_link_libraries(DKRCustomTracksCore PRIVATE rt64)
else()
    target_link_libraries(DKRCustomTracksCore PRIVATE DKRLegacyModMiniz)
endif()
target_link_libraries(DKRLegacyModLaunch PUBLIC DKRLegacyImportLibrary DKRCustomTracksCore DKRLegacyGuestIO)
# Separate worker archive: no renderer/window dependency is pulled into the
# process that handles untrusted online patches and package metadata.
add_library(DKROnlineModPreparation STATIC "${_dkr_mod_src}/online_mod_prepare.cpp")
add_library(DKRCustomTracksWorkerCore STATIC $<TARGET_OBJECTS:DKRCustomTracksCore>)
target_link_libraries(DKRLegacyImportLibrary PUBLIC DKRCustomTracksCore)
target_link_libraries(DKRCustomTracksWorkerCore PRIVATE DKRLegacyModMiniz)
target_include_directories(DKRCustomTracksWorkerCore PUBLIC "${_dkr_mod_src}/..")
target_link_libraries(DKROnlineModPreparation PUBLIC DKRLegacyWorkerCore DKRCustomTracksWorkerCore)
target_include_directories(DKROnlineModPreparation PUBLIC "${_dkr_mod_src}")
target_link_libraries(DKRLegacyModWorker PRIVATE DKROnlineModPreparation)
