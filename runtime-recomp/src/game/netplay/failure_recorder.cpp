#include "failure_recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <vector>

namespace dkr::runtime::netplay {
namespace {

const char* kind_name(FailureEventKind kind) {
    switch (kind) {
    case FailureEventKind::RollbackSave:
        return "rollback-save";
    case FailureEventKind::RollbackLoad:
        return "rollback-load";
    case FailureEventKind::RollbackDesync:
        return "rollback-desync";
    case FailureEventKind::RecoveryRequested:
        return "recovery-requested";
    case FailureEventKind::RecoveryCompleted:
        return "recovery-completed";
    case FailureEventKind::QueuePressure:
        return "queue-pressure";
    case FailureEventKind::QueueOverflow:
        return "queue-overflow";
    case FailureEventKind::FrameCommitted:
        return "frame-committed";
    case FailureEventKind::InputPredicted:
        return "input-predicted";
    case FailureEventKind::LateInputDiscarded:
        return "late-input-discarded";
    case FailureEventKind::FrameWaitBegin:
        return "frame-wait-begin";
    case FailureEventKind::FrameWaitEnd:
        return "frame-wait-end";
    case FailureEventKind::ReplicaPublished:
        return "replica-published";
    case FailureEventKind::ReplicaInstalled:
        return "replica-installed";
    case FailureEventKind::ReplicaUnavailable:
        return "replica-unavailable";
    case FailureEventKind::CatchUpBatch:
        return "catch-up-batch";
    case FailureEventKind::LifecycleBoundary:
        return "lifecycle-boundary";
    case FailureEventKind::ProgressWatchdog:
        return "progress-watchdog";
    case FailureEventKind::SessionFailure:
        return "session-failure";
    }
    return "unknown";
}

} // namespace

void FailureRecorder::record(FailureEventKind kind, std::uint32_t frame, std::uint32_t value_a, std::uint32_t value_b,
                             std::string_view detail) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    std::scoped_lock lock(mutex_);
    FailureEvent& event = events_[static_cast<std::size_t>((next_sequence_ - 1U) % kCapacity)];
    event = {};
    event.sequence = next_sequence_++;
    event.monotonic_micros =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    event.kind = kind;
    event.frame = frame;
    event.value_a = value_a;
    event.value_b = value_b;
    const std::size_t copied = (std::min)(detail.size(), event.detail.size() - 1U);
    if (copied > 0U)
        std::memcpy(event.detail.data(), detail.data(), copied);
    event.detail[copied] = '\0';
}

void FailureRecorder::dump(std::FILE* stream, std::size_t maximum_events) const {
    if (stream == nullptr || maximum_events == 0U)
        return;
    const auto text = snapshot_text(maximum_events);
    std::fwrite(text.data(), 1U, text.size(), stream);
    std::fflush(stream);
}

std::string FailureRecorder::snapshot_text(std::size_t maximum_events) const {
    std::vector<FailureEvent> copy;
    copy.reserve((std::min)(maximum_events, kCapacity));
    {
        std::scoped_lock lock(mutex_);
        const std::uint64_t first_available = next_sequence_ > kCapacity ? next_sequence_ - kCapacity : 1U;
        const std::uint64_t requested_first = next_sequence_ > maximum_events ? next_sequence_ - maximum_events : 1U;
        const std::uint64_t first = (std::max)(first_available, requested_first);
        for (std::uint64_t sequence = first; sequence < next_sequence_; ++sequence) {
            const FailureEvent& event = events_[static_cast<std::size_t>((sequence - 1U) % kCapacity)];
            if (event.sequence != sequence)
                continue;
            copy.push_back(event);
        }
    }
    std::ostringstream out;
    out << "[netplay][flight-recorder] begin\n";
    for (const auto& event : copy) {
        out << "[netplay][flight-recorder] seq=" << event.sequence << " us=" << event.monotonic_micros
            << " kind=" << kind_name(event.kind) << " frame=" << event.frame << " a=" << event.value_a
            << " b=" << event.value_b << " detail=" << event.detail.data() << '\n';
    }
    out << "[netplay][flight-recorder] end\n";
    return out.str();
}

void FailureRecorder::clear() {
    std::scoped_lock lock(mutex_);
    events_ = {};
    next_sequence_ = 1U;
}

FailureRecorder& failure_recorder() {
    static FailureRecorder recorder;
    return recorder;
}

} // namespace dkr::runtime::netplay
