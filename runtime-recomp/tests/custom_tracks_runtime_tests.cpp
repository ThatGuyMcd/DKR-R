#include "custom_tracks.hpp"
#include "game_payload.hpp"
#include "revision_addresses.hpp"
#include "shadow_symbols.hpp"
#include "recomp.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" void dkr_custom_tracks_auto_boot(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_prepare_memory(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_prepare_level(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_prepare_vehicle(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_track_heap(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_table_load_begin(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_table_load_end(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_asset_load_begin(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_asset_load_end(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_object_map_links(std::uint8_t*, recomp_context*);
extern "C" int dkr_custom_tracks_is_boss_level(std::int32_t);
extern "C" void dkr_shadow_polygon_budget(std::uint8_t*, recomp_context*);
extern "C" void dkr_shadow_water_polygon_budget(std::uint8_t*, recomp_context*);
extern bool dkr_test_frozen_resources;
extern bool dkr_test_owned_bootstrap;
extern "C" void dkr_shadow_clip_index(std::uint8_t*, recomp_context*);

namespace tracks = dkr::runtime::custom_tracks;
namespace addresses = dkr::runtime::revision_addresses;
namespace {
dkr::runtime::GamePayload payload{};
bool payload_available = true;
unsigned unlocks = 0;
unsigned stage = 0;
int vehicle = 0;
unsigned allowed = 1;
int saved_vehicle = -1;
std::array<int, 8> committed{};
gpr addr(std::uint32_t value) {
    return static_cast<std::int32_t>(value);
}
bool rev80() {
    return addresses::selected_revision() == dkr::runtime::rom::Revision::UsV80;
}

// Guest-call substitutes check prerequisites at the real host boundary. The
// native AI algorithm itself remains untouched; these checks catch a missing
// commit, wrong revision table, uninitialised players or a clobbered context.
void controller(std::uint8_t* rdram, recomp_context* ctx) {
    assert(stage++ == 0 && ctx->r4 == 0);
    MEM_W(0, addr(addresses::current().NumberOfActivePlayers)) = 1;
    for (int i = 0; i < 4; ++i)
        MEM_B(i, addr(addresses::current().ActivePlayersArray)) = i == 0;
    ctx->r4 = 999;
}
void inputs(std::uint8_t*, recomp_context*) {
    assert(stage++ == 1);
}
void time_trial(std::uint8_t* rdram, recomp_context* ctx) {
    assert(stage++ == 2 && ctx->r4 == 0);
    assert(MEM_W(0, addr(addresses::current().TracksMode)) == 1);
}
void drumstick(std::uint8_t*, recomp_context* ctx) {
    ctx->r2 = unlocks & 1;
}
void tt(std::uint8_t*, recomp_context* ctx) {
    ctx->r2 = unlocks & 2;
}
void ai(std::uint8_t* rdram, recomp_context* ctx) {
    assert(stage++ == 3 && ctx->r4 == 1);
    const std::uint32_t tables77[] = {0x800DFDD0, 0x800DFE40, 0x800DFEC0, 0x800DFF40};
    const std::uint32_t tables80[] = {0x800E0350, 0x800E03C0, 0x800E0440, 0x800E04C0};
    assert(static_cast<std::uint32_t>(MEM_W(0, addr(rev80() ? 0x8012696C : 0x801263CC))) ==
           (rev80() ? tables80 : tables77)[unlocks]);
    assert(MEM_W(0, addr(addresses::current().NumberOfActivePlayers)) == 1);
    assert(MEM_W(0, addr(addresses::current().NumberOfReadyPlayers)) == 1);
    for (int i = 0; i < 4; ++i)
        assert(MEM_B(i, addr(addresses::current().CharacterSelectStatus)) == (i == 0 ? 2 : 0));
    assert(MEM_B(0, addr(addresses::current().CharacterIdSlots)) == 9);
    for (int i = 1; i < 8; ++i)
        MEM_B(i, addr(addresses::current().CharacterIdSlots)) = i - 1;
    ctx->r2 = 999;
}
void headers(std::uint8_t* rdram, recomp_context*) {
    assert(stage++ == 4);
    for (int i = 0; i < 8; ++i)
        committed[i] = MEM_B(i, addr(addresses::current().CharacterIdSlots));
}
void default_vehicle(std::uint8_t*, recomp_context* ctx) {
    assert(ctx->r4 == static_cast<gpr>(tracks::track_override()));
    ctx->r2 = vehicle;
    ctx->r4 = 999;
}
void usable(std::uint8_t*, recomp_context* ctx) {
    assert(ctx->r4 == static_cast<gpr>(tracks::track_override()));
    ctx->r2 = allowed;
}
void save_vehicle(std::uint8_t*, recomp_context* ctx) {
    saved_vehicle = static_cast<int>(ctx->r4);
}
} // namespace
namespace dkr::runtime {
const GamePayload* active_payload() {
    return payload_available ? &payload : nullptr;
}
} // namespace dkr::runtime

