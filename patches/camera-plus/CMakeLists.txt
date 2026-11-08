cmake_minimum_required(VERSION 3.20)
project(DKRCameraCompileOnly LANGUAGES C CXX)
# Compiler checks only. No executable, CTest, runtime, mod or headset launch.
get_filename_component(DKR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DKR_CAMERA_RT64_STAGE OR NOT DKR_OPENXR_SOURCE)
    message(FATAL_ERROR "Select the separately staged RT64 and pinned OpenXR SDK")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/SceneKernel.cmake")
foreach(revision 77 80)
    if(DKR_CAMERA_KERNEL_V${revision})
        dkr_camera_scene_kernel(CameraSceneV${revision} "${DKR_CAMERA_KERNEL_V${revision}}" ${revision})
    endif()
endforeach()
add_library(CameraCompileOnly OBJECT
    native/camera_presentation.cpp native/camera_scene.cpp native/camera_snapshot.cpp native/camera_xr.cpp
    native/camera_settings.cpp native/camera_actions.cpp native/camera_session.cpp)
target_compile_features(CameraCompileOnly PRIVATE cxx_std_20)
target_compile_definitions(CameraCompileOnly PRIVATE NOMINMAX)
target_include_directories(CameraCompileOnly PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/sdk" "${CMAKE_CURRENT_LIST_DIR}/native"
    "${DKR_ROOT}/runtime-recomp/src/game"
    "${DKR_CAMERA_RT64_STAGE}/src/contrib"
    "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume"
    "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume/contrib/volk"
    "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume/contrib/Vulkan-Headers/include"
    "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume/contrib/VulkanMemoryAllocator/include"
    "${DKR_OPENXR_SOURCE}/include")
if(MSVC)
    target_compile_options(CameraCompileOnly PRIVATE /W4 /fp:strict /permissive-)
else()
    target_compile_options(CameraCompileOnly PRIVATE -Wall -Wextra -Werror
        -Wno-missing-field-initializers -ffp-contract=off -fno-fast-math)
    find_package(SDL2 REQUIRED)
    target_compile_definitions(CameraCompileOnly PRIVATE PLUME_SDL_VULKAN_ENABLED=1)
    target_include_directories(CameraCompileOnly PRIVATE ${SDL2_INCLUDE_DIRS})
endif()
# Object compilation only: check the patched implementation as well as the
# headers. No Vulkan device or headset is created by this target.
add_library(CameraVulkanCompileOnly OBJECT
    "${DKR_CAMERA_RT64_STAGE}/src/contrib/plume/plume_vulkan.cpp")
target_compile_features(CameraVulkanCompileOnly PRIVATE cxx_std_17)
get_target_property(camera_includes CameraCompileOnly INCLUDE_DIRECTORIES)
target_include_directories(CameraVulkanCompileOnly PRIVATE ${camera_includes})
target_compile_definitions(CameraVulkanCompileOnly PRIVATE NOMINMAX)
if(NOT MSVC)
    target_compile_definitions(CameraVulkanCompileOnly PRIVATE PLUME_SDL_VULKAN_ENABLED=1)
endif()
if(DKR_CAMERA_NATIVE_STAGE)
    # Compile the transformed library against its transformed header, without
    # opening a user's installed library or executing any package.
    add_library(CameraLibraryCompileOnly OBJECT
        "${DKR_CAMERA_NATIVE_STAGE}/src/game/mods/magic_code_library.cpp")
    target_compile_features(CameraLibraryCompileOnly PRIVATE cxx_std_20)
    target_compile_definitions(CameraLibraryCompileOnly PRIVATE NOMINMAX)
    target_include_directories(CameraLibraryCompileOnly PRIVATE
        "${DKR_CAMERA_NATIVE_STAGE}/src/game/mods" "${DKR_CAMERA_RT64_STAGE}/src/contrib")
endif()
