#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace dkr::runtime {
// A shared_ptr one thread publishes and others read. A mutex guards the
// pointer itself, so every standard library behaves the same: libc++ (Apple,
// Android) has no std::atomic<std::shared_ptr>. The memory_order arguments
// mirror that interface; the lock already orders the accesses at least as
// strongly. A replaced value is released outside the lock.
template<class T> class SharedSnapshot {
    mutable std::mutex mutex_;
    std::shared_ptr<T> value_;
public:
    SharedSnapshot() = default;
    explicit SharedSnapshot(std::shared_ptr<T> value) : value_(std::move(value)) {}
    SharedSnapshot(const SharedSnapshot&) = delete;
    SharedSnapshot& operator=(const SharedSnapshot&) = delete;
    std::shared_ptr<T> load(std::memory_order = std::memory_order_seq_cst) const {
        std::scoped_lock lock(mutex_);
        return value_;
    }
    void store(std::shared_ptr<T> value, std::memory_order = std::memory_order_seq_cst) {
        std::shared_ptr<T> previous;
        {
            std::scoped_lock lock(mutex_);
            previous = std::exchange(value_, std::move(value));
        }
    }
};
}
