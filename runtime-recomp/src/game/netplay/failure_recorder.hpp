#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace dkr::runtime::netplay {

enum class FailureEventKind : std::uint8_t {
    RollbackSave,
    RollbackLoad,
    RollbackDesync,
    RecoveryRequested,
    RecoveryCompleted,
    QueuePressure,
    QueueOverflow,
    FrameCommitted,
    InputPredicted,
    LateInputDiscarded,
    FrameWaitBegin,
    FrameWaitEnd,
    ReplicaPublished,
    ReplicaInstalled,
    ReplicaUnavailable,
    CatchUpBatch,
    LifecycleBoundary,
    ProgressWatchdog,
    SessionFailure,
};

struct FailureEvent {
    std::uint64_t sequence = 0U;
    std::uint64_t monotonic_micros = 0U;
    FailureEventKind kind = FailureEventKind::SessionFailure;
    std::uint32_t frame = 0U;
    std::uint32_t value_a = 0U;
    std::uint32_t value_b = 0U;
    std::array<char, 72> detail{};
};

// Fixed and allocation-free. It emits only its bounded tail when a session
// fails, avoiding per-frame disk I/O and unbounded diagnostic logs.
class FailureRecorder final {
  public:
    static constexpr std::size_t kCapacity = 2048U;

    void record(FailureEventKind kind, std::uint32_t frame = 0U, std::uint32_t value_a = 0U, std::uint32_t value_b = 0U,
                std::string_view detail = {});
    void dump(std::FILE* stream = stderr, std::size_t maximum_events = 256U) const;
    std::string snapshot_text(std::size_t maximum_events = 256U) const;
    void clear();

  private:
    mutable std::mutex mutex_;
    std::array<FailureEvent, kCapacity> events_{};
    std::uint64_t next_sequence_ = 1U;
};

FailureRecorder& failure_recorder();

} // namespace dkr::runtime::netplay
