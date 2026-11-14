# Camera++: the local presentation provider (DKRCameraPresentation) and one
# draw-only scene kernel per revision. The kernels compile guest-derived code
# prepared outside the build: DKR_CAMERA_KERNEL_V<rev> holds
# camera_scene_functions.c and camera_scene_hooks.c from
# scripts/camera_scene_kernel.py (scripts/prepare_payloads.py runs it;
# cmake/PreparedInputs.cmake defaults the paths). Neither input is modified.
set(DKR_CAMERA_KERNEL_V77 "" CACHE PATH "Prepared Camera++ scene kernel for revision 77")
set(DKR_CAMERA_KERNEL_V80 "" CACHE PATH "Prepared Camera++ scene kernel for revision 80")

# The prepared hooks file stubs every scene hook. src/game/camera_shadow_kernel.c
# implements the shadow ones, so the kernel compiles the stubs of all the others.
function(dkr_camera_hook_stubs out kernel revision)
    set(shadow "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/game/camera_shadow_kernel.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${kernel}/camera_scene_hooks.c" "${shadow}")
    file(READ "${kernel}/camera_scene_hooks.c" stubs)
    string(REPLACE "\r\n" "\n" stubs "${stubs}")
    # One space after each comma is optional, as clang-format writes it.
    file(STRINGS "${shadow}" defined REGEX "^unsigned dkr_[a-z_]+\\(uint8_t\\* ram, ?recomp_context\\* ctx, ?\\.\\.\\.\\) \\{$")
    if(NOT defined)
        message(FATAL_ERROR "camera_shadow_kernel.c defines no scene hooks")
    endif()
    foreach(line IN LISTS defined)
        string(REGEX MATCH "^unsigned (dkr_[a-z_]+)" name "${line}")
        set(stub "unsigned dkr_camera_v${revision}_${CMAKE_MATCH_1}(uint8_t* ram,recomp_context* ctx,...){(void)ram;(void)ctx;return 0;}\n")
        string(FIND "${stubs}" "${stub}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "The prepared US v${revision} Camera++ kernel has no stub for ${CMAKE_MATCH_1}")
        endif()
        string(REPLACE "${stub}" "" stubs "${stubs}")
    endforeach()
    set(path "${CMAKE_BINARY_DIR}/generated/camera-shadow-v${revision}/camera_scene_hooks.c")
    if(EXISTS "${path}")
        file(READ "${path}" current)
    endif()
    if(NOT EXISTS "${path}" OR NOT current STREQUAL stubs)
        file(WRITE "${path}" "${stubs}")
    endif()
    set(${out} "${path}" PARENT_SCOPE)
endfunction()

function(dkr_camera_scene_kernel target revision)
    set(kernel "${DKR_CAMERA_KERNEL_V${revision}}")
    if(NOT EXISTS "${kernel}/manifest.json")
        message(FATAL_ERROR "Set DKR_CAMERA_KERNEL_V${revision} to a NEW draw-only Camera++ kernel")
    endif()
    dkr_camera_hook_stubs(hooks "${kernel}" "${revision}")
    add_library(${target} STATIC
        "${kernel}/camera_scene_functions.c"
        "${hooks}"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/game/camera_kernel.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/game/camera_shadow_kernel.c")
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
