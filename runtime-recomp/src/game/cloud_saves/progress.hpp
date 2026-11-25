#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace dkr::cloud_saves {
struct ProgressBatch {
    unsigned account_generation;
    std::set<std::string_view> slots;
};

class ProgressWrites {
  public:
    using Clock = std::chrono::steady_clock;
    using Routes = std::vector<std::pair<std::filesystem::path, std::string_view>>;

    void configure(Routes next) {
        std::scoped_lock lock(mutex_);
        routes_ = std::move(next);
        pending_.clear();
    }
    void gameplay(bool active) {
        std::scoped_lock lock(mutex_);
        gameplay_ = active;
        if (!active) {
            toast_until_ = 0;
            next_toast_ = {};
        }
    }
    void account(std::optional<unsigned> generation) {
        std::scoped_lock lock(mutex_);
        if (generation == account_)
            return;
        account_ = generation;
        pending_.clear();
        retry_at_ = {};
        changed_.notify_all();
    }
    void committed(const std::filesystem::path& path, Clock::time_point now = Clock::now()) {
        std::scoped_lock lock(mutex_);
        if (!gameplay_)
            return;
        if (now >= next_toast_) {
            toast_until_ = milliseconds(now + std::chrono::milliseconds(850));
            next_toast_ = now + std::chrono::milliseconds(1050);
        }
        if (account_) {
            for (const auto& [candidate, slot] : routes_) {
                if (candidate == path) {
                    pending_.insert(slot);
                    changed_.notify_one();
                    break;
                }
            }
        }
    }
    bool visible(Clock::time_point now = Clock::now()) const {
        return milliseconds(now) < toast_until_.load();
    }
    std::optional<ProgressBatch> take(std::stop_token stop) {
        std::unique_lock lock(mutex_);
        while (!stop.stop_requested()) {
            if (!changed_.wait(lock, stop, [&] { return account_ && !pending_.empty(); }))
                break;
            const auto account = account_;
            if (changed_.wait_until(lock, stop, retry_at_, [&] { return account_ != account; }))
                continue;
            if (!stop.stop_requested())
                return ProgressBatch{*account, std::exchange(pending_, {})};
        }
        return std::nullopt;
    }
    void retry(const ProgressBatch& batch) {
        std::scoped_lock lock(mutex_);
        if (account_ != batch.account_generation)
            return;
        pending_.insert(batch.slots.begin(), batch.slots.end());
        retry_at_ = Clock::now() + std::chrono::seconds(5);
        changed_.notify_one();
    }

  private:
    static std::int64_t milliseconds(Clock::time_point time) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    }

    std::mutex mutex_;
    std::condition_variable_any changed_;
    Routes routes_;
    std::set<std::string_view> pending_;
    std::optional<unsigned> account_;
    Clock::time_point retry_at_, next_toast_;
    std::atomic<std::int64_t> toast_until_{0};
    bool gameplay_ = false;
};

inline ProgressWrites progress_writes;
} // namespace dkr::cloud_saves
