function(dkr_camera_scene_kernel target directory revision)
    if(NOT EXISTS "${directory}/manifest.json")
        message(FATAL_ERROR "Generate a NEW draw-only Camera++ kernel for revision ${revision}")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scene_kernel_pipeline.py" --verify --output "${directory}"
        RESULT_VARIABLE inspected)
    if(NOT inspected EQUAL 0)
        message(FATAL_ERROR "Private Camera++ draw composition failed inspection")
    endif()
    add_library(${target} STATIC "${directory}/camera_scene_functions.c"
        "${directory}/camera_scene_hooks.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/native/camera_kernel.c")
    target_include_directories(${target} BEFORE PRIVATE "${directory}"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/native"
        "${DKRPORT_ROOT}/runtime-recomp/src/game")
    target_compile_features(${target} PRIVATE c_std_11)
    foreach(helper access step fault)
        target_compile_definitions(${target} PRIVATE
            dkr_camera_${helper}=dkr_camera_v${revision}_${helper})
    endforeach()
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:strict /FS /wd4244 /wd4267)
    else()
        target_compile_options(${target} PRIVATE -fno-strict-aliasing -ffp-contract=off
            -fno-fast-math -frounding-math -Werror=implicit-function-declaration)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()
