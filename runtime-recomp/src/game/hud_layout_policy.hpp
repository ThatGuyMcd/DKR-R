#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dkr::runtime::hud {

// Legacy numeric values are persistent: SafeArea must never become Custom.
enum class LayoutMode { Original, SafeArea, FitToViewport, Custom };
enum class Anchor { TopLeft, TopCentre, TopRight, Centre, BottomLeft, BottomCentre, BottomRight };
enum class Group { Race, Inventory, Timing, Minimap, Message, Challenge, Speedometer };
// DKR composes most visible HUD widgets from several independent HudElement
// draws.  Widget is deliberately more specific than the launcher-facing
// Group: every member of a widget must receive the same translation so icons,
// multipliers, digits, dials and needles retain their authored spacing.
enum class Widget {
    RacePosition,
    Weapon,
    LapCounter,
    BananaCounter,
    RaceTimer,
    CentreMessage,
    Minimap,
    Magnet,
    GoldenBalloon,
    LapTimer,
    Stopwatch,
    LapMessage,
    Treasure,
    CourseArrows,
    WrongWay,
    ProAm,
    Speedometer,
    SilverCoins,
    ChallengeFinish,
    ChallengePortrait,
    EggChallenge,
    BattleBananas,
    RaceFinish,
    TwoPlayerPortrait,
    TimerGlyphs,
};

enum class ViewportClass { FullWidth, Quadrant };

inline constexpr float kOriginalHudAspect = 4.0F / 3.0F;
inline constexpr float kHudViewportCoverQuantisation = 1024.0F;
// HudPass variants: zero is the ordinary clip-only pass. Counter variants
// select an outer-edge rectangle origin without changing authored geometry.
inline constexpr std::uint8_t kHudAnchorLeft = 1U;
inline constexpr std::uint8_t kHudAnchorRight = 2U;

constexpr bool quadrant_item_element(std::size_t element) {
    return element == 2U || element == 49U; // weapon icon and quantity sprite
}

constexpr bool counter_requires_immediate_texture_draw(float draw_scale, std::uint32_t colour) {
    // Only scale==1 uses hud_element_render's deferred texture batch. Scaled
    // animation frames already draw immediately and NEED alpha 254 to select
    // texrect_draw_scaled's XLU path. Alpha 255 selects OPA and exposes the
    // transparent texture background. Preserve all other colours/scales.
    return draw_scale == 1.0F && colour == 0xFFFFFFFEU;
}

constexpr std::uint8_t quadrant_counter_anchor(bool expanded_modern, int layout, int player, std::size_t element) {
    if (!expanded_modern || (layout != 2 && layout != 3) || player < 0 || player >= 4 ||
        (element > 1U && !quadrant_item_element(element)))
        return 0U;
    return (player & 1) ? kHudAnchorRight : kHudAnchorLeft;
}

struct ElementDefinition {
    std::string_view id;
    std::string_view label;
    Group group;
    Widget widget;
    Anchor anchor;
    bool shared = false;
};

