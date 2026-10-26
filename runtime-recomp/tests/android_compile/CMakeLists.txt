cmake_minimum_required(VERSION 3.24)
project(DKRAndroidCompileCheck LANGUAGES C CXX)
if(NOT ANDROID)
    message(FATAL_ERROR "Configure with the official Android NDK CMake toolchain")
endif()
if(NOT ANDROID_NDK_REVISION VERSION_EQUAL "29.0.14206865")
    message(FATAL_ERROR "Device CI pins Android NDK 29.0.14206865")
endif()
if(NOT ANDROID_STL STREQUAL "c++_shared")
    message(FATAL_ERROR "Use -DANDROID_STL=c++_shared, matching the application")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(../../cmake/SourceRoots.cmake)
set(DKR_RUNTIME_BUILD_RT64 OFF)
set(DKR_ANDROID_SDL2_SOURCE "${DKR_DEPENDENCIES_DIR}/sdl2" CACHE PATH "Pinned SDL2 source")
include(../../cmake/AndroidPlatform.cmake)
file(STRINGS "${DKR_SOURCE_ROOT}/VERSION" DKR_RELEASE_VERSION LIMIT_COUNT 1)
set(game "${DKR_SOURCE_ROOT}/runtime-recomp/src/game")
set(android "${DKR_SOURCE_ROOT}/runtime-recomp/src/android")
set(rt64 "${DKR_DEPENDENCIES_DIR}/rt64")
set(runtime "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime")

# Compile real JNI/window/touch adapters without inventing game or renderer
# entrypoints. These objects are never presented as a runnable application.
add_library(
    DKRAndroidCompileCheck
    STATIC
    "${android}/android_platform.cpp"
    "${android}/android_rt64_window.cpp"
    "${android}/mobile_ui.cpp"
)
target_compile_definitions(DKRAndroidCompileCheck PRIVATE DKR_RELEASE_VERSION="${DKR_RELEASE_VERSION}")
target_include_directories(
    DKRAndroidCompileCheck
    PRIVATE
        "${rt64}/src"
        "${rt64}/src/contrib"
        "${rt64}/src/contrib/plume"
        "${runtime}/ultramodern/include"
        "${runtime}/thirdparty"
)
target_link_libraries(DKRAndroidCompileCheck PRIVATE SDL2::SDL2)

# This self-contained production subset can also link: catch missing Android
# APIs, shared-C++ dependencies, and ELF page alignment without payload stubs.
add_library(
    DKRAndroidPlatformLinkCheck
    SHARED
    "${android}/android_file_dialog.cpp"
    "${game}/runtime_portable.cpp"
    "${game}/runtime_support.cpp"
)
target_include_directories(
    DKRAndroidPlatformLinkCheck
    PRIVATE "${rt64}/src/contrib/nativefiledialog-extended/src/include"
)
target_compile_definitions(DKRAndroidPlatformLinkCheck PRIVATE DKR_RELEASE_VERSION="${DKR_RELEASE_VERSION}")
target_link_libraries(DKRAndroidPlatformLinkCheck PRIVATE SDL2::SDL2 android log)
target_link_options(DKRAndroidPlatformLinkCheck PRIVATE "-Wl,--no-undefined")
add_dependencies(DKRAndroidCompileCheck DKRAndroidPlatformLinkCheck)
