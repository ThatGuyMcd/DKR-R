#pragma once
#include "control.hpp"
#include <array>
#include <span>

namespace dkr::online_stability {
struct LiveInputs {
    std::uint64_t controls = 0, epoch = 0, lease = 0;
    std::uint32_t first = 0;
    unsigned owner = 0, count = 0;
    struct Value {
        std::uint16_t buttons = 0;
        std::int8_t x = 0, y = 0;
        std::uint8_t accelerator = 0, brake = 0, driving_flags = 0;
        bool operator==(const Value&) const = default;
    };
    std::array<Value, 8> inputs{};
};
inline Bytes encode_live_inputs(const LiveInputs& value) {
    if (!value.controls || !value.epoch || !value.lease || value.owner >= 4 || !value.count || value.count > 8 ||
        std::uint64_t(value.first) + value.count > UINT32_MAX)
        return {};
    Bytes b;
    b.reserve(30 + value.count * 7);
    const auto put = [&](std::uint64_t n, unsigned width) {
        for (unsigned i = 0; i < width; ++i)
            b.push_back(std::uint8_t(n >> (8 * i)));
    };
    put(value.controls, 8);
    put(value.epoch, 8);
    put(value.lease, 8);
    put(value.first, 4);
    put(value.owner, 1);
    put(value.count, 1);
    for (unsigned i = 0; i < value.count; ++i) {
        const auto v = value.inputs[i];
        if (v.driving_flags > 1 || (!v.driving_flags && (v.accelerator || v.brake)))
            return {};
        put(v.buttons, 2);
        put(std::uint8_t(v.x), 1);
        put(std::uint8_t(v.y), 1);
        put(v.accelerator, 1);
        put(v.brake, 1);
        put(v.driving_flags, 1);
    }
    return b;
}
inline bool decode_live_inputs(std::span<const std::uint8_t> bytes, LiveInputs& value) {
    if (bytes.size() < 37 || bytes.size() > 86)
        return false;
    std::size_t at = 0;
    const auto get = [&](unsigned width) {
        std::uint64_t n = 0;
        for (unsigned i = 0; i < width; ++i)
            n |= std::uint64_t(bytes[at++]) << (8 * i);
        return n;
    };
    LiveInputs v;
    v.controls = get(8);
    v.epoch = get(8);
    v.lease = get(8);
    v.first = std::uint32_t(get(4));
    v.owner = unsigned(get(1));
    v.count = unsigned(get(1));
    if (!v.count || v.count > 8 || bytes.size() != 30 + 7 * v.count)
        return false;
    for (unsigned i = 0; i < v.count; ++i) {
        auto& p = v.inputs[i];
        p.buttons = std::uint16_t(get(2));
        p.x = std::int8_t(get(1));
        p.y = std::int8_t(get(1));
        p.accelerator = std::uint8_t(get(1));
        p.brake = std::uint8_t(get(1));
        p.driving_flags = std::uint8_t(get(1));
        if (p.driving_flags > 1 || (!p.driving_flags && (p.accelerator || p.brake)))
            return false;
    }
    if (!v.controls || !v.epoch || !v.lease || v.owner >= 4 || std::uint64_t(v.first) + v.count > UINT32_MAX)
        return false;
    value = v;
    return true;
}
} // namespace dkr::online_stability
