#include "vehicle_context_policy.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void write_u32(std::vector<std::uint8_t>& rdram, std::uint32_t address, std::uint32_t value) {
    const std::size_t physical = address & 0x00FFFFFFU;
    std::memcpy(rdram.data() + physical, &value, sizeof(value));
}

void write_u8(std::vector<std::uint8_t>& rdram, std::uint32_t address, std::uint8_t value) {
    rdram[(address & 0x00FFFFFFU) ^ 3U] = value;
}

} // namespace

int main() {
    using namespace dkr::runtime::vehicle_context;

    std::vector<std::uint8_t> rdram(kRdramBytes);
    constexpr std::uint32_t global = 0x80100000U;
    constexpr std::uint32_t table = 0x80200000U;
    constexpr std::uint32_t object = 0x80300000U;
    constexpr std::uint32_t racer = 0x80400000U;

    // Character/track selection has no live racer topology and keeps the menu
    // vehicle as its authoritative input policy.
    assert(resolve_for_port(rdram.data(), rdram.size(), global, 0U, 2) == 2);

    write_u32(rdram, global, table);
    write_u32(rdram, table, object);
    write_u32(rdram, object + kObjectRacerOffset, racer);
    write_u8(rdram, racer + kVehicleIdOffset, 1U);
    write_u8(rdram, racer + kPreviousVehicleIdOffset, 0U);

    // Adventure's live hovercraft overrides a stale car menu selection.
    assert(resolve_for_port(rdram.data(), rdram.size(), global, 0U, 0) == 1);

    // A special/boss vehicle falls back to the last player vehicle class.
    write_u8(rdram, racer + kVehicleIdOffset, 7U);
    write_u8(rdram, racer + kPreviousVehicleIdOffset, 2U);
    assert(resolve_for_port(rdram.data(), rdram.size(), global, 0U, 0) == 2);

    // Invalid topology never escapes RDRAM and safely preserves menu policy.
    write_u32(rdram, global, 0x80FFFFFCU);
    assert(resolve_for_port(rdram.data(), rdram.size(), global, 0U, 1) == 1);
    assert(!live_vehicle_for_port(rdram.data(), rdram.size(), global, 4U));

    std::puts("[test][vehicle-context] PASS");
    return 0;
}
