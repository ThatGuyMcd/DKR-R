#pragma once

#include "authoritative_state.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace dkr::runtime::netplay {

// Rollback uses the same pointer-free, authored simulation contract as
// recovery and transition verification. The fixed envelope is local to a
// machine; only the small Gekko token crosses the network.
inline constexpr std::size_t kRollbackSimulationHeaderBytes = 32U;
inline constexpr std::size_t kRollbackSimulationStateBytes =
    kRollbackSimulationHeaderBytes + kMaximumAuthoritativeStateBytes;

bool capture_rollback_simulation_state(const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t frame,
                                       std::span<std::uint8_t> destination, std::uint64_t& checksum,
                                       std::string& error);

bool restore_rollback_simulation_state(std::uint8_t* rdram, std::size_t rdram_size,
                                       std::span<const std::uint8_t> source, std::uint32_t expected_frame,
                                       std::uint64_t expected_checksum, std::string& error);

} // namespace dkr::runtime::netplay
