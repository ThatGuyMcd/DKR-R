# Default the ROM-derived build inputs to what scripts/prepare_payloads.py
# prepared (Prepare-DKR-Runtime-macOS.sh runs it), so a configure after
# preparing needs no paths. Each is a cache default: anything set on the
# command line wins. The set is all or nothing: a configure that names its own
# US v1.0 payload gets none of it, since the owned engine and kernels belong to
# the payload they were prepared from. Without a prepared directory nothing is
# set, and the options keep their old defaults (no owned engine, no Camera++
# or local model kernels).
set(DKR_PREPARED_DIR "${DKRPORT_ROOT}/build/prepared" CACHE PATH
    "Inputs scripts/prepare_payloads.py prepares from the N64Recomp payload")
if(DKR_RUNTIME_BUILD_GENERATED AND EXISTS "${DKR_PREPARED_DIR}/prepared.json"
        AND (NOT DEFINED CACHE{DKR_GENERATED_SOURCE_V77}
            OR DKR_GENERATED_SOURCE_V77 STREQUAL "${DKR_PREPARED_DIR}/generated-v77"))
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DKR_PREPARED_DIR}/prepared.json")
    set(DKR_GENERATED_SOURCE_V77 "${DKR_PREPARED_DIR}/generated-v77" CACHE PATH
        "N64Recomp-generated CPU source for DKR US v1.0")
    set(DKR_EXPERIMENTAL_OWNED_GENERATED "${DKR_PREPARED_DIR}/owned-v77" CACHE PATH
        "NEW isolated owned CPU Patch Pipeline output; integration targets only")
    set(DKR_VISUAL_MODEL_GENERATED_V77 "${DKR_PREPARED_DIR}/model-v77" CACHE PATH
        "NEW renderer-only model kernel output for US v1.0")
    set(DKR_CAMERA_KERNEL_V77 "${DKR_PREPARED_DIR}/camera-kernel-v77" CACHE PATH
        "Prepared Camera++ scene kernel for revision 77")
    if(EXISTS "${DKR_PREPARED_DIR}/generated-v80")
        set(DKR_GENERATED_SOURCE_V80 "${DKR_PREPARED_DIR}/generated-v80" CACHE PATH
            "N64Recomp-generated CPU source for DKR US Rev A/v1.1")
        set(DKR_EXPERIMENTAL_OWNED_GENERATED_V80 "${DKR_PREPARED_DIR}/owned-v80" CACHE PATH
            "NEW isolated US Rev A full-scene owned CPU Patch Pipeline output")
        set(DKR_VISUAL_MODEL_GENERATED_V80 "${DKR_PREPARED_DIR}/model-v80" CACHE PATH
            "NEW renderer-only model kernel output for US v1.1")
        set(DKR_CAMERA_KERNEL_V80 "${DKR_PREPARED_DIR}/camera-kernel-v80" CACHE PATH
            "Prepared Camera++ scene kernel for revision 80")
    endif()
endif()
