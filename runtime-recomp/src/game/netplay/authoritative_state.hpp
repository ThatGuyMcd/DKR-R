#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::netplay {

inline constexpr std::uint32_t kAuthoritativeStateSchema = 11U;
// State fragments are 900 bytes and the authenticated protocol supports a
// sixteen-fragment missing-piece mask. Stay below that hard wire budget.
inline constexpr std::size_t kMaximumAuthoritativeStateBytes = 14336U;

// Portable, authored-only state used at the safe main-loop boundary. It never
// contains host pointers, render buffers, audio handles, OS queues or raw
// RDRAM. Racer objects are matched by DKR's canonical racer-array index, and
// moving track actors by their immutable level-map entry, on the receiving
// machine.
bool capture_authoritative_state(const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t frame,
                                 std::vector<std::uint8_t>& output, std::string& error);
bool apply_authoritative_state(std::uint8_t* rdram, std::size_t rdram_size, std::span<const std::uint8_t> snapshot,
                               std::uint32_t expected_frame, std::string& error);

// A forward jump must not substitute global state for native lifecycle work.
// Reject changed roster/actor membership and race-finish transitions without
// writing anything; the caller continues ordinary authored ticks instead.
bool apply_catch_up_authoritative_state(std::uint8_t* rdram, std::size_t rdram_size,
                                        std::span<const std::uint8_t> snapshot, std::uint32_t expected_frame,
                                        std::string& error);

// Live race replicas are corrective samples, not lifecycle checkpoints. A
// collected, spawned or unloaded map actor can legitimately cross the wire
// while the receiving machine is one authored tick either side of the same
// lifecycle boundary. Apply every validated global, racer and matching actor
// field atomically, while consuming (but not writing) actors which are not
// present locally yet. Strict track-start and recovery checkpoints continue to
// use apply_authoritative_state() and reject any topology disagreement.
bool apply_live_authoritative_state(std::uint8_t* rdram, std::size_t rdram_size, std::span<const std::uint8_t> snapshot,
                                    std::uint32_t expected_frame, std::uint32_t& unmatched_actors, std::string& error);

// The retail plane camera derives its heading from cameraYaw and
// steerVisualRotation. Those fields are intentionally absent from the strict
// determinism contract because rollback presentation can update them at a
// different wall-clock instant. Player 1 therefore sends this tiny post-frame
// sample separately. A guest compares it with its own sample from the same
// authored frame and applies only the wrapped difference to its current state;
// a late packet never rewinds the simulation to an old camera angle.
bool capture_racer_orientation_state(const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t frame,
                                     std::vector<std::uint8_t>& output, std::string& error);
bool apply_racer_orientation_correction(std::uint8_t* rdram, std::size_t rdram_size,
                                        std::span<const std::uint8_t> host_snapshot,
                                        std::span<const std::uint8_t> matching_local_snapshot,
                                        std::uint32_t expected_sample_frame, std::uint32_t& corrected_racers,
                                        std::string& error);
std::uint64_t authoritative_state_checksum(std::span<const std::uint8_t> snapshot);

} // namespace dkr::runtime::netplay
