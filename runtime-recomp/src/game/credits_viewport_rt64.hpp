#pragma once
#include "hle/rt64_state.h"
#include "hle/rt64_rdp.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_workload_queue.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace dkr::runtime::presentation {

// The immutable task must belong to MENU_CREDITS, and the command must also
// match its reviewed inset. Credits have the same inset, so geometry alone
// is NOT enough to admit this correction. No changes to retail RAM or timers.
class CreditsViewport {
    bool enabled_ = false;
    float cover_ = 1.F;
    bool picture_ = false;
    int top_ = 0, bottom_ = 0, height_ = 240;
    static bool inset(int left, int top, int right, int bottom) {
        return left == 0 && (right == 319 * 4 || right == 320 * 4) &&
               ((top == 40 * 4 && bottom == 196 * 4) || (top == 38 * 4 && bottom == 224 * 4));
    }

  public:
    void begin(bool enabled, float aspect) {
        enabled_ = enabled;
        picture_ = false;
        top_ = bottom_ = 0;
        height_ = 240;
        cover_ = std::clamp(std::isfinite(aspect) ? aspect / (4.F / 3.F) : 1.F, 1.F, 8.F);
    }
    bool enabled() const {
        return enabled_;
    }
    void scissor(RT64::State& state) {
        if (!enabled_)
            return;
        auto& r = state.rdp->scissorRectStack[state.rdp->scissorStackSize - 1];
        picture_ = inset(r.ulx, r.uly, r.lrx, r.lry);
        if (!picture_) {
            // Ordinary 2D/transition resets use the inclusive 319/239 bounds.
            // Keep their native aspect reference complete, without widening
            // arbitrary partial UI scissors or changing their projection.
            if (r.ulx == 0 && r.uly == 0 && (r.lrx == 319 * 4 || r.lrx == 320 * 4) &&
                (r.lry == 239 * 4 || r.lry == 240 * 4 || r.lry == 287 * 4 || r.lry == 288 * 4)) {
                r.lrx = 320 * 4;
                r.lry = r.lry > 240 * 4 ? 288 * 4 : 240 * 4;
                state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
            }
            return;
        }
        top_ = r.uly;
        bottom_ = r.lry;
        height_ = top_ == 40 * 4 ? 240 : 288;
        r.ulx = 0;
        r.uly = 0;
        r.lrx = 320 * 4;
        r.lry = height_ * 4;
        // Reveal the complete picture behind the existing copyright text.
        // Its later 2D viewport is separate and keeps the original text layout.
        auto& rdp = *state.rdp;
        rdp.extended.scissorLeftOriginStack[rdp.scissorStackSize - 1] = G_EX_ORIGIN_NONE;
        rdp.extended.scissorRightOriginStack[rdp.scissorStackSize - 1] = G_EX_ORIGIN_NONE;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
    }
    void viewport(RT64::State& state) const {
        if (!enabled_ || !picture_)
            return;
        auto& rsp = *state.rsp;
        auto& v = rsp.viewportStack[rsp.viewportStackSize - 1];
        const float authored_center = (top_ + bottom_) / 8.F + 0.5F;
        // Match only the custom 3D intro viewport, not viewport_reset's
        // ordinary 2D copyright viewport. Preserve X/Y scales and handedness.
        if (std::abs(std::abs(float(v.scale.x)) - 160.F) > 0.01F || std::abs(float(v.translate.x) - 160.5F) > 0.01F ||
            std::abs(float(v.translate.y) - authored_center) > 0.01F)
            return;
        // Retail's inclusive rectangle gives a half-pixel X centre (160.5).
        // Keeping that would still leave viewport.rect().ulx > framebuffer.ulx
        // and make RT64 refuse full-width expansion despite a fixed scissor.
        v.translate.x = 160.F;
        v.translate.y = height_ / 2.F;
        rsp.viewportChanged = true;
        static thread_local bool notice = false;
        if (!notice) {
            std::fprintf(stderr,
                         "[credits][viewport] full-frame cinematic; borders removed; original scene proportions\n");
            notice = true;
        }
    }
    bool fill(RT64::State& state, const RT64::DisplayList& command) const {
        if (!enabled_ || state.rdp->colorImage.width != 320)
            return false;
        const int left = command.p1(12, 12), top = command.p1(0, 12);
        const int right = command.p0(12, 12), bottom = command.p0(0, 12);
        if (!inset(left, top, right, bottom))
            return false;
        auto& rdp = *state.rdp;
        state.flush();
        const auto at = rdp.scissorStackSize - 1;
        const auto previous = rdp.scissorRectStack[at];
        const auto left_origin = rdp.extended.scissorLeftOriginStack[at];
        const auto right_origin = rdp.extended.scissorRightOriginStack[at];
        const auto aspect = rdp.extended.global.rectAspect;
        const int height = top == 40 * 4 ? 240 : 288;
        const int gutter = int(std::ceil(640.F * (cover_ - 1.F))) + 8;
        // Rectangle origins alone cannot escape a still-native draw scissor.
        // Use the same signed window-domain clip as the accepted font path.
        auto& clip = rdp.scissorRectStack[at];
        clip.ulx = -gutter;
        clip.uly = 0;
        clip.lrx = 1280 + gutter;
        clip.lry = height * 4;
        rdp.extended.scissorLeftOriginStack[at] = rdp.extended.scissorRightOriginStack[at] = G_EX_ORIGIN_NONE;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        rdp.setRectAspect(G_EX_ASPECT_ADJUST);
        RT64::ExtendedAlignment alignment{};
        alignment.leftOrigin = alignment.rightOrigin = G_EX_ORIGIN_NONE;
        rdp.fillRect(-gutter, 0, 1280 + gutter, height * 4, alignment);
        state.flush();
        // Preserve the signed per-draw scissor/rectangle for widescreen, but
        // never expose overscan to native RAM/depth readback or target sizing.
        auto& workload = state.ext.workloadQueue->workloads[state.ext.workloadQueue->writeCursor];
        normalise_native_bounds(workload.fbPairs[workload.currentFramebufferPairIndex()], height);
        rdp.scissorRectStack[at] = previous;
        rdp.extended.scissorLeftOriginStack[at] = left_origin;
        rdp.extended.scissorRightOriginStack[at] = right_origin;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        rdp.setRectAspect(aspect);
        static thread_local bool notice = false;
        if (!notice) {
            std::fprintf(
                stderr,
                "[credits][fade] signed full-window rectangle and scissor; original opacity; horizontal raster-edge overlap; native readback bounds\n");
            notice = true;
        }
        return true;
    }
    template <class Pair> static void normalise_native_bounds(Pair& pair, int height) {
        if (pair.colorImage.width != 320)
            return;
        const RT64::FixedRect native(0, 0, 320 * 4, height * 4);
        const auto constrain = [&](auto& rect) {
            if (rect.isNull())
                return;
            rect = rect.intersection(native);
            if (rect.isNull() || rect.isEmpty())
                rect.reset();
        };
        constrain(pair.drawColorRect);
        constrain(pair.drawDepthRect);
        // Aggregate extent only, not the saved per-draw signed clipping.
        if (!pair.scissorRect.isNull())
            pair.scissorRect = native;
    }
    template <class Pair> void normalise(Pair& pair) const {
        if (enabled_)
            normalise_native_bounds(pair, height_);
    }
};
} // namespace dkr::runtime::presentation
