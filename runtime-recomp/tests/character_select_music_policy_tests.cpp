#include "character_select_music_policy.hpp"

#include <array>
#include <cstdio>

using namespace dkr::runtime::enhancements;

static_assert(character_music_channel_mask(-1) == 0x000FU);
static_assert(character_music_channel_mask(10) == 0x000FU);

int main() {
    constexpr std::array<std::uint16_t, 10> expected{
        0x800FU, 0x108FU, 0x020FU, 0x040FU, 0x010FU, 0x080FU, 0x200FU, 0x400FU, 0x002FU, 0x001FU,
    };

    for (std::size_t character = 0; character < expected.size(); ++character) {
        if (character_music_channel_mask(static_cast<int>(character)) != expected[character]) {
            std::fputs("[test][character-select-music-policy] FAIL\n", stderr);
            return 1;
        }
    }

    std::puts("[test][character-select-music-policy] PASS");
    return 0;
}
