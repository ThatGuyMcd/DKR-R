# PR #52's pre-fetch guard must also compose on older prepared dependencies.
# Never patch the dependency checkout. Stage EVERY class-layout consumer after
# the water/mobile/performance layers, including when enhanced water is off.
set(hle_guard_stage "${CMAKE_BINARY_DIR}/generated/hle-command-guard")
set(hle_guard_input "${CMAKE_BINARY_DIR}/generated/hle-command-guard-inputs")
file(MAKE_DIRECTORY "${hle_guard_input}/src/hle" "${hle_guard_stage}/src/hle")
set(hle_guard_patch "${DKRPORT_ROOT}/patches/rt64/0018-validate-hle-commands-before-fetch.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${hle_guard_patch}")
set(hle_guard_headers_root "${DKR_RT64_SOURCE}")
if(DKR_ENHANCED_WATER)
    set(hle_guard_headers_root "${water_stage}")
endif()
file(
    GLOB_RECURSE hle_guard_headers
    CONFIGURE_DEPENDS
    "${hle_guard_headers_root}/include/*.h"
    "${hle_guard_headers_root}/src/common/*.h"
    "${hle_guard_headers_root}/src/gbi/*.h"
    "${hle_guard_headers_root}/src/hle/*.h"
    "${hle_guard_headers_root}/src/render/*.h"
    "${hle_guard_headers_root}/src/shared/*.h"
    "${hle_guard_headers_root}/src/include/*.h"
)
foreach(header IN LISTS hle_guard_headers)
    file(RELATIVE_PATH relative "${hle_guard_headers_root}" "${header}")
    get_filename_component(parent "${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${hle_guard_stage}/${parent}")
    if(relative STREQUAL "src/hle/rt64_interpreter.h")
        configure_file("${header}" "${hle_guard_input}/${relative}" COPYONLY)
    else()
        configure_file("${header}" "${hle_guard_stage}/${relative}" COPYONLY)
    endif()
endforeach()
get_target_property(hle_guard_original_sources rt64 SOURCES)
set(hle_guard_sources)
foreach(source IN LISTS hle_guard_original_sources)
    if(source MATCHES "/src/(common|gbi|hle|render)/([^/]+\\.cpp)$")
        set(relative "src/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
        get_filename_component(parent "${relative}" DIRECTORY)
        file(MAKE_DIRECTORY "${hle_guard_stage}/${parent}")
        if(relative STREQUAL "src/hle/rt64_interpreter.cpp")
            configure_file("${source}" "${hle_guard_input}/${relative}" COPYONLY)
        else()
            configure_file("${source}" "${hle_guard_stage}/${relative}" COPYONLY)
        endif()
        # Preserve any interface header composed by an earlier isolated stage.
        get_filename_component(source_parent "${source}" DIRECTORY)
        file(GLOB active_headers "${source_parent}/*.h")
        foreach(header IN LISTS active_headers)
            get_filename_component(name "${header}" NAME)
            if(parent STREQUAL "src/hle" AND name STREQUAL "rt64_interpreter.h")
                configure_file("${header}" "${hle_guard_input}/${parent}/${name}" COPYONLY)
            else()
                configure_file("${header}" "${hle_guard_stage}/${parent}/${name}" COPYONLY)
            endif()
        endforeach()
        foreach(property IN ITEMS INCLUDE_DIRECTORIES COMPILE_OPTIONS COMPILE_DEFINITIONS)
            get_source_file_property(value "${source}" TARGET_DIRECTORY rt64 ${property})
            if(value AND NOT value STREQUAL "NOTFOUND")
                set_source_files_properties(
                    "${hle_guard_stage}/${relative}"
                    TARGET_DIRECTORY rt64
                    PROPERTIES ${property} "${value}"
                )
            endif()
        endforeach()
        list(APPEND hle_guard_sources "${hle_guard_stage}/${relative}")
    else()
        list(APPEND hle_guard_sources "${source}")
    endif()
endforeach()
file(READ "${hle_guard_input}/src/hle/rt64_interpreter.h" hle_guard_header)
if(NOT hle_guard_header MATCHES "hleCommandValidator")
    execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${hle_guard_input}" COMMAND_ERROR_IS_FATAL ANY)
    foreach(mode IN ITEMS check apply)
        set(flags --recount --ignore-space-change --whitespace=error)
        if(mode STREQUAL "check")
            list(APPEND flags --check)
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${hle_guard_input}" apply ${flags} "${hle_guard_patch}"
            RESULT_VARIABLE result
            ERROR_VARIABLE error
        )
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "HLE pre-fetch guard ${mode} failed: ${error}")
        endif()
    endforeach()
endif()
# Fail closed if a partially prepared tree declared the member without the
# actual pre-dereference guard. Do not silently disable the client validator.
file(READ "${hle_guard_input}/src/hle/rt64_interpreter.cpp" hle_guard_impl)
if(
    NOT hle_guard_impl
        MATCHES
        "while \\(dl != nullptr\\) \\{[\r\n ]+if \\(hleCommandValidator != nullptr && !hleCommandValidator\\(state, dl\\)\\)"
)
    message(FATAL_ERROR "HLE command validator is not installed before opcode fetch")
endif()
# Avoid invalidating every ABI consumer on an unchanged configure: compose
# separately and copy the final bytes only if their content actually changed.
foreach(file IN ITEMS rt64_interpreter.h rt64_interpreter.cpp)
    configure_file("${hle_guard_input}/src/hle/${file}" "${hle_guard_stage}/src/hle/${file}" COPYONLY)
endforeach()
set_property(TARGET rt64 PROPERTY SOURCES "${hle_guard_sources}")
target_include_directories(rt64 BEFORE PUBLIC "${hle_guard_stage}/src" "${hle_guard_stage}/include")
