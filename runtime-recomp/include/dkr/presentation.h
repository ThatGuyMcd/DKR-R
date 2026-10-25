#ifndef DKR_PRESENTATION_SDK_H
#define DKR_PRESENTATION_SDK_H

/* Presentation v1. All lengths are bytes, angles are radians, distances are
 * game-world units and time is seconds. No field contains a guest/host pointer.
 * The host copies these values into a PRIVATE presentation VM. This API is
 * deliberately unavailable to gameplay callbacks, including online rollback.
 */
#ifdef DKR_PRESENTATION_HOST
#include <cstdint>
typedef std::uint32_t DkrViewU32;
typedef std::int32_t DkrViewS32;
#else
#include "magic_codes.h"
typedef DkrU32 DkrViewU32;
typedef DkrS32 DkrViewS32;
#endif

#define DKR_PRESENTATION_VERSION 1U
enum DkrViewCapability {
    DKR_VIEW_POSE=1U, DKR_VIEW_PROJECTION=2U, DKR_VIEW_REAR_COVERAGE=4U,
    DKR_VIEW_ORBIT_COVERAGE=8U, DKR_VIEW_HIDE_LOCAL_MODEL=16U,
    DKR_VIEW_HIDE_RIDER=32U, DKR_VIEW_STEREO=64U,
    DKR_VIEW_STATIC_COLLISION=128U, DKR_VIEW_DYNAMIC_COLLISION=256U
};
enum DkrViewState {
    DKR_VIEW_DRIVING=1U, DKR_VIEW_SCRIPTED=2U, DKR_VIEW_CUT=4U,
    DKR_VIEW_GROUNDED=8U, DKR_VIEW_DRIFTING=16U, DKR_VIEW_BOOSTING=32U,
    DKR_VIEW_IMPACT=64U, DKR_VIEW_ONLINE=128U, DKR_VIEW_INSPECTION_ALLOWED=256U
};
enum DkrViewAction {
    DKR_VIEW_LOOK_BACK=1U, DKR_VIEW_RECENTER=2U, DKR_VIEW_CYCLE=4U,
    DKR_VIEW_ZOOM_IN=8U, DKR_VIEW_ZOOM_OUT=16U,
    DKR_VIEW_FIRST_PERSON=32U, DKR_VIEW_XR_RECENTER=64U
};
enum DkrViewMode {
    DKR_VIEW_ORIGINAL=0U, DKR_VIEW_CHASE=1U, DKR_VIEW_BONNET=2U,
    DKR_VIEW_FIRST=3U, DKR_VIEW_COMFORT_FIRST=4U,
    DKR_VIEW_CINEMATIC=5U, DKR_VIEW_ORBIT=6U, DKR_VIEW_VR_DRIVER=7U
};
enum DkrViewVisibility {
    DKR_VIEW_SHOW_MODEL=0U, DKR_VIEW_HIDE_VEHICLE=1U, DKR_VIEW_HIDE_RIDER_ONLY=2U
};
enum DkrViewQueryStatus {
    DKR_VIEW_QUERY_NONE=0U, DKR_VIEW_QUERY_HIT=1U,
    DKR_VIEW_QUERY_UNSUPPORTED=2U, DKR_VIEW_QUERY_BUDGET=3U
};

typedef struct DkrViewSnapshot {
    DkrViewU32 api,size,capabilities,flags;
    DkrViewU32 scene,epoch,frame,view;
    DkrViewU32 racer_id,racer_generation,vehicle,character;
    float dt,time,aspect,speed;
    float position[3],velocity[3];
    float yaw,pitch,roll,steer;
    float original_position[3],original_yaw,original_pitch,original_roll;
    float original_fov,original_near,original_far,boost;
    DkrViewU32 reserved[4];
} DkrViewSnapshot;

typedef struct DkrViewInput {
    DkrViewU32 api,size,held,pressed,released,blocked;
    float orbit_x,orbit_y,zoom;
    DkrViewU32 reserved[3];
} DkrViewInput;

