# Default the ROM-derived build inputs to what scripts/prepare_payloads.py
# prepared (Prepare-DKR-Runtime-macOS.sh runs it), so a configure after
# preparing needs no paths. Each is a cache default: anything set on the
# command line wins. The set is all or nothing: a configure that names its own
# payload for either revision gets none of it, since the owned engines and
# kernels belong to the payloads they were prepared from. Without a prepared directory nothing is
# set, and the options keep their old defaults (no owned engine, no Camera++
# or local model kernels).
set(DKR_PREPARED_DIR
    "${DKRPORT_ROOT}/build/prepared"
    CACHE PATH
    "Inputs scripts/prepare_payloads.py prepares from the N64Recomp payload"
)
# An empty cached value counts as unset, so a build directory configured
# before preparing picks the prepared inputs up on its next configure.
macro(dkr_prepared_default variable value doc)
    if(NOT DEFINED CACHE{${variable}} OR "$CACHE{${variable}}" STREQUAL "")
        set(${variable} "${value}" CACHE PATH "${doc}" FORCE)
    endif()
endmacro()

# Compared as real paths: Windows callers pass backslashed paths.
file(TO_CMAKE_PATH "${DKR_PREPARED_DIR}" dkr_prepared_dir)
get_filename_component(dkr_prepared_dir "${dkr_prepared_dir}" REALPATH)
set(dkr_foreign_payload OFF)
foreach(dkr_revision IN ITEMS 77 80)
    set(dkr_named_payload "$CACHE{DKR_GENERATED_SOURCE_V${dkr_revision}}")
    if(NOT dkr_named_payload STREQUAL "")
        file(TO_CMAKE_PATH "${dkr_named_payload}" dkr_named_payload)
        get_filename_component(dkr_named_payload "${dkr_named_payload}" REALPATH)
        if(NOT dkr_named_payload STREQUAL "${dkr_prepared_dir}/generated-v${dkr_revision}")
            set(dkr_foreign_payload ON)
        endif()
    endif()
endforeach()
if(DKR_RUNTIME_BUILD_GENERATED AND EXISTS "${dkr_prepared_dir}/prepared.json" AND dkr_foreign_payload)
    message(
        WARNING
        "DKR_GENERATED_SOURCE_V77/V80 name a payload outside ${dkr_prepared_dir}, so none of the prepared "
        "inputs apply: this build has no owned engines, Camera++ or local model kernels. Clear both to build "
        "from the prepared payloads."
    )
endif()
if(DKR_RUNTIME_BUILD_GENERATED AND EXISTS "${dkr_prepared_dir}/prepared.json" AND NOT dkr_foreign_payload)
    set(DKR_PREPARED_DIR "${dkr_prepared_dir}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DKR_PREPARED_DIR}/prepared.json")
    dkr_prepared_default(
        DKR_GENERATED_SOURCE_V77
        "${DKR_PREPARED_DIR}/generated-v77"
        "N64Recomp-generated CPU source for DKR US v1.0"
    )
    dkr_prepared_default(
        DKR_EXPERIMENTAL_OWNED_GENERATED
        "${DKR_PREPARED_DIR}/owned-v77"
        "NEW isolated owned CPU Patch Pipeline output; integration targets only"
    )
    dkr_prepared_default(
        DKR_VISUAL_MODEL_GENERATED_V77
        "${DKR_PREPARED_DIR}/model-v77"
        "NEW renderer-only model kernel output for US v1.0"
    )
    dkr_prepared_default(
        DKR_CAMERA_KERNEL_V77
        "${DKR_PREPARED_DIR}/camera-kernel-v77"
        "Prepared Camera++ scene kernel for revision 77"
    )
    if(EXISTS "${DKR_PREPARED_DIR}/generated-v80")
        dkr_prepared_default(
            DKR_GENERATED_SOURCE_V80
            "${DKR_PREPARED_DIR}/generated-v80"
            "N64Recomp-generated CPU source for DKR US Rev A/v1.1"
        )
        dkr_prepared_default(
            DKR_EXPERIMENTAL_OWNED_GENERATED_V80
            "${DKR_PREPARED_DIR}/owned-v80"
            "NEW isolated US Rev A full-scene owned CPU Patch Pipeline output"
        )
        dkr_prepared_default(
            DKR_VISUAL_MODEL_GENERATED_V80
            "${DKR_PREPARED_DIR}/model-v80"
            "NEW renderer-only model kernel output for US v1.1"
        )
        dkr_prepared_default(
            DKR_CAMERA_KERNEL_V80
            "${DKR_PREPARED_DIR}/camera-kernel-v80"
            "Prepared Camera++ scene kernel for revision 80"
        )
    endif()
    # The one-function-per-file guest fixtures the allocator release gates
    # (DKRMemoryStability77/80Checks) and the guest crash regressions compile.
    if(EXISTS "${DKR_PREPARED_DIR}/fixtures-v77" AND EXISTS "${DKR_PREPARED_DIR}/fixtures-v80")
        dkr_prepared_default(
            DKR_MEMORY_FIXTURE_ROOT
            "${DKR_PREPARED_DIR}"
            "Fresh one-function-per-file Patch Pipeline fixtures for headless crash regression"
        )
    endif()
endif()
