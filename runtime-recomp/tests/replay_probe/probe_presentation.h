#ifndef DKR_PROBE_PRESENTATION_H
#define DKR_PROBE_PRESENTATION_H
#include <stdint.h>
/* Only fixed-width, zero-initialized fields enter checkpoints. No renderer,
   heap pointer, local window, device or mutable legacy registry is borrowed. */
enum { DKR_OWNED_LIFETIMES=1024, DKR_OWNED_OBJECT_DEPTH=16 };
typedef struct dkr_owned_lifetime {
    uint32_t object,generation,token,attachments[32];
} dkr_owned_lifetime;
typedef struct dkr_owned_camera {
    uint32_t valid,epoch,frame,position[4],owner,node,shot_owner,shot_node,mode;
} dkr_owned_camera;
typedef struct dkr_owned_identity_state {
    uint32_t scene,next_generation,next_token;
    dkr_owned_lifetime lifetimes[DKR_OWNED_LIFETIMES];
    dkr_owned_camera cameras[8];
} dkr_owned_identity_state;
struct recomp_context;
struct dkr_owned_draw_event;
typedef void (*dkr_owned_emit)(const struct dkr_owned_draw_event*);
void dkr_probe_parity_begin(void);
void dkr_probe_parity_scene(dkr_owned_identity_state* state);
void dkr_probe_parity_lifetime(dkr_owned_identity_state* state,uint32_t object,int spawn);
int dkr_probe_parity_hook(const char* name,uint8_t* rdram,struct recomp_context* context,
    dkr_owned_identity_state* state,uint32_t frame,dkr_owned_emit emit);
int dkr_probe_parity_args(const char* name,uint8_t* rdram,struct recomp_context* context,
    const uint32_t* arguments,unsigned count,dkr_owned_emit emit);
#endif
