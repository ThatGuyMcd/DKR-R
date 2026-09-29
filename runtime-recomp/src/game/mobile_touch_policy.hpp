#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dkr::runtime::mobile {
struct Point {
    float x = 0, y = 0;
};
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(Point p) const {
        return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
    }
};
struct Placement {
    float x = .5F, y = .5F, size = 1, opacity = .75F;
    bool visible = true;
};
inline constexpr int group_count = 9;
using Layout = std::array<Placement, group_count>;
inline Layout classic(bool left = false) {
    Layout l{{{.16F, .76F, 1, .75F, true},
              {.86F, .77F, 1, .8F, true},
              {.84F, .40F, 1, .75F, true},
              {.16F, .37F, 1, .6F, true},
              {.31F, .65F, 1, .8F, true},
              {.08F, .12F, 1, .7F, true},
              {.92F, .12F, 1, .7F, true},
              {.50F, .88F, 1, .7F, true},
              {.50F, .10F, 1, .8F, true}}};
    if (left)
        for (auto& p : l)
            p.x = 1 - p.x;
    return l;
}
inline void validate(Layout& l) {
    const auto defaults = classic();
    for (int i = 0; i < group_count; ++i) {
        auto& p = l[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.size) || !std::isfinite(p.opacity))
            p = defaults[i];
        p.x = std::clamp(p.x, 0.0F, 1.0F);
        p.y = std::clamp(p.y, 0.0F, 1.0F);
        p.size = std::clamp(p.size, 1.0F, 1.5F);
        p.opacity = std::clamp(p.opacity, .25F, 1.0F);
    }
    l[8].visible = true; // An escape route cannot be removed by a bad layout.
}
inline Rect place(Rect safe, Point extent, const Placement& p) {
    const float w = std::min(extent.x * p.size, safe.w), h = std::min(extent.y * p.size, safe.h);
    return {safe.x + std::clamp(p.x * safe.w - w / 2, 0.0F, std::max(0.0F, safe.w - w)),
            safe.y + std::clamp(p.y * safe.h - h / 2, 0.0F, std::max(0.0F, safe.h - h)), w, h};
}
struct Target {
    Rect rect;
    std::uint16_t mask = 0;
    bool stick = false, menu = false;
};
struct Sample {
    std::uint16_t buttons = 0;
    float x = 0, y = 0;
    bool stick_owned = false;
};
class Contacts {
    struct Contact {
        bool used = false;
        std::int64_t id = 0;
        Target target;
        Point point, origin;
    };
    std::array<Contact, 16> contacts_{};

  public:
    void clear() {
        contacts_ = {};
    }
    bool down(std::int64_t id, Point point, const Target& target, bool floating = false) {
        for (const auto& c : contacts_)
            if (c.used && (c.id == id || (c.target.stick && target.stick)))
                return false;
        for (auto& c : contacts_)
            if (!c.used) {
                c = {true, id, target, point,
                     floating ? point : Point{target.rect.x + target.rect.w / 2, target.rect.y + target.rect.h / 2}};
                return true;
            }
        return false;
    }
    void move(std::int64_t id, Point p) {
        for (auto& c : contacts_)
            if (c.used && c.id == id)
                c.point = p;
    }
    void up(std::int64_t id) {
        for (auto& c : contacts_)
            if (c.used && c.id == id)
                c.used = false;
    }
    Sample sample(float deadzone = .12F, float sensitivity = 1) const {
        Sample out;
        for (const auto& c : contacts_)
            if (c.used) {
                if (c.target.stick) {
                    out.stick_owned = true;
                    const float radius = std::max(.0001F, c.target.rect.w * .38F);
                    float x = (c.point.x - c.origin.x) / radius,
                          y = (c.origin.y - c.point.y) / std::max(.0001F, c.target.rect.h * .38F);
                    float length = std::hypot(x, y);
                    if (length > deadzone) {
                        float mag = std::clamp((length - deadzone) / (1 - deadzone) * sensitivity, 0.0F, 1.0F);
                        out.x = x / length * mag;
                        out.y = y / length * mag;
                    }
                } else if (c.target.rect.contains(c.point))
                    out.buttons |= c.target.mask;
            }
        return out;
    }
};
} // namespace dkr::runtime::mobile
