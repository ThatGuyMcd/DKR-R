#pragma once
#include "polish_symbols.hpp"
#include "revision_addresses.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_rdp.h"
#include "hle/rt64_rsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace dkr::runtime::presentation {
// Recognise only the actual white SMALLFONT copyright run in MENU_LOGOS.
// Font texels, metrics and load commands come from this immutable task.
class IntroCaption {
    struct Glyph {
        const RT64::DisplayList* command = nullptr;
        int x = 0, y = 0, w = 0, h = 0, s = 0, t = 0, atlas = 0;
    };
    std::array<Glyph, 64> glyphs_{};
    std::array<const RT64::DisplayList*, 32> original_{};
    unsigned count_ = 0, original_count_ = 0;
    const std::uint8_t* ram_ = nullptr;
    const std::array<std::uint32_t, 8>* symbols_ = nullptr;
    std::uint32_t font_ = 0;
    bool enabled_ = false, drawn_ = false;
    int left_ = 0, top_ = 0, right_ = 0, bottom_ = 0;
    float cover_ = 1;
    static constexpr std::string_view copyright_ = "(C) COPYRIGHT RARE 1997";
    static constexpr std::string_view credit_ = "RECOMPILED BY THATGUYMCD!";
    static bool valid(std::uint32_t p, std::uint64_t n) {
        p &= 0x1FFFFFFFU;
        return p > 0 && p < 0x800000 && n <= 0x800000U - p;
    }
    unsigned byte(std::uint32_t p) const {
        return ram_[(p & 0x7FFFFF) ^ 3];
    }
    unsigned half(std::uint32_t p) const {
        std::uint16_t v;
        std::memcpy(&v, ram_ + ((p & 0x7FFFFF) ^ 2), 2);
        return v;
    }
    std::uint32_t word(std::uint32_t p) const {
        std::uint32_t v;
        std::memcpy(&v, ram_ + (p & 0x7FFFFC), 4);
        return v;
    }
    bool texture(unsigned atlas) const {
        if (atlas >= 32)
            return false;
        const auto header = word(font_ + 0x80 + atlas * 4);
        if (!valid(header, 32))
            return false;
        const auto commands = word(header + 12), count = half(header + 10);
        if (!count || count > 32 || !valid(commands, std::uint64_t(count) * 8))
            return false;
        for (unsigned i = 0; i < count; ++i) {
            const auto p = commands + i * 8, op = word(p) >> 24;
            if (op != 0xFD && op != 0xF5 && op != 0xF2 && op != 0xF3 && op != 0xF4 && op != 0xF0 && op != 0xE6 &&
                op != 0xE7 && op != 0xE8)
                return false;
            if (op == 0xFD && !valid(word(p + 4), 1))
                return false;
        }
        return true;
    }
    int width(std::string_view text) const {
        int result = 0;
        const bool compact = word((*symbols_)[4]) != 0;
        for (unsigned char ch : text) {
            const auto p = font_ + 0x100 + (ch - 32) * 8;
            int step = ch == 32 ? half(font_ + 0x24) : half(font_ + 0x20) ? half(font_ + 0x20) : byte(p + 1);
            if (compact && step)
                --step;
            result += step;
        }
        return result;
    }
    bool line(std::string_view text, int y) {
        int x = 160 - (width(text) >> 1);
        const bool compact = word((*symbols_)[4]) != 0;
        for (unsigned char ch : text) {
            if (ch == 32) {
                x += half(font_ + 0x24);
                continue;
            }
            const auto p = font_ + 0x100 + (ch - 32) * 8;
            const auto atlas = byte(p);
            if (count_ >= glyphs_.size() || !texture(atlas) || !byte(p + 6) || !byte(p + 7))
                return false;
            auto& g = glyphs_[count_++];
            g = {nullptr,          x + int(byte(p + 2)),  y + int(byte(p + 3)),  int(byte(p + 6)),
                 int(byte(p + 7)), int(byte(p + 4)) * 32, int(byte(p + 5)) * 32, int(atlas)};
            left_ = std::min(left_, g.x);
            top_ = std::min(top_, g.y);
            right_ = std::max(right_, g.x + g.w);
            bottom_ = std::max(bottom_, g.y + g.h);
            int step = half(font_ + 0x20) ? half(font_ + 0x20) : byte(p + 1);
            if (compact && step)
                --step;
            x += step;
        }
        return true;
    }

