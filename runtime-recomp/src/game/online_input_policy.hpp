#pragma once

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::netplay {

inline constexpr std::uint8_t kNoOnlinePlayerSlot = 0xFFU;

constexpr bool keyboard_owns_input_port(bool online_routing, std::size_t player, int configured_keyboard_player) {
    (void)online_routing;
    return static_cast<int>(player) == configured_keyboard_player;
}

constexpr bool online_port_occupied(bool online_routing, std::uint8_t occupied_mask, std::size_t player) {
    return online_routing && player < 4U && (occupied_mask & static_cast<std::uint8_t>(1U << player)) != 0U;
}

// Physical devices are sampled into a private lane during online play.  They
// must never be published directly to DKR's virtual N64 ports: only the
// confirmed lockstep frame may populate those ports.  This also keeps the
// controller-discovery poll performed by input_init neutral on every peer.
constexpr bool publish_physical_input_to_virtual_port(bool online_routing) {
    return !online_routing;
}

constexpr int physical_rumble_port(bool online_routing, std::uint8_t local_online_slot, std::size_t local_input_profile,
                                   int requested_port) {
    if (!online_routing)
        return requested_port;
    return requested_port == static_cast<int>(local_online_slot) ? static_cast<int>(local_input_profile) : -1;
}

} // namespace dkr::runtime::netplay
