#pragma once
#include <array>
#include <cstdint>
namespace dkr::sdk2 {
// Explicit fixed-width words. The transport codec writes these little-endian;
// the guest adapter writes the same words through its big-endian Memory bus.
// word 0: canonical package index + 1; 1: request ID; 2: mod action ID;
// 3..14: mod-defined deterministic arguments; 15: expected scene generation.
// Zero is the only empty command.
struct Command {
    std::array<std::uint32_t, 16> words{};
    bool empty() const {
        return words == std::array<std::uint32_t, 16>{};
    }
    bool valid() const {
        return empty() || (words[0] >= 1 && words[0] <= 128 && words[1] && words[2] && words[15]);
    }
    bool operator==(const Command&) const = default;
};
static_assert(sizeof(Command) == 64);
} // namespace dkr::sdk2
