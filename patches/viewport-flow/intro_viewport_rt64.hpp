#pragma once
#include "hle/rt64_state.h"
#include "hle/rt64_rdp.h"
#include "hle/rt64_rsp.h"
#include <cmath>
#include <cstdio>

namespace dkr::runtime::presentation {

// The immutable task must belong to MENU_LOGOS, and the command must also
// match its reviewed inset. Credits have the same inset, so geometry alone
// is NOT enough to admit this correction. No changes to retail RAM or timers.
class IntroViewport {
    bool enabled_=false;
    bool picture_=false;
    int top_=0,bottom_=0,height_=240;
    static bool inset(int left,int top,int right,int bottom) {
        return left==0 && (right==319*4 || right==320*4) &&
            ((top==40*4 && bottom==196*4) || (top==38*4 && bottom==224*4));
    }
public:
    void begin(bool enabled) {enabled_=enabled;picture_=false;top_=bottom_=0;height_=240;}
    bool enabled() const {return enabled_;}
    void scissor(RT64::State& state) {
        if(!enabled_)return;
        auto& r=state.rdp->scissorRectStack[state.rdp->scissorStackSize-1];
        picture_=inset(r.ulx,r.uly,r.lrx,r.lry);
        if(!picture_)return;
        top_=r.uly;bottom_=r.lry;
        height_=top_==40*4 ? 240 : 288;
        r.ulx=0;r.uly=0;r.lrx=320*4;r.lry=height_*4;
        // Reveal the complete picture behind the existing copyright text.
        // Its later 2D viewport is separate and keeps the original text layout.
        auto& rdp=*state.rdp;
        rdp.extended.scissorLeftOriginStack[rdp.scissorStackSize-1]=G_EX_ORIGIN_NONE;
        rdp.extended.scissorRightOriginStack[rdp.scissorStackSize-1]=G_EX_ORIGIN_NONE;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
    }
    void viewport(RT64::State& state) const {
        if(!enabled_ || !picture_)return;
        auto& rsp=*state.rsp;
        auto& v=rsp.viewportStack[rsp.viewportStackSize-1];
        const float authored_center=(top_+bottom_)/8.F+0.5F;
        // Match only the custom 3D intro viewport, not viewport_reset's
        // ordinary 2D copyright viewport. Preserve X/Y scales and handedness.
        if(std::abs(std::abs(float(v.scale.x))-160.F)>0.01F ||
           std::abs(float(v.translate.x)-160.5F)>0.01F ||
           std::abs(float(v.translate.y)-authored_center)>0.01F)return;
        // Retail's inclusive rectangle gives a half-pixel X centre (160.5).
        // Keeping that would still leave viewport.rect().ulx > framebuffer.ulx
        // and make RT64 refuse full-width expansion despite a fixed scissor.
        v.translate.x=160.F;
        v.translate.y=height_/2.F;
        rsp.viewportChanged=true;
        static thread_local bool notice=false;
        if(!notice) {
            std::fprintf(stderr,"[intro][viewport] full-frame picture; copyright text retained; logo proportions preserved\n");
            notice=true;
        }
    }
    bool fill(RT64::State& state,const RT64::DisplayList& command) const {
        if(!enabled_ || state.rdp->colorImage.width!=320)return false;
        const int left=command.p1(12,12),top=command.p1(0,12);
        const int right=command.p0(12,12),bottom=command.p0(0,12);
        if(!inset(left,top,right,bottom))return false;
        RT64::ExtendedAlignment alignment{};
        alignment.leftOrigin=G_EX_ORIGIN_LEFT;
        alignment.rightOrigin=G_EX_ORIGIN_RIGHT;
        const int height=top==40*4 ? 240 : 288;
        state.rdp->fillRect(0,0,0,height*4,alignment);
        return true;
    }
    template<class Pair> void normalise(Pair& pair) const {
        if(!enabled_ || pair.colorImage.width!=320 || pair.scissorRect.isNull())return;
        pair.scissorRect.ulx=0;pair.scissorRect.lrx=320*4;
    }
};
}
