cmake_minimum_required(VERSION 3.24)
project(DKRIosCompileCheck LANGUAGES C CXX OBJC OBJCXX)

if(NOT IOS OR NOT CMAKE_OSX_SYSROOT MATCHES "[iI][pP]hone[oO][sS]")
    message(FATAL_ERROR "Use the iPhoneOS SDK: -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos")
endif()
if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "This device compile check requires -DCMAKE_OSX_ARCHITECTURES=arm64")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(../../cmake/SourceRoots.cmake)
file(STRINGS "${DKR_SOURCE_ROOT}/VERSION" DKR_RELEASE_VERSION LIMIT_COUNT 1)
# No renderer, generated CPU payload, host shader tools, or bundle is needed
# to compile the actual platform adapters against the device SDK.
set(DKR_RUNTIME_BUILD_RT64 OFF)
include(../../cmake/iOSPlatform.cmake)

set(game "${DKR_SOURCE_ROOT}/runtime-recomp/src/game")
set(ios "${DKR_SOURCE_ROOT}/runtime-recomp/src/ios")
set(rt64 "${DKR_DEPENDENCIES_DIR}/rt64")
foreach(header src/contrib/imgui/imgui.h src/contrib/nativefiledialog-extended/src/include/nfd.h)
    if(NOT EXISTS "${rt64}/${header}")
        message(FATAL_ERROR "Missing pinned dependency header ${rt64}/${header}; bootstrap rt64 recursively")
    endif()
endforeach()

add_library(
    DKRIosCompileCheck
    STATIC
    "${ios}/ios_platform.mm"
    "${ios}/ios_metal_layer.mm"
    "${ios}/ios_file_dialog.mm"
    "${DKR_SOURCE_ROOT}/runtime-recomp/src/android/mobile_ui.cpp"
    "${game}/runtime_portable.cpp"
    "${game}/runtime_support.cpp"
)
target_compile_options(DKRIosCompileCheck PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_compile_definitions(DKRIosCompileCheck PRIVATE DKR_RELEASE_VERSION="${DKR_RELEASE_VERSION}")
target_include_directories(
    DKRIosCompileCheck
    PRIVATE "${rt64}/src/contrib" "${rt64}/src/contrib/nativefiledialog-extended/src/include"
)
target_link_libraries(DKRIosCompileCheck PRIVATE SDL2::SDL2)
