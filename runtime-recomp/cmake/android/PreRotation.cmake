# Android-only final-pass rotation. Every dependency edit is a checked stage;
# compose AFTER surface, cache/barrier and RGBA fixes, never replace them.
dkr_stage_patch(
    plume
    "${CMAKE_BINARY_DIR}/generated/android-read-barriers"
    android-prerotation
    "${DKRPORT_ROOT}/patches/android/vulkan-prerotation.patch"
    src/contrib/plume/plume_vulkan.cpp
    src/contrib/plume/plume_vulkan.h
)
target_include_directories(plume BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-prerotation/src/contrib/plume")
target_include_directories(rt64 BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated/android-prerotation/src/contrib/plume")
dkr_stage_patch(
    rt64
    "${DKR_RT64_SOURCE}"
    android-vi-prerotation
    "${DKRPORT_ROOT}/patches/android/vi-prerotation.patch"
    src/render/rt64_vi_renderer.cpp
)
dkr_stage_patch(
    rt64
    "${DKR_RT64_SOURCE}"
    android-inspector-prerotation
    "${DKRPORT_ROOT}/patches/android/inspector-prerotation.patch"
    src/gui/rt64_inspector.cpp
)
dkr_stage_patch(
    rt64
    "${CMAKE_BINARY_DIR}/generated/android-renderer"
    android-video-prerotation
    "${DKRPORT_ROOT}/patches/android/video-library-prerotation.patch"
    src/render/rt64_shader_library.cpp
)
set_source_files_properties(
    "${CMAKE_BINARY_DIR}/generated/android-video-prerotation/src/render/rt64_shader_library.cpp"
    TARGET_DIRECTORY rt64
    PROPERTIES INCLUDE_DIRECTORIES "${DKR_RT64_SOURCE}/src/render;${CMAKE_CURRENT_LIST_DIR}/../../src/game"
)
if(CMAKE_HOST_WIN32)
    set(DKR_ANDROID_VIDEO_DXC "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxc.exe")
else()
    set(DKR_ANDROID_VIDEO_DXC
        "${CMAKE_COMMAND}"
        -E
        env
        "LD_LIBRARY_PATH=${DKR_RT64_SOURCE}/src/contrib/dxc/lib/x64"
        "${DKR_RT64_SOURCE}/src/contrib/dxc/bin/x64/dxc-linux"
    )
endif()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/android-video")
set(DKR_ANDROID_VIDEO_OUTPUTS)
foreach(variant Regular Pixel)
    set(options)
    if(variant STREQUAL "Pixel")
        list(APPEND options -D PIXEL_ANTIALIASING)
    endif()
    set(output "${CMAKE_BINARY_DIR}/android-video/AndroidVideo${variant}")
    add_custom_command(
        OUTPUT "${output}.spv"
        COMMAND
            ${DKR_ANDROID_VIDEO_DXC} -spirv -fspv-target-env=vulkan1.0 -fvk-use-dx-layout -T ps_6_0 -E PSMain ${options}
            "${DKRPORT_ROOT}/runtime-recomp/src/android/AndroidVideoPS.hlsl" -Fo "${output}.spv"
        DEPENDS "${DKRPORT_ROOT}/runtime-recomp/src/android/AndroidVideoPS.hlsl"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${output}.spirv.c" "${output}.spirv.h"
        COMMAND
            "${DKR_HOST_FILE_TO_C}" "${output}.spv" "AndroidVideo${variant}BlobSPIRV" "${output}.spirv.c"
            "${output}.spirv.h"
        DEPENDS "${output}.spv" "${DKR_HOST_FILE_TO_C}"
        VERBATIM
    )
    target_sources(rt64 PRIVATE "${output}.spirv.c" "${output}.spirv.h")
    list(APPEND DKR_ANDROID_VIDEO_OUTPUTS "${output}.spirv.c" "${output}.spirv.h")
endforeach()
# rt64 is created in a child directory. An explicit producer target retains
# these parent-directory commands and orders headers before consumer compiles.
add_custom_target(DKRAndroidVideoShaders DEPENDS ${DKR_ANDROID_VIDEO_OUTPUTS})
add_dependencies(rt64 DKRAndroidVideoShaders)
target_include_directories(rt64 PRIVATE "${CMAKE_BINARY_DIR}")
