#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/game/mobile_touch_policy.hpp"
#include <cassert>
#include <limits>
using namespace dkr::runtime::mobile;
int main() {
    Contacts c;
    Target stick{{.05F, .4F, .2F, .4F}, 0, true, false};
    Target a{{.7F, .7F, .1F, .1F}, 0x8000}, r{{.7F, .1F, .1F, .1F}, 0x10};
    assert(c.down(71, {.15F, .6F}, stick));
    assert(!c.down(81, {.15F, .6F}, stick));
    assert(c.down(902, {.75F, .75F}, a));
    assert(c.down(19, {.75F, .15F}, r));
    auto s = c.sample();
    assert(s.buttons == 0x8010 && s.stick_owned && std::abs(s.x) < .001F);
    c.move(71, {.23F, .6F});
    s = c.sample();
    assert(s.x > .99F && std::abs(s.y) < .001F);
    c.move(71, {.15F, .44F});
    s = c.sample();
    assert(s.y > .99F && std::abs(s.x) < .001F);
    c.move(71, {1, 0});
    s = c.sample();
    assert(std::hypot(s.x, s.y) <= 1.001F);
    c.up(902);
    assert(c.sample().buttons == 0x10); // Releasing one finger must not release another.
    c.move(19, {0, 0});
    assert(c.sample().buttons == 0); // Drag off releases a button.
    c.clear();
    assert(!c.sample().stick_owned && c.sample().buttons == 0);
    assert(c.down(2, {.1F, .5F}, stick, true));
    assert(std::abs(c.sample().x) < .001F);
    c.clear();
    for (int i = 0; i < 16; ++i)
        assert(c.down(i, {.75F, .75F}, a));
    assert(!c.down(20, {.75F, .75F}, a));
    c.clear();
    assert(c.sample().buttons == 0);
    auto l = classic();
    l[0].x = std::numeric_limits<float>::quiet_NaN();
    l[1].size = 100;
    l[8].visible = false;
    validate(l);
    assert(std::isfinite(l[0].x) && l[1].size == 1.5F && l[8].visible);
    for (auto safe : {Rect{20, 10, 740, 320}, Rect{0, 0, 300, 200}})
        for (const auto& p : l) {
            const auto rect = place(safe, {144, 144}, p);
            assert(rect.x >= safe.x && rect.y >= safe.y);
            assert(rect.x + rect.w <= safe.x + safe.w + .01F && rect.y + rect.h <= safe.y + safe.h + .01F);
        }
    const auto left = classic(true), right = classic();
    assert(std::abs(left[0].x + right[0].x - 1) < .001F);
}
