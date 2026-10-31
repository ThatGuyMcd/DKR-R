#ifndef DKR_CAMERA_SHADOW_KERNEL_H
#define DKR_CAMERA_SHADOW_KERNEL_H
#include "camera_kernel.h"
enum { DKR_CAMERA_MAX_SHADOWS = 128 };
typedef struct dkr_camera_shadow {
    uint32_t begin, end, object, shadow;
} dkr_camera_shadow;
typedef struct dkr_camera_shadows {
    uint32_t count;
    dkr_camera_shadow entries[DKR_CAMERA_MAX_SHADOWS];
} dkr_camera_shadows;
#ifdef __cplusplus
extern "C" {
#endif
int dkr_camera_v77_shadow_draw(uint8_t*,size_t,const dkr_camera_draw_request*,dkr_camera_draw_result*,dkr_camera_shadows*);
int dkr_camera_v80_shadow_draw(uint8_t*,size_t,const dkr_camera_draw_request*,dkr_camera_draw_result*,dkr_camera_shadows*);
#ifdef __cplusplus
}
#endif
#endif
