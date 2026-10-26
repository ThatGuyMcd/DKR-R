#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace dkr::runtime::vehicle_context {

inline constexpr std::size_t kRdramBytes = 0x00800000U;
inline constexpr std::uint32_t kObjectRacerOffset = 0x64U;
inline constexpr std::uint32_t kVehicleIdOffset = 0x1D6U;
inline constexpr std::uint32_t kPreviousVehicleIdOffset = 0x1D7U;
inline constexpr int kFirstVehicleId = 0;
inline constexpr int kLastVehicleId = 2;

inline bool valid_vehicle_id(int vehicle) {
    return vehicle >= kFirstVehicleId && vehicle <= kLastVehicleId;
}

inline std::optional<std::size_t> physical_address(std::uint32_t address, std::size_t bytes, std::size_t rdram_size) {
    const std::size_t physical = address & 0x00FFFFFFU;
    if (physical > rdram_size || bytes > rdram_size - physical) {
        return std::nullopt;
    }
    return physical;
}

inline std::optional<std::uint32_t> read_u32(const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t address) {
    if (rdram == nullptr)
        return std::nullopt;
    const auto physical = physical_address(address, sizeof(std::uint32_t), rdram_size);
    if (!physical)
        return std::nullopt;
    std::uint32_t value = 0U;
    std::memcpy(&value, rdram + *physical, sizeof(value));
    return value;
}

inline std::optional<std::uint8_t> read_u8(const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t address) {
    if (rdram == nullptr)
        return std::nullopt;
    const auto physical = physical_address(address ^ 3U, sizeof(std::uint8_t), rdram_size);
    if (!physical)
        return std::nullopt;
    return rdram[*physical];
}

// gRacersByPort is the decomp's controller-port ordered Object **. Following
// that topology gives Adventure mode the live vehicle selected by Taj or by a
// level spawn; the menu selection array is only a fallback for screens where
// racer objects do not exist yet.
inline std::optional<int> live_vehicle_for_port(const std::uint8_t* rdram, std::size_t rdram_size,
                                                std::uint32_t racers_by_port_global, std::size_t port) {
    if (port >= 4U)
        return std::nullopt;
    const auto racers_by_port = read_u32(rdram, rdram_size, racers_by_port_global);
    if (!racers_by_port || *racers_by_port == 0U)
        return std::nullopt;

    const auto racer_object =
        read_u32(rdram, rdram_size, *racers_by_port + static_cast<std::uint32_t>(port * sizeof(std::uint32_t)));
    if (!racer_object || *racer_object == 0U)
        return std::nullopt;

    const auto racer = read_u32(rdram, rdram_size, *racer_object + kObjectRacerOffset);
    if (!racer || *racer == 0U)
        return std::nullopt;

    const auto current = read_u8(rdram, rdram_size, *racer + kVehicleIdOffset);
    if (current && valid_vehicle_id(static_cast<std::int8_t>(*current))) {
        return static_cast<std::int8_t>(*current);
    }

    // Boss and transformed vehicles use IDs outside the three player vehicle
    // classes. vehicleIDPrev retains the player's car/hovercraft/plane policy.
    const auto previous = read_u8(rdram, rdram_size, *racer + kPreviousVehicleIdOffset);
    if (previous && valid_vehicle_id(static_cast<std::int8_t>(*previous))) {
        return static_cast<std::int8_t>(*previous);
    }
    return std::nullopt;
}

inline int resolve_for_port(const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t racers_by_port_global,
                            std::size_t port, int menu_fallback) {
    const auto live = live_vehicle_for_port(rdram, rdram_size, racers_by_port_global, port);
    return live.value_or(valid_vehicle_id(menu_fallback) ? menu_fallback : 0);
}

} // namespace dkr::runtime::vehicle_context
