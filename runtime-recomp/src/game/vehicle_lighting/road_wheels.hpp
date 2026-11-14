#pragma once
#include "capture.hpp"
#include "../atomic_snapshot.hpp"
#include <atomic>
#include <memory>
#include <optional>
#include <span>

namespace dkr::runtime::vehicles::lighting::road_wheels {
inline constexpr unsigned BeginColour = 26, EndColour = 27;
// A character-only launch need not prepare any replacement-car resources.
// Publish just its immutable car-header identities before render workers run.
inline AtomicSnapshot<const std::vector<unsigned>> driver_headers;
inline void publish_headers(std::vector<unsigned> headers) {
    std::sort(headers.begin(), headers.end());
    headers.erase(std::unique(headers.begin(), headers.end()), headers.end());
    driver_headers.store(std::make_shared<const std::vector<unsigned>>(std::move(headers)), std::memory_order_release);
}
inline bool custom_header(unsigned header) {
    const auto headers = driver_headers.load(std::memory_order_acquire);
    return headers && std::binary_search(headers->begin(), headers->end(), header);
}
struct Symbols {
    unsigned intensity = 0, inverse_view = 0, viewport_layout = 0;
};
class Memory {
    std::span<std::uint8_t> bytes_;
    std::size_t offset(unsigned address, unsigned count) const {
        if (address < 0x80000000U || (bytes_.size() & 3U))
            throw std::runtime_error("Road-wheel address.");
        const auto p = std::size_t(address - 0x80000000U);
        if (p > bytes_.size() || count > bytes_.size() - p)
            throw std::runtime_error("Road-wheel span.");
        return p;
    }

  public:
    explicit Memory(std::span<std::uint8_t> bytes) : bytes_(bytes) {}
    unsigned get(unsigned address, unsigned count = 4) const {
        const auto p = offset(address, count);
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i)
            value = (value << 8) | bytes_[(p + i) ^ 3U];
        return value;
    }
    void set(unsigned address, unsigned value) {
        const auto p = offset(address, 4);
        if (address & 3U)
            throw std::runtime_error("Road-wheel word alignment.");
        for (unsigned i = 0; i < 4; ++i)
            bytes_[(p + i) ^ 3U] = std::uint8_t(value >> ((3 - i) * 8));
    }
    void marker(unsigned holder, unsigned kind, unsigned value) {
        const std::array<std::array<unsigned, 2>, 1> packet{{{kind, value}}};
        markers(holder, packet);
    }
    void markers(unsigned holder, std::span<const std::array<unsigned, 2>> packet) {
        if (holder & 3U)
            throw std::runtime_error("Road-wheel holder alignment.");
        const auto p = get(holder);
        if (p & 7U)
            throw std::runtime_error("Road-wheel command alignment.");
        (void)offset(holder, 4);
        (void)offset(p, unsigned(packet.size() * 8));
        for (unsigned i = 0; i < packet.size(); ++i) {
            set(p + i * 8, 0xb0ca0000U | packet[i][0]);
            set(p + i * 8 + 4, packet[i][1]);
        }
        set(holder, p + unsigned(packet.size() * 8));
    }
};
// The original sprites contain baked tyre detail. Use one rounded upper/outer
// surface normal to modulate that detail, not the camera-facing sprite plane.
// Rotation about the axle changes the animation, never the tyre's lighting.
inline Vec normal(float side, std::int16_t steering) {
    constexpr float diagonal = .7071067811865475F;
    return yaw({side * diagonal, diagonal, 0}, -float(steering) * 6.283185307179586F / 65536.F);
}
inline Vec rotate(Vec n, std::int16_t y, std::int16_t x, std::int16_t z) {
    constexpr float angle = 6.283185307179586F / 65536.F;
    const auto sy = std::sin(y * angle), cy = std::cos(y * angle), sx = std::sin(x * angle), cx = std::cos(x * angle),
               sz = std::sin(z * angle), cz = std::cos(z * angle);
    const Matrix m{{{sx * sy * sz + cz * cy, sz * cx, sx * cy * sz - cz * sy},
                    {sx * sy * cz - sz * cy, cz * cx, sx * cy * cz + sz * sy},
                    {cx * sy, -sx, cx * cy}}};
    return transform(n, m);
}
inline float multiplier(const Sample& sample, Vec n) {
    if (!sample.enabled() || sample.exposure <= 0)
        return 1;
    // Primitive RGB already contains native exposure. Apply it exactly once;
    // a bright wheel may retain its original colour but can never be boosted.
    return std::clamp(brightness(sample, n) / sample.exposure, 0.F, 1.F);
}
struct Observation {
    unsigned wheel = 0, ordinal = 0;
    Sample sample;
};
class Scope {
    std::uint8_t* ram_ = nullptr;
    unsigned parent_ = 0, wheel_ = 0, holder_ = 0;
    Symbols symbols_{};
    Sample sample_{};
    bool emitted_ = false;

