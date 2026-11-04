#ifndef DKR_ROAD_WHEELS_H
#define DKR_ROAD_WHEELS_H
#include <stddef.h>
#include <stdint.h>

/* Draw-only, shared by the normal, owned and private-camera renderers. */
typedef struct dkr_road_wheel_call {
    uint32_t event, object, holder, sprite, flags;
    uint32_t intensity, inverse_view, viewport_layout, custom_car;
} dkr_road_wheel_call;
#ifdef __cplusplus
extern "C" {
#endif
void dkr_native_road_wheel_scope(uint8_t*, size_t, const dkr_road_wheel_call*);
#ifdef __cplusplus
}
#endif
#endif
