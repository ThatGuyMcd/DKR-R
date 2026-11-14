#include "widescreen_policy.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace dkr::runtime::enhancements;

int main() {
    // A fixed sky direction and a world direction must move by the same
    // horizontal screen distance for the same camera turn at every aspect.
    // RT64 divides projected X by the expansion factor for both draws.
    for (const float aspect : {4.0F / 3.0F, 16.0F / 9.0F, 21.0F / 9.0F, 32.0F / 9.0F}) {
        const float cover = aspect / kOriginalPresentationAspect;
        const float world_motion = (std::tan(0.2F) - std::tan(0.1F)) / cover;
        const float sky_motion = world_motion * sky_projection_column_scale(cover, 0);
        assert(std::fabs(sky_motion - world_motion) < 0.0001F);
        assert(sky_projection_column_scale(cover, 1) == sky_vertical_cover_scale(cover));
    }
    static_assert(sky_vertical_cover_scale(1.0F) == 1.0F);
    static_assert(sky_vertical_cover_scale(4.0F / 3.0F) == 4.0F / 3.0F);
    static_assert(!is_split_screen_sky_layout(0));
    static_assert(is_split_screen_sky_layout(1));
    static_assert(is_split_screen_sky_layout(2));
    static_assert(is_split_screen_sky_layout(3));
    static_assert(!is_split_screen_sky_layout(4));
    static_assert(!needs_split_world_aspect_adjust(true, true, 0));
    static_assert(!needs_split_world_aspect_adjust(true, true, 1));
    static_assert(needs_split_world_aspect_adjust(true, true, 2));
    static_assert(needs_split_world_aspect_adjust(true, true, 3));
    static_assert(!needs_split_world_aspect_adjust(false, true, 3));
    static_assert(!needs_split_world_aspect_adjust(true, false, 3));
    static_assert(world_aspect_policy(true, true, 0) == WorldAspectPolicy::Automatic);
    static_assert(world_aspect_policy(true, true, 2) == WorldAspectPolicy::AdjustToHost);
    static_assert(world_aspect_policy(false, true, 0) == WorldAspectPolicy::Automatic);
    static_assert(world_aspect_policy(true, false, 0) == WorldAspectPolicy::Automatic);
    constexpr auto left_quadrant = expand_split_viewport_horizontal_range(0.0F, 160.0F, 4.0F / 3.0F, 0);
    constexpr auto lower_left_quadrant = expand_split_viewport_horizontal_range(0.0F, 160.0F, 4.0F / 3.0F, 2);
    constexpr auto right_quadrant = expand_split_viewport_horizontal_range(160.0F, 320.0F, 4.0F / 3.0F, 1);
    static_assert(left_quadrant.right == 160.0F);
    static_assert(lower_left_quadrant.left == left_quadrant.left);
    static_assert(right_quadrant.left == 160.0F);
    static_assert(left_quadrant.right == right_quadrant.left);
    static_assert(left_quadrant.left < 0.0F);
    static_assert(right_quadrant.right > 320.0F);
    constexpr auto unchanged_two_player = expand_split_viewport_horizontal_range(0.0F, 320.0F, 1.0F, 0);
    static_assert(unchanged_two_player.left == 0.0F);
    static_assert(unchanged_two_player.right == 320.0F);
    static_assert(is_fullscreen_track_preview(0, 0, 320, 240, 0, 0, 319, 239, 320, 240));
    static_assert(!is_fullscreen_track_preview(80, 32, 240, 152, 80, 32, 240, 152, 320, 240));
    static_assert(!is_fullscreen_track_preview(0, 0, 319, 240, 0, 0, 319, 239, 320, 240));
    static_assert(is_presented_fullscreen_track_preview(0, 0, 320, 240, 0, 0, 319, 239, 320, 240));
    static_assert(is_presented_fullscreen_track_preview(0, 0, 320, 240, 0, 0, 320, 240, 320, 240));
    static_assert(!is_presented_fullscreen_track_preview(80, 24, 240, 216, 80, 24, 240, 216, 320, 240));
    static_assert(!is_presented_fullscreen_track_preview(0, 0, 320, 240, 0, 0, 320, 239, 320, 240));
    static_assert(preserve_track_select_lens_flare_tint(true, true, true, false));
    static_assert(!preserve_track_select_lens_flare_tint(true, true, true, true));
    static_assert(!preserve_track_select_lens_flare_tint(false, true, true, false));
    static_assert(!preserve_track_select_lens_flare_tint(true, false, true, false));
    static_assert(!preserve_track_select_lens_flare_tint(true, true, false, false));
    static_assert(split_sky_horizontal_cover_scale(4.0F / 3.0F, 0) == 1.0F);
    static_assert(split_sky_vertical_cover_scale(4.0F / 3.0F, 0) == 1.0F);
    static_assert(split_sky_horizontal_cover_scale(4.0F / 3.0F, 1) == 8.0F / 3.0F);
    static_assert(split_sky_horizontal_cover_scale(4.0F / 3.0F, 2) == 4.0F / 3.0F);
    static_assert(split_sky_horizontal_cover_scale(4.0F / 3.0F, 3) == 4.0F / 3.0F);

    assert(std::fabs(sky_vertical_cover_scale(7.0F / 4.0F) - 7.0F / 4.0F) < 0.0001F);
    const float super_ultrawide = sky_vertical_cover_scale(8.0F / 3.0F);
    assert(super_ultrawide < kSkyVerticalCorrectionCover);
    assert(std::fabs(super_ultrawide - 1.1484375F) < 0.0001F);
    const float two_player_cover = 16.0F / 3.0F;
    const float two_player_vertical = sky_vertical_cover_scale(two_player_cover);
    assert(std::fabs(split_sky_horizontal_cover_scale(8.0F / 3.0F, 1) - two_player_cover) < 0.0001F);
    assert(std::fabs(split_sky_vertical_cover_scale(8.0F / 3.0F, 1) - two_player_vertical) < 0.0001F);
    for (int layout = 2; layout <= 3; ++layout) {
        assert(std::fabs(split_sky_horizontal_cover_scale(8.0F / 3.0F, layout) - 8.0F / 3.0F) < 0.0001F);
        assert(std::fabs(split_sky_vertical_cover_scale(8.0F / 3.0F, layout) - super_ultrawide) < 0.0001F);
    }
    std::puts("[test][widescreen-policy] PASS");
    return 0;
}