typedef struct DkrViewPose {
    DkrViewU32 api,size,view,mode;
    DkrViewU32 racer_id,racer_generation,visibility,cut;
    float position[3],yaw,pitch,roll;
    float vertical_fov,near_plane,far_plane;
    DkrViewU32 reserved[3];
} DkrViewPose;

typedef struct DkrViewQuery {
    DkrViewU32 api,size,view,exclude_racer;
    float origin[3],target[3],radius;
    DkrViewU32 include_dynamic,reserved[2];
} DkrViewQuery;

typedef struct DkrViewHit {
    DkrViewU32 api,size,status,reserved;
    float fraction,position[3],normal[3];
    DkrViewU32 reserved2;
} DkrViewHit;

enum DkrViewMath {DKR_VIEW_SIN=0U,DKR_VIEW_COS=1U,DKR_VIEW_ATAN2=2U,
    DKR_VIEW_EXP=3U,DKR_VIEW_SQRT=4U};

#ifdef DKR_PRESENTATION_HOST
static_assert(sizeof(DkrViewSnapshot)==160);
static_assert(sizeof(DkrViewInput)==48);
static_assert(sizeof(DkrViewPose)==80);
static_assert(sizeof(DkrViewQuery)==56);
static_assert(sizeof(DkrViewHit)==48);
#else
/* Exactly one on_presentation callback is required. It runs once for an
 * eligible local view, not once per gameplay tick or per eye. All writes are
 * confined to this package's private memory and validated output structures.
 * Extra callbacks, gameplay imports, raw references and native hooks are
 * rejected before activation. Tracking and graphics devices stay host-owned.
 */
DKR_IMPORT DkrU32 dkr_view_snapshot(DkrViewSnapshot* out) {(void)out;return 0;}
DKR_IMPORT DkrU32 dkr_view_input(DkrViewInput* out) {(void)out;return 0;}
DKR_IMPORT DkrU32 dkr_view_submit(const DkrViewPose* pose) {(void)pose;return 0;}
DKR_IMPORT DkrU32 dkr_view_query(const DkrViewQuery* query,DkrViewHit* hit) {(void)query;(void)hit;return 0;}
/* Float bits avoid platform-dependent O32 mixed floating/integer arguments. */
DKR_IMPORT DkrU32 dkr_view_math(DkrU32 operation,DkrU32 x,DkrU32 y) {(void)operation;(void)x;(void)y;return 0;}
static inline float dkr_view_real(DkrU32 bits) {union {DkrU32 u;float f;} v;v.u=bits;return v.f;}
static inline DkrU32 dkr_view_bits(float value) {union {DkrU32 u;float f;} v;v.f=value;return v.u;}
static inline float dkr_view_sin(float x) {return dkr_view_real(dkr_view_math(DKR_VIEW_SIN,dkr_view_bits(x),0));}
static inline float dkr_view_cos(float x) {return dkr_view_real(dkr_view_math(DKR_VIEW_COS,dkr_view_bits(x),0));}
static inline float dkr_view_atan2(float y,float x) {return dkr_view_real(dkr_view_math(DKR_VIEW_ATAN2,dkr_view_bits(y),dkr_view_bits(x)));}
static inline float dkr_view_exp(float x) {return dkr_view_real(dkr_view_math(DKR_VIEW_EXP,dkr_view_bits(x),0));}
static inline float dkr_view_sqrt(float x) {return dkr_view_real(dkr_view_math(DKR_VIEW_SQRT,dkr_view_bits(x),0));}
_Static_assert(sizeof(DkrViewSnapshot)==160,"presentation snapshot ABI");
_Static_assert(sizeof(DkrViewInput)==48,"presentation input ABI");
_Static_assert(sizeof(DkrViewPose)==80,"presentation pose ABI");
_Static_assert(sizeof(DkrViewQuery)==56,"presentation query ABI");
_Static_assert(sizeof(DkrViewHit)==48,"presentation hit ABI");
#endif
#endif
