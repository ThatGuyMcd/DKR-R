# Custom vehicles: package parsing, the local library, resource preparation
# and character binding. The launcher, renderer and guest bridges live in
# DKRPortGame and DKRLegacyModLaunch.
add_library(DKRCustomVehicles STATIC
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_package.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_library.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_resources.cpp"
    "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_binding.cpp")
target_compile_features(DKRCustomVehicles PUBLIC cxx_std_20)
target_compile_definitions(DKRCustomVehicles PRIVATE NOMINMAX)
target_include_directories(DKRCustomVehicles PUBLIC
    "${DKRPORT_ROOT}/runtime-recomp/src/game/mods"
    "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib"
    "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/thirdparty")
target_link_libraries(DKRCustomVehicles PUBLIC DKRCustomMagicCodes DKRLegacyModCore)
target_link_libraries(DKRLegacyImportLibrary PRIVATE DKRCustomVehicles)
target_link_libraries(DKRLegacyModLaunch PRIVATE DKRCustomVehicles)

# Original-texture package checks (synthetic packages, no game data). Built on
# demand; the sanitizer variant compiles the preparation source directly.
add_executable(DKRVehicleOriginalChecks EXCLUDE_FROM_ALL
    "${DKRPORT_ROOT}/runtime-recomp/tests/vehicle_package_checks.cpp")
target_compile_features(DKRVehicleOriginalChecks PRIVATE cxx_std_20)
target_include_directories(DKRVehicleOriginalChecks PRIVATE "${DKRPORT_ROOT}/runtime-recomp/src/game")
target_link_libraries(DKRVehicleOriginalChecks PRIVATE DKRCustomVehicles)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
    add_executable(DKRVehicleOriginalSanitizerChecks EXCLUDE_FROM_ALL
        "${DKRPORT_ROOT}/runtime-recomp/tests/vehicle_package_checks.cpp"
        "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_package.cpp")
    target_compile_features(DKRVehicleOriginalSanitizerChecks PRIVATE cxx_std_20)
    target_compile_definitions(DKRVehicleOriginalSanitizerChecks PRIVATE NOMINMAX)
    target_compile_options(DKRVehicleOriginalSanitizerChecks PRIVATE
        -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(DKRVehicleOriginalSanitizerChecks PRIVATE -fsanitize=address,undefined)
    target_include_directories(DKRVehicleOriginalSanitizerChecks PRIVATE
        "${DKRPORT_ROOT}/runtime-recomp/src/game" "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib")
    target_link_libraries(DKRVehicleOriginalSanitizerChecks PRIVATE DKRCustomMagicCodes DKRLegacyModCore)
endif()

# v1.0.11 vehicle numeric alignment: on ARM64, GCC and Clang may fuse the
# preparation's multiply-adds, which x86-64 never does. Keep vehicle_package.cpp
# and its checks unfused there.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE "${DKRPORT_ROOT}/runtime-recomp/src/game/vehicle_package.cpp"
        APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
    foreach(target DKRVehicleOriginalChecks DKRVehicleOriginalSanitizerChecks)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -ffp-contract=off)
        endif()
    endforeach()
endif()
