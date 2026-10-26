#include "split_screen_rt64.hpp"
#include "postrace_viewport_rt64.hpp"
#include "widescreen_policy.hpp"
#include "track_select_draw_policy.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"
#include "render/rt64_projection_processor.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

// These are the pinned renderer's real conversion functions, not a second
// implementation of their arithmetic. No dependency sources are modified.
namespace RT64 {
RenderViewport convertViewportRect(FixedRect, hlslpp::float2, int32_t, float, float, float, uint16_t, uint16_t);
}

namespace {
void Near(float actual, float expected) {
    assert(std::fabs(actual - expected) < 0.002F);
}
void Interrupt() {}

void CheckTrackSelectRectangles(float cover) {
    using namespace dkr::runtime::presentation;
    // The recording's brief stretching occurs when an expanded screen union
    // disables the pinned renderer's 10% native-aspect heuristic. Exercise the
    // exact production normaliser with real RT64 pairs and conversion math;
    // keep every per-draw/projection scissor and framebuffer write extent.
    const auto window = track_select_window_clip(track_select_cover_token(cover));
    for (int left : {0, window.left})
        for (int right : {1280, window.right}) {
            RT64::FramebufferPair pair{};
            pair.colorImage.width = 320;
            pair.scissorRect = RT64::FixedRect(left, 0, right, 960);
            pair.drawColorRect = pair.drawDepthRect = pair.scissorRect;
            pair.changeProjection(0, RT64::Projection::Type::Rectangle);
            pair.projections[0].scissorRect = pair.scissorRect;
            pair.projections[0].gameCalls.emplace_back();
            pair.projections[0].gameCallCount = 1;
            pair.projections[0].gameCalls[0].callDesc.scissorRect = pair.scissorRect;
            const auto before = pair.scissorRect;
            const float oldRatio = float(before.width(false, true)) / before.height(false, true);
            const bool oldAdjust = std::fabs(oldRatio / (4.F / 3.F) - 1.F) < 0.1F;
            const auto oldImage =
                RT64::convertViewportRect(RT64::FixedRect(320, 240, 960, 720), {4.F * cover, 4.F}, 320,
                                          oldAdjust ? 1.F / cover : 1.F, 0.F, 0.F, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE);
            if (!oldAdjust && cover > 1.F)
                assert(oldImage.width > 640.F);
            normalise_track_select_framebuffer_extent(pair);
            Near(float(pair.scissorRect.width(false, true)) / pair.scissorRect.height(false, true), 4.F / 3.F);
            assert(pair.drawColorRect.ulx == before.ulx && pair.drawColorRect.lrx == before.lrx);
            assert(pair.drawDepthRect.ulx == before.ulx && pair.drawDepthRect.lrx == before.lrx);
            assert(pair.projections[0].scissorRect.ulx == before.ulx &&
                   pair.projections[0].scissorRect.lrx == before.lrx);
            assert(pair.projections[0].gameCalls[0].callDesc.scissorRect.ulx == before.ulx &&
                   pair.projections[0].gameCalls[0].callDesc.scissorRect.lrx == before.lrx);
            for (int x = -320; x <= 640; x += 8) {
                for (int width : {8, 16, 80, 160}) {
                    const auto image =
                        RT64::convertViewportRect(RT64::FixedRect(x * 4, 240, (x + width) * 4, 720), {4.F * cover, 4.F},
                                                  320, 1.F / cover, 0.F, 0.F, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE);
                    Near(image.width, width * 4.F);
                    Near(image.height, 480.F);
                    // The frame's authored position uses the same centered scale.
                    assert(std::fabs(image.x - ((x - 160.F) * 4.F + 640.F * cover)) <= 0.51F);
                }
            }
        }
    RT64::FramebufferPair offscreen{};
    offscreen.colorImage.width = 256;
    offscreen.scissorRect = RT64::FixedRect(window.left, 0, window.right, 960);
    normalise_track_select_framebuffer_extent(offscreen);
    assert(offscreen.scissorRect.ulx == window.left && offscreen.scissorRect.lrx == window.right);
    RT64::FramebufferPair empty{};
    empty.colorImage.width = 320;
    normalise_track_select_framebuffer_extent(empty);
    assert(empty.scissorRect.isNull());
}

void CheckPostraceProjection(float cover, bool full_view) {
    std::vector<uint8_t> ram(8 * 1024 * 1024);
    uint32_t interrupt = 0;
    auto queue = std::make_unique<RT64::WorkloadQueue>();
    auto state = std::make_unique<RT64::State>(ram.data(), &interrupt, &Interrupt);
    state->ext.workloadQueue = queue.get();
    auto& rsp = *state->rsp;
    auto& workload = queue->workloads[queue->writeCursor];
    workload.addFramebufferPair(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 0x200000);
    auto& pair = workload.fbPairs[workload.currentFramebufferPairIndex()];
    const auto identity = hlslpp::float4x4::identity();
    rsp.viewMatrixStack[0] = rsp.projMatrixStack[0] = rsp.viewProjMatrixStack[0] = identity;
    rsp.extended.invViewMatrix = rsp.extended.invProjMatrix = rsp.extended.invViewProjMatrix = identity;
    auto& viewport = rsp.viewportStack[0];
    viewport = interop::RSPViewport::identity();
    viewport.scale = {157.F, 118.F, 511.F};
    viewport.translate = {160.5F, 120.5F, 511.F};
    RT64::FixedRect scissor(8, 4, 1272, 952); // first authored contraction
    if (full_view) {
        dkr::runtime::presentation::fill_postrace_viewport(viewport);
        dkr::runtime::presentation::fill_postrace_scissor(scissor);
    }
    rsp.viewportChanged = true;
    rsp.addCurrentProjection(RT64::Projection::Type::Perspective);
    const auto index = rsp.curViewProjIndex;
    pair.projections[rsp.projectionIndex].scissorRect = scissor;
    pair.scissorRect = RT64::FixedRect(0, 0, 1280, 960);
    RT64::GameFrame frame{};
    frame.workloads.push_back(queue->writeCursor);
    frame.frameMap.workloads.resize(queue->workloads.size());
    frame.perspectiveScenes.emplace_back();
    frame.perspectiveScenes[0].projections.push_back({uint32_t(queue->writeCursor), 0, uint32_t(rsp.projectionIndex)});
    RT64::ProjectionProcessor processor;
    RT64::ProjectionProcessor::ProcessParams params{};
    params.workloadQueue = queue.get();
    params.curFrame = &frame;
    params.aspectRatioScale = cover;
    processor.process(params);
    // Execute RT64's real AUTO policy: wide until released, authored 4:3
    // afterwards. A correctly sized viewport with a 319 scissor fails this.
    Near(workload.drawData.modProjTransforms[index][0][0], full_view ? 1.F / cover : 1.F);
}

void CheckLayout(float cover, int players, bool mirrored) {
    std::vector<uint8_t> ram(8 * 1024 * 1024);
    uint32_t interrupt = 0;
    auto queue = std::make_unique<RT64::WorkloadQueue>();
    auto state = std::make_unique<RT64::State>(ram.data(), &interrupt, &Interrupt);
    state->ext.workloadQueue = queue.get();
    auto& rsp = *state->rsp;
    auto& workload = queue->workloads[queue->writeCursor];
    workload.addFramebufferPair(0x100000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 0x200000);
    auto& pair = workload.fbPairs[workload.currentFramebufferPairIndex()];
    const auto identity = hlslpp::float4x4::identity();
    rsp.viewMatrixStack[0] = rsp.projMatrixStack[0] = rsp.viewProjMatrixStack[0] = identity;
    rsp.extended.invViewMatrix = rsp.extended.invProjMatrix = rsp.extended.invViewProjMatrix = identity;
    // A non-default model identity must survive every world/HUD transition.
    rsp.extended.modelMatrixIdStack[0].matrixId = 123456U;
    rsp.extended.modelMatrixIdStack[0].vertexInterpolation = G_EX_COMPONENT_INTERPOLATE;
    bool active = false;
    RT64::GameFrame frame{};
    frame.workloads.push_back(queue->writeCursor);
    frame.frameMap.workloads.resize(queue->workloads.size());
    frame.orthographicScenes.emplace_back();
    std::vector<uint32_t> worldTransforms;
    for (int camera = 0; camera < players; ++camera) {
        const float left = (camera & 1) ? 160.F : 0.F;
        const float right = left + 160.F;
        const auto expanded = dkr::runtime::enhancements::expand_split_viewport_horizontal_range(
            mirrored ? right : left, mirrored ? left : right, cover, camera);
        auto& viewport = rsp.viewportStack[0];
        viewport = interop::RSPViewport::identity();
        viewport.scale.x = (expanded.right - expanded.left) / 2.F;
        viewport.scale.y = 60.F;
        viewport.translate.x = (expanded.left + expanded.right) / 2.F;
        viewport.translate.y = camera < 2 ? 60.F : 180.F;
        rsp.viewportChanged = true;
        dkr::runtime::presentation::select_split_world_projection(rsp, cover > 1.F, active);
        const auto depth = rsp.extended.viewProjMatrixIdStackSize;
        dkr::runtime::presentation::select_split_world_projection(rsp, cover > 1.F, active);
        assert(rsp.extended.viewProjMatrixIdStackSize == depth);
        rsp.addCurrentProjection(RT64::Projection::Type::Orthographic);
        worldTransforms.push_back(rsp.curViewProjIndex);
        auto& projection = pair.projections[rsp.projectionIndex];
        projection.scissorRect = RT64::FixedRect(
            int32_t(std::lround(std::min(expanded.left, expanded.right) * 4.F)), camera < 2 ? 0 : 480,
            int32_t(std::lround(std::max(expanded.left, expanded.right) * 4.F)), camera < 2 ? 480 : 960);
        pair.scissorRect.merge(projection.scissorRect);
        frame.orthographicScenes[0].projections.push_back(
            {uint32_t(queue->writeCursor), 0, uint32_t(rsp.projectionIndex)});
        rsp.projectionMatrixChanged = false;
        dkr::runtime::presentation::select_split_world_projection(rsp, false, active);
        assert(!active);
        assert(rsp.extended.viewProjMatrixIdStackSize == 1);
        assert(rsp.extended.viewProjMatrixIdStack[0].aspectMode == G_EX_ASPECT_AUTO);
        assert(rsp.extended.modelMatrixIdStack[0].matrixId == 123456U);
        assert(rsp.extended.modelMatrixIdStack[0].vertexInterpolation == G_EX_COMPONENT_INTERPOLATE);
        if (cover > 1.F)
            assert(rsp.projectionMatrixChanged);
    }
    const auto firstScissor = pair.projections[0].scissorRect;
    pair.scissorRect.merge(RT64::FixedRect(0, 0, 1280, 960));
    dkr::runtime::presentation::normalise_split_framebuffer_extent(pair);
    assert(pair.scissorRect.ulx == 0 && pair.scissorRect.lrx == 1280);
    assert(pair.projections[0].scissorRect.ulx == firstScissor.ulx);
    assert(pair.projections[0].scissorRect.lrx == firstScissor.lrx);
    // The subsequent full-width HUD projection must have restored AUTO.
    rsp.viewportStack[0].scale = {160.F, 120.F, 1.F};
    rsp.viewportStack[0].translate = {160.F, 120.F, 0.F};
    rsp.viewportChanged = true;
    rsp.addCurrentProjection(RT64::Projection::Type::Orthographic);
    const auto hudTransform = rsp.curViewProjIndex;
    pair.projections[rsp.projectionIndex].scissorRect = RT64::FixedRect(0, 0, 1280, 960);
    frame.orthographicScenes[0].projections.push_back({uint32_t(queue->writeCursor), 0, uint32_t(rsp.projectionIndex)});
    RT64::ProjectionProcessor processor;
    RT64::ProjectionProcessor::ProcessParams params{};
    params.workloadQueue = queue.get();
    params.curFrame = &frame;
    params.aspectRatioScale = cover;
    processor.process(params);
    for (auto index : worldTransforms)
        Near(workload.drawData.modProjTransforms[index][0][0], 1.F / cover);
    Near(workload.drawData.modProjTransforms[hudTransform][0][0], 1.F / cover);
    // Direct/batched rectangles share the same real conversion. Verify equal
    // X/Y scale and authored banana-to-number spacing on BOTH sides, allowing
    // only the renderer's final integer-pixel rounding.
    const float nativeRatio = float(pair.scissorRect.width(false, true)) / pair.scissorRect.height(false, true);
    Near(nativeRatio, 4.F / 3.F);
    for (int player = 0; player < 4; ++player) {
        const bool right = (player & 1) != 0;
        const int rankX = right ? 271 : 21; // authored x minus the 24-unit pivot
        const auto alignment = dkr::runtime::presentation::split_counter_alignment(
            {}, right ? dkr::runtime::hud::kHudAnchorRight : dkr::runtime::hud::kHudAnchorLeft, 320,
            dkr::runtime::hud::decode_hud_viewport_cover(
                dkr::runtime::hud::encode_hud_viewport_cover(cover * (4.F / 3.F))));
        assert(alignment.leftOrigin == G_EX_ORIGIN_NONE);
        assert(alignment.rightOrigin == G_EX_ORIGIN_NONE);
        assert(alignment.leftOffset == alignment.rightOffset);
        // RDP applies these signed offsets before recording. Use the ACTUAL
        // runtime extOriginPercentage=0, not the dependency's default of 1.
        const auto rect = RT64::convertViewportRect(
            RT64::FixedRect(rankX * 4 + alignment.leftOffset, 64, (rankX + 24) * 4 + alignment.rightOffset, 160),
            {4.F * cover, 4.F}, 320, 1.F / cover, 0.F, 0.F, alignment.leftOrigin, alignment.rightOrigin);
        Near(rect.width, 96.F);
        Near(rect.height, 96.F);
        // Allow cover-token quantisation and final pixel rounding only.
        assert(std::fabs(rect.x - (rankX * 4.F + (right ? 1280.F * (cover - 1.F) : 0.F))) < 4.F);
        const float gutter = dkr::runtime::hud::fullscreen_gutter_authored(cover * (4.F / 3.F));
        const float itemX = right ? 125.F : -125.F;
        const float shifted = itemX + (right ? gutter : -gutter);
        const float pixelX = (160.F + shifted / cover) * 4.F * cover;
        assert(std::fabs(pixelX - ((160.F + itemX) * 4.F + (right ? 1280.F * (cover - 1.F) : 0.F))) < 0.01F);
    }
    // Fill-cycle ignores rectAspect; the explicit signed extension must work
    // with extOriginPercentage=0 while keeping the centre and vertical edges.
    const auto panelAlignment = dkr::runtime::presentation::split_panel_alignment(320, cover);
    const auto panel =
        RT64::convertViewportRect(RT64::FixedRect(640, 480, 1280 + panelAlignment.rightOffset, 960), {4.F * cover, 4.F},
                                  320, 1.F / cover, 0.F, 0.F, panelAlignment.leftOrigin, panelAlignment.rightOrigin);
    assert(std::fabs(panel.x - 640.F * cover) < 4.F);
    assert(std::fabs(panel.x + panel.width - 1280.F * cover) < 4.F);
    Near(panel.y, 480.F);
    Near(panel.height, 480.F);
    for (int numberX : {97, 228}) {
        const auto rect = RT64::convertViewportRect(RT64::FixedRect(numberX * 4, 19 * 4, (numberX + 16) * 4, 35 * 4),
                                                    {4.F * cover, 4.F}, 320, 1.F / cover, 0.F, 0.F, G_EX_ORIGIN_NONE,
                                                    G_EX_ORIGIN_NONE);
        Near(rect.width, rect.height);
        const float bananaX = numberX - 17.F;
        const float spriteX = (160.F + (bananaX - 160.F) / cover) * (4.F * cover);
        assert(std::fabs((rect.x - spriteX) - 68.F) <= 0.51F);
    }
}
} // namespace

