#pragma once
// Self-test only (DKR_OWNED_CHECK_TRACK): choose the world to race by steering
// the real track-select cursor with scripted inputs, then confirm with A.
//
// A fresh session save only unlocks the first track of each of the four worlds,
// each with its default vehicle (a vehicle choice needs a cleared race), so the
// value is a world index 0..3 and nothing else can be selected.
#include "analog_driving/packed_input.hpp"
#include <atomic>
#include <cstdint>
#include <optional>

namespace dkr::runtime::netplay::experimental {

inline constexpr int kCheckCourseWorlds = 4;
inline std::atomic<int> g_check_course_world{-1};

inline void set_check_course_world(int world) {
    g_check_course_world.store(world, std::memory_order_relaxed);
}
// -1 when the switch is off.
inline int check_course_world() {
    return g_check_course_world.load(std::memory_order_relaxed);
}

// Input for the track-select menu `age` frames after it appeared. Only the host
// moves the cursor (the menu takes the combined input), one row down per pulse;
// every peer holds A until the cursor is in place and then presses it every 30
// frames, which also carries the later option screens.
inline std::optional<PackedInput> check_track_select_input(int world, bool host, std::uint32_t age) {
    constexpr std::uint32_t first = 50, step = 20, settle = 40, repeat = 30;
    const std::uint32_t moves = world > 0 ? std::uint32_t(world) : 0U;
    if (host && age >= first && (age - first) % step == 0 && (age - first) / step < moves)
        return PackedInput{0, 0, -75};
    const std::uint32_t confirm = first + step * moves + settle;
    if (age >= confirm && (age - confirm) % repeat == 0)
        return PackedInput{0x8000, 0, 0};
    return std::nullopt;
}

} // namespace dkr::runtime::netplay::experimental
