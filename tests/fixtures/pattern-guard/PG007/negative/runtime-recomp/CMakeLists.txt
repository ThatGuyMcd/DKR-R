dkr_stage_patch(rt64 "${DKR_RT64_SOURCE}" staged
    "${DKRPORT_ROOT}/patches/rt64/0001-cache-limit.patch" src/render/rt64_texture_cache.cpp)
dkr_stage_patch(rt64 "${CMAKE_BINARY_DIR}/generated/staged" staged-on-staged
    "${DKRPORT_ROOT}/patches/rt64/staged/0002-staged.patch" src/render/rt64_texture_cache.cpp)
