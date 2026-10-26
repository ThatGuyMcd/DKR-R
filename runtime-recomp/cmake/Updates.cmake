# First-party update boundary. Pinned libraries are compiled unmodified;
# no generated CPU functions or dependency checkout is edited.
add_library(DKRUpdateEd25519 STATIC "${DKR_MONOCYPHER_SOURCE}/src/optional/monocypher-ed25519.c")
target_include_directories(DKRUpdateEd25519 PUBLIC "${DKR_MONOCYPHER_SOURCE}/src/optional")
target_link_libraries(DKRUpdateEd25519 PUBLIC DKRNetplayCrypto)
if(TARGET mbedcrypto)
    set(_dkr_update_hash mbedcrypto)
else()
    add_library(
        DKRUpdateHash
        STATIC
        "${DKR_MBEDTLS_SOURCE}/library/sha256.c"
        "${DKR_MBEDTLS_SOURCE}/library/platform_util.c"
    )
    target_include_directories(
        DKRUpdateHash
        PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src/game/updates" "${DKR_MBEDTLS_SOURCE}/include"
    )
    target_compile_definitions(DKRUpdateHash PUBLIC MBEDTLS_CONFIG_FILE="update_hash_config.h")
    set(_dkr_update_hash DKRUpdateHash)
endif()
add_library(DKRUpdateZip STATIC "${DKR_RT64_SOURCE}/src/contrib/miniz/miniz.c")
add_library(DKRUpdateCore STATIC src/game/updates/update_core.cpp)
target_include_directories(
    DKRUpdateCore
    PUBLIC src/game/updates
    PRIVATE "${DKR_RT64_SOURCE}/src/contrib" "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty" "${DKR_MBEDTLS_SOURCE}/include"
)
target_compile_definitions(DKRUpdateCore PRIVATE NOMINMAX)
target_link_libraries(DKRUpdateCore PUBLIC DKRUpdateEd25519 PRIVATE DKRUpdateZip ${_dkr_update_hash})

file(READ "${CMAKE_CURRENT_LIST_DIR}/../../packaging/update-public-key.json" _dkr_update_trust)
string(JSON DKR_UPDATE_PUBLIC_KEY GET "${_dkr_update_trust}" public_key)
string(LENGTH "${DKR_UPDATE_PUBLIC_KEY}" _dkr_update_key_length)
if(NOT _dkr_update_key_length EQUAL 64 OR NOT DKR_UPDATE_PUBLIC_KEY MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Invalid pinned DKR-R update verification key")
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/updates")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/update_trust.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/updates/update_trust.hpp"
    @ONLY
)
target_include_directories(DKRUpdateCore PUBLIC "${CMAKE_BINARY_DIR}/generated/updates")
find_package(Threads REQUIRED)
add_library(DKRUpdateInstall STATIC src/game/updates/update_process.cpp src/game/updates/update_install.cpp)
target_include_directories(DKRUpdateInstall PRIVATE "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
target_compile_definitions(DKRUpdateInstall PRIVATE NOMINMAX)
target_link_libraries(DKRUpdateInstall PUBLIC DKRUpdateCore Threads::Threads)
if(WIN32)
    target_link_libraries(DKRUpdateInstall PUBLIC shell32)
endif()
add_library(DKRUpdateClient STATIC src/game/updates/update_http.cpp src/game/updates/update_service.cpp)
target_include_directories(DKRUpdateClient PRIVATE "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
target_compile_definitions(DKRUpdateClient PRIVATE NOMINMAX)
target_link_libraries(DKRUpdateClient PUBLIC DKRUpdateInstall)
if(WIN32)
    target_link_libraries(DKRUpdateClient PRIVATE winhttp)
elseif(ANDROID)
    target_sources(DKRUpdateClient PRIVATE src/android/android_update_http.cpp)
    target_link_libraries(DKRUpdateClient PRIVATE SDL2::SDL2)
elseif(APPLE)
    target_sources(DKRUpdateClient PRIVATE src/ios/apple_update_http.mm)
    target_compile_options(DKRUpdateClient PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_link_libraries(DKRUpdateClient PRIVATE "-framework Foundation")
else()
    find_package(CURL 7.85 REQUIRED)
    target_link_libraries(DKRUpdateClient PRIVATE CURL::libcurl)
endif()
if(NOT ANDROID AND NOT IOS)
    add_executable(DKRUpdateHelper src/game/updates/update_helper_main.cpp)
    target_link_libraries(DKRUpdateHelper PRIVATE DKRUpdateInstall)
    set_target_properties(DKRUpdateHelper PROPERTIES OUTPUT_NAME "DKR-R-Updater")
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" upper)
        set_target_properties(
            DKRUpdateHelper
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/bin/${config}/libexec/dkr-r"
        )
    endforeach()
    if(WIN32)
        add_executable(DKRUpdateLauncher WIN32 src/game/updates/update_bootstrap_win.cpp resources/dkr-r.rc)
        target_link_libraries(DKRUpdateLauncher PRIVATE DKRUpdateInstall)
        string(REPLACE "." "," _dkr_update_resource_version "${DKR_RELEASE_VERSION_NUMERIC}")
        target_compile_definitions(
            DKRUpdateLauncher
            PRIVATE
                DKR_RELEASE_VERSION="${DKR_RELEASE_VERSION}"
                "DKR_RELEASE_RESOURCE_VERSION=${_dkr_update_resource_version},${DKR_RELEASE_BUILD_NUMBER}"
        )
        set_target_properties(DKRUpdateLauncher PROPERTIES OUTPUT_NAME "DKR-R")
        foreach(config Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${config}" upper)
            set_target_properties(
                DKRUpdateLauncher
                PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} "${CMAKE_BINARY_DIR}/bin/${config}"
            )
        endforeach()
    endif()
endif()
if(BUILD_TESTING AND NOT ANDROID)
    add_executable(DKRUpdateCoreTests tests/update_core_tests.cpp)
    target_include_directories(DKRUpdateCoreTests PRIVATE "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
    target_link_libraries(DKRUpdateCoreTests PRIVATE DKRUpdateCore)
    add_test(NAME DKRUpdateCore COMMAND DKRUpdateCoreTests)
    add_executable(DKRUpdateServiceTests tests/update_service_tests.cpp)
    target_include_directories(DKRUpdateServiceTests PRIVATE "${DKR_MODERN_RUNTIME_SOURCE}/thirdparty")
    target_link_libraries(DKRUpdateServiceTests PRIVATE DKRUpdateClient)
    add_test(NAME DKRUpdateService COMMAND DKRUpdateServiceTests)
    add_executable(DKRUpdateProcessTests tests/update_process_tests.cpp)
    target_link_libraries(DKRUpdateProcessTests PRIVATE DKRUpdateInstall)
    add_test(NAME DKRUpdateProcess COMMAND DKRUpdateProcessTests)
endif()