  public:
    void reset() {
        ram_ = nullptr;
        parent_ = wheel_ = holder_ = 0;
        symbols_ = {};
        sample_ = {};
        emitted_ = false;
    }
    void begin(std::span<std::uint8_t> ram, unsigned object, bool custom, bool custom_car, Symbols symbols) {
        // A failed private camera draw can abandon its C stack. Every new
        // model starts fresh; no draw scratch survives into another world.
        reset();
        if (!custom || custom_car)
            return;
        Memory g(ram);
        const auto header = g.get(object + 0x40), racer = g.get(object + 0x64), points = g.get(object + 0x60);
        if (g.get(object + 0x48, 2) != 1 || !header || !racer || !points || g.get(racer + 0x1d6, 1) != 0 ||
            g.get(header + 0x53, 1) != 0 || g.get(header + 0x71, 1) != 0 || g.get(points) != 4)
            return;
        ram_ = ram.data();
        parent_ = object;
        symbols_ = symbols;
    }
    std::optional<Observation> wheel_begin(std::span<std::uint8_t> ram, unsigned object, unsigned holder,
                                           unsigned sprite, unsigned flags) {
        if (!parent_ || ram.data() != ram_ || (flags & 0x10bU) != 0x10bU)
            return {};
        if (wheel_)
            throw std::runtime_error("Nested native road-wheel draw.");
        Memory g(ram);
        const auto points = g.get(parent_ + 0x60);
        unsigned ordinal = 4;
        for (unsigned i = 0; i < 4; ++i)
            if (g.get(points + 4 + i * 4) == object)
                ordinal = i;
        if (ordinal == 4)
            return {};
        const auto h = g.get(object + 0x40);
        // Four native car tyre sprites only: not eggs, propellers or effects.
        if (!h || g.get(h + 0x53, 1) != 2 || g.get(h + 0x55, 1) != 4 || !sprite || g.get(sprite, 2) != 16 ||
            !(g.get(sprite + 6, 2) & 16U))
            return {};
        const auto slots = g.get(parent_ + 0x68), lod = g.get(parent_ + 0x3a, 1);
        if (!slots || lod >= g.get(g.get(parent_ + 0x40) + 0x55, 1))
            return {};
        const auto instance = g.get(slots + lod * 4);
        if (!instance)
            return {};
        const auto model = g.get(instance), anchors = g.get(model + 0x14);
        if (!anchors || g.get(model + 0x18, 2) < 4)
            return {};
        const auto indices = g.get(points + 0x2c);
        if (!indices)
            return {};
        const auto anchor = std::int8_t(g.get(indices + ordinal, 1));
        if (anchor < 0 || unsigned(anchor) >= g.get(model + 0x18, 2))
            return {};
        const auto vertex = g.get(anchors + unsigned(anchor) * 2, 2), vertices = g.get(parent_ + 0x44);
        if (!vertices || vertex >= g.get(model + 0x24, 2))
            return {};
        const auto side = std::int16_t(g.get(vertices + vertex * 10, 2));
        if (!side)
            return {};
        std::array<std::array<unsigned, 2>, 11> packet{};
        unsigned count = 0;
        if (!emitted_) {
            const auto racer = g.get(parent_ + 0x64);
            const bool fast = g.get(racer, 2) == 0xffffU || g.get(symbols_.viewport_layout) != 0;
            sample_ = capture([&](unsigned p, unsigned size) { return g.get(p, size); }, parent_, symbols_.intensity,
                              symbols_.inverse_view, fast);
            if (!sample_.enabled())
                return {};
            const auto light = words(sample_);
            Stream admissible;
            for (unsigned i = 0; i < light.size(); ++i)
                if (!admissible.put(i, light[i]))
                    return {};
            for (unsigned i = 0; i < light.size(); ++i)
                packet[count++] = {28 + i, light[i]};
            packet[count++] = {36, (g.get(parent_, 2) << 16) | g.get(parent_ + 2, 2)};
            packet[count++] = {37, g.get(parent_ + 4, 2)};
        }
        // Raw angles and side only. Trigonometry/brightness belongs to the
        // decoder; host libm must never enter a canonical RAM/display proof.
        packet[count++] = {BeginColour, g.get(object, 2) | (side < 0 ? 0x10000U : 0U)};
        g.markers(holder, std::span(packet).first(count));
        emitted_ = true;
        wheel_ = object;
        holder_ = holder;
        return Observation{object, ordinal, sample_};
    }
    void wheel_end(std::span<std::uint8_t> ram, unsigned object) {
        if (!wheel_ || object != wheel_ || ram.data() != ram_)
            return;
        Memory(ram).marker(holder_, EndColour, 0);
        wheel_ = holder_ = 0;
    }
    void end(unsigned object) {
        if (object == parent_)
            reset();
    }
};
} // namespace dkr::runtime::vehicles::lighting::road_wheels
