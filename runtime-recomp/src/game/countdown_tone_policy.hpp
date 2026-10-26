#pragma once

#include <cstdint>

namespace dkr::runtime::ui_cues {

struct CountdownTone {
    float frequency_hz = 0.0F;
    std::uint32_t duration_ms = 0U;

    constexpr explicit operator bool() const {
        return frequency_hz > 0.0F && duration_ms > 0U;
    }
};

// The online launch card displays 5, 4, 3, 2, 1. Those five visual beats map
// to four matching E5 pulses and a short F#5 release cue. The whole-tone rise
// makes the final beat distinct without borrowing a sampled game sound.
constexpr CountdownTone online_launch_tone(std::uint32_t displayed_second) {
    if (displayed_second < 1U || displayed_second > 5U) {
        return {};
    }
    return displayed_second == 1U ? CountdownTone{739.99F, 180U} : CountdownTone{659.25F, 125U};
}

} // namespace dkr::runtime::ui_cues
