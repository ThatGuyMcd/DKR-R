#pragma once
#include "enhanced_water_registry.hpp"
#include "revision_addresses.hpp"
#include <array>

namespace dkr::runtime::water {
using FlowFractions = std::array<std::array<float, 2>, 256>;

// Retail adds speed*updateRate, emits accumulator>>2 into triangle ST, then
// retains accumulator&3. The float decoder must include that retained quarter
// of an ST unit or a slow current alternates a stationary frame with a jump.
// Observe only this task's immutable bytes; never change the simulation phase.
inline FlowFractions capture_flow_fractions(const RT64::State& state) {
    FlowFractions result{};
    enhanced::Memory m(std::span<const std::uint8_t>(state.RDRAM, 0x800000));
    const auto list = m.u32(revision_addresses::ObjectList);
    const auto count = m.u32(revision_addresses::ObjectCount);
    if (!list || count > 4096 || !m.valid(list, std::uint64_t(count) * 4))
        return result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto object = m.u32(list + i * 4);
        if (!object || !m.valid(object, 0x68) || (m.u16(object + 6) & 0x8000U) || m.s16(object + 0x48) != 29)
            continue; // BHV_TEXTURE_SCROLL
        const auto scroll = m.u32(object + 0x64);
        if (!scroll || !m.valid(scroll, 12))
            continue;
        const auto texture = m.s16(scroll);
        if (texture < 0 || texture >= 256)
            continue;
        result[texture][0] += float(m.u16(scroll + 8) & 3U) / 128.F;
        result[texture][1] += float(m.u16(scroll + 10) & 3U) / 128.F;
    }
    return result;
}
} // namespace dkr::runtime::water
