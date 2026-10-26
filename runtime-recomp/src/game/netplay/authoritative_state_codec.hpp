#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::netplay {

inline constexpr std::uint32_t kAuthoritativeDeltaKeyframeInterval = 30U;

// Lossless wire codec for portable authoritative state. The native snapshot
// remains the canonical state/checksum input; this codec exists solely to
// avoid encrypting and fragmenting long repeated regions every authored tick.
// A raw snapshot is returned when compression would not reduce its size.
std::vector<std::uint8_t> encode_authoritative_state_wire(std::span<const std::uint8_t> state);

std::vector<std::uint8_t> encode_authoritative_state_delta_wire(std::span<const std::uint8_t> state,
                                                                std::span<const std::uint8_t> keyframe,
                                                                std::uint32_t keyframe_frame);

bool authoritative_state_wire_is_delta(std::span<const std::uint8_t> wire);

bool decode_authoritative_state_delta_wire(std::span<const std::uint8_t> wire, std::span<const std::uint8_t> keyframe,
                                           std::uint32_t keyframe_frame, std::size_t maximum_decoded_size,
                                           std::vector<std::uint8_t>& state, std::string& error);

bool decode_authoritative_state_wire(std::span<const std::uint8_t> wire, std::size_t maximum_decoded_size,
                                     std::vector<std::uint8_t>& state, std::string& error);

} // namespace dkr::runtime::netplay
