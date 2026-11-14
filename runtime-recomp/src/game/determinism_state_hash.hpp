#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::netplay {

struct GameplayStateDigest {
    std::uint64_t combined = 0U;
    std::uint64_t globals = 0U;
    std::uint64_t roster = 0U;
    std::uint64_t racers = 0U;
    std::uint32_t racer_count = 0U;
    std::array<std::uint64_t, 10U> racer_details{};

    bool operator==(const GameplayStateDigest&) const = default;
};

// Hash only authored gameplay state. Render targets, display lists, audio DMA
// buffers, OS queues, stacks, camera-distance values and host pointers are
// deliberately outside this contract because they may differ without changing
// the simulation.
std::uint64_t canonical_gameplay_state_hash(const std::uint8_t* rdram, std::size_t rdram_size);
GameplayStateDigest canonical_gameplay_state_digest(const std::uint8_t* rdram, std::size_t rdram_size);

// Frontend checkpoint used at authored menu transitions.  It includes only
// menu/roster state that can influence the next simulation; music players,
// animation cursors, cameras and renderer state remain local presentation.
std::uint64_t canonical_frontend_state_hash(const std::uint8_t* rdram, std::size_t rdram_size);

} // namespace dkr::runtime::netplay