int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("dkr-track-lab-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto folder = root / "tracks" / "test.dkrmap";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "manifest.json")
        << R"({"schemaVersion":1,"id":"test","name":"Test","adds":[{"section":"LEVEL_HEADERS","file":"header.bin"},)"
           R"({"section":"LEVEL_MODELS","file":"model.bin"}]})";
    std::string header(196, '\0');
    header[0x37] = 73; // Inherit Ancient Lake's retail object maps.
    header[0xBB] = 5;
    std::ofstream(folder / "header.bin", std::ios::binary) << header;
    // Two segments drawing 3 and 4 batches, in one stored DEFLATE block.
    std::string model(0x4C + 2 * 0x44, '\0');
    model[7] = 0x4C;
    model[0x1B] = 2;
    model[0x4C + 0x21] = 3;
    model[0x4C + 0x44 + 0x21] = 4;
    const auto length = static_cast<std::uint16_t>(model.size());
    std::string container{static_cast<char>(length & 0xFF),
                          static_cast<char>(length >> 8),
                          0,
                          0,
                          0x09,
                          0x01,
                          static_cast<char>(length & 0xFF),
                          static_cast<char>(length >> 8),
                          static_cast<char>(~length & 0xFF),
                          static_cast<char>((~length >> 8) & 0xFF)};
    std::ofstream(folder / "model.bin", std::ios::binary) << container << model;
    tracks::scan(root / "tracks");
    const std::int32_t retail[] = {0, 196, -1};
    assert(!tracks::build_extended_table(tracks::Section::LevelHeaders, retail).empty());
    tracks::arm_track_override("test");
    assert(tracks::track_override() == 1);

    payload.titlescreen_controller_assign = controller;
    payload.input_assign_players = inputs;
    payload.unlock_drumstick = drumstick;
    payload.unlock_tt = tt;
    payload.charselect_assign_ai = ai;
    payload.init_racer_headers = headers;
    payload.set_time_trial_enabled = time_trial;
    payload.leveltable_vehicle_default = default_vehicle;
    payload.leveltable_vehicle_usable = usable;
    payload.set_level_default_vehicle = save_vehicle;
    std::vector<std::uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();

    for (const auto revision : {dkr::runtime::rom::Revision::UsV77, dkr::runtime::rom::Revision::UsV80}) {
        assert(addresses::select(revision));
        for (unlocks = 0; unlocks < 4; ++unlocks) {
            std::fill(memory.begin(), memory.end(), 0xA5);
            stage = 0;
            committed.fill(0);
            tracks::set_auto_boot(false);
            tracks::set_auto_boot(true);
            recomp_context context{};
            context.r2 = 123;
            context.r4 = 456;
            context.r29 = 0x80400000;
            auto expected = context;
            // An unavailable guest payload must not consume the one-shot.
            payload_available = false;
            dkr_custom_tracks_auto_boot(rdram, &context);
            assert(std::memcmp(&context, &expected, sizeof(context)) == 0 && stage == 0);
            payload_available = true;
            dkr_custom_tracks_auto_boot(rdram, &context);
            expected.r2 = 0x201;
            assert(std::memcmp(&context, &expected, sizeof(context)) == 0);
            assert(stage == 5 && committed[0] == 9);
            for (int i = 1; i < 8; ++i)
                assert(committed[i] == i - 1);
            assert(MEM_W(0, addr(rev80() ? 0x80123A80 : 0x80123500)) == 0);
            // This is a HUD byte, not gGameNumPlayers; never overwrite it.
            assert(static_cast<unsigned char>(MEM_B(0, addr(addresses::current().NumberOfGameplayPlayers))) == 0xA5);
            const auto booted = memory;
            dkr_custom_tracks_auto_boot(rdram, &context);
            assert(memory == booted && stage == 5);
        }
        // Cars, hovercraft and planes replace stale selections on every load,
        // including restarts and multiplayer tests; the roster stays intact.
        MEM_W(0, addr(addresses::current().GameMode)) = 0;
        for (vehicle = 0; vehicle < 3; ++vehicle) {
            for (int players = 1; players <= 4; ++players) {
                recomp_context context{};
                context.r4 = tracks::track_override();
                context.r5 = players - 1;
                context.r6 = 3;
                context.r7 = 99;
                auto expected = context;
                expected.r7 = vehicle;
                for (int i = 0; i < 4; ++i)
                    MEM_B(i, addr(addresses::current().PlayerSelectVehicle)) = 99;
                dkr_custom_tracks_prepare_vehicle(rdram, &context);
                assert(std::memcmp(&context, &expected, sizeof(context)) == 0);
                assert(saved_vehicle == vehicle);
                for (int i = 0; i < 4; ++i)
                    assert(MEM_B(i, addr(addresses::current().PlayerSelectVehicle)) == (i < players ? vehicle : 99));
                const auto prepared = memory;
                dkr_custom_tracks_prepare_vehicle(rdram, &context);
                assert(memory == prepared);
            }
        }
        // Debug vehicles remain load arguments, never indices into the three
        // normal vehicle records; AI selections use an allowed normal vehicle.
        vehicle = 7;
        allowed = 4;
        recomp_context context{};
        context.r4 = tracks::track_override();
        dkr_custom_tracks_prepare_vehicle(rdram, &context);
        assert(context.r7 == 7 && MEM_B(0, addr(addresses::current().PlayerSelectVehicle)) == 2);

        // Unrelated levels, menu-style player counts and disarmed testing must
        // leave both guest memory and every register untouched.
        for (int inactive = 0; inactive < 4; ++inactive) {
            context.r4 = inactive == 0 ? 0 : 1;
            context.r5 = inactive == 1 ? static_cast<gpr>(-1) : 0;
            if (inactive == 2)
                tracks::arm_track_override("");
            if (inactive == 3) {
                tracks::arm_track_override("test");
                MEM_W(0, addr(addresses::current().GameMode)) = 1; // menu preview with racers
            }
            const auto before = memory;
            const auto registers = context;
            dkr_custom_tracks_prepare_vehicle(rdram, &context);
            assert(memory == before && std::memcmp(&context, &registers, sizeof(context)) == 0);
        }
        tracks::arm_track_override("test");

        // A custom level grows DKR's 4 MB main pool over the unused expansion
        // RAM exactly once, whether its tail slot is free or allocated.
        const std::uint32_t pool = rev80() ? 0x80123B00 : 0x80123580;
        const std::uint32_t slots = 0x8012D3F0;
        const auto slot = [&](int index) { return addr(slots + index * 0x14U); };
        const auto make_pool = [&](std::uint32_t tail_end, bool tail_free, int used) {
            for (int i = 0; i < 1600; ++i)
                MEM_H(14, slot(i)) = static_cast<std::int16_t>(i);
            MEM_W(0, slot(0)) = 0x80135100;
            MEM_W(4, slot(0)) = 0x100;
            MEM_H(8, slot(0)) = 1;
            MEM_H(10, slot(0)) = -1;
            MEM_H(12, slot(0)) = 1;
            MEM_W(0, slot(1)) = 0x80135200;
            MEM_W(4, slot(1)) = static_cast<std::int32_t>(tail_end - 0x80135200U);
            MEM_H(8, slot(1)) = tail_free ? 0 : 1;
            MEM_H(10, slot(1)) = 0;
            MEM_H(12, slot(1)) = -1;
            MEM_H(14, slot(used)) = 7; // next spare index
            MEM_W(0, addr(pool)) = 1600;
            MEM_W(4, addr(pool)) = used;
            MEM_W(8, addr(pool)) = static_cast<std::int32_t>(slots);
            MEM_W(12, addr(pool)) = 0x2D2C10;
        };
        const auto prepare = [&](std::int32_t level) {
            recomp_context memory_context{};
            memory_context.r4 = level;
            memory_context.r5 = 0;
            auto registers = memory_context;
            dkr_custom_tracks_prepare_memory(rdram, &memory_context);
            assert(std::memcmp(&memory_context, &registers, sizeof(memory_context)) == 0);
        };
        const std::int32_t custom = tracks::track_override();

        make_pool(0x80400000, true, 2);
        auto untouched = memory;
        prepare(0); // retail level
        assert(memory == untouched);
        prepare(custom);
        assert(MEM_W(4, slot(1)) == 0x6CAE00); // 0x80800000 - 0x80135200
        assert(MEM_W(4, addr(pool)) == 2 && MEM_W(12, addr(pool)) == 0x2D2C10 + 0x400000);
        auto grown = memory;
        prepare(custom); // restarts and later loads keep the grown pool
        assert(memory == grown);

        make_pool(0x80400000, false, 2);
        prepare(custom);
        assert(MEM_H(12, slot(1)) == 7 && MEM_W(4, addr(pool)) == 3);
        assert(static_cast<std::uint32_t>(MEM_W(0, slot(7))) == 0x80400000);
        assert(MEM_W(4, slot(7)) == 0x400000 && MEM_H(8, slot(7)) == 0);
        assert(MEM_H(10, slot(7)) == 1 && MEM_H(12, slot(7)) == -1);
        assert(MEM_W(12, addr(pool)) == 0x2D2C10 + 0x400000);
        grown = memory;
        prepare(custom);
        assert(memory == grown);

        // mmInit's alignment can leave the retail end a few bytes high.
        make_pool(0x80400008, true, 2);
        prepare(custom);
        assert(0x80135200U + static_cast<std::uint32_t>(MEM_W(4, slot(1))) == 0x80800000U);

        // A full slot table or an unrecognised pool is left alone.
        make_pool(0x80400000, false, 1599);
        untouched = memory;
        prepare(custom);
        assert(memory == untouched);
        make_pool(0x803F0000, true, 2);
        untouched = memory;
        prepare(custom);
        assert(memory == untouched);

        // load_level_game's entry sizes the display lists for the custom
        // model in every viewport, and hands retail levels the retail table.
        const std::uint32_t table = rev80() ? 0x800DD920 : 0x800DD3B0;
        const std::uint32_t previous_players = rev80() ? 0x80123A8C : 0x8012350C;
        const auto commands = [&](int players) { return MEM_W(players * 4, addr(table)); };
        const auto load_level = [&](std::int32_t level) {
            recomp_context level_context{};
            level_context.r4 = level;
            level_context.r5 = 0;
            level_context.r7 = 1;
            auto registers = level_context;
            MEM_W(0, addr(previous_players)) = 0;
            dkr_custom_tracks_prepare_level(rdram, &level_context);
            assert(std::memcmp(&level_context, &registers, sizeof(level_context)) == 0);
        };
        assert(tracks::level_model_batches(custom) == 7);
        const std::int32_t retail_commands[] = {4500, 7000, 11000, 11000};
        for (int players = 0; players < 4; ++players)
            MEM_W(players * 4, addr(table)) = retail_commands[players];
        make_pool(0x80400000, true, 2);
        load_level(custom);
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == retail_commands[players] + 7 * 10 * (players + 1));
        assert(MEM_W(0, addr(previous_players)) == -1); // the heap is rebuilt now
        assert(MEM_W(4, slot(1)) == 0x6CAE00);          // with the pool grown first
        load_level(custom);                             // L+Z: the heap already fits
        assert(MEM_W(0, addr(previous_players)) == 0);
        load_level(0);
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == retail_commands[players]);
        assert(MEM_W(0, addr(previous_players)) == -1);
        load_level(0);
        assert(MEM_W(0, addr(previous_players)) == 0);
    }
    // -----------------------------------------------------------------------
    // The level model heap generate_track reserves
    // -----------------------------------------------------------------------
    // Outside the revision loop because it replaces the scanned track list, and
    // because nothing here is revision specific: only the policy's hook address
    // differs between the two, and that is not reachable from a host test.
    {
        const auto be32_into = [](std::string& bytes, std::size_t at, std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8)
                bytes[at++] = static_cast<char>((value >> shift) & 0xFF);
        };
        const auto be16_into = [](std::string& bytes, std::size_t at, std::uint16_t value) {
            bytes[at] = static_cast<char>(value >> 8);
            bytes[at + 1] = static_cast<char>(value & 0xFF);
        };
        // Little-endian inflated size, the 0x09 container tag, then raw DEFLATE.
        // A stored block caps at 65535 bytes, so a large model needs several.
        const auto container_for = [](const std::string& model) {
            std::string out;
            const auto size = static_cast<std::uint32_t>(model.size());
            for (int shift = 0; shift < 32; shift += 8)
                out.push_back(static_cast<char>((size >> shift) & 0xFF));
            out.push_back(0x09);
            for (std::size_t at = 0; at < model.size();) {
                const std::size_t chunk = std::min<std::size_t>(0xFFFF, model.size() - at);
                const bool last = at + chunk == model.size();
                out.push_back(last ? 0x01 : 0x00);
                out.push_back(static_cast<char>(chunk & 0xFF));
                out.push_back(static_cast<char>((chunk >> 8) & 0xFF));
                out.push_back(static_cast<char>(~chunk & 0xFF));
                out.push_back(static_cast<char>((~chunk >> 8) & 0xFF));
                out.append(model, at, chunk);
                at += chunk;
            }
            return out;
        };
        const auto measure = [](const std::string& container) {
            return tracks::measure_level_model_arena(reinterpret_cast<const std::uint8_t*>(container.data()),
                                                     container.size());
        };

        // One segment, one batch, two collidable triangles sharing an edge.
        std::string small(0x200, '\0');
        be32_into(small, 0x04, 0x4C);        // segments
        be16_into(small, 0x1A, 1);           // one of them
        be32_into(small, 0x48, 0x200);       // modelSize: where the arena starts
        be32_into(small, 0x4C + 0x04, 0x90); // triangles
        be32_into(small, 0x4C + 0x0C, 0xB0); // batches
        be32_into(small, 0x4C + 0x14, 0xC8); // collision facets
        be16_into(small, 0x4C + 0x1E, 2);    // two triangles
        be16_into(small, 0x4C + 0x20, 1);    // one batch
        be16_into(small, 0xB0 + 4, 0);       // drawing triangles [0, 2)
        be16_into(small, 0xBC + 4, 2);       // as its sentinel says
        // An edge with no neighbour names its own triangle's plane, which is
        // how retail spells "put a wall straight up from this edge". Edge 1 of
        // each triangle names the other, so that pair shares one plane.
        be16_into(small, 0xC8 + 0, 0);
        be16_into(small, 0xC8 + 2, 0);
        be16_into(small, 0xC8 + 4, 1);
        be16_into(small, 0xC8 + 6, 0);
        be16_into(small, 0xD0 + 0, 1);
        be16_into(small, 0xD0 + 2, 1);
        be16_into(small, 0xD0 + 4, 0);
        be16_into(small, 0xD0 + 6, 1);
        // Two base planes, then five edge planes rather than six: the shared
        // edge is built once. align16(0x200 + 2 * 2) + 7 * 16 = 640.
        assert(measure(container_for(small)) == 640);

        // A batch that opts out of collision still draws, so its triangles keep
        // a base plane each, and it builds no edge planes at all: 2, not 7.
        // That gap - 80 bytes on two triangles - is the whole lever an author
        // has over the arena.
        std::string decorative = small;
        be32_into(decorative, 0xB0 + 8, 0x200); // RENDER_NO_COLLISION
        assert(measure(container_for(decorative)) == 528 + 2 * 16);
        // A triangle retail skips entirely gets no plane at all, not even a
        // base one. Here only triangle 1 survives, with three edges of its own.
        std::string skipped = small;
        skipped[0x90] = static_cast<char>(0x80); // TRI_FLAG_80 on triangle 0
        be16_into(skipped, 0xD0 + 0, 0);         // renumbered onto the one base
        be16_into(skipped, 0xD0 + 2, 0);
        be16_into(skipped, 0xD0 + 4, 0);
        be16_into(skipped, 0xD0 + 6, 0);
        assert(measure(container_for(skipped)) == 528 + 4 * 16);

        // Malformed payloads are declined rather than guessed at, so the heap
        // stays retail when a track cannot be measured.
        std::string headless = small;
        be32_into(headless, 0x48, 0); // no modelSize
        assert(measure(container_for(headless)) == -1);
        assert(tracks::measure_level_model_arena(nullptr, 0) == -1);

        // A blob that alone overruns the retail heap, with no collision at all.
        constexpr std::uint32_t kBig = 0x83000;
        std::string big(kBig, '\0');
        be32_into(big, 0x04, 0x4C);
        be16_into(big, 0x1A, 1);
        be32_into(big, 0x48, kBig);
        assert(measure(container_for(big)) == static_cast<std::int32_t>(kBig));

        const auto grown = root / "grown";
        const auto folder = grown / "big.dkrmap";
        std::filesystem::create_directories(folder);
        std::ofstream(folder / "manifest.json")
            << R"({"schemaVersion":1,"id":"big","name":"Big","adds":[{"section":"LEVEL_HEADERS","file":"header.bin"},)"
               R"({"section":"LEVEL_MODELS","file":"model.bin"}]})";
        std::string big_header(196, '\0');
        big_header[0x37] = 73;
        big_header[0xBB] = 5;
        std::ofstream(folder / "header.bin", std::ios::binary) << big_header;
        std::ofstream(folder / "model.bin", std::ios::binary) << container_for(big);
        tracks::scan(grown);
        const std::int32_t retail_headers[] = {0, 196, -1};
        assert(!tracks::build_extended_table(tracks::Section::LevelHeaders, retail_headers).empty());
        const std::int32_t big_level = tracks::resolved_level_id("big");
        assert(big_level > 0 && tracks::owns_level_id(big_level));
        assert(tracks::level_model_arena_bytes(big_level) == static_cast<std::int32_t>(kBig));

        constexpr std::int32_t kRetailHeap = tracks::kRetailTrackHeap;
        constexpr std::int32_t kGrownHeap = 0x84000; // align16(0x83000 + 0x1000)
        // level_load's entry decides the heap, so Track Select previews, which
        // load through load_level_for_menu and never reach load_level_game's
        // hook, get it as well as races do.
        const auto heap_for = [&](std::int32_t level, std::int32_t held) {
            recomp_context level_context{};
            level_context.r4 = level;
            dkr_custom_tracks_prepare_memory(rdram, &level_context);
            recomp_context inside{};
            inside.r21 = held;
            dkr_custom_tracks_track_heap(rdram, &inside);
            return static_cast<std::int32_t>(inside.r21);
        };
        assert(heap_for(big_level, kRetailHeap) == kGrownHeap);
        // Every retail level keeps the retail reservation byte for byte.
        assert(heap_for(0, kRetailHeap) == kRetailHeap);
        // A register that is not holding the retail constant is never written:
        // a revision whose prologue differs keeps the retail heap.
        assert(heap_for(big_level, 0x1234) == 0x1234);

        // Taking the size clears it, so a generate_track reached without a
        // level_load of its own cannot inherit the previous level's heap.
        recomp_context prepared{};
        prepared.r4 = big_level;
        dkr_custom_tracks_prepare_memory(rdram, &prepared);
        recomp_context once{};
        once.r21 = kRetailHeap;
        dkr_custom_tracks_track_heap(rdram, &once);
        assert(static_cast<std::int32_t>(once.r21) == kGrownHeap);
        recomp_context twice{};
        twice.r21 = kRetailHeap;
        dkr_custom_tracks_track_heap(rdram, &twice);
        assert(static_cast<std::int32_t>(twice.r21) == kRetailHeap);

        // load_level_game's hook leaves the heap to level_load's, which every
        // load reaches next; deciding it twice would report it twice.
        dkr_custom_tracks_prepare_level(rdram, &prepared);
        recomp_context unprepared{};
        unprepared.r21 = kRetailHeap;
        dkr_custom_tracks_track_heap(rdram, &unprepared);
        assert(static_cast<std::int32_t>(unprepared.r21) == kRetailHeap);
    }

    // -----------------------------------------------------------------------
    // Display lists for Track Select previews
    // -----------------------------------------------------------------------
    // A preview draws into the menu's one-player list, and nothing on
    // load_level_for_menu's path may resize it, so the boot header table sizes
    // it for the largest .dkrmap course Track Select offers.
    {
        assert(addresses::select(dkr::runtime::rom::Revision::UsV77));
        const auto library = root / "track-select";
        const auto folder = library / "preview.dkrmap";
        std::filesystem::create_directories(folder);
        std::ofstream(folder / "manifest.json")
            << R"({"schemaVersion":1,"id":"preview","name":"Preview","adds":[{"section":"LEVEL_HEADERS","file":"header.bin"},)"
               R"({"section":"LEVEL_MODELS","file":"model.bin"}]})";
        std::string race(200, '\0');
        race[0] = static_cast<char>(tracks::kCustomTrackWorld);
        race[0x4E] = 7; // car, hovercraft and plane
        race[0x37] = 73;
        race[0xBB] = 5;
        std::ofstream(folder / "header.bin", std::ios::binary) << race;
        // One segment drawing 700 batches: past even the retail four-player list.
        std::string model(0x4C + 0x44, '\0');
        model[7] = 0x4C;
        model[0x1B] = 1;
        model[0x4C + 0x20] = 0x02;
        model[0x4C + 0x21] = static_cast<char>(0xBC);
        const auto length = static_cast<std::uint16_t>(model.size());
        std::string container{static_cast<char>(length & 0xFF),
                              static_cast<char>(length >> 8),
                              0,
                              0,
                              0x09,
                              0x01,
                              static_cast<char>(length & 0xFF),
                              static_cast<char>(length >> 8),
                              static_cast<char>(~length & 0xFF),
                              static_cast<char>((~length >> 8) & 0xFF)};
        std::ofstream(folder / "model.bin", std::ios::binary) << container << model;
        tracks::scan(library);

        std::fill(memory.begin(), memory.end(), 0xA5);
        payload.mempool_alloc_safe = [](std::uint8_t*, recomp_context* ctx) { ctx->r2 = addr(0x80300000); };
        const std::uint32_t retail_table = 0x80200000;
        const std::int32_t retail_headers[] = {0, 196, -1};
        for (int i = 0; i < 3; ++i)
            MEM_W(i * 4, addr(retail_table)) = retail_headers[i];
        const std::uint32_t table = 0x800DD3B0, previous_players = 0x8012350C, lists = 0x801211F0;
        const std::int32_t retail_commands[] = {4500, 7000, 11000, 11000};
        const auto set_table = [&] {
            for (int players = 0; players < 4; ++players)
                MEM_W(players * 4, addr(table)) = retail_commands[players];
        };
        const auto commands = [&](int players) { return MEM_W(players * 4, addr(table)); };
        const auto load_headers = [&] {
            recomp_context begin{};
            begin.r4 = 22; // ASSET_LEVEL_HEADERS_TABLE
            dkr_custom_tracks_table_load_begin(rdram, &begin);
            recomp_context end{};
            end.r2 = addr(retail_table);
            dkr_custom_tracks_table_load_end(rdram, &end);
            return static_cast<std::uint32_t>(end.r2);
        };
        // The retail 4 MB pool, its tail free, so the session's larger lists
        // can come out of expansion RAM.
        const std::uint32_t pool = 0x80123580, slots = 0x8012D3F0;
        const auto slot = [&](int index) { return addr(slots + index * 0x14U); };
        for (int i = 0; i < 1600; ++i)
            MEM_H(14, slot(i)) = static_cast<std::int16_t>(i);
        MEM_W(0, slot(0)) = 0x80135100;
        MEM_W(4, slot(0)) = 0x100;
        MEM_H(8, slot(0)) = 1;
        MEM_H(10, slot(0)) = -1;
        MEM_H(12, slot(0)) = 1;
        MEM_W(0, slot(1)) = 0x80135200;
        MEM_W(4, slot(1)) = 0x80400000 - 0x80135200;
        MEM_H(8, slot(1)) = 0;
        MEM_H(10, slot(1)) = 0;
        MEM_H(12, slot(1)) = -1;
        MEM_W(0, addr(pool)) = 1600;
        MEM_W(4, addr(pool)) = 2;
        MEM_W(8, addr(pool)) = static_cast<std::int32_t>(slots);
        MEM_W(12, addr(pool)) = 0x2D2C10;

        // Boot: level_global_init runs before default_alloc_displaylist_heap.
        set_table();
        MEM_W(0, addr(lists)) = 0;
        assert(load_headers() == 0x80300000);
        const std::int32_t level = tracks::resolved_level_id("preview");
        assert(level == 1 && tracks::track_select_entries().size() == 1);
        constexpr std::int32_t kFloor = 4500 + 700 * 10;
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == kFloor);
        assert(MEM_W(4, slot(1)) == 0x6CAE00 && MEM_W(12, addr(pool)) == 0x2D2C10 + 0x400000);

        // The game indexes five-world arrays with the header's world, so it is
        // served Dino Domain - both the level_global_init read and the full
        // level_load read - while the catalogue keeps the payload's world.
        const auto header_offset = static_cast<std::uint32_t>(MEM_W(4, addr(0x80300000)));
        for (const std::int32_t size : {196, 200}) {
            const std::uint32_t destination = 0x80310000;
            recomp_context load{};
            load.r4 = 23; // ASSET_LEVEL_HEADERS
            load.r5 = addr(destination);
            load.r6 = header_offset;
            load.r7 = size;
            dkr_custom_tracks_asset_load_begin(rdram, &load);
            dkr_custom_tracks_asset_load_end(rdram, &load);
            assert(MEM_B(0, addr(destination)) == 1);
            assert(MEM_B(0x4E, addr(destination)) == 7);
        }
        assert(tracks::track_select_entries().size() == 1);

        // Once the lists exist, header table loads never touch the table.
        MEM_W(0, addr(lists)) = 0x80140000;
        set_table();
        load_headers();
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == retail_commands[players]);

        // A retail race keeps the floor, so the heap it leaves for the menu can
        // still draw every preview; a .dkrmap race adds its own on top.
        const auto load_level = [&](std::int32_t id) {
            recomp_context level_context{};
            level_context.r4 = id;
            MEM_W(0, addr(previous_players)) = 0;
            dkr_custom_tracks_prepare_level(rdram, &level_context);
        };
        load_level(0);
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == kFloor);
        assert(MEM_W(0, addr(previous_players)) == -1);
        load_level(0);
        assert(MEM_W(0, addr(previous_players)) == 0);
        load_level(level);
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == std::max(kFloor, retail_commands[players] + 7000 * (players + 1)));
        assert(MEM_W(0, addr(previous_players)) == -1);

        // A boot whose Track Select offers no .dkrmap course keeps retail lists.
        tracks::scan(root / "empty");
        MEM_W(0, addr(lists)) = 0;
        set_table();
        load_headers();
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == retail_commands[players]);
        load_level(0);
        for (int players = 0; players < 4; ++players)
            assert(commands(players) == retail_commands[players]);
        payload.mempool_alloc_safe = nullptr;
    }

    // Large boot-owned portrait tables must not be silently truncated at
    // 4096 words. Exercise the real guest hook on both supported revisions,
    // including the highest supported record count after one appended image.
    {
        std::string texture(544, '\0');
        texture[0] = 16;
        texture[1] = 16;
        texture[2] = 0x11;
        texture[5] = 1;
        texture[0x12] = 1;
        texture[0x16] = 2;
        texture[0x17] = 0x20;
        std::ofstream(folder / "texture.bin", std::ios::binary) << texture;
        std::ofstream(folder / "manifest.json")
            << R"({"schemaVersion":1,"id":"test","name":"Test","adds":[{"section":"LEVEL_HEADERS","file":"header.bin"},)"
               R"({"section":"LEVEL_MODELS","file":"model.bin"},{"section":"TEXTURES_2D","file":"texture.bin"}]})";
        tracks::scan(root / "tracks");
        payload.mempool_alloc_safe = [](std::uint8_t*, recomp_context* ctx) { ctx->r2 = addr(0x80600000); };
        for (const auto revision : {dkr::runtime::rom::Revision::UsV77, dkr::runtime::rom::Revision::UsV80}) {
            assert(addresses::select(revision));
            for (const unsigned count : {5000U, 32766U}) {
                const auto source = 0x80200000U;
                for (unsigned i = 0; i <= count; ++i)
                    MEM_W(i * 4, addr(source)) = i * 16;
                MEM_W((count + 1) * 4, addr(source)) = -1;
                recomp_context begin{};
                begin.r4 = 5;
                dkr_custom_tracks_table_load_begin(rdram, &begin);
                recomp_context end{};
                end.r2 = addr(source);
                dkr_custom_tracks_table_load_end(rdram, &end);
                assert(static_cast<std::uint32_t>(end.r2) == 0x80600000);
                assert(MEM_W(count * 4, end.r2) == count * 16);
                assert(MEM_W((count + 1) * 4, end.r2) == count * 16 + texture.size());
                assert(MEM_W((count + 2) * 4, end.r2) == -1);
            }
            // No sentinel before the end of RDRAM: refuse without reading
            // beyond the guest or changing the returned table pointer.
            const auto source = 0x807FFFB0U;
            for (unsigned at = source; at < 0x80800000U; at += 4)
                MEM_W(0, addr(at)) = 0;
            recomp_context begin{};
            begin.r4 = 5;
            dkr_custom_tracks_table_load_begin(rdram, &begin);
            recomp_context end{};
            end.r2 = addr(source);
            dkr_custom_tracks_table_load_end(rdram, &end);
            assert(static_cast<std::uint32_t>(end.r2) == source);
        }
        payload.mempool_alloc_safe = nullptr;
    }
    // -----------------------------------------------------------------------
    // Hub exits: levelLinks written into the inflated object map
    // -----------------------------------------------------------------------
    // track_spawn_objects inflates every map into one OBJECT_MAP_SIZE buffer. The
    // entry byte count at its start comes from the package, so the write must stay
    // inside the buffer however hostile that count is.
    {
        assert(addresses::select(dkr::runtime::rom::Revision::UsV77));
        const auto library = root / "links";
        const auto write_package = [&](const char* id, const char* extra) {
            const auto folder = library / (std::string(id) + ".dkrmap");
            std::filesystem::create_directories(folder);
            std::ofstream(folder / "manifest.json")
                << R"({"schemaVersion":1,"id":")" << id << R"(","name":")" << id
                << R"(","adds":[{"section":"LEVEL_HEADERS","file":"h.bin"},)"
                   R"({"section":"LEVEL_OBJECT_MAPS","slot":"structure","file":"s.bin"},)"
                   R"({"section":"LEVEL_OBJECT_MAPS","slot":"collectables","file":"o.bin"}])"
                << extra << "}";
            std::ofstream(folder / "h.bin", std::ios::binary) << std::string(200, '\0');
            // Five-byte container, then one stored block holding a 16-byte map with no entries.
            const std::string empty_map = std::string("\x10\0\0\0\x09\x01\x10\0\xEF\xFF", 10) + std::string(16, '\0');
            std::ofstream(folder / "s.bin", std::ios::binary) << empty_map;
            std::ofstream(folder / "o.bin", std::ios::binary) << empty_map;
        };
        write_package("target", "");
        write_package("hub", R"(,"levelLinks":[{"slot":"structure","offset":24,"track":"target"},)"
                             R"({"slot":"structure","offset":40,"track":"target"},)"
                             R"({"slot":"structure","offset":12272,"track":"target"}])");
        tracks::scan(library);
        const std::int32_t retail_headers[] = {0, 196, -1};
        const std::int32_t retail_maps[] = {0, 0x80, 0x100, -1};
        assert(!tracks::build_extended_table(tracks::Section::LevelHeaders, retail_headers).empty());
        const auto maps = tracks::build_extended_table(tracks::Section::LevelObjectMaps, retail_maps);
        const auto headers = tracks::build_extended_table(tracks::Section::LevelHeaders, retail_headers);
        const std::int32_t hub_level = tracks::resolved_level_id("hub");
        const std::int32_t target_level = tracks::resolved_level_id("target");
        assert(hub_level > 0 && target_level > 0 && hub_level <= tracks::kMaxLevelId);
        const std::int32_t structure =
            tracks::sibling_index(tracks::Section::LevelHeaders, static_cast<std::uint32_t>(headers[hub_level]),
                                  tracks::Section::LevelObjectMaps, tracks::MapSlot::Structure);
        assert(structure >= 0);
        const auto map_offset = static_cast<std::uint32_t>(maps[structure]);

        constexpr std::uint32_t kBuffer = 0x80500000U, kStack = 0x80700000U;
        const auto run = [&](std::int32_t entry_bytes) {
            for (unsigned at = kBuffer - 0x20; at < kBuffer + tracks::kObjectMapBufferBytes + 0x40; ++at)
                MEM_B(0, addr(at)) = 0x55;
            MEM_W(0, addr(kStack + 0x48)) = addr(kBuffer);
            MEM_W(0, addr(kBuffer)) = entry_bytes;
            recomp_context context{};
            context.r16 = addr(map_offset);
            context.r29 = addr(kStack);
            dkr_custom_tracks_object_map_links(rdram, &context);
        };
        const auto written = [&](std::uint32_t offset) {
            return static_cast<unsigned char>(MEM_B(0, addr(kBuffer + offset))) == target_level;
        };
        const auto untouched_outside = [&] {
            for (unsigned at = kBuffer - 0x20; at < kBuffer; ++at)
                if (static_cast<unsigned char>(MEM_B(0, addr(at))) != 0x55)
                    return false;
            for (unsigned at = kBuffer + tracks::kObjectMapBufferBytes;
                 at < kBuffer + tracks::kObjectMapBufferBytes + 0x40; ++at)
                if (static_cast<unsigned char>(MEM_B(0, addr(at))) != 0x55)
                    return false;
            return true;
        };
        // A map whose entries end at 0x30 takes the two links inside it; the one at 0x2FF0 is
        // outside the entries and stays untouched.
        run(0x20);
        assert(written(24) && written(40) && !written(0x2FF0) && untouched_outside());
        // A count that claims more than the buffer holds opens the whole buffer, never more.
        run(0x7FFFFFFF);
        assert(written(24) && written(40) && written(0x2FF0) && untouched_outside());
        run(0x40000000);
        assert(written(0x2FF0) && untouched_outside());
        // A count that is negative or zero admits nothing.
        run(-1);
        assert(!written(24) && !written(40) && !written(0x2FF0) && untouched_outside());
        run(0);
        assert(!written(24) && !written(0x2FF0) && untouched_outside());
        // A buffer pointer whose last byte falls outside guest memory writes nothing.
        MEM_W(0, addr(kStack + 0x48)) = addr(0x807FFFF0U);
        recomp_context edge{};
        edge.r16 = addr(map_offset);
        edge.r29 = addr(kStack);
        dkr_custom_tracks_object_map_links(rdram, &edge);
    }

    // -----------------------------------------------------------------------
    // Only an installed custom boss takes the finish hook. A stock course or a
    // different custom level kind must keep the retail path.
    {
        const auto library = root / "boss-finish";
        for (const auto* id : {"boss", "race"}) {
            const auto package = library / (std::string(id) + ".dkrmap");
            std::filesystem::create_directories(package);
            std::ofstream(package / "manifest.json")
                << "{\"schemaVersion\":1,\"id\":\"" << id
                << "\",\"name\":\"Finish test\",\"adds\":[{\"section\":\"LEVEL_HEADERS\",\"file\":\"header.bin\"}]}";
            std::string header(196, '\0');
            header[0] = static_cast<char>(tracks::kCustomTrackWorld);
            header[0x37] = 73; // borrow a valid retail object map
            header[0xBB] = 5;
            header[0x4C] = std::string_view(id) == "boss" ? tracks::kRaceTypeBoss : 0;
            std::ofstream(package / "header.bin", std::ios::binary) << header;
        }
        tracks::scan(library);
        std::array<std::int32_t, 72> retail{};
        for (std::size_t i = 0; i < retail.size() - 1; ++i)
            retail[i] = static_cast<std::int32_t>(i * 196U);
        retail.back() = -1;
        assert(!tracks::build_extended_table(tracks::Section::LevelHeaders, retail.data()).empty());
        const auto boss = tracks::resolved_level_id("boss");
        assert(boss >= 70 && dkr_custom_tracks_is_boss_level(boss));
        assert(!dkr_custom_tracks_is_boss_level(tracks::resolved_level_id("race")));
        assert(!dkr_custom_tracks_is_boss_level(0));
        assert(!dkr_custom_tracks_is_boss_level(-1));
        assert(!dkr_custom_tracks_is_boss_level(255));
    }

    // -----------------------------------------------------------------------
    // Shadow budget: only writes that reach live storage are refused
    // -----------------------------------------------------------------------
    // The shadow passes append each clipped polygon to D_8011C238[32] and its unseen vertices
    // to D_8011B120[32], and each clip point to D_8011B330[120]. The hooks refuse only the
    // write that would pass an array; within-capacity polygons remain unchanged.
    for (const auto revision : {dkr::runtime::rom::Revision::UsV77, dkr::runtime::rom::Revision::UsV80}) {
        assert(addresses::select(revision));
        const auto& symbols =
            dkr::runtime::shadow_symbols::for_revision(revision == dkr::runtime::rom::Revision::UsV80);
        // The guest is 16 MiB here, as in the owned engine and the extended funcs runtime.
        std::vector<std::uint8_t> guest(0x1000000U);
        std::uint8_t* const rdram = guest.data();
        constexpr std::uint32_t kStack = 0x80700000U;
        const auto put_float = [&](std::uint32_t at, float value) {
            std::int32_t bits;
            std::memcpy(&bits, &value, 4);
            MEM_W(0, addr(at)) = bits;
        };
        // Vertex i of the clipped polygon, or of the shared array: (x, z) and the unkC/unkE word.
        const auto put_vertex = [&](std::uint32_t at, float x, float z, std::int16_t index) {
            put_float(at, x);
            put_float(at + 4U, 0.0F);
            put_float(at + 8U, z);
            MEM_W(0, addr(at + 12U)) = static_cast<std::int32_t>(static_cast<std::uint16_t>(index));
        };
        const auto shared = [&](int count) {
            MEM_W(0, addr(symbols.vertex_count)) = count;
            for (int i = 0; i < count; ++i)
                put_vertex(symbols.vertices + 16U * i, static_cast<float>(i), static_cast<float>(i) * 2.0F, 0);
        };
        std::uint32_t stack = kStack;
        const auto polygon_result_at = [&](void (*hook)(std::uint8_t*, recomp_context*), std::uint32_t offset,
                                           int polygons, int clipped, auto&& fill) {
            MEM_W(0, addr(symbols.polygon_count)) = polygons;
            recomp_context context{};
            context.r2 = clipped;
            context.r29 = addr(stack);
            fill(stack + offset);
            hook(rdram, &context);
            return static_cast<std::int32_t>(context.r2);
        };
        // The floor shadow pass keeps its polygon at sp+0xD0; the water shadow pass at sp+0xA8.
        const auto polygon_result = [&](int polygons, int clipped, auto&& fill) {
            return polygon_result_at(dkr_shadow_polygon_budget, 0xD0U, polygons, clipped, fill);
        };
        constexpr std::int16_t kNew = -1;
        // Plenty of room: untouched.
        shared(10);
        assert(polygon_result(3, 4, [&](std::uint32_t at) {
                   for (int i = 0; i < 4; ++i)
                       put_vertex(at + 16U * i, 100.0F + i, 7.0F, kNew);
               }) == 4);
        // 31 vertices in use and 4 clipped, three of them already known and one new: the
        // pass appends exactly one, so it fits and is left alone.
        shared(31);
        assert(polygon_result(3, 4, [&](std::uint32_t at) {
                   put_vertex(at, 3.0F, 6.0F, kNew);
                   put_vertex(at + 16U, 4.0F, 8.0F, kNew);
                   put_vertex(at + 32U, 5.0F, 10.0F, kNew);
                   put_vertex(at + 48U, 500.0F, 7.0F, kNew);
               }) == 4);
        // Two genuinely new vertices need slots 31 and 32: refused.
        assert(polygon_result(3, 4, [&](std::uint32_t at) {
                   put_vertex(at, 3.0F, 6.0F, kNew);
                   put_vertex(at + 16U, 4.0F, 8.0F, kNew);
                   put_vertex(at + 32U, 500.0F, 7.0F, kNew);
                   put_vertex(at + 48U, 600.0F, 7.0F, kNew);
               }) == 0);
        // Equal new vertices within the polygon count once.
        assert(polygon_result(3, 4, [&](std::uint32_t at) {
                   for (int i = 0; i < 4; ++i)
                       put_vertex(at + 16U * i, 500.0F, 7.0F, kNew);
               }) == 4);
        // A full vertex array still takes a polygon whose vertices carry existing indices.
        shared(32);
        assert(polygon_result(3, 3, [&](std::uint32_t at) {
                   for (int i = 0; i < 3; ++i)
                       put_vertex(at + 16U * i, 900.0F + i, 7.0F, static_cast<std::int16_t>(i));
               }) == 3);
        // ...but not one that needs a new vertex.
        assert(polygon_result(3, 3, [&](std::uint32_t at) {
                   put_vertex(at, 900.0F, 7.0F, 0);
                   put_vertex(at + 16U, 901.0F, 7.0F, 1);
                   put_vertex(at + 32U, 902.0F, 7.0F, kNew);
               }) == 0);
        // The next allocation is live water input, so both passes stop at 32.
        shared(2);
        const auto three_known = [&](std::uint32_t at) {
            for (int i = 0; i < 3; ++i)
                put_vertex(at + 16U * i, 1.0F + i, 1.0F, 0);
        };
        assert(polygon_result(31, 3, three_known) == 3);
        assert(polygon_result(32, 3, three_known) == 0);
        assert(polygon_result(100, 3, three_known) == 0);
        assert(polygon_result(137, 3, three_known) == 0);
        assert(polygon_result(138, 3, three_known) == 0);
        assert(polygon_result(100000, 3, three_known) == 0);
        assert(polygon_result(-1, 3, three_known) == 0);
        // A polygon with more vertices than the entry's unk2[8] is refused.
        assert(polygon_result(3, 9, three_known) == 0);
        // Stacks can sit anywhere in guest memory, including the top of RDRAM: the polygon is
        // judged by its own bytes, not by a fixed address ceiling.
        stack = 0x807FFEE0U;
        assert(polygon_result(3, 3, three_known) == 3);
        // The owned engine's guest stack, past the retail 8 MiB, is guest memory too.
        stack = 0x80FDFC50U;
        assert(polygon_result(3, 3, three_known) == 3);
        // Where the polygon would run past the 16 MiB guest, or start below it, it is
        // refused without being read.
        for (const std::uint32_t outside : {0x80FFFF30U, 0x81000000U, 0x00001000U}) {
            stack = outside;
            recomp_context context{};
            context.r2 = 3;
            context.r29 = addr(stack);
            dkr_shadow_polygon_budget(rdram, &context);
            assert(context.r2 == 0);
        }
        stack = kStack;
        // A clip that returned fewer than three vertices is skipped by retail already.
        assert(polygon_result(32, 2, [&](std::uint32_t) {}) == 2);

        // Water shadows append the same way from another frame offset, and are held to the
        // same rule.
        shared(31);
        const auto water_fill = [&](std::uint32_t at) {
            put_vertex(at, 3.0F, 6.0F, kNew);
            put_vertex(at + 16U, 500.0F, 7.0F, kNew);
            put_vertex(at + 32U, 600.0F, 7.0F, kNew);
        };
        assert(polygon_result_at(dkr_shadow_water_polygon_budget, 0xA8U, 3, 3, water_fill) == 0);
        shared(30);
        assert(polygon_result_at(dkr_shadow_water_polygon_budget, 0xA8U, 3, 3, water_fill) == 3);

        assert(polygon_result_at(dkr_shadow_water_polygon_budget, 0xA8U, 31, 3, three_known) == 3);
        assert(polygon_result_at(dkr_shadow_water_polygon_budget, 0xA8U, 32, 3, three_known) == 0);

        // Run the guest's count update after each append, including repeated
        // saturation. Every later edge traversal must stop at entry 119.
        for (int group = 0; group < 4; ++group) {
            const int capacity = group == 3 ? 24 : 32;
            int count = 0;
            for (int attempt = 0; attempt < 40; ++attempt) {
                recomp_context context{};
                context.r4 = std::min(count, 31); // guest's existing clamp
                context.r3 = group * 32 + context.r4;
                dkr_shadow_clip_index(rdram, &context);
                assert(context.r3 >= group * 32 && context.r3 < group * 32 + capacity);
                count = static_cast<int>(context.r4) + 1; // guest continuation
                assert(count == std::min(attempt + 1, capacity));
                assert(group * 32 + count - 1 < 120);
            }
        }
    }

    // Online/owned bootstrap must not change any register or guest byte,
    // even when the supplied counts and stack would otherwise be rejected.
    for (bool* gate : {&dkr_test_frozen_resources, &dkr_test_owned_bootstrap}) {
        *gate = true;
        assert(dkr_custom_tracks_is_boss_level(70) == 0);
        std::vector<std::uint8_t> guest(0x1000000U, 0xFF);
        const auto before = guest;
        for (auto hook : {dkr_shadow_polygon_budget, dkr_shadow_water_polygon_budget, dkr_shadow_clip_index}) {
            recomp_context context{};
            context.r2 = 9;
            context.r3 = 120;
            context.r4 = 24;
            const auto saved = context;
            hook(guest.data(), &context);
            assert(std::memcmp(&saved, &context, sizeof(context)) == 0);
            assert(guest == before);
        }
        *gate = false;
    }

    tracks::set_auto_boot(false);
    tracks::arm_track_override("");
    std::filesystem::remove_all(root);
    std::puts("[test][track-lab-runtime] both revisions PASS");
}
