#ifndef DKR_PRIVATE_REPLAY_SAVE_H
#define DKR_PRIVATE_REPLAY_SAVE_H
#include "probe_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private closed SAVE component, not a replacement of retail input_update. */
void dkr_probe_save_arguments(unsigned slot, unsigned initials, int write);
void dkr_probe_save_tick(uint8_t* ram, struct recomp_context* context);
#ifdef __cplusplus
}
#include <span>
bool dkr_probe_retail_save_check(std::span<const uint8_t> fixture);
#endif
#endif