// This table intentionally mirrors HudTypes in extern/dkr-decomp/src/game_ui.h.
// Its order is ABI: the index is the HudData::entry index used by the retail
// game. Keep the certification test in sync if the decomp names an unknown.
inline constexpr std::array<ElementDefinition, 59> kElements{{
    {"race.position", "Race position", Group::Race, Widget::RacePosition, Anchor::TopLeft},
    {"race.position.end", "Race position ordinal", Group::Race, Widget::RacePosition, Anchor::TopLeft},
    {"weapon.display", "Weapon", Group::Inventory, Widget::Weapon, Anchor::BottomLeft},
    {"lap.label", "Lap label", Group::Race, Widget::LapCounter, Anchor::TopCentre},
    {"lap.current", "Current lap", Group::Race, Widget::LapCounter, Anchor::TopCentre},
    {"lap.separator", "Lap separator", Group::Race, Widget::LapCounter, Anchor::TopCentre},
    {"lap.total", "Total laps", Group::Race, Widget::LapCounter, Anchor::TopCentre},
    {"banana.icon.spin", "Spinning banana", Group::Inventory, Widget::BananaCounter, Anchor::TopCentre},
    {"banana.number.ones", "Banana count ones", Group::Inventory, Widget::BananaCounter, Anchor::TopCentre},
    {"banana.number.tens", "Banana count tens", Group::Inventory, Widget::BananaCounter, Anchor::TopCentre},
    {"time.label", "Time label", Group::Timing, Widget::RaceTimer, Anchor::TopRight},
    {"time.value", "Race time", Group::Timing, Widget::RaceTimer, Anchor::TopRight},
    {"start.go", "Go message", Group::Message, Widget::CentreMessage, Anchor::Centre},
    {"start.ready", "Get ready message", Group::Message, Widget::CentreMessage, Anchor::Centre},
    {"finish.banner", "Finish banner", Group::Message, Widget::CentreMessage, Anchor::Centre},
    {"minimap.marker", "Minimap markers", Group::Minimap, Widget::Minimap, Anchor::BottomRight},
    {"lap.flag", "Lap flag", Group::Race, Widget::LapCounter, Anchor::TopCentre},
    {"weapon.magnet", "Magnet reticle", Group::Inventory, Widget::Magnet, Anchor::Centre},
    {"banana.x", "Banana multiplier", Group::Inventory, Widget::BananaCounter, Anchor::TopCentre},
    {"balloon.icon", "Golden balloon", Group::Inventory, Widget::GoldenBalloon, Anchor::TopLeft},
    {"balloon.x", "Balloon multiplier", Group::Inventory, Widget::GoldenBalloon, Anchor::TopLeft},
    {"balloon.number.ones", "Balloon count ones", Group::Inventory, Widget::GoldenBalloon, Anchor::TopLeft},
    {"balloon.number.tens", "Balloon count tens", Group::Inventory, Widget::GoldenBalloon, Anchor::TopLeft},
    {"lap.time", "Lap time", Group::Timing, Widget::LapTimer, Anchor::TopRight},
    {"trial.lap.label", "Time trial lap label", Group::Timing, Widget::LapTimer, Anchor::TopRight},
    {"trial.lap.value", "Time trial lap number", Group::Timing, Widget::LapTimer, Anchor::TopRight},
    {"stopwatch.hands", "Stopwatch hands", Group::Timing, Widget::Stopwatch, Anchor::BottomRight},
    {"banana.icon.static", "Static banana", Group::Inventory, Widget::BananaCounter, Anchor::TopCentre},
    {"banana.sparkle", "Banana sparkle", Group::Inventory, Widget::BananaCounter, Anchor::TopCentre},
    {"lap.final", "Final lap message", Group::Message, Widget::LapMessage, Anchor::Centre},
    {"lap.word", "Lap message", Group::Message, Widget::LapMessage, Anchor::Centre},
    {"lap.two", "Lap two message", Group::Message, Widget::LapMessage, Anchor::Centre},
    {"challenge.treasure", "Treasure meter", Group::Challenge, Widget::Treasure, Anchor::TopCentre},
    {"course.arrows", "Course arrows", Group::Race, Widget::CourseArrows, Anchor::Centre},
    {"stopwatch", "Stopwatch", Group::Timing, Widget::Stopwatch, Anchor::BottomRight},
    {"wrongway.first", "Wrong way line 1", Group::Message, Widget::WrongWay, Anchor::Centre},
    {"wrongway.second", "Wrong way line 2", Group::Message, Widget::WrongWay, Anchor::Centre},
    {"proam.logo", "Pro AM logo", Group::Message, Widget::ProAm, Anchor::Centre},
    {"speed.arrow", "Speedometer arrow", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"speed.zero", "Speedometer 0", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"speed.thirty", "Speedometer 30", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"speed.sixty", "Speedometer 60", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"speed.ninety", "Speedometer 90", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"speed.120", "Speedometer 120", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"speed.150", "Speedometer 150", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"speed.background", "Speedometer background", Group::Speedometer, Widget::Speedometer, Anchor::BottomRight},
    {"challenge.silver.coins", "Silver coin tally", Group::Challenge, Widget::SilverCoins, Anchor::TopLeft},
    {"challenge.finish.first", "Challenge finish position 1", Group::Challenge, Widget::ChallengeFinish,
     Anchor::Centre},
    {"challenge.finish.second", "Challenge finish position 2", Group::Challenge, Widget::ChallengeFinish,
     Anchor::Centre},
    {"weapon.quantity", "Weapon quantity", Group::Inventory, Widget::Weapon, Anchor::BottomLeft},
    {"challenge.portrait", "Challenge portrait", Group::Challenge, Widget::ChallengePortrait, Anchor::TopLeft},
    {"challenge.egg", "Egg challenge icon", Group::Challenge, Widget::EggChallenge, Anchor::TopLeft},
    {"battle.banana.icon", "Battle banana icon", Group::Challenge, Widget::BattleBananas, Anchor::TopLeft},
    {"battle.banana.x", "Battle banana multiplier", Group::Challenge, Widget::BattleBananas, Anchor::TopLeft},
    {"battle.banana.ones", "Battle banana ones", Group::Challenge, Widget::BattleBananas, Anchor::TopLeft},
    {"battle.banana.tens", "Battle banana tens", Group::Challenge, Widget::BattleBananas, Anchor::TopLeft},
    {"race.finish.first", "Race finish position 1", Group::Race, Widget::RaceFinish, Anchor::Centre},
    {"twoplayer.portrait", "Two-player adventure portrait", Group::Challenge, Widget::TwoPlayerPortrait,
     Anchor::TopRight},
    {"race.finish.second", "Race finish position 2", Group::Race, Widget::RaceFinish, Anchor::Centre},
}};

