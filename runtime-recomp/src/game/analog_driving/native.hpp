#pragma once
#include "input.hpp"
#include <bit>
#include <cstring>

namespace dkr::runtime::driving {
// The symbols are resolved by the active GameWorld's revision table. This
// adapter runs synchronously inside the current guest tick on its RAM only.
inline std::uint32_t apply(std::uint8_t* ram, unsigned channel, std::uint32_t racer, std::uint32_t bits) {
    const auto symbol = [](std::uint32_t a) { return a >= 0x80000400U && a <= 0x807FFFFCU; };
    if (!ram || !frame.available || !symbol(frame.symbols.buttons) || !symbol(frame.symbols.player) ||
        !symbol(frame.symbols.blocked) || racer < 0x80000400U || racer > 0x807FFDDCU || racer % 4)
        return bits;
    const auto word = [&](std::uint32_t address) {
        std::uint32_t out = 0;
        if (address >= 0x80000400U && address <= 0x807FFFFCU && address % 4 == 0)
            std::memcpy(&out, ram + address - 0x80000000U, 4);
        return out;
    };
    const auto byte = [&](std::uint32_t address) { return ram[(address - 0x80000000U) ^ 3U]; };
    std::int16_t player;
    std::memcpy(&player, ram + ((racer - 0x80000000U) ^ 2U), 2);
    if (player < 0 || player >= 4 || word(frame.symbols.player) != unsigned(player) || byte(frame.symbols.blocked) ||
        byte(racer + 0x1D8) || word(racer + 0x108))
        return bits;
    if (symbol(frame.symbols.adventure) && symbol(frame.symbols.lead) && byte(frame.symbols.adventure) &&
        byte(frame.symbols.lead) && player < 2)
        player = 1 - player;
    if (!frame.requests[player].valid)
        return bits;
    const auto p = frame.requests[player].pedals;
    if (!(p.flags & Enabled))
        return bits;
    const auto vehicle = byte(racer + 0x1D6);
    if (vehicle > 2 || (channel == 2 && vehicle != 0) || (channel < 2 && vehicle == 0))
        return bits;
    const auto buttons = word(frame.symbols.buttons);
    const auto cap = [&](unsigned field, std::uint32_t original) {
        const auto requested = field ? p.brake : p.accelerator;
        if (!(buttons & (field ? 0x4000U : 0x8000U)) || requested == 0 || requested == 255)
            return original;
        const float value = std::bit_cast<float>(original);
        if (!std::isfinite(value))
            return original;
        const float bounded = partial(value, requested, field && vehicle ? 1.2F : 1.F);
        const auto result = std::bit_cast<std::uint32_t>(bounded);
        std::memcpy(ram + racer - 0x80000000U + 0xB4 + field * 4, &result, 4);
        return result;
    };
    if (channel == 2) {
        cap(0, word(racer + 0xB4));
        cap(1, word(racer + 0xB8));
        return bits;
    }
    return channel < 2 ? cap(channel, bits) : bits;
}
} // namespace dkr::runtime::driving
