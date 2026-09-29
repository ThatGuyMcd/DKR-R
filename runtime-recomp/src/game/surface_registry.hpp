#pragma once
#include <cstdint>
#include <mutex>

namespace dkr::runtime {
// Ref-counted platform handles may outlive a surface-destroyed callback while
// the renderer drains old work. GPU ownership remains with the renderer.
template<class Handle, void (*Acquire)(Handle*), void (*Release)(Handle*)>
class SurfaceRegistry {
    mutable std::mutex mutex_;
    Handle* handle_ = nullptr;
    std::uint64_t generation_ = 0;
public:
    ~SurfaceRegistry() { if (handle_) Release(handle_); }
    void publish(Handle* next) {
        std::lock_guard lock(mutex_);
        if (next == handle_) return;
        if (next) Acquire(next);
        if (handle_) Release(handle_);
        handle_ = next; ++generation_;
    }
    bool matches(std::uint64_t expected) const {
        std::lock_guard lock(mutex_);
        return handle_ && generation_ == expected;
    }
    struct Lease {
        Handle* window;
        std::uint64_t generation;
        explicit Lease(SurfaceRegistry& owner) {
            std::lock_guard lock(owner.mutex_);
            window = owner.handle_; generation = owner.generation_;
            if (window) Acquire(window);
        }
        ~Lease() { if (window) Release(window); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
    };
};
}
