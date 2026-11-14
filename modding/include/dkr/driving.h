#ifndef DKR_DRIVING_H
#define DKR_DRIVING_H
#include "game.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive analogue-driving v1 capability, DKR-R 1.0.14-beta.3 or newer.
 * Logical, calibrated, recorded input. No physical controller access.
 * Levels are independent integers 0..255; flags bit 0 means enabled.
 * Existing DkrInput and API 1 binary layouts are unchanged. */
typedef struct DkrDrivingInput {
    DkrU32 api, size, flags, accelerator, brake;
} DkrDrivingInput;
DKR_IMPORT int dkr2_driving_input(DkrU32 player, DkrDrivingInput* output) {
    (void)player;
    (void)output;
    return -1;
}
/* Request native effort caps for this tick using that exact recorded sample.
 * Call from on_frame. Native coast, boosts and control restrictions retain
 * ownership; CPU/boss/special vehicles are not modified. */
DKR_IMPORT int dkr2_driving_apply(DkrU32 player) {
    (void)player;
    return -1;
}
#ifdef __cplusplus
}
#endif
#endif
