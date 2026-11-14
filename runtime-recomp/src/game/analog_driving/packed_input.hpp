#pragma once
#include <bit>
#include <cstdint>

namespace dkr::runtime::netplay {
// Explicitly serialized as seven bytes. The eighth in-memory byte is always
// zero (also for legacy raw-input adapters). API 1 DkrInput is unchanged.
struct PackedInput {
    std::uint16_t buttons = 0;
    std::int8_t stick_x = 0, stick_y = 0;
    std::uint8_t accelerator = 0, brake = 0, driving_flags = 0;
    std::uint8_t reserved = 0;
    constexpr bool operator==(const PackedInput&) const = default;
};
static_assert(sizeof(PackedInput) == 8);
inline constexpr unsigned PackedInputBytes = 7;
constexpr bool valid_input(PackedInput p) {
    return !p.reserved && p.driving_flags <= 1 && (p.driving_flags || (!p.accelerator && !p.brake));
}
constexpr std::uint64_t packed_input_bits(PackedInput p) {
    return p.buttons | (std::uint64_t(std::uint8_t(p.stick_x)) << 16) | (std::uint64_t(std::uint8_t(p.stick_y)) << 24) |
           (std::uint64_t(p.accelerator) << 32) | (std::uint64_t(p.brake) << 40) |
           (std::uint64_t(p.driving_flags) << 48);
}
constexpr PackedInput input_from_bits(std::uint64_t p) {
    return {std::uint16_t(p),
            std::bit_cast<std::int8_t>(std::uint8_t(p >> 16)),
            std::bit_cast<std::int8_t>(std::uint8_t(p >> 24)),
            std::uint8_t(p >> 32),
            std::uint8_t(p >> 40),
            std::uint8_t(p >> 48)};
}
} // namespace dkr::runtime::netplay
