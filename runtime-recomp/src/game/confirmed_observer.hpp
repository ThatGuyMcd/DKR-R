#pragma once
#include "confirmed_feed.hpp"
#include "checkpoint_transfer.hpp"
#include <atomic>
#include <mutex>

namespace dkr::runtime::spectators {
// Export is opt-in. With no watching member, the racing path allocates no
// checkpoint copy, command vector or frame-history entry for spectators.
class ConfirmedExport final {
  public:
    explicit ConfirmedExport(std::uint64_t match) : match_(match) {}
    void requested(bool enabled) noexcept;
    bool enabled() const noexcept {
        return requested_.load(std::memory_order_acquire);
    }
    bool wants_baseline() const noexcept;
    void begin_capture(Cursor next) noexcept;
    void publish_baseline(Cursor next, std::uint64_t schema, std::uint64_t controls,
                          std::shared_ptr<const std::vector<std::uint8_t>> bytes, Digest hash) noexcept;
    void committed(ConfirmedFrame frame) noexcept;
    std::shared_ptr<const Baseline> baseline() const;
    std::vector<ConfirmedFrame> after(Cursor first, std::size_t maximum = 30) const;
    Cursor next() const;
    bool covers(Cursor first) const;
    void resync() noexcept;
    std::string failure() const;

  private:
    std::uint64_t match_ = 0;
    std::atomic<bool> requested_{false}, baseline_requested_{true};
    mutable std::mutex mutex_;
    ConfirmedFeed feed_;
    std::optional<Cursor> capture_;
    std::deque<ConfirmedFrame> captured_tail_;
    std::string failure_;
};
// Binding and callback run on the SAME simulation owner. The thread-local
// pointer is never serialized, retained by transport or invoked on a GPU.
class ConfirmedExportBinding final {
  public:
    explicit ConfirmedExportBinding(ConfirmedExport* stream) noexcept;
    ~ConfirmedExportBinding();
    ConfirmedExportBinding(const ConfirmedExportBinding&) = delete;
    ConfirmedExportBinding& operator=(const ConfirmedExportBinding&) = delete;

  private:
    ConfirmedExport* previous_;
};
// Called by Driver only after successful irreversible commit, with actual
// inputs and the actual deterministic SDK2 command. Exceptions are contained
// here: a broken/slow watcher cannot stop a racing commit.
void observe_confirmed(std::uint64_t epoch, std::uint32_t frame, const std::array<PortInput, RacingPorts>& inputs,
                       std::span<const std::uint32_t> command, bool boundary) noexcept;
class ConfirmedFrameBinding final {
  public:
    using Sink = std::function<void(ConfirmedFrame)>;
    explicit ConfirmedFrameBinding(Sink sink);
    ~ConfirmedFrameBinding();
    ConfirmedFrameBinding(const ConfirmedFrameBinding&) = delete;

  private:
    Sink previous_;
};
// The registry owns no raw session pointer. A launch's host owner and its
// transport share this bounded stream; weak entries expire after retirement.
std::shared_ptr<ConfirmedExport> acquire_export(std::uint64_t match);
} // namespace dkr::runtime::spectators
