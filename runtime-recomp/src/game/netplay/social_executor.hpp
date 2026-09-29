#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace dkr::runtime::netplay {

// SDK callbacks only enqueue events. No callback may wait for the social state
// lock, and no network construction, close or disk flush runs on the UI thread.
class SocialExecutor {
public:
    using Job = std::function<void()>;
    ~SocialExecutor() { stop(); }
    void start(Job tick, Job failure) {
        stop();
        {
            std::lock_guard lock(mutex_);
            accepting_ = true;
        }
        thread_ = std::thread([this, tick = std::move(tick), failure = std::move(failure)] {
            auto deadline = std::chrono::steady_clock::now();
            for (;;) {
                Job job;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait_until(lock, deadline, [this] {
                        return !accepting_ || !jobs_.empty();
                    });
                    if (!accepting_ && jobs_.empty()) break;
                    if (!jobs_.empty()) {
                        job = std::move(jobs_.front());
                        jobs_.pop_front();
                    }
                }
                const auto report_failure = [&] {
                    try { failure(); } catch (...) {}
                };
                // A failed queued operation must not starve scheduled ticks.
                try { if (job) job(); } catch (...) { report_failure(); }
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    // Advance even if user code throws: no expired-deadline loop.
                    deadline = now + std::chrono::milliseconds(100);
                    try { tick(); } catch (...) { report_failure(); }
                    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                }
            }
        });
    }
    bool post(Job job) {
        std::lock_guard lock(mutex_);
        if (!accepting_ || jobs_.size() >= 1024U) {
            ++rejected_;
            return false;
        }
        jobs_.push_back(std::move(job));
        changed_.notify_one();
        return true;
    }
    void stop() {
        {
            std::lock_guard lock(mutex_);
            accepting_ = false;
        }
        changed_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    std::uint64_t rejected() const {
        std::lock_guard lock(mutex_);
        return rejected_;
    }
private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Job> jobs_;
    std::thread thread_;
    bool accepting_ = false;
    std::uint64_t rejected_ = 0U;
};

} // namespace dkr::runtime::netplay
