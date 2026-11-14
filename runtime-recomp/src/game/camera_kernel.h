#ifndef DKR_CAMERA_KERNEL_H
#define DKR_CAMERA_KERNEL_H
#include <stdint.h>
#include <stddef.h>
enum {DKR_CAMERA_KERNEL_BYTES=0x1000000,DKR_CAMERA_MAX_MATRICES=4096};
typedef struct dkr_camera_matrix {uint32_t address,identity,root;} dkr_camera_matrix;
typedef struct dkr_camera_draw_request {
    uint32_t view,scene,return_address,hide_object,full_screen;
    float position[3],yaw,pitch,roll,fov,near_plane,far_plane,aspect;
} dkr_camera_draw_request;
typedef struct dkr_camera_draw_result {
    uint32_t start,end,operations,matrix_count;
    const char* failure;
    dkr_camera_matrix matrices[DKR_CAMERA_MAX_MATRICES];
} dkr_camera_draw_result;
#ifdef __cplusplus
extern "C" {
#endif
int dkr_camera_v77_draw(uint8_t*,size_t,const dkr_camera_draw_request*,dkr_camera_draw_result*);
int dkr_camera_v80_draw(uint8_t*,size_t,const dkr_camera_draw_request*,dkr_camera_draw_result*);
#ifdef __cplusplus
}
#endif
#endif