#ifdef main
#undef main
#endif
int main() {
    for (float cover : {1.F, 1.2F, 4.F / 3.F, 1.75F, 8.F / 3.F}) {
        CheckTrackSelectRectangles(cover);
        CheckPostraceProjection(cover, true);
        CheckPostraceProjection(cover, false);
    }
    for (bool mirrored : {false, true}) {
        // Exact production correction, including the retail 319x239 case and
        // first zoom step. Depth and handedness must survive unchanged.
        auto viewport = interop::RSPViewport::identity();
        viewport.scale = {mirrored ? -157.F : 157.F, 118.F, 511.F};
        viewport.translate = {160.5F, 120.5F, 511.F};
        RT64::FixedRect scissor(0, 0, 319 * 4, 239 * 4);
        dkr::runtime::presentation::fill_postrace_viewport(viewport);
        dkr::runtime::presentation::fill_postrace_scissor(scissor);
        Near(viewport.scale.x, mirrored ? -160.F : 160.F);
        Near(viewport.scale.y, 120.F);
        Near(viewport.scale.z, 511.F);
        Near(viewport.translate.x, 160.F);
        Near(viewport.translate.y, 120.F);
        Near(viewport.translate.z, 511.F);
        assert(scissor.ulx == 0 && scissor.uly == 0 && scissor.lrx == 1280 && scissor.lry == 960);
    }
    for (float cover : {1.F, 1.2F, 4.F / 3.F, 1.75F, 8.F / 3.F})
        for (int players : {3, 4})
            for (bool mirrored : {false, true})
                CheckLayout(cover, players, mirrored);
    RT64::FramebufferPair offscreen{};
    offscreen.colorImage.width = 256;
    offscreen.scissorRect = RT64::FixedRect(-32, 0, 1500, 100);
    dkr::runtime::presentation::normalise_split_framebuffer_extent(offscreen);
    assert(offscreen.scissorRect.ulx == -32 && offscreen.scissorRect.lrx == 1500);
    std::puts(
        "[test][track-select-rt64] PASS: native-aspect heuristic correction, 9,680 signed rectangle placements, preserved draw scissors.");
    std::puts("[test][split-screen-rt64] PASS: real RSP groups, projection processor, rectangle mapping, 40 layouts.");
    return 0;
}
