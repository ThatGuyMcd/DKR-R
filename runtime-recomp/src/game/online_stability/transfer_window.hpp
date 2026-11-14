#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <span>
#include <stdexcept>
#include <vector>

namespace dkr::online_stability {
inline constexpr std::size_t LegacyTransferWindow = 256U * 1024U;
inline constexpr std::size_t MaximumTransferWindow = 2U * 1024U * 1024U;
inline std::size_t requested_window(std::span<const std::uint8_t> bytes) {
    if (bytes.empty())
        return LegacyTransferWindow;
    if (bytes.size() != 4)
        throw std::invalid_argument("Invalid negotiated mod window.");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(bytes[i]) << (8 * i);
    if (value < LegacyTransferWindow || value > MaximumTransferWindow || value % 8192)
        throw std::invalid_argument("Mod window exceeds its negotiated limits.");
    return value;
}
inline std::vector<std::uint8_t> window_bytes(std::size_t size) {
    std::vector<std::uint8_t> bytes;
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(std::uint8_t(size >> (8 * i)));
    return bytes;
}
inline std::size_t packet_window(std::size_t chunk) {
    if (!chunk || chunk > 8192)
        throw std::invalid_argument("Invalid mod chunk budget.");
    return std::clamp((chunk * 512 / 8192) * 8192, LegacyTransferWindow, MaximumTransferWindow);
}
// Per-recipient slow start on verified contiguous progress; a real gap backs
// off. Neither an arbitrary peer nor slow storage can grow the bound past 2 MiB.
class TransferWindow {
  public:
    using Clock = std::chrono::steady_clock;
    explicit TransferWindow(std::size_t chunk = 8192) : packet_cap_(packet_window(chunk)) {}
    void chunk_budget(std::size_t chunk) {
        packet_cap_ = packet_window(chunk);
        cap_ = std::min(cap_, packet_cap_);
        window_ = std::min(window_, cap_);
    }
    void negotiate(std::span<const std::uint8_t> capability) {
        cap_ = std::min(requested_window(capability), packet_cap_);
        enabled_ = !capability.empty();
        window_ = std::min(window_, cap_);
    }
    std::size_t size() const {
        return window_;
    }
    std::size_t stride() const {
        return std::min<std::size_t>(window_ / 2, 128U * 1024U);
    }
    std::size_t peak() const {
        return peak_;
    }
    void observed(std::size_t bytes, Clock::time_point now = Clock::now()) {
        if (!enabled_ || !request_at_.time_since_epoch().count())
            return;
        if (!rtt_ms_)
            rtt_ms_ = std::clamp<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - request_at_).count(), 1, 5000);
        measured_bytes_ += bytes;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - measure_at_).count();
        // A short burst reflects queued data, not this recipient's fair share.
        // Average verified drain over the payload, including intervening gaps.
        if (ms >= 250)
            rate_ = measured_bytes_ * 1000 / std::uint64_t(ms);
    }
    std::chrono::milliseconds retry_delay() const {
        if (!enabled_)
            return std::chrono::milliseconds(1000);
        const auto drain = rate_ ? window_ * 2000 / rate_ : 2000;
        return std::chrono::milliseconds(std::clamp<std::uint64_t>(rtt_ms_ * 2 + drain, 1000, 8000));
    }
    void credit(bool first, bool gap, Clock::time_point now = Clock::now()) {
        if (!enabled_)
            return;
        if (first) {
            request_at_ = measure_at_ = now;
            measured_bytes_ = 0;
            rtt_ms_ = 0;
        }
        if (gap)
            window_ = std::max(LegacyTransferWindow, (window_ / 2 / 8192) * 8192);
        else if (!first) {
            const auto demand = rate_ && rtt_ms_
                                    ? std::clamp<std::size_t>(
                                          ((rate_ * rtt_ms_ * 2 / 1000 + LegacyTransferWindow + 8191) / 8192) * 8192,
                                          LegacyTransferWindow, cap_)
                                    : cap_;
            window_ = std::min(demand, window_ * 2);
        }
        peak_ = std::max(peak_, window_);
    }
    template <class Message> Message request(Message message) const {
        if (enabled_ && !message.payload.empty())
            message.bytes = window_bytes(window_);
        return message;
    }

  private:
    std::size_t window_ = LegacyTransferWindow, cap_ = LegacyTransferWindow, peak_ = LegacyTransferWindow;
    bool enabled_ = false;
    std::size_t packet_cap_ = MaximumTransferWindow;
    Clock::time_point request_at_{}, measure_at_{};
    std::uint64_t measured_bytes_ = 0, rate_ = 0, rtt_ms_ = 0;
};
} // namespace dkr::online_stability
