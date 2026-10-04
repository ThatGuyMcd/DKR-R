#ifndef DKR_PROBE_COMPONENT_H
#define DKR_PROBE_COMPONENT_H
#include "probe_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct dkr_probe_pad { uint16_t buttons; int8_t stick_x, stick_y; } dkr_probe_pad;
void dkr_probe_component_inputs(const dkr_probe_pad inputs[4]);
void dkr_probe_component_tick(uint8_t* ram, struct recomp_context* context);
// Adds audited real CPU water/contact-shadow/lighting/fog/texture phases.
// Still excludes camera/HUD rendering, scene loads and live native workers.
void dkr_probe_water_component_tick(uint8_t* ram, struct recomp_context* context);
// Full real mode_game CPU camera/geometry/HUD path under the explicitly
// audited canonical private profile, plus retail frame-end guest phases.
// Does NOT submit GPU/audio tasks, replace the scheduler or admit scene loads.
void dkr_probe_mode_component_tick(uint8_t* ram, struct recomp_context* context);
// Retains the audited main-loop CPU span including RSP/RDP setup, background,
// missing-controller HUD and complete DL terminators. No task/VI completion.
void dkr_probe_authored_component_tick(uint8_t* ram, struct recomp_context* context);
// Confirmed private game-to-game transaction resumes the saved guest stack.
// Does not sample input again or rerun the suspended scene's CPU prefix.
void dkr_probe_authored_scene_resume(uint8_t* ram, struct recomp_context* context);
#ifdef __cplusplus
}
#endif
#endif
