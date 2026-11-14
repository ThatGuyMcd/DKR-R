include_guard(GLOBAL)
function(dkr_generate_revision_symbol_map source prefix output)
    # Track membership as well as contents; Ninja must reconfigure when a
    # generated translation unit is added, removed, or changes its definitions.
    file(GLOB inputs CONFIGURE_DEPENDS "${source}/*.c" "${source}/*.cpp")
    set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate_revision_symbol_map.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${inputs} "${script}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${script}" "${source}" "${prefix}" "${output}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not namespace the Rev A CPU payload: ${error}")
    endif()
endfunction()
