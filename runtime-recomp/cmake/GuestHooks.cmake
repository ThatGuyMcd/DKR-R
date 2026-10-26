# Guest hooks: runtime-recomp/guest-hooks/hooks.toml applied to recompiled
# guest code by scripts/guest_hooks.py, at configure time like the code-mod
# dispatch staging. Protected generated input is never edited: hooked copies
# go to a content-addressed build directory and replace their originals in the
# payload's source list.
#
#   dkr_apply_guest_hooks(<sources-var> <source-dir> <revision> <funcs|owned>)
include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(dkr_apply_guest_hooks sources_var source revision payload)
    set(spec "${DKRPORT_ROOT}/runtime-recomp/guest-hooks/hooks.toml")
    set(script "${DKRPORT_ROOT}/scripts/guest_hooks.py")
    set(symbols "${DKRPORT_ROOT}/modding/reference-us-v${revision}/data.toml")
    set(parts "")
    foreach(input IN ITEMS "${spec}" "${script}" "${symbols}")
        file(SHA256 "${input}" digest)
        string(APPEND parts "${input}:${digest}|")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    endforeach()
    file(GLOB inputs CONFIGURE_DEPENDS "${source}/*.c")
    foreach(input IN LISTS inputs)
        file(SHA256 "${input}" digest)
        get_filename_component(name "${input}" NAME)
        string(APPEND parts "${name}:${digest}|")
    endforeach()
    string(SHA256 identity "${payload}|${revision}|${parts}")
    set(out "${CMAKE_BINARY_DIR}/generated/guest-hooks-${payload}-v${revision}-${identity}")
    if(NOT EXISTS "${out}/hooked.txt")
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${script}" apply --spec "${spec}"
            --in "${source}" --out "${out}" --revision "${revision}" --payload "${payload}"
            RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            file(REMOVE_RECURSE "${out}")
            message(FATAL_ERROR "Guest hooks for US v${revision} ${payload} did not apply; see above")
        endif()
    endif()
    file(STRINGS "${out}/hooked.txt" hooked)
    set(sources ${${sources_var}})
    foreach(name IN LISTS hooked)
        list(FIND sources "${source}/${name}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "Guest hooks: ${name} is not a source of the US v${revision} ${payload} payload")
        endif()
        list(REMOVE_AT sources ${at})
        list(INSERT sources ${at} "${out}/${name}")
    endforeach()
    set(${sources_var} ${sources} PARENT_SCOPE)
endfunction()
