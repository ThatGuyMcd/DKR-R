# Release builds compile only pinned ROM-derived inputs: with
# DKR_VERIFY_BUILD_INPUTS on (the release-* presets), each prepared input
# directory must match its digest in build-inputs.lock.json. The digest is
# scripts/release/lock_inputs.py's: sha256 over "relative-path sha256\n" of
# every file but the JSON/TOML provenance, sorted, CRLF read as LF.
# lock_inputs_tests.py checks the two agree.
option(DKR_VERIFY_BUILD_INPUTS "Refuse prepared inputs that differ from build-inputs.lock.json" OFF)

function(dkr_input_digest out directory)
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${directory}" "${directory}/*")
    list(SORT files)
    set(lines "")
    foreach(file IN LISTS files)
        if(file MATCHES "\\.(json|toml)$")
            continue()
        endif()
        # The inputs are generated C and headers (no NUL bytes), read as text.
        file(READ "${directory}/${file}" bytes)
        string(REPLACE "\r\n" "\n" bytes "${bytes}")
        string(SHA256 digest "${bytes}")
        string(APPEND lines "${file} ${digest}\n")
    endforeach()
    string(SHA256 total "${lines}")
    set(${out} "${total}" PARENT_SCOPE)
endfunction()

if(DKR_VERIFY_BUILD_INPUTS)
    set(lock_file "${CMAKE_CURRENT_LIST_DIR}/../build-inputs.lock.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${lock_file}")
    file(READ "${lock_file}" lock)
    foreach(revision IN ITEMS 77 80)
        set(suffix "_V${revision}")
        set(owned_suffix "${suffix}")
        if(revision EQUAL 77)
            set(owned_suffix "")
        endif()
        foreach(
            pair
            IN
            ITEMS
                "generated-v${revision}=DKR_GENERATED_SOURCE${suffix}"
                "owned-v${revision}=DKR_EXPERIMENTAL_OWNED_GENERATED${owned_suffix}"
                "model-v${revision}=DKR_VISUAL_MODEL_GENERATED${suffix}"
                "camera-kernel-v${revision}=DKR_CAMERA_KERNEL${suffix}"
        )
            string(REPLACE "=" ";" pair "${pair}")
            list(GET pair 0 name)
            list(GET pair 1 variable)
            if(NOT IS_DIRECTORY "${${variable}}")
                message(FATAL_ERROR "A release build needs ${variable} (${name}); run the prepare script.")
            endif()
            string(JSON locked GET "${lock}" inputs "${name}")
            dkr_input_digest(actual "${${variable}}")
            if(NOT actual STREQUAL locked)
                message(
                    FATAL_ERROR
                    "${variable} (${name}) differs from build-inputs.lock.json: ${actual}, "
                    "locked ${locked}. Prepare from the pinned ROMs and dependencies, or relock deliberately "
                    "(scripts/release/lock_inputs.py write)."
                )
            endif()
        endforeach()
    endforeach()
    message(STATUS "Prepared build inputs match build-inputs.lock.json")
endif()
