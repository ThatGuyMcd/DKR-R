#pragma once

#include "hud_group_layout.hpp"

// Golden Balloon 83a847ccbd3e6334c9cc9c697122d5b3cddb91c1:
// game/src/game_ui.c sHudWidescreenAnchor, platform/hud_layout.c.
// Explicit semantic mapping, not an inference from a moving element's X.
// See THIRD_PARTY.md and GOLDEN-BALLOON-NOTICE.txt for provenance/permission.
namespace dkr::runtime::hud::reference {
inline constexpr std::uint8_t kPassVariant = 3; // 0/1/2 remain quadrant-only.
enum class Mode { Race, TimeTrial, Boss, Challenge, Hub };
using Row = std::array<int, 5>;
inline constexpr Row left{-1, -1, -1, -1, -1}, centre{0, 0, 0, 0, 0}, right{1, 1, 1, 1, 1};
inline constexpr Row lap{1, -1, 1, 1, 1}, banana{1, 1, -1, 0, 1};
inline constexpr Row challenge{-1, -1, -1, 0, -1};
// Retail HudTypes order. Do not merge slots merely because their current
// textures match: the same asset may serve different semantic groups.
inline constexpr std::array<Row, 59> anchors{{left,      left,
                                              left,      lap,
                                              lap,       lap,
                                              lap,       banana,
                                              banana,    banana,
                                              right,     right,
                                              centre,    centre,
                                              centre,    centre,
                                              lap,       centre,
                                              banana,    left,
                                              left,      left,
                                              left,      Row{0, 1, 0, 0, 0},
                                              right,     right,
                                              left,      banana,
                                              banana,    centre,
                                              centre,    centre,
                                              challenge, centre,
                                              left,      centre,
                                              centre,    centre,
                                              right,     right,
                                              right,     right,
                                              right,     right,
                                              right,     right,
                                              left,      centre,
                                              centre,    left,
                                              challenge, challenge,
                                              challenge, challenge,
                                              challenge, challenge,
                                              centre,    Row{0, 0, 0, 0, 1},
                                              centre}};
constexpr Mode mode(groups::Scenario scenario) {
    switch (scenario) {
    case groups::Scenario::TimeTrial:
        return Mode::TimeTrial;
    case groups::Scenario::Boss:
        return Mode::Boss;
    case groups::Scenario::Adventure:
        return Mode::Hub;
    case groups::Scenario::Battle:
    case groups::Scenario::Treasure:
    case groups::Scenario::Eggs:
        return Mode::Challenge;
    default:
        return Mode::Race;
    }
}
constexpr int anchor(std::size_t element, Widget widget, groups::Scenario scenario, bool two_player, bool general_pass,
                     float authored_x = 0.0F) {
    // Requested DKR-R override: retail draws slot 33 twice at opposite Xs.
    // Pick the side BEFORE hud_element_render adds the entry slide/bounce.
    // Never infer side from the arrow texture/rotation: both copies point in
    // the same course direction, including on mirrored tracks. Other widgets
    // continue to use the explicit Golden Balloon table, not their live X.
    if (element == 33U && widget == Widget::CourseArrows) {
        return authored_x < 0.0F ? -1 : (authored_x > 0.0F ? 1 : 0);
    }
    // Golden Balloon moves gMinimapScreenX once, before both background and
    // marker construction. DKR-R carries that same translation on BOTH draw
    // paths instead; the upstream marker's CENTER must not drop that offset.
    if (widget == Widget::Minimap)
        return 1;
    // Golden Balloon gates its table to one viewport. DKR's two-player shared
    // challenge list is a right-hand vertical strip, unlike the centred 1P row.
    if (two_player && general_pass && mode(scenario) == Mode::Challenge &&
        (widget == Widget::ChallengePortrait || widget == Widget::Treasure || widget == Widget::EggChallenge ||
         widget == Widget::BattleBananas || widget == Widget::BananaCounter))
        return 1;
    if (element < anchors.size())
        return anchors[element][static_cast<std::size_t>(mode(scenario))];
    // Call-site-qualified transient draws have no HudData pointer identity.
    if (widget == Widget::RaceTimer || widget == Widget::LapTimer)
        return 1;
    if (widget == Widget::Stopwatch)
        return -1;
    return 0; // Finish strings/times stay centred, never guessed from live X.
}
inline groups::Transform transform(LayoutMode layout, float aspect, int edge, float native_slide, bool slides) {
    groups::Transform result;
    if (layout != LayoutMode::FitToViewport)
        return result;
    const float gutter = fullscreen_gutter_authored(aspect);
    result.x = edge * gutter;
    // Match Golden Balloon's draw-only slide scaling. Bounce and the gameplay
    // countdown remain untouched. Use a whole-canvas sweep, not guessed bounds.
    if (slides)
        result.x += native_slide * (hud_viewport_horizontal_cover(aspect) - 1.0F);
    return result; // Native uniform size, Y, rotation, UVs and animation survive.
}
} // namespace dkr::runtime::hud::reference
