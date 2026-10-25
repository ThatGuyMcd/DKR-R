# Camera++: the local presentation provider (DKRCameraPresentation) and one
# draw-only scene kernel per revision. The kernels compile guest-derived code
# prepared outside the build: DKR_CAMERA_KERNEL_V<rev> holds
# camera_scene_functions.c from patches/camera-plus/scene_kernel_pipeline.py,
# and DKR_CAMERA_SHADOW_KERNEL_V<rev> holds the canonical-shadow
# camera_scene_hooks.c and camera_shadow_kernel.c. Neither input is modified.
set(DKR_CAMERA_KERNEL_V77 "" CACHE PATH "Prepared Camera++ scene kernel for revision 77")
set(DKR_CAMERA_KERNEL_V80 "" CACHE PATH "Prepared Camera++ scene kernel for revision 80")
set(DKR_CAMERA_SHADOW_KERNEL_V77 "" CACHE PATH "Prepared Camera++ shadow kernel for revision 77")
set(DKR_CAMERA_SHADOW_KERNEL_V80 "" CACHE PATH "Prepared Camera++ shadow kernel for revision 80")

function(dkr_camera_scene_kernel target revision)
    set(kernel "${DKR_CAMERA_KERNEL_V${revision}}")
    set(shadow "${DKR_CAMERA_SHADOW_KERNEL_V${revision}}")
    if(NOT EXISTS "${kernel}/manifest.json")
        message(FATAL_ERROR "Set DKR_CAMERA_KERNEL_V${revision} to a NEW draw-only Camera++ kernel")
    endif()
    if(NOT EXISTS "${shadow}/camera_scene_hooks.c" OR NOT EXISTS "${shadow}/camera_shadow_kernel.c")
        message(FATAL_ERROR "Set DKR_CAMERA_SHADOW_KERNEL_V${revision} to the prepared Camera++ shadow kernel")
    endif()
    add_library(${target} STATIC
        "${kernel}/camera_scene_functions.c"
        "${shadow}/camera_scene_hooks.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/game/camera_kernel.c"
        "${shadow}/camera_shadow_kernel.c")
    target_include_directories(${target} BEFORE PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/game" "${kernel}")
    target_compile_features(${target} PRIVATE c_std_11)
    foreach(helper access step fault)
        target_compile_definitions(${target} PRIVATE dkr_camera_${helper}=dkr_camera_v${revision}_${helper})
    endforeach()
    target_compile_definitions(${target} PRIVATE DKR_SHADOW_ENTRY=dkr_camera_v${revision}_shadow_draw)
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:strict /FS /wd4244 /wd4267)
    else()
        target_compile_options(${target} PRIVATE -fno-strict-aliasing -ffp-contract=off
            -fno-fast-math -frounding-math -Werror=implicit-function-declaration)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

add_library(DKRCameraPresentation STATIC
    src/game/camera_presentation.cpp
    src/game/camera_scene.cpp
    src/game/camera_snapshot.cpp
    src/game/camera_settings.cpp
    src/game/camera_actions.cpp
    src/game/camera_session.cpp
    src/game/camera_runtime.cpp
    src/game/camera_rt64.cpp)
target_compile_features(DKRCameraPresentation PUBLIC cxx_std_20)
target_include_directories(DKRCameraPresentation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game")
target_include_directories(DKRCameraPresentation PRIVATE "${DKR_DEPENDENCIES_DIR}/rt64/src/contrib")
target_link_libraries(DKRCameraPresentation PUBLIC DKRCustomMagicCodes)
if(MSVC)
    target_compile_options(DKRCameraPresentation PRIVATE /fp:strict /W4)
else()
    target_compile_options(DKRCameraPresentation PRIVATE -ffp-contract=off -fno-fast-math -Wall -Wextra)
endif()
dkr_camera_scene_kernel(DKRCameraSceneV77 77)
dkr_camera_scene_kernel(DKRCameraSceneV80 80)
target_link_libraries(DKRCameraPresentation PRIVATE DKRCameraSceneV77 DKRCameraSceneV80)
