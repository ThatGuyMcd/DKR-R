# Release builds compile only pinned ROM-derived inputs: with
# DKR_VERIFY_BUILD_INPUTS on (the release-* presets), each prepared input
# directory must match its digest in build-inputs.lock.json. The digest is
# scripts/release/lock_inputs.py's: sha256 over "relative-path sha256\n" of
# every file but the JSON/TOML provenance, sorted (text files with CRLF read
# as LF), then "manifest.json:field value\n" for each manifest field the
# configure reads. release_tools_tests.py checks the two agree, and that no
# excluded file is a compile or configure input.
option(DKR_VERIFY_BUILD_INPUTS "Refuse prepared inputs that differ from build-inputs.lock.json" OFF)

# lock_inputs.py CONFIGURE_FIELDS ("#name" is an array's length).
set(dkr_input_fields_owned
    revision
    symbol_prefix
    full_scenes
    local_model_observations
    online_view_observations
    local_world_observations
    owned_mod_services
    scene_cuts
    audio_services
    input_services
    authored_cpu
    audio_symbol_isolation
    runtime_symbol_isolation
    code_dispatch_schema
    "#code_protected_policy"
)
set(dkr_input_fields_model schema revision)

function(dkr_input_digest out directory)
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${directory}" "${directory}/*")
    list(SORT files)
    set(lines "")
    foreach(file IN LISTS files)
        if(file MATCHES "\\.(json|toml)$")
            continue()
        endif()
        if(file MATCHES "\\.(c|cc|cpp|h|hpp|inl|inc)$")
            # Generated C and headers (no NUL bytes): read as text, CRLF as LF.
            file(READ "${directory}/${file}" bytes)
            string(REPLACE "\r\n" "\n" bytes "${bytes}")
            string(SHA256 digest "${bytes}")
        else()
            file(SHA256 "${directory}/${file}" digest)
        endif()
        string(APPEND lines "${file} ${digest}\n")
    endforeach()
    foreach(field IN LISTS ARGN)
        if(NOT DEFINED manifest)
            file(READ "${directory}/manifest.json" manifest)
        endif()
        string(REPLACE "#" "" key "${field}")
        if(field MATCHES "^#")
            string(JSON value ERROR_VARIABLE error LENGTH "${manifest}" "${key}")
        else()
            string(JSON value ERROR_VARIABLE error GET "${manifest}" "${key}")
        endif()
        if(error)
            set(value "<missing>")
        endif()
        string(APPEND lines "manifest.json:${field} ${value}\n")
    endforeach()
    string(SHA256 total "${lines}")
    set(${out} "${total}" PARENT_SCOPE)
endfunction()

# lock_inputs.py policy_digest: an N64Recomp config, CRLF as LF, its three
# machine paths masked.
function(dkr_policy_digest out path)
    file(READ "${path}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(REGEX REPLACE "(^|\n)(elf_path|rom_file_path|output_func_path) = \"[^\n]*\"" "\\1\\2" text "${text}")
    string(SHA256 digest "${text}")
    set(${out} "${digest}" PARENT_SCOPE)
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
            string(REGEX REPLACE "-v[0-9]+$" "" kind "${name}")
            string(REPLACE "-" "_" kind "${kind}")
            dkr_input_digest(actual "${${variable}}" ${dkr_input_fields_${kind}})
            if(NOT actual STREQUAL locked)
                message(
                    FATAL_ERROR
                    "${variable} (${name}) differs from build-inputs.lock.json: ${actual}, "
                    "locked ${locked}. Prepare from the pinned ROMs and dependencies, or relock deliberately "
                    "(scripts/release/lock_inputs.py write)."
                )
            endif()
        endforeach()
        # The config the code-mod protection staging reads (CustomMagicCodes.cmake
        # defaults it to v<rev>.toml beside the payload).
        if(DEFINED CACHE{DKR_CODE_POLICY_V${revision}})
            set(policy "$CACHE{DKR_CODE_POLICY_V${revision}}")
        else()
            get_filename_component(parent "${DKR_GENERATED_SOURCE${suffix}}" DIRECTORY)
            set(policy "${parent}/v${revision}.toml")
        endif()
        if(NOT EXISTS "${policy}")
            message(FATAL_ERROR "A release build needs the v${revision} code policy: ${policy}")
        endif()
        string(JSON locked GET "${lock}" inputs "code-policy-v${revision}")
        dkr_policy_digest(actual "${policy}")
        if(NOT actual STREQUAL locked)
            message(FATAL_ERROR "${policy} differs from build-inputs.lock.json: ${actual}, locked ${locked}")
        endif()
    endforeach()
    message(STATUS "Prepared build inputs match build-inputs.lock.json")
endif()
