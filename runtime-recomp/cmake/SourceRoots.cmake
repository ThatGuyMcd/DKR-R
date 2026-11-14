# First-party sources always belong to the configuring worktree.
get_filename_component(DKR_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
if(DEFINED DKRPORT_ROOT)
    get_filename_component(_dkr_old_root "${DKRPORT_ROOT}" REALPATH)
    if(NOT _dkr_old_root STREQUAL DKR_SOURCE_ROOT)
        message(
            FATAL_ERROR
            "DKRPORT_ROOT can no longer redirect first-party sources to another checkout. "
            "Configure a fresh build directory with -DDKR_DEPENDENCIES_DIR=${_dkr_old_root}/extern "
            "to reuse its read-only dependencies, and remove the DKRPORT_ROOT override."
        )
    endif()
endif()
set(DKRPORT_ROOT "${DKR_SOURCE_ROOT}")
set(DKR_DEPENDENCIES_DIR
    "${DKR_SOURCE_ROOT}/extern"
    CACHE PATH
    "Prepared dependency directory (may be shared read-only between worktrees)"
)
get_filename_component(DKR_DEPENDENCIES_DIR "${DKR_DEPENDENCIES_DIR}" ABSOLUTE)
