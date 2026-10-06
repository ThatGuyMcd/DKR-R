#ifndef DKR_CAMERA_CLEARANCE_METADATA_H
#define DKR_CAMERA_CLEARANCE_METADATA_H
#include <stdint.h>

/* Shared by the native Patch Pipeline observers and canonical CPU replay.
   Qualification uses only guest scene state, never local graphics settings. */
#ifdef __cplusplus
#define DKR_CLEARANCE_CONSTEXPR constexpr
#else
#define DKR_CLEARANCE_CONSTEXPR
#endif
static inline DKR_CLEARANCE_CONSTEXPR int dkr_world_projection_eligible(
    int32_t game_mode, uint32_t race_type, uint32_t layout, int perspective) {
    return game_mode == 0 && layout <= 3 && perspective &&
        (race_type == 0 || race_type == 3 || race_type == 5 ||
         race_type == 8 || (race_type <= 255 && (race_type & 0x40) != 0));
}
static inline DKR_CLEARANCE_CONSTEXPR int dkr_world_projection_metadata_valid(
    uint32_t authored_fov, uint32_t effective_fov_bits, uint32_t layout) {
    if (!authored_fov) return !effective_fov_bits && !layout;
    /* Positive IEEE-754 encodings are ordered; this also rejects NaN/Inf,
       signed values and denormals without type-punning in the C adapter. */
    return authored_fov < 120 && effective_fov_bits >= 0x3F800000U &&
        effective_fov_bits < 0x42F00000U && layout <= 3;
}
#undef DKR_CLEARANCE_CONSTEXPR
#endif
