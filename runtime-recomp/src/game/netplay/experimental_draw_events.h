#ifndef DKR_EXPERIMENTAL_DRAW_EVENTS_H
#define DKR_EXPERIMENTAL_DRAW_EVENTS_H
#include <stdint.h>
/* Canonical, bounded observations of Patch Pipeline draw sites. No window,
   renderer, live identity registry or local graphics setting is read by CPU
   replay. The immutable frame owner applies them ONLY to its decode copy. */
enum dkr_owned_draw_kind {
    DKR_OWNED_BACKGROUND_BEGIN=1, DKR_OWNED_BACKGROUND_END,
    DKR_OWNED_BACKGROUND_QUAD,
    DKR_OWNED_POSTRACE_FULL_VIEWPORT,
    DKR_OWNED_FRAMED_BEGIN, DKR_OWNED_FRAMED_END,
    DKR_OWNED_LENS_BEGIN, DKR_OWNED_LENS_END,
    DKR_OWNED_SPLIT_VIEWPORT,
    DKR_OWNED_SPLIT_WORLD_BEGIN, DKR_OWNED_SPLIT_WORLD_END,
    DKR_OWNED_SPLIT_SKY_QUAD, DKR_OWNED_SPLIT_VOID_QUAD,
    DKR_OWNED_MATRIX, DKR_OWNED_GEOMETRY, DKR_OWNED_SHADOW,
    DKR_OWNED_HUD_PASS, DKR_OWNED_HUD_WIDGET, DKR_OWNED_HUD_RECT,
    DKR_OWNED_TRANSITION, DKR_OWNED_SKY_MATRIX, DKR_OWNED_FRAME_METADATA,
    DKR_OWNED_LOCAL_SCENERY
};
typedef struct dkr_owned_draw_event {
    uint32_t kind,address,token;
    /* Void's camera basis changes between quadrants. Capture its four float
       bits at the draw site, not the final camera's globals at decode time. */
    uint32_t parameters[16];
} dkr_owned_draw_event;

static inline int dkr_owned_draw_event_valid(const dkr_owned_draw_event* e) {
    if(e->kind<DKR_OWNED_BACKGROUND_BEGIN || e->kind>DKR_OWNED_LOCAL_SCENERY ||
       e->address<0x80000000U || e->address>0x807FFFD8U)return 0;
    const int quad=e->kind==DKR_OWNED_BACKGROUND_QUAD ||
        e->kind==DKR_OWNED_SPLIT_SKY_QUAD || e->kind==DKR_OWNED_SPLIT_VOID_QUAD;
    if(e->address&(quad ? 1U:7U))return 0;
    if(e->kind>=DKR_OWNED_MATRIX) {
        unsigned used=0;
        switch(e->kind) {
        case DKR_OWNED_MATRIX:
            if(e->address>0x807FFFC0U || e->parameters[0]>15U || e->parameters[1]>1U || e->parameters[3]>255U)return 0;
            used=4;break;
        case DKR_OWNED_SKY_MATRIX:case DKR_OWNED_TRANSITION:
            if(e->address>0x807FFFC0U || e->token)return 0;break;
        case DKR_OWNED_GEOMETRY:
            if(e->token>65535U || e->parameters[0]>9U || e->parameters[1]>31U)return 0;
            used=2;break;
        case DKR_OWNED_SHADOW:
            if(e->token>65535U || e->parameters[0]>16U || e->parameters[1]>65535U || e->parameters[2]>65535U ||
                (int32_t)e->parameters[3]<-128 || (int32_t)e->parameters[3]>127 || e->parameters[4]>255U ||
                (int32_t)e->parameters[8]<-32768 || (int32_t)e->parameters[8]>32767)return 0;
            used=9;break;
        case DKR_OWNED_HUD_PASS:
            if(e->token>3U || e->parameters[0]>1U)return 0;used=1;break;
        case DKR_OWNED_HUD_WIDGET:
            if(e->token>1U || e->parameters[0]>3U || e->parameters[1]>=4U || e->parameters[2]>59U || e->parameters[3]>24U ||
                e->parameters[4]>255U || e->parameters[5]>1U || e->parameters[6]>1U || e->parameters[9]>1U || e->parameters[10]>2U)return 0;
            used=11;break;
        case DKR_OWNED_HUD_RECT:
            if(e->token>1U || e->parameters[0]>3U)return 0;used=1;break;
        case DKR_OWNED_FRAME_METADATA:
            if(e->token || e->address!=0x80000000U || !e->parameters[0])return 0;used=1;break;
        case DKR_OWNED_LOCAL_SCENERY:
            if(e->token>3U || !e->parameters[0] || e->parameters[1]>127U || e->parameters[6]>1U ||
               !e->parameters[8] || e->parameters[8]>=128U || e->parameters[13]>7U || !e->parameters[14] || e->parameters[15]>1U)return 0;
            for(unsigned segment=e->parameters[8];segment<128;++segment)
                if(e->parameters[9+segment/32]&(1U<<(segment&31U)))return 0;
            used=16;break;
        default:return 0;
        }
        for(unsigned i=used;i<16;++i)if(e->parameters[i])return 0;
        return 1;
    }
    if(e->kind==DKR_OWNED_SPLIT_VIEWPORT) {if(e->token>3)return 0;}
    else if(e->kind==DKR_OWNED_SPLIT_SKY_QUAD || e->kind==DKR_OWNED_SPLIT_VOID_QUAD) {
        if(e->token!=2 && e->token!=3)return 0;
    } else if(e->kind==DKR_OWNED_BACKGROUND_BEGIN) {if(e->token>2)return 0;}
    else if(e->token>1)return 0;
    for(unsigned i=0;i<16;++i)
        if((e->kind!=DKR_OWNED_SPLIT_VOID_QUAD || i>=4) && e->parameters[i])return 0;
    return 1;
}
enum { DKR_OWNED_MAX_DRAW_EVENTS=8192 };
#endif
