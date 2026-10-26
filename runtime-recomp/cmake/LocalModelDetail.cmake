# Model-only Patch Pipeline output: never use a generated input in place.
function(dkr_add_local_model_kernel target directory revision)
    if(NOT EXISTS "${directory}/manifest.json")
        message(
            FATAL_ERROR
            "Missing NEW visual model kernel output; run scripts/generate_visual_model_kernel.py for revision ${revision}"
        )
    endif()
    file(READ "${directory}/manifest.json" manifest)
    string(JSON schema GET "${manifest}" schema)
    string(JSON actual_revision GET "${manifest}" revision)
    if(NOT schema EQUAL 1 OR NOT actual_revision EQUAL revision)
        message(FATAL_ERROR "Visual model kernel revision/schema mismatch")
    endif()
    add_library(
        ${target}
        STATIC
        "${directory}/visual_model_functions.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/game/visual_model_kernel.c"
    )
    target_include_directories(${target} BEFORE PRIVATE "${directory}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/game")
    target_compile_features(${target} PRIVATE c_std_11)
    foreach(helper read write step)
        target_compile_definitions(${target} PRIVATE dkr_visual_${helper}=dkr_visual_v${revision}_${helper})
    endforeach()
    if(NOT MSVC)
        target_compile_options(
            ${target}
            PRIVATE -fno-strict-aliasing -ffp-contract=off -fno-fast-math -Werror=implicit-function-declaration
        )
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()
