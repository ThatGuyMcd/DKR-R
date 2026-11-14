#include "hud_group_rt64.hpp"
#include "hud_layout_config.hpp"
#include "hud_rectangle_layout.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
#include "hud_reference_rt64_tests.hpp"

namespace hg = dkr::runtime::hud::groups;
using dkr::runtime::hud::Widget;
using dkr::runtime::hud::LayoutMode;
static void near(float a, float b) {
    assert(std::abs(a - b) < .002F);
}
int main() {
    hud_reference_test::run();
    hg::Layout l;
    for (float scale : {.5F, 1.F, 1.5F})
        for (int sign : {-1, 1}) {
            const hg::Transform t{scale, -72, 14};
            const auto rect =
                hg::transform_rectangle(t, 1024 + 80, 512 + 40, 1024 + 144, 512 + 104, sign * 1024, 1024, 3);
            near(float(rect.ulx), 80 * scale - 288);
            near(float(rect.lrx), 144 * scale - 288);
            near(float(rect.uly), 40 * scale + 56);
            near(float(rect.lry), 104 * scale + 56);
            assert(rect.ulx < 0); // signed widescreen destination; no unsigned wrap
            assert(std::abs(float(rect.dsdx) * scale - sign * 1024) < .6F);
            assert(rect.lrx - rect.ulx == int(64 * scale));
        }
    assert(hg::valid(l));
    assert(hg::decode(hg::encode(l)) == l);
    l.scale = std::numeric_limits<float>::quiet_NaN();
    assert(!hg::valid(l));
    l = {};
    auto bad = hg::encode(l);
    bad["version"] = 100;
    assert(!hg::decode(bad));
    bad = hg::encode(l);
    bad["mode"] = "2";
    assert(!hg::decode(bad));
    assert(static_cast<int>(LayoutMode::SafeArea) == 1);
    assert(static_cast<int>(LayoutMode::Custom) == 3);
    for (int players = 1; players <= 4; ++players)
        for (int layout = 0; layout <= 3; ++layout)
            assert(hg::eligible(true, players, layout, layout) == (players <= 2 && layout <= 1));
    assert(!hg::eligible(false, 1, 0, 0));
    assert(!hg::eligible(true, 4, 0, 0)); // temporary single-camera postrace
    assert(hg::scenario_for(8, false) == hg::Scenario::Boss);
    assert(hg::scenario_for(5, false) == hg::Scenario::Adventure);
    assert(hg::scenario_for(0, true) == hg::Scenario::TimeTrial);
    assert(!hg::available(hg::Scenario::Race, hg::Target::Top, Widget::Minimap));
    assert(hg::available(hg::Scenario::Race, hg::Target::Shared, Widget::Minimap));
    l.mode = LayoutMode::Custom;
    auto& p = l.entries[hg::index(hg::Aspect::All, hg::Scenario::Race, hg::Target::Top, Widget::Weapon)];
    p = {.13F, .05F, 1.2F, 6, true};
    hg::copy_target(l, hg::Aspect::All, hg::Scenario::Race, hg::Target::Top, hg::Target::Bottom);
    assert(l.entries[hg::index(hg::Aspect::All, hg::Scenario::Race, hg::Target::Bottom, Widget::Weapon)] == p);
    assert(hg::decode(hg::encode(l)) == l);
    auto& wide = l.entries[hg::index(hg::Aspect::Wide, hg::Scenario::Race, hg::Target::Top, Widget::Weapon)];
    wide = {-.05F, .1F, .75F, 0, true};
    assert(hg::placement(l, hg::Scenario::Race, hg::Target::Top, Widget::Weapon, 16.0F / 9) == wide);
    assert(hg::placement(l, hg::Scenario::Race, hg::Target::Top, Widget::Weapon, 16.0F / 10) == p);
    assert(hg::decode(hg::encode(l)) == l);
    std::size_t checks = 0;
    for (float aspect : {4.0F / 3, 16.0F / 10, 16.0F / 9, 21.0F / 9, 32.0F / 9})
        for (float size : {.5F, 1.0F, 1.5F})
            for (std::size_t scene = 0; scene < hg::kScenarios; ++scene)
                for (std::size_t target = 0; target < hg::kTargets; ++target)
                    for (std::size_t widget = 0; widget < hg::kWidgets; ++widget) {
                        const auto s = static_cast<hg::Scenario>(scene), unused = s;
                        (void)unused;
                        const auto t = static_cast<hg::Target>(target);
                        const auto w = static_cast<Widget>(widget);
                        if (!hg::available(s, t, w))
                            continue;
                        hg::Layout test;
                        test.mode = LayoutMode::Custom;
                        test.scale = size;
                        const auto transform = hg::transform(test, s, t, w, aspect);
                        assert(transform.valid());
                        const auto b = hg::bounds(s, t, w), c = hg::canvas(t, aspect);
                        const auto a = transform.apply({b.x, b.y}), z = transform.apply({b.x + b.w, b.y + b.h});
                        const auto parked = hg::entry_transform(transform, b, c, 320);
                        if (w != Widget::Speedometer)
                            assert(parked.apply({b.x + 320, b.y}).x >= c.x + c.w + 3.99F);
                        assert(hg::entry_transform(transform, b, c, 0).x == transform.x);
                        near(z.x - a.x, b.w * transform.scale);
                        near(z.y - a.y, b.h * transform.scale);
                        // Native dimensions may be larger than a short split viewport; never
                        // clamp animation independently or distort the oversized group.
                        assert(a.x >= c.x + 3.99F);
                        assert(z.x <= c.x + c.w - 3.99F);
                        assert(a.y >= c.y + 3.99F);
                        assert(z.y <= c.y + c.h - 3.99F);
                        // Verify the actual production clip-matrix adapter agrees with the
                        // texture-rectangle affine, including widescreen aspect correction.
                        const auto identity = hlslpp::float4x4::identity();
                        auto viewport = interop::RSPViewport::identity();
                        viewport.scale = {160, 120, 511};
                        viewport.translate = {160, 120, 511};
                        auto m = hlslpp::float4x4::identity();
                        const float x = b.x + b.w * .35F, y = b.y + b.h * .65F;
                        m[3][0] = (x - 160) / 160;
                        m[3][1] = -(y - 120) / 120;
                        const auto transformed = hg::transform_mvp(m, viewport, transform);
                        const auto v = hlslpp::mul(hlslpp::float4(0, 0, 0, 1), transformed);
                        const auto original_anchor = hlslpp::mul(hlslpp::float4(0, 0, 0, 1), m);
                        const auto restored_anchor = hg::untransform_anchor(v, viewport, transform);
                        near(float(restored_anchor.x), float(original_anchor.x));
                        near(float(restored_anchor.y), float(original_anchor.y));
                        // The sprite's quad is assembled relative to the RAW anchor, then
                        // transformed once; its centre must remain registered with the map.
                        auto billboard = hlslpp::float4x4::identity();
                        billboard[3] = restored_anchor;
                        const auto billboard_mvp = hg::transform_mvp(billboard, viewport, transform);
                        const auto centre = hlslpp::mul(hlslpp::float4(0, 0, 0, 1), billboard_mvp);
                        near(float(centre.x), float(v.x));
                        near(float(centre.y), float(v.y));
                        const float cover = aspect / (4.0F / 3);
                        const auto expected = transform.apply({x, y});
                        near((160 + static_cast<float>(v.x) / cover * 160) * cover, 160 * (cover - 1) + expected.x);
                        near(120 - static_cast<float>(v.y) * 120, expected.y);
                        near(static_cast<float>(v.z), 0);
                        near(static_cast<float>(v.w), 1);
                        near(m[3][0], (x - 160) / 160); // input matrix is immutable
                        ++checks;
                    }
    std::printf("HUD group policy/config/production matrix checks: %zu cases passed\n", checks);
}
