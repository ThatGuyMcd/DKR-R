// Extended first-party input owner. The legacy capsule base remains frozen.
#pragma once

#include "controller_snapshot.hpp"
#include "controller_feedback/policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dkr::runtime::sdl3_input::protocol {

inline constexpr std::uint32_t kMagic = 0x334B5244U; // "DKR3"
inline constexpr std::uint16_t kVersion = 2U;
inline constexpr std::size_t kTokenBytes = 16U;
inline constexpr std::size_t kMaximumDevices = 8U;
inline constexpr std::size_t kNameBytes = 96U;
inline constexpr std::size_t kKeyBytes = 48U;
inline constexpr std::size_t kMappingSourceBytes = 48U;
inline constexpr std::size_t kErrorBytes = 192U;

enum class PacketType : std::uint16_t {
    Hello = 1U,
    State = 2U,
    Rumble = 3U,
    Shutdown = 4U,
    Feedback = 5U,
};

struct PacketHeader {
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    PacketType type = PacketType::Hello;
    std::uint32_t size = 0U;
    std::array<std::uint8_t, kTokenBytes> token{};
};

struct HelloPacket {
    PacketHeader header{};
    std::uint8_t ready = 0U;
    std::uint8_t sdl_major = 0U;
    std::uint8_t sdl_minor = 0U;
    std::uint8_t sdl_micro = 0U;
    std::array<char, kErrorBytes> detail{};
};

struct DevicePacket {
    std::uint32_t feedback_capabilities = 0U;
    std::uint64_t feedback_ack = 0U, feedback_failures = 0U;
    std::int32_t feedback_result = 0;
    std::uint16_t vendor = 0U, product = 0U, firmware = 0U;
    feedback::Kind kind = feedback::Kind::Generic;
    feedback::Transport transport = feedback::Transport::Unknown;
    std::int16_t battery = -1;
    std::array<char, kErrorBytes> feedback_detail{};
    std::array<float, 6> touch{}; // two pads/fingers: x, y, pressure
    std::array<std::uint8_t, 2> touch_down{};
    std::uint32_t audio_count=0;
    std::array<std::uint32_t,4> audio_ids{};
    std::array<std::array<char,kNameBytes>,4> audio_names{};
    std::int32_t instance = -1;
    std::uint8_t connected = 0U;
    std::uint8_t mapped = 0U;
    std::uint8_t rumble = 0U;
    std::uint8_t gyro = 0U;
    std::uint8_t gyro_valid = 0U;
    std::array<std::uint8_t, 3> reserved{};
    std::array<std::uint8_t, controllers::kSnapshotButtonCount> buttons{};
    std::array<std::uint8_t, controllers::kSnapshotButtonCount> button_bound{};
    std::array<std::int16_t, controllers::kSnapshotAxisCount> axes{};
    std::array<float, 3> gyro_data{};
    std::uint64_t gyro_timestamp_us = 0U;
    float gyro_rate_hz = 0.0F;
    std::array<char, kNameBytes> name{};
    std::array<char, kKeyBytes> persistent_key{};
    std::array<char, kMappingSourceBytes> mapping_source{};
};

struct StatePacket {
    PacketHeader header{};
    std::uint64_t sequence = 0U;
    std::uint32_t device_count = 0U;
    std::uint32_t reserved = 0U;
    std::array<DevicePacket, kMaximumDevices> devices{};
};

struct RumblePacket {
    PacketHeader header{};
    std::int32_t instance = -1;
    std::uint16_t low_frequency = 0U;
    std::uint16_t high_frequency = 0U;
    std::uint32_t duration_ms = 0U;
};

struct ShutdownPacket {
    PacketHeader header{};
};

struct FeedbackPacket {
    PacketHeader header{};
    std::int32_t instance = -1;
    std::uint32_t lease_ms = 250;
    std::uint64_t sequence = 0;
    feedback::Output output{};
    std::array<char,kKeyBytes> device_key{};
};
static_assert(std::is_trivially_copyable_v<FeedbackPacket>);

static_assert(std::is_trivially_copyable_v<HelloPacket>);
static_assert(std::is_trivially_copyable_v<StatePacket>);
static_assert(std::is_trivially_copyable_v<RumblePacket>);
static_assert(std::is_trivially_copyable_v<ShutdownPacket>);

template <typename Packet>
void initialise(Packet& packet, PacketType type,
                const std::array<std::uint8_t, kTokenBytes>& token) {
    packet = {};
    packet.header.magic = kMagic;
    packet.header.version = kVersion;
    packet.header.type = type;
    packet.header.size = static_cast<std::uint32_t>(sizeof(Packet));
    packet.header.token = token;
}

inline bool valid_header(
    const PacketHeader& header, PacketType type, std::size_t size,
    const std::array<std::uint8_t, kTokenBytes>& token) {
    return header.magic == kMagic && header.version == kVersion &&
           header.type == type && header.size == size &&
           header.token == token;
}

} // namespace dkr::runtime::sdl3_input::protocol