  public:
    void begin(const RT64::State& state, bool modern, float aspect) {
        enabled_ = false;
        count_ = original_count_ = 0;
        drawn_ = false;
        ram_ = state.RDRAM;
        if (!modern || word(revision_addresses::CurrentMenuId) != 1)
            return;
        cover_ = std::clamp(std::isfinite(aspect) ? aspect / (4.F / 3.F) : 1.F, 1.F, 8.F);
        symbols_ =
            &polish_symbols[revision_addresses::CurrentMenuId == revision_addresses::kUsV80.CurrentMenuId ? 1 : 0];
        const auto fonts = word((*symbols_)[2]);
        if (word((*symbols_)[3]) < 3 || word((*symbols_)[3]) > 32 || !valid(fonts, 3 * 0x400))
            return;
        font_ = fonts + 0x400; // SMALLFONT = 1.
        if (!half(font_ + 0x22) || half(font_ + 0x22) > 20)
            return;
        for (unsigned i = 0; i < copyright_.size(); ++i)
            if (byte((*symbols_)[5] + i) != unsigned(copyright_[i]))
                return;
        enabled_ = true;
    }
    void prepare(RT64::State& state, const RT64::DisplayList* command) {
        if (!enabled_ || command->p0(16, 1) ||
            (state.rsp->fromSegmented(command->w1) & 0x1FFFFFFFU) != ((*symbols_)[0] & 0x1FFFFFFFU))
            return;
        count_ = original_count_ = 0;
        drawn_ = false;
        const auto offset = reinterpret_cast<const std::uint8_t*>(command) - ram_;
        if (offset < 0 || std::uint64_t(offset) + 8 > 0x800000 || (offset & 7))
            return;
        bool modes = false, ended = false;
        unsigned n = 0;
        int atlas = -1;
        std::array<Glyph, 32> raw{};
        std::uint32_t env = 0, prim = 0;
        for (unsigned i = 1; i < 256 && std::uint64_t(offset) + (i + 1) * 8 <= 0x800000; ++i) {
            const auto& c = command[i];
            const auto op = c.w0 >> 24;
            if (op == 0xB7 && c.w1 == 0x205) {
                ended = true;
                break;
            }
            if (op == 0xE7)
                continue;
            if (op == 0xFA) {
                prim = c.w1;
                continue;
            }
            if (op == 0xFB) {
                env = c.w1;
                continue;
            }
            if (op == 7) {
                if ((c.w1 & 0x1FFFFFFFU) == ((*symbols_)[1] & 0x1FFFFFFFU) && c.p0(16, 8) == 2)
                    modes = true;
                else {
                    atlas = -1;
                    for (unsigned a = 0; a < 32; ++a) {
                        const auto h = word(font_ + 0x80 + a * 4);
                        if (valid(h, 32) && (word(h + 12) & 0x1FFFFFFFU) == (c.w1 & 0x1FFFFFFFU) &&
                            half(h + 10) == c.p0(16, 8)) {
                            atlas = int(a);
                            break;
                        }
                    }
                    if (atlas < 0)
                        return;
                }
                continue;
            }
            if (op != 0xE4 || !modes || atlas < 0 || env != 0xFFFFFFFFU || (prim & 0xFFFFFF00U) != 0xFFFFFF00U ||
                n == raw.size() || std::uint64_t(offset) + (i + 3) * 8 > 0x800000 || command[i + 1].w0 != 0xB3000000U ||
                command[i + 2].w0 != 0xB2000000U || command[i + 2].w1 != 0x04000400U || c.p1(24, 3) != 0)
                return;
            raw[n++] = {&c,
                        int(c.p1(12, 12)) / 4,
                        int(c.p1(0, 12)) / 4,
                        (int(c.p0(12, 12)) - int(c.p1(12, 12))) / 4,
                        (int(c.p0(0, 12)) - int(c.p1(0, 12))) / 4,
                        int(command[i + 1].p1(16, 16)),
                        int(command[i + 1].p1(0, 16)),
                        atlas};
            i += 2;
        }
        if (!ended || !n)
            return;
        // Build the exact expected run, including spaces/compact alignment.
        int x = 160 - (width(copyright_) >> 1), base_y = 0;
        unsigned letter = 0;
        for (unsigned char ch : copyright_) {
            if (ch == 32) {
                x += half(font_ + 0x24);
                continue;
            }
            if (letter >= n)
                return;
            const auto p = font_ + 0x100 + (ch - 32) * 8;
            const auto& g = raw[letter];
            if (!letter)
                base_y = g.y - int(byte(p + 3));
            if (g.x != x + int(byte(p + 2)) || g.y != base_y + int(byte(p + 3)) || g.w != int(byte(p + 6)) ||
                g.h != int(byte(p + 7)) || g.s != int(byte(p + 4)) * 32 || g.t != int(byte(p + 5)) * 32 ||
                g.atlas != int(byte(p)))
                return;
            original_[letter++] = g.command;
            int step = half(font_ + 0x20) ? half(font_ + 0x20) : byte(p + 1);
            if (word((*symbols_)[4]) && step)
                --step;
            x += step;
        }
        if (letter != n || base_y < 190 || base_y > 252)
            return;
        left_ = 320;
        right_ = 0;
        top_ = 288;
        bottom_ = 0;
        if (!line(copyright_, base_y) || !line(credit_, base_y + int(half(font_ + 0x22)) + 3)) {
            count_ = 0;
            return;
        }
        left_ -= 7;
        right_ += 7;
        top_ -= 5;
        bottom_ += 5;
        if (left_ < 4 || right_ > 316 || bottom_ > 284) {
            count_ = 0;
            return;
        }
        original_count_ = n;
    }
    template <class Load> bool draw(RT64::State& state, RT64::DisplayList** cursor, Load load) {
        if (!enabled_ || !count_ || !original_count_ || state.rdp->colorImage.width != 320)
            return false;
        if (std::find(original_.begin(), original_.begin() + original_count_, *cursor) ==
            original_.begin() + original_count_)
            return false;
        if (!drawn_) {
            drawn_ = true;
            auto& r = *state.rdp;
            state.flush();
            const auto other = r.otherMode;
            const auto comb = r.colorCombinerStack[r.colorCombinerStackSize - 1];
            const auto env = r.envColorStack[r.envColorStackSize - 1];
            const auto aspect = r.extended.global.rectAspect;
            const auto alpha = std::uint32_t(
                std::clamp(std::lround(float(r.primColorStack[r.primColorStackSize - 1].w) * 255.F), 0L, 255L));
            const auto original_header = word(font_ + 0x80 + glyphs_[0].atlas * 4);
            r.setRectAspect(G_EX_ASPECT_ADJUST);
            load((*symbols_)[1] + 16, 2);
            r.setEnvColor(std::uint32_t(std::lround(float(alpha) * .68F)));
            RT64::ExtendedAlignment align{};
            align.leftOrigin = align.rightOrigin = G_EX_ORIGIN_NONE;
            // One convex mesh with four identically tessellated arcs. This
            // avoids per-strip viewport rounding and uneven corner coverage.
            constexpr unsigned steps = 12, points = 4 * (steps + 1);
            constexpr float radius = 4.F, pi = 3.14159265359F;
            std::array<std::array<float, 2>, points> rim{};
            const std::array<std::array<float, 2>, 4> centres{{{float(left_) + radius, float(top_) + radius},
                                                               {float(right_) - radius, float(top_) + radius},
                                                               {float(right_) - radius, float(bottom_) - radius},
                                                               {float(left_) + radius, float(bottom_) - radius}}};
            for (unsigned corner = 0; corner < 4; ++corner)
                for (unsigned i = 0; i <= steps; ++i) {
                    const float angle = pi + float(corner) * pi * .5F + float(i) * pi * .5F / steps;
                    rim[corner * (steps + 1) + i] = {centres[corner][0] + std::cos(angle) * radius,
                                                     centres[corner][1] + std::sin(angle) * radius};
                }
            std::array<float, points * 12> positions{}, colours{};
            std::array<float, points * 6> uv{};
            const float middle_x = (left_ + right_) * .5F, middle_y = (top_ + bottom_) * .5F;
            for (unsigned triangle = 0; triangle < points; ++triangle) {
                const std::array<std::array<float, 2>, 3> vertices{
                    {{middle_x, middle_y}, rim[triangle], rim[(triangle + 1) % points]}};
                for (unsigned v = 0; v < 3; ++v) {
                    const auto at = triangle * 12 + v * 4;
                    positions[at] = 160.F + (vertices[v][0] - 160.F) / cover_;
                    positions[at + 1] = vertices[v][1];
                    positions[at + 3] = 1;
                    for (unsigned c = 0; c < 4; ++c)
                        colours[at + c] = 1;
                }
            }
            r.drawTris(points, positions.data(), uv.data(), colours.data(), 0, 0);
            state.flush();
            r.setOtherMode(other.H, other.L);
            r.setCombine((std::uint64_t(comb.H) << 32) | comb.L);
            r.setEnvColor(0xFFFFFFFFU);
            int atlas = -1;
            for (unsigned i = 0; i < count_; ++i) {
                const auto& g = glyphs_[i];
                if (g.atlas != atlas) {
                    const auto h = word(font_ + 0x80 + g.atlas * 4);
                    load(word(h + 12), half(h + 10));
                    atlas = g.atlas;
                }
                r.drawTexRect(g.x * 4, g.y * 4, (g.x + g.w) * 4, (g.y + g.h) * 4, 0, std::int16_t(g.s),
                              std::int16_t(g.t), 1024, 1024, false, align);
            }
            state.flush();
            // Original inline font commands still follow; restore their first
            // atlas, colour, combiner and aspect exactly before continuing.
            load(word(original_header + 12), half(original_header + 10));
            r.setEnvColor((std::uint32_t(std::lround(float(env.x) * 255)) << 24) |
                          (std::uint32_t(std::lround(float(env.y) * 255)) << 16) |
                          (std::uint32_t(std::lround(float(env.z) * 255)) << 8) |
                          std::uint32_t(std::lround(float(env.w) * 255)));
            r.setRectAspect(aspect);
            static thread_local bool notice = false;
            if (!notice) {
                std::fprintf(
                    stderr,
                    "[intro][caption] rounded black plaque; original SMALLFONT; RECOMPILED BY THATGUYMCD!; symmetric mesh, 68-percent opacity\n");
                notice = true;
            }
        }
        *cursor += 2;
        return true;
    }
};
} // namespace dkr::runtime::presentation
