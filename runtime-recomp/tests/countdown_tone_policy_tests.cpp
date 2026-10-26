#include "countdown_tone_policy.hpp"

using dkr::runtime::ui_cues::online_launch_tone;

static_assert(!online_launch_tone(0U));
static_assert(!online_launch_tone(6U));
static_assert(online_launch_tone(5U));
static_assert(online_launch_tone(4U).frequency_hz == online_launch_tone(5U).frequency_hz);
static_assert(online_launch_tone(3U).frequency_hz == online_launch_tone(5U).frequency_hz);
static_assert(online_launch_tone(2U).frequency_hz == online_launch_tone(5U).frequency_hz);
static_assert(online_launch_tone(1U).frequency_hz > online_launch_tone(2U).frequency_hz);
static_assert(online_launch_tone(1U).frequency_hz < online_launch_tone(2U).frequency_hz * 1.15F);
static_assert(online_launch_tone(1U).duration_ms > online_launch_tone(2U).duration_ms);

int main() {
    return 0;
}
