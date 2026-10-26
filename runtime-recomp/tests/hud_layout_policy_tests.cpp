#include "hud_layout_policy.hpp"
#include "runtime_hud_layout.hpp"
#include "virtual_pak_policy.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace dkr::runtime;

int main() {
    for (int layout : {0, 1, 2, 3}) {
        for (int owner = -1; owner <= 4; ++owner) {
            for (std::size_t element = 0; element < 59; ++element) {
                const bool expected =
                    layout >= 2 && owner >= 0 && owner < 4 && (element < 2 || element == 2 || element == 49);
                const auto anchor = hud::quadrant_counter_anchor(true, layout, owner, element);
                assert((anchor != 0) == expected);
                if (expected)
                    assert(anchor == ((owner & 1) ? hud::kHudAnchorRight : hud::kHudAnchorLeft));
                assert(hud::quadrant_counter_anchor(false, layout, owner, element) == 0);
            }
        }
    }
    static_assert(hud::kElements.size() == 59);
    static_assert(hud::kSupplementalElements.size() == 2);
    static_assert(hud::clamp_hud_scale(0.1F) == 0.5F);
    static_assert(hud::clamp_hud_scale(1.0F) == 1.0F);
    static_assert(hud::clamp_hud_scale(2.0F) == 1.5F);
    static_assert(hud::anchor_gutter_delta(hud::Anchor::Centre, 16.0F / 9.0F) == 0.0F);
    static_assert(hud::anchor_gutter_delta(hud::Anchor::TopLeft, 16.0F / 9.0F) < 0.0F);
    static_assert(hud::anchor_gutter_delta(hud::Anchor::BottomRight, 16.0F / 9.0F) > 0.0F);
    assert(std::fabs(hud::split_gutter_authored(16.0F / 9.0F) - (80.0F / 3.0F)) < 0.0001F);
    assert(std::fabs(hud::fullscreen_gutter_authored(16.0F / 9.0F) - (160.0F / 3.0F)) < 0.0001F);
    assert(std::fabs(hud::hud_viewport_horizontal_cover(16.0F / 9.0F) - (4.0F / 3.0F)) < 0.0001F);
    static_assert(hud::hud_viewport_horizontal_cover(4.0F / 3.0F) == 1.0F);
    constexpr auto encoded_cover = hud::encode_hud_viewport_cover(16.0F / 9.0F);
    static_assert(encoded_cover == 1365U);
    assert(std::fabs(hud::decode_hud_viewport_cover(encoded_cover) - (1365.0F / 1024.0F)) < 0.0001F);
    static_assert(hud::decode_hud_viewport_clip_ratio(0U) == 1);
    static_assert(hud::decode_hud_viewport_clip_ratio(1024U) == 1);
    static_assert(hud::decode_hud_viewport_clip_ratio(encoded_cover) == 2);
    static_assert(hud::decode_hud_viewport_clip_ratio(2731U) == 3);
    static_assert(hud::hud_scissor_origin_offset(hud::kExtendedOriginNone, 320U) == 0);
    static_assert(hud::hud_scissor_origin_offset(0U, 320U) == 0);
    static_assert(hud::hud_scissor_origin_offset(0x200U, 320U) == 640);
    static_assert(hud::hud_scissor_origin_offset(hud::kExtendedOriginRight, 320U) == 1280);
    static_assert(hud::rebase_hud_scissor_edge(0, hud::kExtendedOriginNone, 0U, 320U) == 0);
    static_assert(hud::rebase_hud_scissor_edge(1280, hud::kExtendedOriginNone, hud::kExtendedOriginRight, 320U) ==
                  2560);
    static_assert(hud::rebase_hud_scissor_edge(2560, hud::kExtendedOriginRight, hud::kExtendedOriginRight, 320U) ==
                  2560);
    static_assert(hud::rebase_hud_scissor_edge(1920, 0x200U, hud::kExtendedOriginRight, 320U) == 2560);
    static_assert(hud::split_gutter_authored(4.0F / 3.0F) == 0.0F);
    static_assert(hud::fullscreen_gutter_authored(4.0F / 3.0F) == 0.0F);
    static_assert(hud::viewport_class_for_layout(0) == hud::ViewportClass::FullWidth);
    static_assert(hud::viewport_class_for_layout(1) == hud::ViewportClass::FullWidth);
    static_assert(hud::viewport_class_for_layout(2) == hud::ViewportClass::Quadrant);
    static_assert(hud::viewport_class_for_layout(3) == hud::ViewportClass::Quadrant);
    static_assert(hud::placement_delta_x(hud::LayoutMode::Original, hud::Anchor::TopLeft, 16.0F / 9.0F,
                                         hud::ViewportClass::FullWidth) == 0.0F);
    static_assert(hud::placement_delta_x(hud::LayoutMode::SafeArea, hud::Anchor::TopLeft, 16.0F / 9.0F,
                                         hud::ViewportClass::FullWidth) == 6.0F);
    static_assert(hud::placement_delta_x(hud::LayoutMode::SafeArea, hud::Anchor::TopRight, 16.0F / 9.0F,
                                         hud::ViewportClass::FullWidth) == -6.0F);
    assert(std::fabs(hud::placement_delta_x(hud::LayoutMode::FitToViewport, hud::Anchor::TopLeft, 16.0F / 9.0F,
                                            hud::ViewportClass::FullWidth) +
                     (160.0F / 3.0F)) < 0.0001F);
    assert(std::fabs(hud::placement_delta_x(hud::LayoutMode::FitToViewport, hud::Anchor::BottomRight, 16.0F / 9.0F,
                                            hud::ViewportClass::Quadrant) -
                     (80.0F / 3.0F)) < 0.0001F);
    static_assert(hud::placement_delta_x(hud::LayoutMode::FitToViewport, hud::Anchor::BottomRight, 4.0F / 3.0F,
                                         hud::ViewportClass::Quadrant) == 0.0F);

    static_assert(hud::uses_live_horizontal_anchor(32U));
    static_assert(hud::uses_live_horizontal_anchor(50U));
    static_assert(hud::uses_live_horizontal_anchor(51U));
    static_assert(!hud::uses_live_horizontal_anchor(46U));
    static_assert(hud::effective_anchor(32U, 25.0F) == hud::Anchor::TopLeft);
    static_assert(hud::effective_anchor(32U, 247.0F) == hud::Anchor::TopRight);
    static_assert(hud::effective_anchor(50U, 43.0F) == hud::Anchor::TopLeft);
    static_assert(hud::effective_anchor(50U, 263.0F) == hud::Anchor::TopRight);
    static_assert(hud::effective_anchor(46U, 250.0F) == hud::Anchor::TopLeft);
    static_assert(hud::effective_anchor(57U, 30.0F) == hud::Anchor::TopRight);
    static_assert(hud::kElements[0].anchor == hud::Anchor::TopLeft);
    static_assert(hud::kElements[1].anchor == hud::Anchor::TopLeft);

    struct ExpectedWidget {
        hud::Widget widget;
        std::size_t members;
    };
    constexpr std::array<ExpectedWidget, 25> kExpectedWidgets{{
        {hud::Widget::RacePosition, 2U},    {hud::Widget::Weapon, 2U},
        {hud::Widget::LapCounter, 5U},      {hud::Widget::BananaCounter, 6U},
        {hud::Widget::RaceTimer, 2U},       {hud::Widget::CentreMessage, 3U},
        {hud::Widget::Minimap, 2U},         {hud::Widget::Magnet, 1U},
        {hud::Widget::GoldenBalloon, 4U},   {hud::Widget::LapTimer, 3U},
        {hud::Widget::Stopwatch, 2U},       {hud::Widget::LapMessage, 3U},
        {hud::Widget::Treasure, 1U},        {hud::Widget::CourseArrows, 1U},
        {hud::Widget::WrongWay, 2U},        {hud::Widget::ProAm, 1U},
        {hud::Widget::Speedometer, 8U},     {hud::Widget::SilverCoins, 1U},
        {hud::Widget::ChallengeFinish, 2U}, {hud::Widget::ChallengePortrait, 1U},
        {hud::Widget::EggChallenge, 1U},    {hud::Widget::BattleBananas, 4U},
        {hud::Widget::RaceFinish, 2U},      {hud::Widget::TwoPlayerPortrait, 1U},
        {hud::Widget::TimerGlyphs, 1U},
    }};
    std::size_t certified_members = 0U;
    for (const auto& expected : kExpectedWidgets) {
        assert(hud::widget_member_count(expected.widget) == expected.members);
        assert(hud::widget_members_share_anchor(expected.widget));
        certified_members += expected.members;
    }
    assert(certified_members == hud::kElements.size() + hud::kSupplementalElements.size());

    // The composites called out by widescreen regressions are certified as
    // indivisible groups with one common anchor.
    static_assert(hud::kElements[19].widget == hud::Widget::GoldenBalloon);
    static_assert(hud::kElements[20].widget == hud::Widget::GoldenBalloon);
    static_assert(hud::kElements[21].widget == hud::Widget::GoldenBalloon);
    static_assert(hud::kElements[22].widget == hud::Widget::GoldenBalloon);
    static_assert(hud::kElements[38].widget == hud::Widget::Speedometer);
    static_assert(hud::kElements[45].widget == hud::Widget::Speedometer);
    static_assert(hud::kElements[10].widget == hud::Widget::RaceTimer);
    static_assert(hud::kElements[11].widget == hud::Widget::RaceTimer);
    static_assert(hud::kElements[15].widget == hud::Widget::Minimap);
    static_assert(hud::kSupplementalElements[0].widget == hud::Widget::Minimap);

    constexpr std::array<const char*, 59> kExpectedHudAbiIds{{
        "race.position",
        "race.position.end",
        "weapon.display",
        "lap.label",
        "lap.current",
        "lap.separator",
        "lap.total",
        "banana.icon.spin",
        "banana.number.ones",
        "banana.number.tens",
        "time.label",
        "time.value",
        "start.go",
        "start.ready",
        "finish.banner",
        "minimap.marker",
        "lap.flag",
        "weapon.magnet",
        "banana.x",
        "balloon.icon",
        "balloon.x",
        "balloon.number.ones",
        "balloon.number.tens",
        "lap.time",
        "trial.lap.label",
        "trial.lap.value",
        "stopwatch.hands",
        "banana.icon.static",
        "banana.sparkle",
        "lap.final",
        "lap.word",
        "lap.two",
        "challenge.treasure",
        "course.arrows",
        "stopwatch",
        "wrongway.first",
        "wrongway.second",
        "proam.logo",
        "speed.arrow",
        "speed.zero",
        "speed.thirty",
        "speed.sixty",
        "speed.ninety",
        "speed.120",
        "speed.150",
        "speed.background",
        "challenge.silver.coins",
        "challenge.finish.first",
        "challenge.finish.second",
        "weapon.quantity",
        "challenge.portrait",
        "challenge.egg",
        "battle.banana.icon",
        "battle.banana.x",
        "battle.banana.ones",
        "battle.banana.tens",
        "race.finish.first",
        "twoplayer.portrait",
        "race.finish.second",
    }};

    for (std::size_t left = 0; left < hud::kElements.size(); ++left) {
        assert(hud::kElements[left].id == kExpectedHudAbiIds[left]);
        assert(!hud::kElements[left].id.empty());
        assert(!hud::kElements[left].label.empty());
        for (std::size_t right = left + 1; right < hud::kElements.size(); ++right) {
            assert(hud::kElements[left].id != hud::kElements[right].id);
        }
    }
    assert(hud::kSupplementalElements[0].id == "minimap.background");
    assert(hud::kSupplementalElements[1].id == "timer.glyphs");
    static_assert(hud::kSupplementalElements[0].shared);
    static_assert(!hud::kSupplementalElements[1].shared);

    static_assert(static_cast<int>(hud::LayoutMode::Original) == 0);
    static_assert(static_cast<int>(hud::LayoutMode::SafeArea) == 1);
    static_assert(static_cast<int>(hud::LayoutMode::FitToViewport) == 2);

    constexpr std::array<bool, 4> none{};
    constexpr std::array<bool, 4> all{true, true, true, true};
    static_assert(pak::policy::connected_port_mask(false, all, all) == 0U);
    static_assert(pak::policy::connected_port_mask(true, none, none) == 1U);
    static_assert(pak::policy::connected_port_mask(true, all, all) == 0x0FU);
    constexpr std::array<bool, 4> assigned{false, true, true, false};
    constexpr std::array<bool, 4> connected{false, true, false, true};
    static_assert(pak::policy::connected_port_mask(true, assigned, connected) == 0x03U);
    constexpr std::array<bool, 4> rumble{true, true, true, true};
    static_assert(pak::policy::rumble_port_mask(all, all, rumble) == 0x0FU);
    static_assert(pak::policy::combined_rumble_mask(true, 0x01U, all, all, rumble) == 0x0FU);
    static_assert(pak::policy::combined_rumble_mask(false, 0x0FU, all, all, rumble) == 0U);
    constexpr std::array<bool, 4> mixed_rumble{true, false, true, false};
    static_assert(pak::policy::rumble_port_mask(all, all, mixed_rumble) == 0x05U);
    static_assert(pak::policy::preferred_memory_pak_status(true, true, true) == pak::policy::kControllerPakGood);
    static_assert(pak::policy::preferred_memory_pak_status(true, true, false) == pak::policy::kControllerPakBadData);
    static_assert(pak::policy::preferred_memory_pak_status(false, true, true) == pak::policy::kUseRetailAccessoryProbe);
    static_assert(pak::policy::preferred_memory_pak_status(true, false, true) == pak::policy::kUseRetailAccessoryProbe);
    static_assert(pak::policy::expose_rumble_pak(true, true));
    static_assert(!pak::policy::expose_rumble_pak(false, true));
    static_assert(!pak::policy::expose_rumble_pak(true, false));
    // Race-position bounce animates scale away from one. Those immediate
    // frames must keep the original XLU sentinel, never become OPA draws.
    for (float scale : {0.5F, 0.75F, 1.0F, 1.01F, 1.25F, 1.5F, 2.0F}) {
        assert(hud::counter_requires_immediate_texture_draw(scale, 0xFFFFFFFEU) == (scale == 1.0F));
        assert(!hud::counter_requires_immediate_texture_draw(scale, 0xFFFFFFFFU));
        assert(!hud::counter_requires_immediate_texture_draw(scale, 0xFFFFFF80U));
        assert(!hud::counter_requires_immediate_texture_draw(scale, 0xFFFFFF00U));
    }
    // Respect the effective scale when the selected HUD scale is not one.
    assert(!hud::counter_requires_immediate_texture_draw(1.0F * 1.25F, 0xFFFFFFFEU));
    assert(hud::counter_requires_immediate_texture_draw(0.5F * 2.0F, 0xFFFFFFFEU));
    std::puts("[test][hud-layout-policy] PASS");
    return 0;
}
