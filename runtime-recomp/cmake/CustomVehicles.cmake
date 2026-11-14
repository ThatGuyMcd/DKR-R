# Custom vehicles: package parsing, the local library, resource preparation
# and character binding. The launcher, renderer and guest bridges live in
# DKRPortGame and DKRLegacyModLaunch.
add_library(
    DKRCustomVehicles
    STATIC
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_package.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_library.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_resources.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_binding.cpp"
)
target_compile_features(DKRCustomVehicles PUBLIC cxx_std_20)
target_compile_definitions(DKRCustomVehicles PRIVATE NOMINMAX)
target_include_directories(
    DKRCustomVehicles
    PUBLIC
        "${DKRPORT_ROOT}/runtime-recomp/src/game/mods"
        "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib"
        "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty"
)
target_link_libraries(DKRCustomVehicles PUBLIC DKRCustomMagicCodes DKRLegacyModCore)
if(NOT TARGET rt64)
    # Package images decode with stb_image, which RT64 provides in the game.
    add_library(DKRStbImage STATIC "${DKRPORT_ROOT}/runtime-recomp/tests/stb_image_provider.c")
    target_include_directories(DKRStbImage PRIVATE "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib")
    target_link_libraries(DKRCustomVehicles PRIVATE DKRStbImage)
endif()
target_link_libraries(DKRLegacyImportLibrary PRIVATE DKRCustomVehicles)
target_link_libraries(DKRLegacyModLaunch PRIVATE DKRCustomVehicles)

# The vehicle-original-textures release gate (DKRVehicleOriginalAudit). The
# test generates its own packages; no real mod or game data is read. Before
# compiles the production importer with the pre-v1.0.10 texture profile
# admission from tests/historical/vehicle_original_textures, which wins over
# the <vehicle_texture_profile.hpp> in src/game, and links its object ahead of
# DKRCustomVehicles' archive member. It must reproduce the legacy fingerprints
# and then reject the original-resolution package. ASan/UBSan coverage is the
# host-sanitizers preset, which instruments both like every other host test.
if(BUILD_TESTING)
    add_executable(DKRVehicleOriginalTests "${DKRPORT_ROOT}/runtime-recomp/tests/vehicle_original_textures_tests.cpp")
    add_executable(
        DKRVehicleOriginalBeforeTests
        "${DKRPORT_ROOT}/runtime-recomp/tests/vehicle_original_textures_tests.cpp"
        "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_package.cpp"
    )
    target_include_directories(
        DKRVehicleOriginalBeforeTests
        BEFORE
        PRIVATE "${DKRPORT_ROOT}/runtime-recomp/tests/historical/vehicle_original_textures"
    )
    target_compile_definitions(DKRVehicleOriginalBeforeTests PRIVATE NOMINMAX)
    foreach(target DKRVehicleOriginalTests DKRVehicleOriginalBeforeTests)
        target_compile_features(${target} PRIVATE cxx_std_20)
        target_include_directories(${target} PRIVATE "${DKRPORT_ROOT}/runtime-recomp/src/game")
        target_link_libraries(${target} PRIVATE DKRCustomVehicles)
    endforeach()
    add_test(NAME DKRVehicleOriginalAudit COMMAND DKRVehicleOriginalTests)
    set_tests_properties(DKRVehicleOriginalAudit PROPERTIES LABELS "host;runtime" TIMEOUT 120)
    add_test(NAME DKRVehicleOriginalBeforeChecks COMMAND DKRVehicleOriginalBeforeTests)
    set_tests_properties(
        DKRVehicleOriginalBeforeChecks
        PROPERTIES
            LABELS "host;runtime"
            TIMEOUT 120
            PASS_REGULAR_EXPRESSION "VEHICLE AUDIT FAILURE: Custom vehicle: unsupported car texture profile[.]"
            FAIL_REGULAR_EXPRESSION "fingerprint |vehicle compatibility checks passed"
    )
endif()

# v1.0.11 vehicle numeric alignment: on ARM64, GCC and Clang may fuse the
# preparation's multiply-adds, which x86-64 never does. Keep vehicle_package.cpp
# and its checks unfused there.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(
        SOURCE "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_package.cpp"
        APPEND
        PROPERTY COMPILE_OPTIONS -ffp-contract=off
    )
    foreach(target DKRVehicleOriginalTests DKRVehicleOriginalBeforeTests)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -ffp-contract=off)
        endif()
    endforeach()
endif()
