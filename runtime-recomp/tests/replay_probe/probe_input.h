#pragma once
#include "probe_component.h"
#ifdef __cplusplus
#include <span>
bool dkr_probe_retail_input_check(std::span<const uint8_t> fixture);
extern "C" {
#endif
/* Private logical controller owner, never the live SI/input/SDL broker.
   All fields are canonical-width scalars and belong to each checkpoint. */
typedef struct dkr_probe_input_state {
    uint32_t requested, ready, delivered, motors;
    dkr_probe_pad pads[4];
} dkr_probe_input_state;
typedef struct dkr_probe_motor_event { uint32_t channel, enabled; } dkr_probe_motor_event;
void dkr_probe_input_configure(int enabled);
int dkr_probe_input_enabled(void);
void dkr_probe_input_initialize(void);
dkr_probe_input_state dkr_probe_input_capture(void);
void dkr_probe_input_restore(dkr_probe_input_state state);
void dkr_probe_input_arguments(const dkr_probe_pad pads[4], unsigned flags);
void dkr_probe_input_test_arguments(const dkr_probe_pad pads[4], unsigned flags);
void dkr_probe_input_tick(uint8_t* rdram, struct recomp_context* ctx);
void dkr_probe_input_frame(uint8_t* rdram, struct recomp_context* ctx);
/* Stages one already-frozen sample; the retained retail main CPU phase calls
   input_update itself. No duplicate retail update or physical input poll. */
void dkr_probe_input_begin_sample(uint8_t* rdram, struct recomp_context* ctx);
void dkr_probe_input_sample_complete(void);
/* Called only inside a C trampoline; returns 0 for every unowned import. */
int dkr_probe_input_service(const char* name, uint8_t* rdram, struct recomp_context* ctx);
unsigned dkr_probe_motor_events(dkr_probe_motor_event out[128]);
#ifdef __cplusplus
}
#endif
