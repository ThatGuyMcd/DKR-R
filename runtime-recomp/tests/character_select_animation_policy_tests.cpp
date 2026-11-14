#include "character_select_animation_policy.hpp"

#include <cmath>
#include <cstdio>

using namespace dkr::runtime::enhancements;

static_assert(character_select_tempo(-1) == 182);
static_assert(character_select_tempo(0) == 182);
static_assert(character_select_tempo(120) == 120);
static_assert(character_select_tempo(401) == 182);

int main() {
    const float one_vi_tick = advance_character_select_phase(0.0F, 1, 120);
    const float one_authored_update = advance_character_select_phase(0.0F, 2, 120);
    const float wrapped = advance_character_select_phase(0.99F, 2, 120);
    const float bounded = advance_character_select_phase(0.0F, 99, 120);
    const float paused = advance_character_select_phase(0.25F, 0, 120);
    if (std::abs(one_vi_tick - (1.0F / 60.0F)) > 0.0001F || std::abs(one_authored_update - (1.0F / 30.0F)) > 0.0001F ||
        std::abs(wrapped - 0.0233333F) > 0.0001F || std::abs(bounded - (4.0F / 60.0F)) > 0.0001F ||
        std::abs(paused - 0.25F) > 0.0001F) {
        std::fputs("[test][character-select-animation-policy] FAIL\n", stderr);
        return 1;
    }
    std::puts("[test][character-select-animation-policy] PASS");
    return 0;
}
