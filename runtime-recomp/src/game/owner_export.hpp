#pragma once
#include "confirmed_observer.hpp"
#include <chrono>
#include <future>

namespace dkr::runtime::spectators {
// The only canonical-world operation is capture(), invoked on its owner only
// when the complete world frontier equals the committed frontier. Hashing is
// performed on a private immutable copy by one bounded background job.
class OwnerExport final {
  public:
    OwnerExport(std::uint64_t match, bool host);
    template <class World, class Hash>
    void poll(std::uint64_t epoch, std::uint32_t next, std::uint32_t confirmed, bool can_publish, World& world,
              Hash hash) noexcept {
        if (!stream_)
            return;
        try {
            if (epoch_ && epoch != epoch_)
                stream_->resync();
            epoch_ = epoch;
            if (work_.valid() && work_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto completed = work_.get();
                if (completed.next.epoch == epoch)
                    stream_->publish_baseline(completed.next, completed.schema, completed.control_generation,
                                              completed.state, completed.digest);
                else
                    stream_->resync();
            }
            if (work_.valid() || !stream_->wants_baseline() || next != confirmed || !can_publish)
                return;
            const auto now = std::chrono::steady_clock::now();
            if (now < retry_after_)
                return;
            // An unavailable observer snapshot must not turn a hot 1ms poll
            // into repeated full-world allocations or capture/hash attempts.
            retry_after_ = now + std::chrono::seconds(1);
            const auto contract = world.contract();
            if (!contract.schema || !contract.state_bytes || contract.state_bytes > ConfirmedFeed::MaximumState)
                return;
            auto state = std::make_shared<std::vector<std::uint8_t>>(contract.state_bytes);
            std::string error;
            if (!world.capture(*state, error)) {
                stream_->resync();
                return;
            }
            const Cursor cursor{epoch, next};
            stream_->begin_capture(cursor);
            work_ = std::async(std::launch::async,
                               [state = std::shared_ptr<const std::vector<std::uint8_t>>(std::move(state)), cursor,
                                schema = contract.schema, hash] {
                                   Baseline b;
                                   b.next = cursor;
                                   b.schema = schema;
                                   b.control_generation = 1;
                                   b.state = state;
                                   b.digest = hash(*state);
                                   return b;
                               });
        } catch (...) {
            stream_->resync();
        }
    }

  private:
    std::shared_ptr<ConfirmedExport> stream_;
    ConfirmedExportBinding binding_;
    std::future<Baseline> work_;
    std::chrono::steady_clock::time_point retry_after_{};
    std::uint64_t epoch_ = 0;
};
} // namespace dkr::runtime::spectators
