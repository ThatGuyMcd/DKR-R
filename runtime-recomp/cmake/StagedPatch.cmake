# Compose patches in disposable build-local scratch; publish only changed bytes.
#
#   dkr_stage_patch(target source stage_name patch file... [HEADERS header...]
#                   [KEEP_INCLUDE_DIRECTORIES])
#
# Each file replaces the one source of the same name in target. HEADERS are
# patched and published next to them but swap nothing: a staged source finds
# them first through its own directory, every other source keeps the original.
# KEEP_INCLUDE_DIRECTORIES keeps the replaced source's own include directories
# after the staged copy's source directory (a stage on top of a stage).
include_guard(GLOBAL)
find_package(Git REQUIRED)
function(dkr_stage_patch target source stage_name patch)
    cmake_parse_arguments(PARSE_ARGV 4 arg "KEEP_INCLUDE_DIRECTORIES" "" "HEADERS")
    set(stage "${CMAKE_BINARY_DIR}/generated/${stage_name}")
    set(scratch "${CMAKE_BINARY_DIR}/patch-work/${stage_name}")
    file(REMOVE_RECURSE "${scratch}")
    set(files ${arg_UNPARSED_ARGUMENTS})
    set(staged ${files} ${arg_HEADERS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
    foreach(file IN LISTS staged)
        get_filename_component(parent "${file}" DIRECTORY)
        file(MAKE_DIRECTORY "${scratch}/${parent}")
        configure_file("${source}/${file}" "${scratch}/${file}" COPYONLY)
    endforeach()
    execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${scratch}"
        COMMAND_ERROR_IS_FATAL ANY)
    foreach(mode IN ITEMS check apply)
        # Checkout EOLs differ between Windows and Linux. Ignore context-only
        # whitespace; added lines still pass the strict whitespace check.
        set(flags --recount --whitespace=error --ignore-space-change)
        if(mode STREQUAL "check")
            list(APPEND flags --check)
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${scratch}" apply ${flags} "${patch}"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${stage_name} ${mode} failed: ${error}")
        endif()
    endforeach()
    foreach(file IN LISTS staged)
        get_filename_component(parent "${file}" DIRECTORY)
        file(MAKE_DIRECTORY "${stage}/${parent}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${scratch}/${file}" "${stage}/${file}" COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
    get_target_property(sources ${target} SOURCES)
    foreach(file IN LISTS files)
        get_filename_component(name "${file}" NAME)
        set(matches ${sources})
        list(FILTER matches INCLUDE REGEX "(^|/)${name}$")
        list(LENGTH matches count)
        if(NOT count EQUAL 1)
            message(FATAL_ERROR "${stage_name}: expected one ${name}, found ${count}")
        endif()
        list(REMOVE_ITEM sources ${matches})
        list(APPEND sources "${stage}/${file}")
        get_filename_component(parent "${file}" DIRECTORY)
        set(includes "${source}/${parent}")
        if(arg_KEEP_INCLUDE_DIRECTORIES)
            get_source_file_property(previous "${matches}" TARGET_DIRECTORY ${target} INCLUDE_DIRECTORIES)
            if(previous)
                list(APPEND includes ${previous})
            endif()
        endif()
        list(APPEND includes "${DKRPORT_ROOT}/runtime-recomp/src/game")
        list(REMOVE_DUPLICATES includes)
        set_source_files_properties("${stage}/${file}" TARGET_DIRECTORY ${target}
            PROPERTIES INCLUDE_DIRECTORIES "${includes}")
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
endfunction()
