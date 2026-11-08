# Applied only to an isolated, already-composed dependency source copy.
include_guard(GLOBAL)
dkr_stage_patch(librecomp "${CMAKE_BINARY_DIR}/generated/custom-magic-code-heap" startup-memory
    "${DKRPORT_ROOT}/patches/startup-memory/runtime-memory.patch" librecomp/src/recomp.cpp)
