// Execute freshly emitted retail functions, not a reimplementation of their loops.
#include "recomp.h"
#include "track_select_draw_guest.h"
#include "mods/legacy_track_menu_adapter.hpp"
#include <bit>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace dkr::mods;
static unsigned checks;
static void check(bool b) {
    ++checks;
    if (!b)
        throw std::runtime_error("Native Track Select draw regression");
}
static dkr_track_draw_state scope{};
static TrackMenuAdapter* custom;
static constexpr uint32_t fields[] = {
#if DKR_TEST_REVISION == 77
    0x801269c8, 0x801269cc, 0x801269dc, 0x801269e4, 0x801269e8, 0x801269ec, 0x801269f4, 0x801269f8,
    0x80126480, 0x80126478, 0x800df4c4, 0x801263d0, 0x801263d8, 0x80126918, 0x80126930, 0x800df47c,
    0x801267d8, 0x80126818, 0x80126830, 0x800df4c8, 0x801268e8, 0x800e0980, 0x801267d0, 0x800e097c,
    0x800df4d4, 0x800e3770, 0x800df488, 0x801263a0, 0x80126924
#else
    0x80126f88, 0x80126f8c, 0x80126f9c, 0x80126fa4, 0x80126fa8, 0x80126fac, 0x80126fb4, 0x80126fb8,
    0x80126a20, 0x80126a18, 0x800dfa44, 0x80126970, 0x80126978, 0x80126ed8, 0x80126ef0, 0x800df9fc,
    0x80126d78, 0x80126db8, 0x80126dd0, 0x800dfa48, 0x80126ea8, 0x800e0f00, 0x80126d70, 0x800e0efc,
    0x800dfa54, 0x800e3d00, 0x800dfa08, 0x8012693c, 0x80126ee4
#endif
};
struct Cell {
    int x, y, border, opacity;
};
static std::vector<Cell> cells;
extern "C" {
void trackmenu_render_names(uint8_t*, recomp_context*);
void texrect_draw_scaled(uint8_t*, recomp_context*);
void texrect_draw(uint8_t*, recomp_context*);
unsigned dkr_track_select_draw(uint8_t* rdram, recomp_context* ctx, unsigned event, unsigned) {
    return dkr_track_draw_guest(&scope, rdram, ctx, event);
}
int dkr_legacy_track_menu(uint8_t* rdram, recomp_context* ctx, unsigned event, const uint32_t* actual,
                          unsigned observed) {
    check(event == 4 && observed == 5 && actual[14] == uint32_t(ctx->r29) + 0x90);
    if (custom) {
        TrackMenuFields a{};
        std::copy_n(actual, a.size(), a.begin());
        custom->apply(4, {rdram, 8U * 1024U * 1024U}, a, 0, observed);
    }
    return 0;
}
void get_settings(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (int32_t)0x80020000U;
}
void get_misc_asset(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (int32_t)0x80021000U;
}
void level_world_id(uint8_t*, recomp_context* ctx) {
    ctx->r2 = ctx->r4;
}
void level_name(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (int32_t)(0x80022000U + uint32_t(ctx->r4) * 32);
}
void camDisableUserView(uint8_t*, recomp_context*) {}
void menu_camera_centre(uint8_t*, recomp_context*) {}
void mtx_ortho(uint8_t*, recomp_context*) {}
void rendermode_reset(uint8_t*, recomp_context*) {}
void trackmenu_render_2D(uint8_t* rdram, recomp_context* ctx) {
    cells.push_back({int32_t(ctx->r4), int32_t(ctx->r5), MEM_W(0x14, ctx->r29), MEM_W(0x10, ctx->r29)});
}
void fb_size(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (240U << 16) | 320;
}
void dkr_hud_rect_begin(uint8_t*, recomp_context*, unsigned) {}
void dkr_hud_rect_extent(uint8_t*, recomp_context*) {}
void dkr_hud_rect_end(uint8_t*, recomp_context*) {}
void do_break(uint32_t) {
    throw std::runtime_error("Unexpected native arithmetic trap");
}
}
int main() {
    std::vector<uint8_t> memory(8U * 1024U * 1024U, 0);
    auto* rdram = memory.data();
    recomp_context registers{};
    auto* ctx = &registers;
    ctx->f_odd = &ctx->f0.u32h;
    const auto put = [&](unsigned field, uint32_t value) { MEM_W(0, (gpr)(int32_t)fields[field]) = value; };
    TrackMenuFields addresses{};
    std::copy_n(fields, addresses.size(), addresses.begin());
    std::vector<Root> roots(512);
    for (unsigned i = 0; i < roots.size(); ++i) {
        roots[i].content_id = std::string(64, '0');
        roots[i].content_id.replace(64 - std::to_string(i).size(), std::to_string(i).size(), std::to_string(i));
        roots[i].name = std::string(255, 'A');
        roots[i].carrier = 5;
        roots[i].vehicles = 7;
    }
    TrackMenuAdapter adapter(std::move(roots));
    check(adapter.name_bytes().size() < 24U * 1024U);
    put(8, 240);
    put(9, 120);
    MEM_H(0, (gpr)(int32_t)fields[13]) = -1;
    MEM_W(4, (gpr)(int32_t)0x80020000U) = 0x80023000U;
    MEM_W(0, (gpr)(int32_t)0x80023000U) = 2;
    // Retail's fifth stock row shares the adjacent reviewed FFL allocation.
    for (unsigned i = 0; i < 30; ++i)
        MEM_H(i * 2, (gpr)(int32_t)fields[20]) = 0;
    MEM_H(0, (gpr)(int32_t)fields[13]) = -1;
    for (unsigned i = 0; i < 144; i += 4)
        MEM_W(i, (gpr)(int32_t)fields[14]) = 0xA5A5A5A5U;
    adapter.apply(15, memory, addresses, 15);
    adapter.apply(0, memory, addresses);
    adapter.install_names(memory, 0x80040000U);
    adapter.apply(1, memory, addresses);
    for (bool future : {false, true})
        for (bool mods : {false, true})
            for (int row : mods ? std::vector<int>{0, 3, 4, 5, 6, 130, 131, 132} : std::vector<int>{0, 1, 3, 4})
                for (int column = 0; column < 6; ++column)
                    for (int offset : {0, 1, 80, 160, 239, 319}) {
                        custom = mods ? &adapter : nullptr;
                        cells.clear();
                        MEM_H(0, (gpr)(int32_t)fields[13]) = future ? 0 : -1;
                        put(2, std::bit_cast<uint32_t>(float(column * 320 + offset)));
                        put(3, std::bit_cast<uint32_t>(float(-row * 240)));
                        put(6, column);
                        put(7, row);
                        put(12, 8);
                        put(15, 0);
                        put(27, 0x80010000U);
                        ctx->r29 = (int32_t)0x80700000U;
                        ctx->r31 = (int32_t)0x80000004U;
                        MEM_W(-244, ctx->r29) = 0xDEADBEEFU;
                        MEM_W(0, ctx->r29) = 0xCAFEBABEU;
                        trackmenu_render_names(rdram, ctx);
                        check(uint32_t(ctx->r29) == 0x80700000U && uint32_t(ctx->r31) == 0x80000004U);
                        check(!scope.active && !scope.pending && cells.size() <= 15);
                        check(uint32_t(MEM_W(-244, ctx->r29)) == 0xDEADBEEFU &&
                              uint32_t(MEM_W(0, ctx->r29)) == 0xCAFEBABEU);
                        for (unsigned i = 0; i < 144; i += 4)
                            check(uint32_t(MEM_W(i, (gpr)(int32_t)fields[14])) == 0xA5A5A5A5U);
                        for (const auto& cell : cells)
                            check(cell.x >= -640 - offset && cell.x <= 640 - offset && cell.opacity >= 0 &&
                                  cell.opacity <= 255);
                        if (row == 1 && column == 2)
                            check(cells.size() == 15);
                    }
    // Actual retail scaled/unscaled image helpers across negative and >320 bounds.
    custom = nullptr;
    constexpr gpr slot = (int32_t)0x80008000U, element = (int32_t)0x80030000U, texture = (int32_t)0x80030100U;
    MEM_W(0, element) = texture;
    MEM_W(8, element) = 0;
    MEM_B(0, texture) = 64;
    MEM_B(1, texture) = 96;
    MEM_H(0xA, texture) = 1;
    MEM_W(0xC, texture) = 0x80030200U;
    for (int x : {-640, -100, -1, 0, 100, 319, 320, 600})
        for (bool flip : {false, true})
            for (bool scaled : {false, true}) {
                ctx->r29 = (int32_t)0x80700000U;
                MEM_W(0, slot) = 0x80010000U;
                ctx->r4 = slot;
                ctx->r5 = element;
                ctx->r6 = scaled ? std::bit_cast<uint32_t>(float(x)) : uint32_t(x);
                ctx->r7 = scaled ? std::bit_cast<uint32_t>(60.F) : 60;
                MEM_W(0x10, ctx->r29) = scaled ? std::bit_cast<uint32_t>(1.25F) : 255;
                MEM_W(0x14, ctx->r29) = scaled ? std::bit_cast<uint32_t>(1.25F) : 255;
                MEM_W(0x18, ctx->r29) = scaled ? 0xFFFFFFFFU : 255;
                MEM_W(0x1C, ctx->r29) = scaled ? (flip ? 0x1000 : 0) : 255;
                dkr_track_draw_guest(&scope, rdram, ctx, 0);
                if (scaled)
                    texrect_draw_scaled(rdram, ctx);
                else
                    texrect_draw(rdram, ctx);
                const uint32_t end = MEM_W(0, slot);
                bool found = false;
                for (gpr p = (int32_t)0x80010000U; p < gpr(int32_t(end)); p += 8)
                    if (uint32_t(MEM_W(0, p)) == 0xD5000001U) {
                        check(uint32_t(MEM_W(4, p)) == 0x54524354U);
                        const int left = int16_t(uint32_t(MEM_W(8, p)) >> 16),
                                  right = int16_t(uint32_t(MEM_W(12, p)) >> 16);
                        check(left == x * 4 - (scaled && flip ? 320 : 0));
                        check(right - left == (scaled ? 320 : 256));
                        found = true;
                        p += 16;
                    }
                check(found && !scope.pending);
                dkr_track_draw_guest(&scope, rdram, ctx, 1);
            }
    std::printf("Real v%d Track Select/table/rectangle checks: %u passed\n", DKR_TEST_REVISION, checks);
}
