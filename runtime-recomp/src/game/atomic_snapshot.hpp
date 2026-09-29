#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace dkr::runtime {
// NDK libc++ does not yet implement atomic<shared_ptr>. Its shared_ptr
// atomic free functions provide the same publication/ownership semantics.
// Keep the existing desktop specialization unchanged.
#if defined(__ANDROID__) || defined(DKR_TEST_ATOMIC_SNAPSHOT_FALLBACK)
template<class T> class AtomicSnapshot {
    std::shared_ptr<T> value_;
public:
    AtomicSnapshot() = default;
    explicit AtomicSnapshot(std::shared_ptr<T> value) : value_(std::move(value)) {}
    AtomicSnapshot(const AtomicSnapshot&) = delete;
    AtomicSnapshot& operator=(const AtomicSnapshot&) = delete;
    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const {
        return std::atomic_load_explicit(&value_, order);
    }
    void store(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst) {
        std::atomic_store_explicit(&value_, std::move(value), order);
    }
};
#else
template<class T> using AtomicSnapshot = std::atomic<std::shared_ptr<T>>;
#endif
}
