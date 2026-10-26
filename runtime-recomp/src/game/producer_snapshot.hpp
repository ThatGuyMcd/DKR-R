#pragma once
#include "graphics_snapshot_pool.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <utility>

namespace dkr::runtime::graphics_handoff {
// One owner transfers the memory image across BOTH scheduler queues. Keys are
// checked in submission order; searching by a recycled display-list address
// would risk handing an older frame to a newer task.
template <std::size_t Bytes, std::size_t Capacity = 4> class Mailbox {
  public:
    using Pool = GraphicsSnapshotPool<Bytes>;
    using Snapshot = typename Pool::Snapshot;
    struct Metrics {
        std::uint64_t captured = 0, transferred = 0, fallback = 0, mismatch = 0, overflow = 0;
    };
    struct CopyBytes {
        void operator()(std::uint8_t* dst, const std::uint8_t* src, std::size_t bytes) const {
            std::memcpy(dst, src, bytes);
        }
    };

  private:
    struct Entry {
        const void* owner = nullptr;
        std::uint32_t root = 0, count = 0;
        Snapshot image{};
    };
    Pool pool_;
    std::mutex mutex_;
    std::array<Entry, Capacity> pending_{};
    std::size_t first_ = 0, size_ = 0;
    bool desynchronised_ = false;
    std::uint64_t era_ = 0;
    Metrics metrics_{};
    void discard_locked() {
        for (auto& entry : pending_)
            entry = Entry{};
        first_ = size_ = 0;
    }
    static bool address(std::uint32_t raw, std::uint32_t& physical) {
        const auto segment = raw & 0xE0000000U;
        if (segment != 0U && segment != 0x80000000U)
            return false;
        physical = raw & 0x1FFFFFFFU;
        return physical <= Bytes && !(physical & 7U);
    }

  public:
    template <class Copy = CopyBytes>
    bool capture(const std::uint8_t* ram, std::uint32_t begin, std::uint32_t end, Copy copy = {}) {
        std::uint32_t root = 0, finish = 0;
        if (!ram || !address(begin, root) || !address(end, finish) || finish <= root) {
            invalidate();
            return false;
        }
        std::uint64_t era = 0;
        {
            std::lock_guard lock(mutex_);
            if (desynchronised_)
                return false;
            if (size_ == Capacity) {
                ++metrics_.overflow;
                discard_locked();
                desynchronised_ = true;
                return false;
            }
            era = era_;
        }
        auto image = pool_.acquire();
        copy(image.get(), ram, Bytes);
        std::lock_guard lock(mutex_);
        if (desynchronised_ || era != era_)
            return false;
        if (size_ == Capacity) {
            ++metrics_.overflow;
            discard_locked();
            desynchronised_ = true;
            return false;
        }
        auto& slot = pending_[(first_ + size_) % Capacity];
        slot = {ram, root, (finish - root) / 8U, std::move(image)};
        ++size_;
        ++metrics_.captured;
        return true;
    }
    Snapshot take(const std::uint8_t* ram, std::uint32_t begin, std::uint32_t count) {
        std::uint32_t root = 0;
        const bool valid = ram && address(begin, root) && root < Bytes && count > 0 && count <= (Bytes - root) / 8U;
        std::lock_guard lock(mutex_);
        if (size_ && !desynchronised_) {
            auto& slot = pending_[first_];
            if (valid && slot.owner == ram && slot.root == root && slot.count == count) {
                auto image = std::move(slot.image);
                slot = Entry{};
                first_ = (first_ + 1U) % Capacity;
                --size_;
                ++metrics_.transferred;
                return image;
            }
            ++metrics_.mismatch;
            desynchronised_ = true;
        }
        // Never let an unmatched producer image survive and match a later
        // reuse of the same root address. The caller retains the old safe
        // submission-copy fallback for nonstandard tasks.
        discard_locked();
        ++metrics_.fallback;
        return Snapshot{};
    }
    Snapshot acquire() {
        return pool_.acquire();
    }
    void invalidate() {
        std::lock_guard lock(mutex_);
        discard_locked();
        desynchronised_ = true;
    }
    Metrics metrics() {
        std::lock_guard lock(mutex_);
        return metrics_;
    }
    Metrics reset() {
        std::lock_guard lock(mutex_);
        auto result = metrics_;
        ++era_;
        discard_locked();
        metrics_ = {};
        desynchronised_ = false;
        return result;
    }
};

using ProductionMailbox = Mailbox<0x800000>;
using Snapshot = ProductionMailbox::Snapshot;
bool capture(const std::uint8_t*, std::uint32_t, std::uint32_t) noexcept;
Snapshot take(const std::uint8_t*, std::uint32_t, std::uint32_t);
Snapshot acquire();
void discard(const std::uint8_t*, std::uint32_t, std::uint32_t);
void reset() noexcept;
} // namespace dkr::runtime::graphics_handoff
