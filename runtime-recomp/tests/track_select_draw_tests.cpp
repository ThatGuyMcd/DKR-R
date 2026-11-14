#include "recomp.h"
#include "track_select_draw_guest.h"
#include "track_select_draw_policy.hpp"
#include "netplay/experimental_draw_events.h"
#include "f3ddkr_execution_policy.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
static unsigned checks;
static void check(bool okay) {
    ++checks;
    if (!okay)
        throw std::runtime_error("Track Select drawing regression");
}
int main() {
    using namespace dkr::runtime::presentation;
    std::vector<uint8_t> memory(8U * 1024U * 1024U, 0xCD);
    auto* rdram = memory.data();
    recomp_context registers{};
    auto* ctx = &registers;
    dkr_track_draw_state state{};
    constexpr gpr slot = (int32_t)0x80008000U, packet = (int32_t)0x80010000U;
    for (int x = -700; x <= 1000; x += 7)
        for (unsigned event : {3U, 5U, 7U}) {
            dkr_track_draw_guest(&state, rdram, ctx, 0);
            std::array<int32_t, 8> wanted{x * 4, 240, (x + 64) * 4, 624, 0, 0, 1024, 1024};
            if (event == 3) {
                wanted[4] = 2016;
                wanted[6] = -819;
                ctx->r11 = wanted[0];
                ctx->r10 = wanted[1];
                ctx->r18 = wanted[2];
                ctx->r19 = wanted[3];
                ctx->r13 = wanted[4];
                ctx->r8 = wanted[5];
                ctx->r12 = wanted[6];
                ctx->r7 = wanted[7];
            } else if (event == 5) {
                ctx->r6 = wanted[0];
                ctx->r7 = wanted[1];
                ctx->r11 = wanted[2];
                ctx->r12 = wanted[3];
            } else {
                wanted[4] = 96;
                wanted[5] = 128;
                ctx->r5 = wanted[0];
                ctx->r6 = wanted[1];
                ctx->r10 = wanted[2];
                ctx->r9 = wanted[3];
                ctx->r12 = wanted[4];
                ctx->r14 = wanted[5];
            }
            dkr_track_draw_guest(&state, rdram, ctx, event);
            MEM_W(0, packet) = 0xE4000000;
            MEM_W(4, packet) = 0;
            MEM_W(8, packet) = 0xB3000000;
            MEM_W(12, packet) = 0;
            MEM_W(16, packet) = 0xB2000000;
            MEM_W(20, packet) = 0x04000400;
            ctx->r16 = slot;
            ctx->r17 = slot;
            MEM_W(0, slot) = (int32_t)(packet + 24);
            const auto before = MEM_W(0, slot);
            dkr_track_draw_guest(&state, rdram, ctx, event + 1);
            check(MEM_W(0, slot) == before && !state.pending);
            check((uint32_t)MEM_W(0, packet) == 0xD5000001U && (uint32_t)MEM_W(4, packet) == 0x54524354U);
            for (unsigned i = 0; i < 8; i += 2)
                check((uint32_t)MEM_W(8 + i * 2, packet) == dkr_track_pair(wanted[i], wanted[i + 1]));
            const auto corrected = MEM_W(0, packet);
            dkr_track_draw_guest(&state, rdram, ctx, 1);
            check(dkr_track_draw_guest(&state, rdram, ctx, 2) == 0);
            dkr_track_draw_guest(&state, rdram, ctx, event);
            dkr_track_draw_guest(&state, rdram, ctx, event + 1);
            check(MEM_W(0, packet) == corrected);
        }
    ctx->r29 = (int32_t)0x80020000U;
    dkr_track_draw_guest(&state, rdram, ctx, 10);
    check((uint32_t)MEM_W(0x8C, ctx->r29) == 0xCDCDCDCDU);
    for (unsigned i = 0x90; i < 0x180; i += 4)
        check(MEM_W(i, ctx->r29) == 0);
    check((uint32_t)MEM_W(0x180, ctx->r29) == 0xCDCDCDCDU);
    for (float aspect : {4.F / 3, 16.F / 10, 16.F / 9, 21.F / 9, 32.F / 9}) {
        const auto token = track_select_cover_token(aspect / (4.F / 3));
        const auto window = track_select_window_clip(token);
        check(window.top == 0 && window.bottom == 960 && window.right + window.left == 1280);
        for (int center = -160; center <= 480; center += 3) {
            const auto clip = track_select_preview_clip({(center - 80) * 4, 240, (center + 80) * 4, 720}, token);
            check(clip.left >= window.left && clip.right <= window.right && clip.left <= clip.right);
            check(clip.top == 240 && clip.bottom == 720);
        }
    }
    for (int scroll = 0; scroll < 320; ++scroll) {
        const auto window = track_select_window_clip(track_select_cover_token(8.F / 3));
        for (int col = -5; col <= 5; ++col) {
            const int center = (col * 320 - scroll + 160) * 4;
            if (center + 110 * 4 > window.left && center - 110 * 4 < window.right)
                check(col >= -2 && col <= 2);
        }
    }
    TrackSelectFramebufferImages images{};
    check(!images.remember(0x100000U, 256U) && images.count == 0U);
    for (unsigned i = 0; i < images.addresses.size(); ++i) {
        check(images.remember(0x100000U + i * 0x10000U, 320U));
        check(images.remember(0x100000U + i * 0x10000U, 320U));
        check(images.count == i + 1 && images.contains(0x100000U + i * 0x10000U));
    }
    check(!images.remember(0x300000U, 320U) && images.count == 8U);
    check(!images.contains(0x300000U));
    images = {};
    check(images.count == 0U && !images.contains(0x100000U));
    constexpr gpr menu = (int32_t)0x80040000U, viewport = (int32_t)0x80040100U;
    MEM_W(0, menu) = 15;
    ctx->r3 = viewport;
    ctx->r2 = packet;
    int32_t bounds[4];
    for (unsigned i = 0; i < 4; ++i)
        MEM_W(i * 4, viewport) = std::array<int, 4>{-200, 60, -40, 180}[i];
    check(dkr_track_preview_bounds(rdram, ctx, (uint32_t)menu, bounds));
    check(bounds[0] == -200 && bounds[2] == -40);
    MEM_W(0, menu) = 3;
    check(!dkr_track_preview_bounds(rdram, ctx, (uint32_t)menu, bounds));
    MEM_W(0, menu) = 15;
    for (unsigned i = 0; i < 4; ++i)
        MEM_W(i * 4, viewport) = std::array<int, 4>{0, 0, 320, 240}[i];
    check(!dkr_track_preview_bounds(rdram, ctx, (uint32_t)menu, bounds));
    dkr_owned_draw_event event{DKR_OWNED_TRACK_PREVIEW, (uint32_t)packet, 0, {}};
    event.parameters[0] = (uint32_t)-200;
    event.parameters[1] = 60;
    event.parameters[2] = (uint32_t)-40;
    event.parameters[3] = 180;
    check(dkr_owned_draw_event_valid(&event));
    event.parameters[4] = 1;
    check(!dkr_owned_draw_event_valid(&event));
    dkr::runtime::f3ddkr::CommandBudget budget;
    const auto count = [](uint8_t opcode) -> std::size_t { return opcode == 0xD5 ? 3 : 1; };
    check(budget.validate(
              0, 0, 16, [] { return uint8_t(0xD5); }, count) == dkr::runtime::f3ddkr::CommandCheck::Truncated);
    check(budget.validate(0, 0, 24, [] { return uint8_t(0xD5); }, count) == dkr::runtime::f3ddkr::CommandCheck::Okay);
    // Local decode settings/resize calculations cannot alter the shared CPU image.
    const auto canonical = memory;
    for (float cover : {1.F, 1.2F, 4.F / 3, 7.F / 4, 8.F / 3, 1.F}) {
        const auto token = track_select_cover_token(cover);
        (void)track_select_preview_clip({-600, 240, 40, 720}, token);
        check(memory == canonical);
    }
    std::printf("Track Select signed draw/window/budget checks: %u passed\n", checks);
}
