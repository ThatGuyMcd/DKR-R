#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace dkr::runtime::driving {
inline constexpr std::uint8_t Enabled = 1;
struct Pedals {
    std::uint8_t accelerator = 0, brake = 0, flags = 0;
    constexpr bool operator==(const Pedals&) const = default;
};
constexpr bool valid(Pedals p) {
    return (p.flags & ~Enabled) == 0;
}
inline std::uint8_t quantize(float value) {
    if (!std::isfinite(value))
        return 0;
    return std::uint8_t(std::lround(std::clamp(value, 0.F, 1.F) * 255.F));
}
constexpr float ratio(std::uint8_t value) {
    return float(value) / 255.F;
}
constexpr std::uint32_t pack(Pedals p) {
    return p.accelerator | (std::uint32_t(p.brake) << 8) | (std::uint32_t(p.flags) << 16);
}
constexpr Pedals unpack(std::uint32_t p) {
    return {std::uint8_t(p), std::uint8_t(p >> 8), std::uint8_t(p >> 16)};
}
inline std::array<std::atomic<std::uint32_t>, 4> physical{};
inline void publish(unsigned profile, Pedals p) {
    if (profile < 4)
        physical[profile].store(pack(p));
}
struct Request {
    Pedals pedals{};
    bool valid = false;
};
struct Symbols {
    std::uint32_t buttons = 0, player = 0, blocked = 0, adventure = 0, lead = 0;
};
struct Frame {
    std::array<Pedals, 4> inputs{};
    std::array<Request, 4> requests{};
    Symbols symbols{};
    bool owned = false, available = false;
};
inline thread_local Frame frame;
// Owned simulation supplies the immutable sample for this tick, including a
// replayed tick. A physical poll can never replace it during rollback.
class InputScope {
    Frame prior_;

  public:
    explicit InputScope(const std::array<Pedals, 4>& values) : prior_(frame) {
        frame = {};
        frame.inputs = values;
        frame.owned = true;
        frame.available = true;
    }
    ~InputScope() {
        frame = prior_;
    }
    InputScope(const InputScope&) = delete;
};
inline void begin_frame(bool online) {
    frame.requests = {};
    frame.symbols = {};
    if (frame.owned)
        return;
    frame.available = !online;
    for (unsigned i = 0; i < 4; ++i)
        frame.inputs[i] = online ? Pedals{} : unpack(physical[i].load());
}
// Cap partial input after native buildup; endpoints stay on the native path.
// Native acceleration, boost ownership and release decay are not replaced.
constexpr float partial(float native, std::uint8_t request, float maximum) {
    return request == 0 || request == 255 ? native : std::min(native, ratio(request) * maximum);
}
} // namespace dkr::runtime::driving