// These two composites are drawn outside the persistent HudData::entry array:
// the minimap is an ObjectSegment local to hud_render_general, while every
// timer digit is a temporary HudElementBase created by hud_timer_render.
// Keeping them explicit closes the two gaps that cannot be represented by the
// 59-entry HudTypes ABI table above.
inline constexpr std::array<ElementDefinition, 2> kSupplementalElements{{
    {"minimap.background", "Minimap background", Group::Minimap, Widget::Minimap, Anchor::BottomRight, true},
    {"timer.glyphs", "Generated timer glyphs", Group::Timing, Widget::TimerGlyphs, Anchor::TopRight, false},
}};

constexpr std::size_t widget_member_count(Widget widget) {
    std::size_t count = 0U;
    for (const auto& element : kElements) {
        if (element.widget == widget)
            ++count;
    }
    for (const auto& element : kSupplementalElements) {
        if (element.widget == widget)
            ++count;
    }
    return count;
}

// Certification guard: a composite may contain many retail draw calls, but
// its authored members must never disagree about the horizontal anchor used
// by Fit to Viewport.  A disagreement would pull the widget apart.
constexpr bool widget_members_share_anchor(Widget widget) {
    bool found = false;
    Anchor expected = Anchor::Centre;
    for (const auto& element : kElements) {
        if (element.widget != widget)
            continue;
        if (!found) {
            expected = element.anchor;
            found = true;
        } else if (element.anchor != expected) {
            return false;
        }
    }
    for (const auto& element : kSupplementalElements) {
        if (element.widget != widget)
            continue;
        if (!found) {
            expected = element.anchor;
            found = true;
        } else if (element.anchor != expected) {
            return false;
        }
    }
    return found;
}

constexpr float clamp_hud_scale(float value) {
    return value < 0.5F ? 0.5F : (value > 1.5F ? 1.5F : value);
}

constexpr bool left_anchor(Anchor anchor) {
    return anchor == Anchor::TopLeft || anchor == Anchor::BottomLeft;
}

constexpr bool right_anchor(Anchor anchor) {
    return anchor == Anchor::TopRight || anchor == Anchor::BottomRight;
}

// Returns authored 320-wide coordinate units. A quadrant is authored as a
// 160-wide 4:3 viewport. Expanding it to the host aspect creates equal side
// gutters; HUD anchored to an outside edge must move by precisely that gutter.
constexpr float split_gutter_authored(float viewport_aspect) {
    return viewport_aspect <= (4.0F / 3.0F) ? 0.0F : 80.0F * (viewport_aspect / (4.0F / 3.0F) - 1.0F);
}

// Full-screen authored space is 320 units wide, so each outside gutter is
// half of the additional width introduced by the host aspect ratio.
constexpr float fullscreen_gutter_authored(float viewport_aspect) {
    return viewport_aspect <= (4.0F / 3.0F) ? 0.0F : 160.0F * (viewport_aspect / (4.0F / 3.0F) - 1.0F);
}

// Fit-to-viewport HUD coordinates extend DKR's authored 320-wide space by the
// same ratio as the host presentation. Carry that ratio through the display-
// list marker so the renderer can widen the independent RSP viewport clip as
// well as the RDP scissor. Keeping this as a compact fixed-point value makes a
// HUD pass deterministic even if the launcher is resized while the task is in
// flight.
constexpr float hud_viewport_horizontal_cover(float viewport_aspect) {
    return viewport_aspect <= kOriginalHudAspect ? 1.0F : viewport_aspect / kOriginalHudAspect;
}

constexpr std::uint16_t encode_hud_viewport_cover(float viewport_aspect) {
    const float quantised = hud_viewport_horizontal_cover(viewport_aspect) * kHudViewportCoverQuantisation + 0.5F;
    return quantised >= 65535.0F ? static_cast<std::uint16_t>(65535U) : static_cast<std::uint16_t>(quantised);
}

constexpr float decode_hud_viewport_cover(std::uint16_t token) {
    return token == 0U ? 1.0F : static_cast<float>(token) / kHudViewportCoverQuantisation;
}

