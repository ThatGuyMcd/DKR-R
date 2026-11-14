// Execute regenerated bgload_start, including the real JR delay slot.
#include "legacy_track_menu_adapter.hpp"
#include "recomp.h"
#include <algorithm>
#include <iostream>
#include <memory>

extern "C" void bgload_start(std::uint8_t*, recomp_context*);
namespace {
using namespace dkr::mods;
Bytes memory(8 * MiB);
TrackMenuFields fields{};
std::unique_ptr<TrackMenuAdapter> adapter;
unsigned calls = 0, returns = 0, checks = 0;
std::uint32_t observation = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value)
        throw Error(why);
}
void put(MenuField field, std::uint32_t value, unsigned size = 4) {
    const auto p = fields[static_cast<unsigned>(field)] & 0x1fffffff;
    for (unsigned i = 0; i < size; ++i)
        memory[(p + i) ^ 3] = static_cast<std::uint8_t>(value >> ((size - 1 - i) * 8));
}
void initialize() {
    std::vector<Root> tracks(2);
    for (unsigned i = 0; i < 2; ++i) {
        tracks[i].name = "SAME CARRIER " + std::to_string(i);
        tracks[i].carrier = 5;
        tracks[i].vehicles = 7;
        tracks[i].content_id = std::string(64, 'a' + i);
    }
    adapter = std::make_unique<TrackMenuAdapter>(tracks, true);
    adapter->install_names(memory, 0x80700000);
    put(MenuField::FutureFunLand, 65535, 2);
    put(MenuField::Height, 240);
    put(MenuField::HalfHeight, 120);
    adapter->apply(15, memory, fields, 15);
    adapter->apply(0, memory, fields);
    adapter->apply(1, memory, fields);
    put(MenuField::CursorY, 4);
    put(MenuField::PreviewCarrier, 5);
}
} // namespace
extern "C" int dkr_legacy_track_menu(std::uint8_t* rdram, recomp_context* ctx, unsigned event,
                                     const std::uint32_t* addresses, unsigned observed) {
    check(rdram == memory.data(), "Guest-memory owner changed");
    if (!adapter) {
        std::copy_n(addresses, fields.size(), fields.begin());
        initialize();
    }
    check(std::equal(fields.begin(), fields.end(), addresses), "Regenerated function has inconsistent ABI fields");
    if (event == 6)
        ++calls;
    else if (event == 7) {
        ++returns;
        observation = observed;
    } else
        throw Error("Unexpected bgload event");
    adapter->apply(event, memory, fields, static_cast<std::uint32_t>(ctx->r4), observed);
    return 0;
}
int main() {
    try {
        recomp_context ctx{};
        ctx.r4 = 5;
        ctx.r5 = 1;
        for (unsigned i = 0; i < 100; ++i) {
            if (adapter) {
                put(MenuField::BackgroundBusy, 0);
                put(MenuField::CursorX, i % 2);
            }
            ctx.r2 = i % 3 == 0 ? 0 : i % 3 == 1 ? 0xffffffffU : 17;
            bgload_start(memory.data(), &ctx);
            check(ctx.r2 == 1 && observation == 1, "Success was sampled before its return delay slot");
            put(MenuField::CursorX, (i + 1) % 2);
            ctx.r2 = 1;
            bgload_start(memory.data(), &ctx);
            check(ctx.r2 == 0 && observation == 0, "Busy result did not preserve the native return value");
            auto load = adapter->apply(8, memory, fields, 5);
            check(load.scene && load.scene->id == std::string(64, 'a' + i % 2),
                  "Busy preview replaced an already accepted same-carrier request");
        }
        check(calls == 200 && returns == 200, "Not every generated entry/exit was observed");
        std::cout << checks << " regenerated background-loader checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
