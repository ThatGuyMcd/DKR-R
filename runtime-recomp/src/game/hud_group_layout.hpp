#pragma once

#include "hud_layout_policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>

// Presentation-only policy for one/two-player HUDs. The quadrant policy in
// hud_layout_policy.hpp deliberately does not depend on this implementation.
namespace dkr::runtime::hud::groups {
enum class Scenario { Race, Adventure, Boss, TimeTrial, Battle, Treasure, Eggs, Count };
enum class Target { Single, Top, Bottom, Shared, Count };
enum class Aspect { All, Standard, Deck, Wide, Ultrawide, Count };
inline constexpr std::size_t kWidgets = 25;
inline constexpr std::size_t kScenarios = static_cast<std::size_t>(Scenario::Count);
inline constexpr std::size_t kTargets = static_cast<std::size_t>(Target::Count);
inline constexpr std::size_t kAspects = static_cast<std::size_t>(Aspect::Count);
inline constexpr std::array<std::string_view, kScenarios> kScenarioKeys{"race",   "adventure", "boss-taj", "time-trial",
                                                                        "battle", "treasure",  "eggs"};
inline constexpr std::array<std::string_view, kScenarios> kScenarioNames{
    "Race", "Adventure hub", "Boss / Taj challenge", "Time trial", "Battle", "Treasure", "Egg challenge"};
inline constexpr std::array<std::string_view, kTargets> kTargetKeys{"single", "top", "bottom", "shared"};
inline constexpr std::array<std::string_view, kTargets> kTargetNames{"Single player", "Player 1 (top)",
                                                                     "Player 2 (bottom)", "Shared HUD"};
inline constexpr std::array<std::string_view, kAspects> kAspectKeys{"all", "4-3", "16-10", "16-9", "ultrawide"};
inline constexpr std::array<std::string_view, kAspects> kAspectNames{"All screens", "4:3 override", "16:10 override",
                                                                     "16:9 override", "Ultrawide override"};
inline constexpr std::array<std::string_view, kWidgets> kWidgetKeys{"position",
                                                                    "item",
                                                                    "laps",
                                                                    "bananas",
                                                                    "timer",
                                                                    "ready-go-finish",
                                                                    "minimap",
                                                                    "reticle",
                                                                    "balloons",
                                                                    "lap-times",
                                                                    "stopwatch",
                                                                    "lap-message",
                                                                    "treasure",
                                                                    "directions",
                                                                    "wrong-way",
                                                                    "pro-am",
                                                                    "speedometer",
                                                                    "silver-coins",
                                                                    "challenge-finish",
                                                                    "challenge-portrait",
                                                                    "eggs",
                                                                    "battle-bananas",
                                                                    "race-finish",
                                                                    "adventure-portrait",
                                                                    "timer-glyphs"};
inline constexpr std::array<std::string_view, kWidgets> kWidgetNames{"Position",
                                                                     "Item + quantity",
                                                                     "Lap counter",
                                                                     "Banana counter",
                                                                     "Race timer",
                                                                     "Ready / Go / Finish",
                                                                     "Minimap + racers",
                                                                     "Target reticle (locked)",
                                                                     "Golden balloons",
                                                                     "Lap times",
                                                                     "Stopwatch",
                                                                     "Lap announcement",
                                                                     "Treasure meter",
                                                                     "Direction arrows",
                                                                     "Wrong way",
                                                                     "Unused logo (locked)",
                                                                     "Speedometer",
                                                                     "Silver coins",
                                                                     "Challenge finish",
                                                                     "Challenge racers + scores",
                                                                     "Egg counter",
                                                                     "Battle bananas",
                                                                     "Finish place",
                                                                     "Adventure portrait",
                                                                     "Timer glyphs (automatic)"};

struct Point {
    float x = 0, y = 0;
    bool operator==(const Point&) const = default;
};
struct Rect {
    float x, y, w, h;
};
struct Transform {
    float scale = 1, x = 0, y = 0;
    float clip_top = 0, clip_bottom = 240;
    // Decoder-only native-font restoration. Zero preserves the ordinary
    // uniform layout scale; user-editable layouts retain their 50-150% range.
    float scale_y = 0;
    float y_scale() const {
        return scale_y == 0 ? scale : scale_y;
    }
    bool valid(float scale_limit = 1.5F) const {
        return std::isfinite(scale) && scale >= .5F && scale <= scale_limit && std::isfinite(scale_y) &&
               (scale_y == 0 || (scale_y >= .5F && scale_y <= scale_limit)) && std::isfinite(x) && std::isfinite(y) &&
               std::abs(x) <= 4096 && std::abs(y) <= 1024 && std::isfinite(clip_top) && std::isfinite(clip_bottom) &&
               clip_top >= 0 && clip_bottom <= 240 && clip_bottom > clip_top;
    }
    Point apply(Point p) const {
        return {p.x * scale + x, p.y * y_scale() + y};
    }
    bool identity() const {
        return scale == 1 && y_scale() == 1 && x == 0 && y == 0;
    }
};
struct Placement {
    // Offsets are fractions of the selected viewport, independent of pixels.
    float x = 0, y = 0, scale = 0; // zero = inherit global group size
    int anchor = -1;               // -1 = native; otherwise nine-point grid (row-major)
    bool enabled = false;          // aspect-specific entries otherwise inherit All
    bool operator==(const Placement&) const = default;
};
struct Layout {
    LayoutMode mode = LayoutMode::Original;
    float scale = 1;
    bool custom_fill = true;
    std::array<Placement, kAspects * kScenarios * kTargets * kWidgets> entries{};
    bool operator==(const Layout&) const = default;
};
constexpr std::size_t index(Aspect aspect, Scenario scenario, Target target, Widget widget) {
    return (((static_cast<std::size_t>(aspect) * kScenarios + static_cast<std::size_t>(scenario)) * kTargets +
             static_cast<std::size_t>(target)) *
                kWidgets +
            static_cast<std::size_t>(widget));
}
inline float safe_scale(float s, float fallback = 1) {
    return std::isfinite(s) ? std::clamp(s, .5F, 1.5F) : fallback;
}
inline bool valid(const Layout& layout) {
    if (layout.mode != LayoutMode::Original && layout.mode != LayoutMode::SafeArea &&
        layout.mode != LayoutMode::FitToViewport && layout.mode != LayoutMode::Custom)
        return false;
    if (!std::isfinite(layout.scale) || layout.scale < .5F || layout.scale > 1.5F)
        return false;
    for (const auto& p : layout.entries) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1 || std::abs(p.y) > 1 ||
            !std::isfinite(p.scale) || (p.scale != 0 && (p.scale < .5F || p.scale > 1.5F)) || p.anchor < -1 ||
            p.anchor > 8)
            return false;
    }
    return true;
}
constexpr bool eligible(bool modern, int session_players, int hud_players, int viewport_layout) {
    return modern && session_players >= 1 && session_players <= 2 && hud_players >= 0 && hud_players <= 1 &&
           viewport_layout >= 0 && viewport_layout <= 1;
}
inline Aspect aspect_family(float a) {
    if (a < 1.46F)
        return Aspect::Standard;
    if (a < 1.69F)
        return Aspect::Deck;
    if (a < 2.0F)
        return Aspect::Wide;
    return Aspect::Ultrawide;
}
constexpr Scenario scenario_for(int race_type, bool time_trial, bool taj_challenge = false) {
    if (race_type == 8 || taj_challenge)
        return Scenario::Boss;
    if (race_type == 5)
        return Scenario::Adventure;
    if (race_type == 64)
        return Scenario::Battle;
    if (race_type == 65)
        return Scenario::Treasure;
    if (race_type == 66)
        return Scenario::Eggs;
    return time_trial ? Scenario::TimeTrial : Scenario::Race;
}
constexpr bool editable(Widget w) {
    return w != Widget::Magnet && w != Widget::ProAm && w != Widget::TimerGlyphs;
}
constexpr bool split(Target t) {
    return t == Target::Top || t == Target::Bottom;
}
constexpr Widget canonical_widget(Scenario s, Widget w) {
    if ((s == Scenario::Race || s == Scenario::Boss || s == Scenario::TimeTrial) && w == Widget::ChallengeFinish)
        return Widget::RaceFinish;
    if ((s == Scenario::Battle || s == Scenario::Treasure || s == Scenario::Eggs) &&
        (w == Widget::Treasure || w == Widget::EggChallenge || w == Widget::BattleBananas))
        return Widget::ChallengePortrait;
    return w;
}
constexpr bool available(Scenario s, Target t, Widget w) {
    if (!editable(w))
        return false;
    if (canonical_widget(s, w) != w)
        return false;
    if (t == Target::Shared)
        return w == Widget::Minimap || (w == Widget::ChallengePortrait &&
                                        (s == Scenario::Battle || s == Scenario::Treasure || s == Scenario::Eggs));
    if (split(t) && w == Widget::Minimap)
        return false;
    if (split(t) && w == Widget::ChallengePortrait)
        return false;
    switch (w) {
    case Widget::GoldenBalloon:
    case Widget::TwoPlayerPortrait:
        return s == Scenario::Adventure;
    case Widget::LapTimer:
    case Widget::Stopwatch:
        return s == Scenario::TimeTrial;
    case Widget::Treasure:
        return s == Scenario::Treasure;
    case Widget::EggChallenge:
        return s == Scenario::Eggs;
    case Widget::BattleBananas:
        return s == Scenario::Battle;
    case Widget::ChallengePortrait:
    case Widget::ChallengeFinish:
        return s == Scenario::Battle || s == Scenario::Treasure || s == Scenario::Eggs;
    default:
        return true;
    }
}
inline Rect canvas(Target target, float aspect) {
    const float gutter = fullscreen_gutter_authored(std::clamp(aspect, 1.0F, 4.0F));
    return {-gutter, target == Target::Bottom ? 120.0F : 0.0F, 320 + 2 * gutter, split(target) ? 120.0F : 240.0F};
}
// Resting bounds are conservative envelopes, never sampled from moving entry/
// finish animations. All coordinates use the same 320x240 top-left canvas.
inline Rect bounds(Scenario scenario, Target target, Widget widget) {
    static constexpr std::array<Rect, kWidgets> single{
        {{24, 12, 56, 36},   {19, 174, 56, 52},  {82, 10, 53, 38},  {143, 16, 66, 40}, {204, 10, 98, 40},
         {92, 74, 136, 32},  {264, 168, 48, 64}, {144, 96, 32, 32}, {18, 8, 78, 48},   {184, 44, 120, 66},
         {16, 172, 76, 60},  {90, 74, 140, 32},  {16, 42, 64, 30},  {16, 72, 44, 44},  {65, 74, 190, 32},
         {220, 170, 80, 30}, {224, 136, 84, 92}, {20, 130, 24, 84}, {106, 54, 80, 48}, {20, 10, 48, 44},
         {24, 48, 68, 28},   {22, 48, 68, 38},   {86, 92, 148, 40}, {238, 8, 56, 50},  {204, 28, 100, 24}}};
    Rect r = single[static_cast<std::size_t>(widget)];
    if (widget == Widget::ChallengePortrait)
        r = target == Target::Shared ? Rect{240, 6, 76, 224} : Rect{16, 8, 288, 62};
    if (target == Target::Shared && widget == Widget::Minimap)
        r = {264, 84, 48, 68};
    if (split(target)) {
        switch (widget) {
        case Widget::Weapon:
            r = {20, 78, 48, 36};
            break;
        case Widget::LapCounter:
            r = {170, 12, 62, 30};
            break;
        case Widget::BananaCounter:
            r = {178, 12, 60, 38};
            break;
        case Widget::RaceTimer:
            r = {164, 14, 110, 24};
            break;
        case Widget::SilverCoins:
            r = {25, 58, 24, 54};
            break;
        case Widget::ChallengePortrait:
            r = {243, 8, 48, 38};
            break;
        case Widget::Treasure:
        case Widget::EggChallenge:
            r = {239, 42, 60, 30};
            break;
        case Widget::BattleBananas:
            r = {240, 40, 70, 34};
            break;
        case Widget::CentreMessage:
        case Widget::LapMessage:
        case Widget::WrongWay:
            r = {68, 32, 184, 28};
            break;
        default:
            if (r.y > 115)
                r.y *= .5F;
            break;
        }
        if (target == Target::Bottom)
            r.y += 108; // retail 2P preset spacing
    } else {
        if (scenario == Scenario::Boss) {
            if (widget == Widget::BananaCounter)
                r.x -= 120;
            if (widget == Widget::LapCounter)
                r.x += 28;
        } else if (scenario == Scenario::TimeTrial) {
            if (widget == Widget::BananaCounter)
                r.x -= 25;
            if (widget == Widget::LapCounter)
                r.x -= 58;
        }
    }
    return r;
}
inline int native_anchor(Scenario scenario, Target target, Widget widget) {
    const Rect r = bounds(scenario, target, widget);
    const float x = r.x + r.w / 2, y = r.y + r.h / 2 - (target == Target::Bottom ? 108 : 0);
    return (y < (split(target) ? 40 : 80)    ? 0
            : y > (split(target) ? 80 : 160) ? 6
                                             : 3) +
           (x < 106   ? 0
            : x > 214 ? 2
                      : 1);
}
inline const Placement& placement(const Layout& layout, Scenario s, Target t, Widget w, float aspect) {
    const auto& specific = layout.entries[index(aspect_family(aspect), s, t, w)];
    return specific.enabled ? specific : layout.entries[index(Aspect::All, s, t, w)];
}
inline Transform transform(const Layout& layout, Scenario scenario, Target target, Widget widget, float aspect) {
    if (!editable(widget))
        return {};
    const Rect b = bounds(scenario, target, widget);
    const Rect c = canvas(target, aspect);
    const Placement p =
        layout.mode == LayoutMode::Custom ? placement(layout, scenario, target, widget, aspect) : Placement{};
    // Bound the whole composite, never its individual members. Long shared
    // challenge strips cannot grow through the viewport edge at 150%.
    const float scale =
        std::min(safe_scale(p.scale == 0 ? layout.scale : p.scale), std::min((c.w - 8) / b.w, (c.h - 8) / b.h));
    const int anchor = p.anchor < 0 ? native_anchor(scenario, target, widget) : p.anchor;
    const float ax = (anchor % 3) * .5F, ay = (anchor / 3) * .5F;
    const Point pivot{b.x + b.w * ax, b.y + b.h * ay};
    float dx = 0, dy = 0;
    if (layout.mode == LayoutMode::FitToViewport || (layout.mode == LayoutMode::Custom && layout.custom_fill))
        dx = (ax * 2 - 1) * (c.w - 320) * .5F;
    if (layout.mode == LayoutMode::SafeArea) {
        dx = (1 - ax * 2) * 6;
        dy = (1 - ay * 2) * 4;
    }
    dx += p.x * c.w;
    dy += p.y * c.h;
    // User placement is constrained by the complete resting envelope. Native
    // animation can leave this envelope; it is NOT clamped/frozen mid-animation.
    if (layout.mode == LayoutMode::Custom) {
        const float left = pivot.x + (b.x - pivot.x) * scale;
        const float top = pivot.y + (b.y - pivot.y) * scale;
        const float max_x = c.x + c.w - 4 - left - b.w * scale;
        const float max_y = c.y + c.h - 4 - top - b.h * scale;
        dx = std::clamp(dx, std::min(c.x + 4 - left, max_x), max_x);
        dy = std::clamp(dy, std::min(c.y + 4 - top, max_y), max_y);
    }
    return {scale, pivot.x * (1 - scale) + dx, pivot.y * (1 - scale) + dy, c.y, c.y + c.h};
}
inline void reset_scenario(Layout& l, Aspect a, Scenario s, Target t) {
    for (std::size_t i = 0; i < kWidgets; ++i)
        l.entries[index(a, s, t, static_cast<Widget>(i))] = {};
}
inline Transform entry_transform(Transform transform, Rect resting, Rect viewport, float authored_slide) {
    if (!std::isfinite(authored_slide) || authored_slide <= 0)
        return transform;
    const float left = transform.apply({resting.x, resting.y}).x;
    const float extra = std::max(0.0F, viewport.x + viewport.w + 4 - left - transform.scale * 320);
    transform.x += extra * std::clamp(authored_slide / 320, 0.0F, 1.0F);
    return transform;
}
inline void copy_target(Layout& l, Aspect a, Scenario s, Target source, Target destination) {
    if (!split(source) || !split(destination))
        return;
    for (std::size_t i = 0; i < kWidgets; ++i)
        l.entries[index(a, s, destination, static_cast<Widget>(i))] =
            l.entries[index(a, s, source, static_cast<Widget>(i))];
}
} // namespace dkr::runtime::hud::groups
