/* Private draw-copy observation only. Never registers live game identities or
 * advances shadow simulation. The original kernel's C fault fence owns errors. */
#include "camera_scene_addresses.h"
#include "funcs.h"
#include "camera_shadow_kernel.h"
#include <string.h>
#if defined(_MSC_VER)
#define SHADOW_TLS __declspec(thread)
#else
#define SHADOW_TLS _Thread_local
#endif
static SHADOW_TLS dkr_camera_shadows* shadow_output;
static SHADOW_TLS unsigned shadow_open;
static uint32_t shadow_word(uint32_t address) {
    return *(uint32_t*)dkr_camera_access((uint64_t)(int64_t)(int32_t)address,4);
}
static uint32_t command(void) {
    const uint32_t address=shadow_word(CAMERA_gTrackDL);
    if(address<0x80800000U||address>0x80900000U||(address&7))
        dkr_camera_fault("camera shadow command span");
    return address;
}
unsigned dkr_shadow_interpolation_begin(uint8_t* ram,recomp_context* ctx,...) {
    (void)ram;
    if(!shadow_output||shadow_open||shadow_output->count==DKR_CAMERA_MAX_SHADOWS)
        dkr_camera_fault("camera shadow metadata budget/scope");
    dkr_camera_shadow* entry=&shadow_output->entries[shadow_output->count++];
    entry->begin=command();entry->end=0;
    entry->object=(uint32_t)ctx->r6;entry->shadow=(uint32_t)ctx->r7;
    shadow_open=1;
    return 0;
}
unsigned dkr_shadow_interpolation_end(uint8_t* ram,recomp_context* ctx,...) {
    (void)ram;(void)ctx;
    if(!shadow_open)return 0; /* Retail early-return path has no matching begin. */
    shadow_output->entries[shadow_output->count-1].end=command();
    shadow_open=0;
    return 0;
}
int DKR_SHADOW_ENTRY(uint8_t* ram,size_t bytes,const dkr_camera_draw_request* request,
                     dkr_camera_draw_result* result,dkr_camera_shadows* shadows) {
    if(!shadows||shadow_output)return 0;
    memset(shadows,0,sizeof(*shadows));shadow_output=shadows;shadow_open=0;
    const int okay=CAMERA_ENTRY(ram,bytes,request,result);
    const int complete=!shadow_open;
    shadow_output=0;shadow_open=0;
    if(!okay||!complete) {
        shadows->count=0;
        if(okay&&result)result->failure="camera shadow scope did not close";
        return 0;
    }
    return 1;
}