// Fast3D clip ratios are integral multipliers of the decoded viewport. Round
// the requested cover upward: the full-width RDP scissor remains the precise
// screen boundary, while this independent clip must never cut a HUD triangle
// at the old 4:3 edge.
constexpr std::int16_t decode_hud_viewport_clip_ratio(std::uint16_t token) {
    if (token == 0U)
        return 1;
    const std::uint32_t rounded =
        (static_cast<std::uint32_t>(token) + static_cast<std::uint32_t>(kHudViewportCoverQuantisation) - 1U) /
        static_cast<std::uint32_t>(kHudViewportCoverQuantisation);
    return static_cast<std::int16_t>(rounded > 32767U ? 32767U : rounded);
}

// RT64 stores an RDP scissor edge after applying its extended-origin offset,
// alongside the origin that produced that stored coordinate. Changing only
// the copied origin metadata reinterprets the old coordinate from the new
// origin and leaves one side at DKR's retail 4:3 boundary. Rebase the stored
// coordinate whenever a live scissor is assigned a new origin.
//
// These values are part of RT64's public extended-GBI ABI
// (G_EX_ORIGIN_RIGHT/G_EX_ORIGIN_NONE). Keeping the calculation here makes the
// policy independently testable without editing or depending on RT64 itself.
inline constexpr std::uint16_t kExtendedOriginRight = 0x400U;
inline constexpr std::uint16_t kExtendedOriginNone = 0x800U;

constexpr std::int32_t hud_scissor_origin_offset(std::uint16_t origin, std::uint16_t colour_image_width) {
    return origin < kExtendedOriginNone
               ? static_cast<std::int32_t>(
                     (static_cast<std::uint32_t>(origin) * static_cast<std::uint32_t>(colour_image_width) * 4U) /
                     static_cast<std::uint32_t>(kExtendedOriginRight))
               : 0;
}

constexpr std::int32_t rebase_hud_scissor_edge(std::int32_t stored_coordinate, std::uint16_t previous_origin,
                                               std::uint16_t new_origin, std::uint16_t colour_image_width) {
    return stored_coordinate - hud_scissor_origin_offset(previous_origin, colour_image_width) +
           hud_scissor_origin_offset(new_origin, colour_image_width);
}

constexpr float anchor_gutter_delta(Anchor anchor, float viewport_aspect) {
    const float gutter = split_gutter_authored(viewport_aspect);
    return left_anchor(anchor) ? -gutter : (right_anchor(anchor) ? gutter : 0.0F);
}

constexpr ViewportClass viewport_class_for_layout(int layout) {
    return layout == 2 || layout == 3 ? ViewportClass::Quadrant : ViewportClass::FullWidth;
}

constexpr float viewport_gutter_authored(float viewport_aspect, ViewportClass viewport_class) {
    return viewport_class == ViewportClass::Quadrant ? split_gutter_authored(viewport_aspect)
                                                     : fullscreen_gutter_authored(viewport_aspect);
}

constexpr float placement_delta_x(LayoutMode mode, Anchor anchor, float viewport_aspect, ViewportClass viewport_class) {
    if (mode == LayoutMode::SafeArea) {
        return left_anchor(anchor) ? 6.0F : right_anchor(anchor) ? -6.0F : 0.0F;
    }
    if (mode != LayoutMode::FitToViewport)
        return 0.0F;
    const float gutter = viewport_gutter_authored(viewport_aspect, viewport_class);
    return left_anchor(anchor) ? -gutter : right_anchor(anchor) ? gutter : 0.0F;
}

// A handful of challenge elements are deliberately reused by retail DKR on
// both sides of the screen. Their live authored X position is the only stable
// way to preserve that intent after the game has arranged portraits/metres for
// the current player count. All other elements retain their explicit ABI
// anchor above, including centre-screen messages that animate offscreen.
constexpr bool uses_live_horizontal_anchor(std::size_t element_index) {
    return element_index == 32U || // HUD_TREASURE_METRE
           element_index == 50U || // HUD_CHALLENGE_PORTRAIT
           element_index == 51U;   // HUD_EGG_CHALLENGE_ICON
}

constexpr Anchor anchor_for_live_x(Anchor fallback, float authored_x) {
    if (authored_x <= 106.0F)
        return Anchor::TopLeft;
    if (authored_x >= 214.0F)
        return Anchor::TopRight;
    return fallback;
}

constexpr Anchor effective_anchor(std::size_t element_index, float authored_x) {
    if (element_index >= kElements.size()) {
        return kSupplementalElements[1].anchor;
    }
    const Anchor fallback = kElements[element_index].anchor;
    return uses_live_horizontal_anchor(element_index) ? anchor_for_live_x(fallback, authored_x) : fallback;
}

} // namespace dkr::runtime::hud
