#pragma once
#include "hud_reference_layout.hpp"
#include "hud_projection_rt64.hpp"
#include "split_screen_rt64.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"
#include "render/rt64_projection_processor.h"
#include <memory>
#include <vector>
#include <source_location>
#ifdef near
#undef near
#endif
#ifdef main
#undef main
#endif

namespace RT64 {
RenderViewport convertViewportRect(FixedRect, hlslpp::float2, int32_t, float, float, float, uint16_t, uint16_t);
}
namespace hud_reference_test {
inline void interrupt() {}
inline void near(float a, float b, std::source_location where = std::source_location::current()) {
    if (std::abs(a - b) >= .003F)
        std::fprintf(stderr, "HUD witness line %u: actual %.6f expected %.6f\n", where.line(), a, b);
    assert(std::abs(a - b) < .003F);
}
inline void run() {
    namespace ref = dkr::runtime::hud::reference;
    namespace hg = dkr::runtime::hud::groups;
    using namespace dkr::runtime::hud;
    for (float aspect : {4.F / 3, 16.F / 10, 16.F / 9, 21.F / 9, 32.F / 9}) {
        const float cover = aspect / (4.F / 3), margin = (240 * aspect - 320) * .5F;
        for (int scene = 0; scene < 7; ++scene)
            for (std::size_t e = 0; e < kElements.size(); ++e) {
                const auto w = kElements[e].widget;
                const auto s = static_cast<hg::Scenario>(scene);
                const int edge = ref::anchor(e, w, s, false, false);
                const auto original = ref::transform(LayoutMode::Original, aspect, edge, 320, true);
                assert(original.identity());
                const auto fit = ref::transform(LayoutMode::FitToViewport, aspect, edge, 0, true);
                near(fit.x, edge * margin);
                near(fit.scale, 1);
                near(fit.y, 0);
                const auto slide = ref::transform(LayoutMode::FitToViewport, aspect, edge, 320, true);
                near(slide.x, edge * margin + 320 * (cover - 1));
                const auto noSlide = ref::transform(LayoutMode::FitToViewport, aspect, edge, 320, false);
                near(noSlide.x, fit.x);
            }
        assert(ref::anchor(3, Widget::LapCounter, hg::Scenario::Race, false, false) == 1);
        assert(ref::anchor(3, Widget::LapCounter, hg::Scenario::TimeTrial, false, false) == -1);
        assert(ref::anchor(7, Widget::BananaCounter, hg::Scenario::Boss, false, false) == -1);
        assert(ref::anchor(7, Widget::BananaCounter, hg::Scenario::Treasure, false, false) == 0);
        assert(ref::anchor(50, Widget::ChallengePortrait, hg::Scenario::Treasure, true, true) == 1);
        assert(ref::anchor(15, Widget::Minimap, hg::Scenario::Race, false, false) ==
               ref::anchor(59, Widget::Minimap, hg::Scenario::Race, false, false));
        // Slot 33 is drawn twice, at -120/+120, regardless of which way the
        // course arrow points. Match the outer gutters without scaling it.
        for (bool twoPlayer : {false, true})
            for (int scene = 0; scene < 7; ++scene) {
                const auto s = static_cast<hg::Scenario>(scene);
                for (float slide : {-320.F, 0.F, 320.F})
                    for (float x : {-120.F, 120.F}) {
                        const int side = x < 0 ? -1 : 1;
                        const int edge = ref::anchor(33, Widget::CourseArrows, s, twoPlayer, false, x);
                        assert(edge == side); // the slide must never swap the two sides
                        const auto t = ref::transform(LayoutMode::FitToViewport, aspect, edge, slide, true);
                        const float slideOnly = slide * (cover - 1);
                        near(t.x - slideOnly, side * margin);
                        near(t.y, 0);
                        near(t.scale, 1);
                        // Position measured from the centred, expanded full canvas:
                        // each anchor retains the same 40 native-pixel edge inset.
                        const float displayed = 160 + margin + x + t.x - slideOnly;
                        near(side < 0 ? displayed : 320 + 2 * margin - displayed, 40);
                        const auto a = t.apply({x - 12, 15}), b = t.apply({x + 12, 45});
                        near(b.x - a.x, 24);
                        near(b.y - a.y, 30); // no stretch or Y shift
                        assert(ref::transform(LayoutMode::Original, aspect, edge, slide, true).identity());
                    }
                // Only the identified course-arrow slot gets this exception.
                for (std::size_t e = 0; e < kElements.size(); ++e)
                    if (e != 33) {
                        const auto w = kElements[e].widget;
                        const int ordinary = ref::anchor(e, w, s, twoPlayer, false);
                        for (float x : {-120.F, 120.F})
                            assert(ref::anchor(e, w, s, twoPlayer, false, x) == ordinary);
                    }
            }
        assert(ref::anchor(33, Widget::CourseArrows, hg::Scenario::Race, false, false, 0) == 0);
        assert(ref::anchor(59, Widget::CourseArrows, hg::Scenario::Race, false, false, -120) == 0);
        // Run the real pinned RT64 projection processor, not a presumed
        // multiplication. The old model-only flag cannot pass this witness.
        std::vector<uint8_t> ram(8 * 1024 * 1024);
        uint32_t irq = 0;
        auto queue = std::make_unique<RT64::WorkloadQueue>();
        auto state = std::make_unique<RT64::State>(ram.data(), &irq, &interrupt);
        state->ext.workloadQueue = queue.get();
        auto& rsp = *state->rsp;
        auto& workload = queue->workloads[queue->writeCursor];
        workload.addFramebufferPair(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 0x200000);
        auto& pair = workload.fbPairs[workload.currentFramebufferPairIndex()];
        const auto identity = hlslpp::float4x4::identity();
        rsp.viewMatrixStack[0] = rsp.projMatrixStack[0] = rsp.viewProjMatrixStack[0] = identity;
        rsp.extended.invViewMatrix = rsp.extended.invProjMatrix = rsp.extended.invViewProjMatrix = identity;
        auto& v = rsp.viewportStack[0];
        v = interop::RSPViewport::identity();
        v.scale = {160, 120, 511};
        v.translate = {160, 120, 511};
        rsp.extended.modelMatrixIdStack[0].matrixId = 1234;
        const int depth = rsp.extended.viewProjMatrixIdStackSize;
        RT64::GameFrame frame{};
        frame.workloads.push_back(queue->writeCursor);
        frame.frameMap.workloads.resize(queue->workloads.size());
        frame.orthographicScenes.emplace_back();
        std::vector<uint32_t> indices;
        for (int owner = 0; owner < 3; ++owner) { // full, 2P top, 2P bottom
            push_hud_projection(rsp);
            assert(rsp.extended.viewProjMatrixIdStackSize == depth + 1);
            rsp.addCurrentProjection(RT64::Projection::Type::Orthographic);
            indices.push_back(rsp.curViewProjIndex);
            pair.projections[rsp.projectionIndex].scissorRect =
                RT64::FixedRect(-int(margin * 4), owner == 2 ? 480 : 0, 1280 + int(margin * 4), owner == 1 ? 480 : 960);
            frame.orthographicScenes[0].projections.push_back(
                {uint32_t(queue->writeCursor), 0, uint32_t(rsp.projectionIndex)});
            pop_hud_projection(rsp);
            assert(rsp.extended.viewProjMatrixIdStackSize == depth);
            assert(rsp.extended.modelMatrixIdStack[0].matrixId == 1234);
            assert(rsp.extended.viewProjMatrixIdStack[depth - 1].aspectMode == G_EX_ASPECT_AUTO);
        }
        pair.scissorRect = RT64::FixedRect(-int(margin * 4), 0, 1280 + int(margin * 4), 960);
        dkr::runtime::presentation::normalise_split_framebuffer_extent(pair);
        assert(pair.scissorRect.ulx == 0 && pair.scissorRect.lrx == 1280);
        RT64::ProjectionProcessor processor;
        RT64::ProjectionProcessor::ProcessParams params{};
        params.workloadQueue = queue.get();
        params.curFrame = &frame;
        params.aspectRatioScale = cover;
        processor.process(params);
        for (auto i : indices)
            near(workload.drawData.modProjTransforms[i][0][0], 1 / cover);
        // A 32x32 textured glyph stays square at the actual output scale, even
        // outside the old safe area. Compare with the corrected MVP sprite.
        const auto t = ref::transform(LayoutMode::FitToViewport, aspect, -1, 0, true);
        const auto r = hg::transform_rectangle(t, 1024 + 80, 512 + 80, 1024 + 208, 512 + 208, 1024, 1024, 3);
        const auto rect = RT64::convertViewportRect(RT64::FixedRect(r.ulx, r.uly, r.lrx, r.lry), {cover * 3, 3}, 320,
                                                    1 / cover, 0, 0, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE);
        near(rect.width, 96);
        near(rect.height, 96);
        // RT64 ceils RDP edges to native pixels, then rounds output pixels.
        // At 3x this permits three output pixels in POSITION, never size drift.
        if (std::abs(rect.x - 3 * (margin + 20 + t.x)) > 3.001F) {
            std::fprintf(stderr, "HUD rectangle aspect %.4f actual x %.4f expected %.4f ulx %d\n", aspect, rect.x,
                         3 * (margin + 20 + t.x), r.ulx);
            assert(false);
        }
        assert(r.dsdx == 1024 && r.dtdy == 1024);
        // Negative control: model aspect != projection aspect. This is the
        // previous bug with an overscan aggregate: AUTO no longer sees a
        // full-width projection and produces horizontal expansion.
        rsp.extended.modelMatrixIdStack[0].aspectMode = G_EX_ASPECT_ADJUST;
        rsp.projectionMatrixChanged = true;
        rsp.addCurrentProjection(RT64::Projection::Type::Orthographic);
        const auto badIndex = rsp.curViewProjIndex;
        pair.projections[rsp.projectionIndex].scissorRect = RT64::FixedRect(0, 0, 1280, 960);
        pair.scissorRect = RT64::FixedRect(0, 0, 2560, 960);
        frame.orthographicScenes[0].projections.clear();
        frame.orthographicScenes[0].projections.push_back(
            {uint32_t(queue->writeCursor), 0, uint32_t(rsp.projectionIndex)});
        processor.process(params);
        near(workload.drawData.modProjTransforms[badIndex][0][0], 1);
    }
    std::puts(
        "HUD reference: 59 slots x 7 scenarios x 5 aspects; real RT64 projection/rectangle/stack witnesses passed");
}
} // namespace hud_reference_test
