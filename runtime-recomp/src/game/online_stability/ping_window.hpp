#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace dkr::online_stability {
// A one-second heartbeat cadence must not invalidate a still-live probe.
// Keep only locally issued tokens; duplicate, expired and reordered replies
// cannot overwrite a newer measurement. Authentication remains with the link.
class PingWindow final {
    using Clock = std::chrono::steady_clock;
    struct Probe {
        std::uint64_t token = 0;
        Clock::time_point sent{};
    };
    std::array<Probe, 8> pending_{};
    unsigned cursor_ = 0;
    Clock::time_point accepted_{};

  public:
    void sent(std::uint64_t token, Clock::time_point now) {
        if (token)
            pending_[cursor_++ % pending_.size()] = {token, now};
    }
    std::optional<unsigned> receive(std::uint64_t token, Clock::time_point now) {
        if (!token)
            return {};
        for (auto& probe : pending_)
            if (probe.token == token) {
                const auto at = probe.sent;
                probe = {};
                if (now < at || now - at >= std::chrono::seconds(5) || at < accepted_)
                    return {};
                accepted_ = at;
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - at).count();
                return unsigned(ms ? ms : 1);
            }
        return {};
    }
};
} // namespace dkr::online_stability
