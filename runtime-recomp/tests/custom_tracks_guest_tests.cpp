// Synthetic guest-memory checks: no ROM, user profile, window or real mods.
#include "custom_tracks.hpp"
#include "game_payload.hpp"
#include <cassert>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

extern "C" void dkr_custom_tracks_extend_table(std::uint8_t*, recomp_context*, std::uint32_t);
extern "C" int dkr_custom_tracks_asset_override(std::uint8_t*, recomp_context*);
namespace {
std::vector<std::uint8_t> memory(8 * 1024 * 1024);
std::uint8_t* rdram = memory.data();
constexpr unsigned Table = 0x80040000, Output = 0x80060000, Destination = 0x80070000;
void put(unsigned at, unsigned value) {
    MEM_W(0, std::int32_t(at)) = value;
}
unsigned get(unsigned at) {
    return MEM_W(0, std::int32_t(at));
}
void allocate(std::uint8_t*, recomp_context* c) {
    assert(c->f_odd == (c->mips3_float_mode ? &c->f1.u32l : &c->f0.u32h));
    *c->f_odd = 123;
    c->r2 = std::int32_t(Output);
}
dkr::runtime::GamePayload payload{};
void write(const std::filesystem::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), s.size());
    assert(f.good());
}
} // namespace
namespace dkr::runtime {
const GamePayload* active_payload() {
    return &payload;
}
} // namespace dkr::runtime
int main() {
    using namespace dkr::runtime::custom_tracks;
    const auto root =
        std::filesystem::temp_directory_path() /
        ("dkr-blender-legacy-bridge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(root));
    const auto pack = root / "fixture.dkrmap";
    assert(std::filesystem::create_directory(pack));
    write(pack / "manifest.json",
          R"({"schemaVersion":1,"id":"fixture","name":"Fixture","adds":[{"section":"LEVEL_HEADERS","file":"h.bin"}]})");
    write(pack / "h.bin", std::string(196, '\x35'));
    scan(root);
    assert(tracks().size() == 1);
    payload.mempool_alloc_safe = allocate;
    // Both a retail table and a longer legacy namespace retain their original
    // offsets/indices, with Blender content appended after the actual end.
    for (unsigned end : {0x400U, 0x900U}) {
        for (unsigned i = 0; i < 5; ++i)
            put(Table + 4 * i, std::array<unsigned, 5>{0, 0x100, 0x250, end, 0xffffffff}[i]);
        recomp_context c{};
        c.r2 = std::int32_t(Table);
        c.f_odd = &c.f0.u32h;
        dkr_custom_tracks_extend_table(rdram, &c, 22);
        assert(unsigned(c.r2) == Output && c.f0.u32h == 0);
        for (unsigned i = 0; i < 4; ++i)
            assert(get(Output + 4 * i) == get(Table + 4 * i));
        assert(get(Output + 16) == end + 196 && get(Output + 20) == 0xffffffff);
        c.r4 = 23;
        c.r5 = std::int32_t(Destination);
        c.r6 = end;
        c.r7 = 196;
        assert(dkr_custom_tracks_asset_override(rdram, &c) == 1 && c.r2 == 196);
        for (unsigned i = 0; i < 196; ++i)
            assert(MEM_BU(i, static_cast<gpr>(static_cast<std::int32_t>(Destination))) == 0x35);
        put(Destination, 0xaabbccdd);
        c.r6 = end - 196;
        assert(!dkr_custom_tracks_asset_override(rdram, &c));
        assert(get(Destination) == 0xaabbccdd);
        c.r6 = end;
        c.r5 = std::int32_t(0x807ffff0);
        assert(!dkr_custom_tracks_asset_override(rdram, &c));
        c.r5 = std::int32_t(Destination);
        c.r7 = 197;
        assert(!dkr_custom_tracks_asset_override(rdram, &c));
        assert(get(Destination) == 0xaabbccdd);
    }
    // Only the uniquely created test leaf is removed; no user paths are used.
    std::filesystem::remove_all(root);
    std::cout << "Blender/legacy appended-table, payload, range and register checks passed.\n";
}
