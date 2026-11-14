#include "confirmed_observer.hpp"
#include <exception>
#include <algorithm>
#include <unordered_map>

namespace dkr::runtime::spectators {
namespace {
thread_local ConfirmedFrameBinding::Sink frame_sink;
}
ConfirmedFrameBinding::ConfirmedFrameBinding(Sink sink) : previous_(std::move(frame_sink)) {
    frame_sink = std::move(sink);
}
ConfirmedFrameBinding::~ConfirmedFrameBinding() {
    frame_sink = std::move(previous_);
}
namespace {
thread_local ConfirmedExport* active = nullptr;
std::mutex registry_mutex;
std::unordered_map<std::uint64_t, std::weak_ptr<ConfirmedExport>> registry;
} // namespace
std::shared_ptr<ConfirmedExport> acquire_export(std::uint64_t match) {
    if (!match)
        return {};
    std::scoped_lock lock(registry_mutex);
    std::erase_if(registry, [](const auto& entry) { return entry.second.expired(); });
    auto& ref = registry[match];
    auto result = ref.lock();
    if (!result) {
        result = std::make_shared<ConfirmedExport>(match);
        ref = result;
    }
    return result;
}
void ConfirmedExport::requested(bool enabled) noexcept {
    const auto before = requested_.exchange(enabled, std::memory_order_acq_rel);
    if (enabled && !before)
        baseline_requested_.store(true, std::memory_order_release);
}
bool ConfirmedExport::wants_baseline() const noexcept {
    return requested_.load(std::memory_order_acquire) && baseline_requested_.load(std::memory_order_acquire);
}
void ConfirmedExport::begin_capture(Cursor next) noexcept {
    try {
        std::scoped_lock lock(mutex_);
        capture_ = next;
        captured_tail_.clear();
    } catch (...) {
        baseline_requested_.store(true, std::memory_order_release);
    }
}
void ConfirmedExport::publish_baseline(Cursor next, std::uint64_t schema, std::uint64_t controls,
                                       std::shared_ptr<const std::vector<std::uint8_t>> bytes, Digest hash) noexcept {
    if (!requested_.load(std::memory_order_acquire))
        return;
    try {
        std::scoped_lock lock(mutex_);
        const auto result = feed_.baseline({next, match_, schema, controls, hash, std::move(bytes)});
        if (result != FeedResult::Accepted && result != FeedResult::Duplicate) {
            failure_ = "The confirmed spectator checkpoint was rejected.";
            return;
        }
        if (capture_ == next) {
            for (auto& f : captured_tail_)
                if (f.cursor.epoch == next.epoch && f.cursor.frame >= next.frame) {
                    const auto added = feed_.append(std::move(f));
                    if (added != FeedResult::Accepted && added != FeedResult::Duplicate) {
                        capture_.reset();
                        captured_tail_.clear();
                        baseline_requested_.store(true, std::memory_order_release);
                        return;
                    }
                }
        }
        capture_.reset();
        captured_tail_.clear();
        failure_.clear();
        baseline_requested_.store(false, std::memory_order_release);
    } catch (...) {
        baseline_requested_.store(true, std::memory_order_release);
    }
}
void ConfirmedExport::committed(ConfirmedFrame frame) noexcept {
    if (!requested_.load(std::memory_order_acquire))
        return;
    try {
        std::scoped_lock lock(mutex_);
        if (capture_ && frame.cursor.epoch == capture_->epoch && frame.cursor.frame >= capture_->frame) {
            if (captured_tail_.size() >= ConfirmedFeed::MaximumFrames) {
                capture_.reset();
                captured_tail_.clear();
                baseline_requested_.store(true, std::memory_order_release);
            } else
                captured_tail_.push_back(frame);
        }
        if (!feed_.current())
            return;
        const auto result = feed_.append(std::move(frame));
        if (result != FeedResult::Accepted && result != FeedResult::Duplicate) {
            baseline_requested_.store(true, std::memory_order_release);
            return;
        }
    } catch (...) {
        baseline_requested_.store(true, std::memory_order_release);
    }
}
std::shared_ptr<const Baseline> ConfirmedExport::baseline() const {
    std::scoped_lock lock(mutex_);
    return feed_.current();
}
std::vector<ConfirmedFrame> ConfirmedExport::after(Cursor c, std::size_t n) const {
    std::scoped_lock lock(mutex_);
    return feed_.after(c, n);
}
Cursor ConfirmedExport::next() const {
    std::scoped_lock lock(mutex_);
    return feed_.next();
}
bool ConfirmedExport::covers(Cursor c) const {
    std::scoped_lock lock(mutex_);
    return feed_.covers(c);
}
void ConfirmedExport::resync() noexcept {
    baseline_requested_.store(true, std::memory_order_release);
}
std::string ConfirmedExport::failure() const {
    std::scoped_lock lock(mutex_);
    return failure_;
}
ConfirmedExportBinding::ConfirmedExportBinding(ConfirmedExport* stream) noexcept : previous_(active) {
    active = stream;
}
ConfirmedExportBinding::~ConfirmedExportBinding() {
    active = previous_;
}
void observe_confirmed(std::uint64_t epoch, std::uint32_t frame, const std::array<PortInput, RacingPorts>& inputs,
                       std::span<const std::uint32_t> command, bool boundary) noexcept {
    if (!frame_sink && (!active || !active->enabled()))
        return;
    try {
        ConfirmedFrame out{{epoch, frame}, inputs, {}, boundary};
        if (!command.empty()) {
            if (command.size() != 16)
                return;
            if (std::any_of(command.begin(), command.end(), [](auto word) { return word != 0; })) {
                out.command.reserve(64);
                for (auto word : command)
                    for (unsigned i = 0; i < 4; ++i)
                        out.command.push_back(std::uint8_t(word >> (8 * i)));
            }
        }
        if (frame_sink)
            frame_sink(out);
        if (active && active->enabled())
            active->committed(std::move(out));
    } catch (...) {
        if (active)
            active->resync();
    }
}
} // namespace dkr::runtime::spectators
