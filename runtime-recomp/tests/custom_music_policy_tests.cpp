#include "custom_music_policy.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace dkr::runtime::custom_music;

namespace {

bool near(double a, double b, double tolerance = 1e-4) {
    return std::fabs(a - b) <= tolerance;
}

} // namespace

int main() {
    // Loop ranges: 0 means the end of the file; a range the scan would refuse
    // degrades to looping the whole file rather than reading out of bounds.
    LoopRange loop = loop_range(1000U, 100U, 0U);
    assert(near(loop.start, 100.0) && near(loop.end, 1000.0));
    loop = loop_range(1000U, 100U, 400U);
    assert(near(loop.start, 100.0) && near(loop.end, 400.0));
    loop = loop_range(1000U, 900U, 400U);
    assert(near(loop.start, 0.0) && near(loop.end, 400.0));
    loop = loop_range(1000U, 0U, 5000U);
    assert(near(loop.end, 1000.0));

    // Game gain: base * slider * fade, with the song's base divided back out.
    assert(near(game_gain(127 * 256, 127U), 1.0));
    assert(near(game_gain(110 * 256, 110U), 1.0));                // another base, same 100%
    assert(near(game_gain(127 * 128, 127U), 0.5));                // slider at half
    assert(near(game_gain((127 * 256) >> 2, 127U), 0.25));        // pause menu
    assert(game_gain(0, 127U) == 0.0F && game_gain(-5, 127U) == 0.0F);
    assert(game_gain(1000, 0U) == 0.0F);
    assert(near(game_gain(127 * 256 * 2, 127U), 1.0));            // never above full

    // Tempo: the final lap's 1.12, only when asked for, and bounded.
    assert(near(tempo_ratio(112, 100, true), 1.12));
    assert(near(tempo_ratio(112, 100, false), 1.0));
    assert(near(tempo_ratio(-1, 100, true), 1.0));
    assert(near(tempo_ratio(100, 0, true), 1.0));
    assert(near(tempo_ratio(1000, 100, true), 2.0));
    assert(near(tempo_ratio(10, 100, true), 0.5));

    // Advancing wraps at the loop end back to the loop start, several times if
    // a step is long, and the intro before the loop is never revisited.
    loop = loop_range(100U, 20U, 80U);
    assert(near(advance(10.0, 5.0, loop), 15.0));
    assert(near(advance(79.0, 2.0, loop), 21.0));
    assert(near(advance(79.0, 122.0, loop), 21.0));   // 2 + two whole loops

    // Rendering: a ramp 0..99 in both channels, read at half speed, adds the
    // interpolated value with the gain ramping across the block.
    std::vector<std::int16_t> pcm;
    for (int i = 0; i < 100; ++i) {
        pcm.push_back(static_cast<std::int16_t>(i * 100));
        pcm.push_back(static_cast<std::int16_t>(-i * 100));
    }
    double position = 0.0;
    loop = loop_range(100U, 0U, 0U);
    {
        std::vector<float> fresh(8U, 0.0F);
        position = 10.0;
        render(pcm.data(), 100U, loop, position, 0.5, 1.0F, 1.0F, fresh.data(), 4U);
        assert(near(fresh[0], 1000.0, 1e-2) && near(fresh[1], -1000.0, 1e-2));
        assert(near(fresh[2], 1050.0, 1e-2) && near(fresh[3], -1050.0, 1e-2));
        assert(near(fresh[6], 1150.0, 1e-2));
        assert(near(position, 12.0));
    }

    // Across the seam the frame after the loop's last is the loop's first.
    {
        std::vector<float> seam(4U, 0.0F);
        loop = loop_range(100U, 20U, 80U);
        position = 79.5;
        render(pcm.data(), 100U, loop, position, 1.0, 1.0F, 1.0F, seam.data(), 2U);
        assert(near(seam[0], (7900.0 + 2000.0) / 2.0, 1e-1));   // 79 blended into 20
        assert(near(seam[2], 2050.0, 1e-1));                     // wrapped to 20.5
    }

    // The gain ramps linearly to its target over the block.
    {
        std::vector<std::int16_t> flat(200U, 1000);
        std::vector<float> ramp(8U, 0.0F);
        loop = loop_range(100U, 0U, 0U);
        position = 0.0;
        render(flat.data(), 100U, loop, position, 1.0, 0.0F, 1.0F, ramp.data(), 4U);
        assert(near(ramp[0], 250.0, 1e-2) && near(ramp[6], 1000.0, 1e-2));
    }

    // Nothing to read renders nothing.
    {
        std::vector<float> none(4U, 0.0F);
        position = 0.0;
        render(nullptr, 0U, loop, position, 1.0, 1.0F, 1.0F, none.data(), 2U);
        assert(none[0] == 0.0F && none[3] == 0.0F);
    }

    std::printf("[test][custom-music-policy] PASS\n");
    return 0;
}
