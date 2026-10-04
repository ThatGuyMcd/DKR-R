#pragma once
#include "probe_bridge.h"
#ifdef __cplusplus
#include <span>
bool dkr_probe_retail_audio_check(std::span<const uint8_t> fixture);
extern "C" {
#endif
/* Private scratch belongs to the isolated audio owner, never a live heap. */
#define DKR_PROBE_AUDIO_COMMANDS 0x80F90000U
#define DKR_PROBE_AUDIO_OUTPUT 0x80F80000U
#define DKR_PROBE_AUDIO_LENGTH 0x80F8FFF0U
void dkr_probe_audio_arguments(unsigned samples, int play_sound);
void dkr_probe_audio_tick(uint8_t* rdram, struct recomp_context* ctx);
/* Owned DMEM is copied into every audio checkpoint. Constants are immutable. */
void dkr_probe_audio_rsp_initialize(void);
void dkr_probe_audio_dmem_capture(uint8_t out[4096]);
void dkr_probe_audio_dmem_restore(const uint8_t in[4096]);
void dkr_probe_audio_rsp(uint8_t* rdram, uint32_t commands, unsigned bytes);
/* Per-world native event guards are part of the audio checkpoint. */
typedef struct dkr_probe_audio_guard {
    uint32_t queue, immediate_events, logged_empty, logged_corrupt;
} dkr_probe_audio_guard;
void dkr_probe_audio_guards_initialize(void);
void dkr_probe_audio_guards_capture(dkr_probe_audio_guard out[8]);
void dkr_probe_audio_guards_restore(const dkr_probe_audio_guard in[8]);
uint32_t dkr_probe_audio_event_next(uint8_t* rdram, struct recomp_context* ctx);
int dkr_probe_audio_voice_guard(uint8_t* rdram, struct recomp_context* ctx);
int dkr_probe_audio_bus_guard(uint8_t* rdram, struct recomp_context* ctx);
#ifdef __cplusplus
}
#endif
