#pragma once
#include "texture_capture_bridge.hpp"
#include "legacy_mod_format.hpp"
#include <array>
#include <algorithm>

namespace dkr::runtime::texture_dump {
struct Picture {
    unsigned width = 0, height = 0;
    mods::Bytes rgba;
};
inline std::array<std::uint8_t, 4> rgba16(unsigned v) {
    auto c = [](unsigned n) { return std::uint8_t((n << 3) | (n >> 2)); };
    return {c((v >> 11) & 31), c((v >> 6) & 31), c((v >> 1) & 31), std::uint8_t(v & 1 ? 255 : 0)};
}
inline std::array<std::uint8_t, 4> intensity(unsigned v, unsigned a) {
    return {std::uint8_t(v), std::uint8_t(v), std::uint8_t(v), std::uint8_t(a)};
}
// Same integer channel expansion and TMEM addressing as the pinned renderer's
// Formats.hlsli / TextureDecoder.hlsli. No shader, gamma or filtering is baked in.
inline Picture decode_tmem(const texture_capture::Tile& t, const std::array<std::uint8_t, 4096>& m) {
    if (!t.width || !t.height || t.width > 4096 || t.height > 4096 ||
        std::uint64_t(t.width) * t.height > 16 * mods::MiB || !t.line || t.siz > 3)
        throw mods::Error("Invalid captured tile dimensions or stride.");
    if (t.fmt == 1 || t.fmt > 4)
        throw mods::Error("This tile needs an unsupported colour conversion; it was not exported.");
    Picture p{t.width, t.height, mods::Bytes(std::size_t(t.width) * t.height * 4)};
    const bool split = t.fmt == 0 && t.siz == 3;
    const unsigned mask = (split || t.tlut) ? 2047 : 4095, stride = t.line * 8, start = t.tmem * 8;
    for (unsigned y = 0; y < t.height; ++y)
        for (unsigned x = 0; x < t.width; ++x) {
            const auto address = y * stride + ((x << (split ? 2 : t.siz)) >> 1);
            const auto load = [&](unsigned rel, unsigned bank = 0) {
                const unsigned row = (rel / stride) * stride, word = (rel - row) / 4;
                const unsigned at = (y & 1) ? start + row + ((word ^ 1) * 4) + (rel & 3) : start + rel;
                return unsigned(m[((at & mask) | bank) & 4095]);
            };
            const unsigned a = load(address), b = load(address + 1), n = (x & 1) ? a & 15 : a >> 4;
            std::array<std::uint8_t, 4> c{};
            if (t.tlut) {
                const unsigned at = 2048 + ((t.siz == 0 ? ((t.palette << 4) | n) : a) << 3);
                const unsigned v = (unsigned(m[at & 4095]) << 8) | m[(at + 1) & 4095];
                if (t.tlut == 2)
                    c = rgba16(v);
                else if (t.tlut == 3)
                    c = intensity(v >> 8, v & 255);
                else
                    throw mods::Error("Unknown captured palette mode.");
            } else if (t.siz == 0) {
                if (t.fmt == 3) {
                    const auto i = n & 14;
                    c = intensity((i << 4) | (i << 1) | (i >> 2), n & 1 ? 255 : 0);
                } else if (t.fmt == 2) {
                    const auto i = (t.palette << 4) | n;
                    c = intensity(i, i);
                } else
                    c = intensity(n * 17, n * 17);
            } else if (t.siz == 1) {
                c = t.fmt == 3 ? intensity((a >> 4) * 17, (a & 15) * 17) : intensity(a, a);
            } else if (t.siz == 2) {
                if (t.fmt == 0)
                    c = rgba16((a << 8) | b);
                else if (t.fmt == 3)
                    c = intensity(a, b);
                else
                    c = {std::uint8_t(a), std::uint8_t(b), std::uint8_t(a), std::uint8_t(b)};
            } else {
                const auto hi = split ? address : address + 2;
                const unsigned cc = load(hi, split ? 2048 : 0), d = load(hi + 1, split ? 2048 : 0);
                if (t.fmt == 0)
                    c = {std::uint8_t(a), std::uint8_t(b), std::uint8_t(cc), std::uint8_t(d)};
                else
                    c = (x & 1) ? std::array<std::uint8_t, 4>{std::uint8_t(a), std::uint8_t(b), std::uint8_t(a),
                                                              std::uint8_t(b)}
                                : std::array<std::uint8_t, 4>{std::uint8_t(cc), std::uint8_t(d), std::uint8_t(cc),
                                                              std::uint8_t(d)};
            }
            std::copy(c.begin(), c.end(), p.rgba.begin() + (std::size_t(y) * t.width + x) * 4);
        }
    return p;
}
inline Picture decode_asset(mods::View bytes, unsigned width, unsigned height, unsigned format, bool interlaced,
                            mods::View palette = {}) {
    static constexpr unsigned bits[]{32, 16, 8, 4, 16, 8, 4, 4, 8};
    if (!width || !height || format >= 9)
        throw mods::Error("Invalid source texture header.");
    const unsigned bpp = bits[format], row = (width * bpp + 7) / 8;
    if (std::size_t(row) * height > bytes.size())
        throw mods::Error("Source texture pixels are truncated.");
    auto value = [&](unsigned y, unsigned offset) {
        if (interlaced && (y & 1)) {
            const unsigned chunk = bpp == 32 ? 8 : 4, whole = (row / (chunk * 2)) * (chunk * 2);
            if (offset < whole)
                offset ^= chunk;
        }
        return unsigned(bytes[std::size_t(y) * row + offset]);
    };
    Picture p{width, height, mods::Bytes(std::size_t(width) * height * 4)};
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const unsigned offset = x * bpp / 8, a = value(y, offset), n = (x & 1) ? a & 15 : a >> 4;
            std::array<std::uint8_t, 4> c{};
            switch (format) {
            case 0:
                c = {std::uint8_t(a), std::uint8_t(value(y, offset + 1)), std::uint8_t(value(y, offset + 2)),
                     std::uint8_t(value(y, offset + 3))};
                break;
            case 1:
                c = rgba16((a << 8) | value(y, offset + 1));
                break;
            case 2:
                c = intensity(a, a);
                break;
            case 3:
                c = intensity(n * 17, n * 17);
                break;
            case 4:
                c = intensity(a, value(y, offset + 1));
                break;
            case 5:
                c = intensity((a >> 4) * 17, (a & 15) * 17);
                break;
            case 6: {
                auto i = n & 14;
                c = intensity((i << 4) | (i << 1) | (i >> 2), n & 1 ? 255 : 0);
                break;
            }
            default: {
                const unsigned i = format == 7 ? n : a;
                if (i * 2 + 2 > palette.size())
                    throw mods::Error("Palette is unavailable; capture this texture in game.");
                c = rgba16(mods::be16(palette, i * 2));
                break;
            }
            }
            std::copy(c.begin(), c.end(), p.rgba.begin() + (std::size_t(y) * width + x) * 4);
        }
    return p;
}
} // namespace dkr::runtime::texture_dump
