#pragma once
// Self-test driver that follows the track like the game's CPU racers.
//
// The netplay self-test (--self-test-owned-host/-join) used to hold A and flip
// the stick every 15 frames. check_autopilot_input() instead steers along the
// checkpoint spline, a port of the steering half of func_80045C48 (decomp
// src/racer.c, called from racer_AI_pathing_inputs). When the guest state is
// unreadable (menus, loading, no race) it falls back to that square wave.
// Setting DKR_OWNED_CHECK_INPUT=wave selects the square wave outright.
//
// Test-only: it only ever produces the value a local controller would have
// supplied, which is then sent to peers like any real input.
#include "analog_driving/packed_input.hpp"
#include <array>
#include <cstdint>

namespace dkr::runtime::netplay::experimental {

struct CheckpointPoint {
    float x = 0, y = 0, z = 0;
};

// What the autopilot needs from one racer. Filled from guest RAM by
// capture_autopilot_observations(); tests build it directly.
struct AutopilotObservation {
    bool valid = false;
    // Object_Racer::vehicleID (Vehicle enum: 0 car, 1 hovercraft, 2 plane, ...).
    std::uint8_t vehicle = 0;
    // Racer object position and Object_Racer::steerVisualRotation (u16 angle,
    // 0x10000 = a full turn).
    CheckpointPoint position;
    std::int16_t heading = 0;
    // Object_Racer::nextCheckpoint and ::checkpoint_distance (1 at the
    // previous checkpoint, 0 at the next one).
    std::int32_t next_checkpoint = 0;
    float checkpoint_distance = 1;
    // Nodes nextCheckpoint-2 .. nextCheckpoint+1, wrapped, alternate route
    // applied. Spline segment [1] -> [2] is the one the racer is on.
    std::array<CheckpointPoint, 4> nodes{};
};

// Previous tangent angle: the game keeps it in the racer (unk1BE) and uses
// its frame-to-frame change as a feed-forward steer term.
struct AutopilotState {
    bool has_tangent = false;
    std::uint16_t tangent = 0;
};

inline constexpr int AutopilotStickLimit = 75; // CLAMP in racer_AI_pathing_inputs

// The old test input: A held, stick flipping between +-30 every 15 frames.
PackedInput check_square_wave_input(std::uint32_t frame);

// Stick X for one frame of CPU-style steering. Pure given (obs, state).
// An invalid observation returns the square wave for `frame`.
PackedInput check_autopilot_steer(const AutopilotObservation& obs, std::uint32_t frame, AutopilotState& state);

// Convenience for the self-test call sites: one observation per port, the port
// this process drives, the frame. Keeps its own AutopilotState (thread local),
// returns the same answer if asked twice for one frame, and logs about once
// per 60 frames: "[check][autopilot] frame=.. stick=.. checkpoint=.. valid=..".
using AutopilotView = std::array<AutopilotObservation, 4>;
PackedInput check_autopilot_input(const AutopilotView& view, unsigned port, std::uint32_t frame);

// Reads every port's racer from guest RAM (the owned game's byte-swapped
// image). Entries that are not a readable, racing racer have valid == false.
AutopilotView capture_autopilot_observations(const std::uint8_t* ram, std::size_t bytes, unsigned revision);

} // namespace dkr::runtime::netplay::experimental
