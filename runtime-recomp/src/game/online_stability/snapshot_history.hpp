#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace dkr::online_stability {
// Private rollback history, never network state. Capture directly into a
// bounded pool of full images when it fits the owner's budget.
// This avoids the second full-image copy, page comparison and delta assembly on
// every replay tick. Larger mod worlds retain the existing bounded paged store.
template <class PagedStore> class SnapshotHistory final {
    static constexpr unsigned MaximumWindow = 20;
    struct Slot {
        std::unique_ptr<std::uint8_t[]> image;
        std::uint32_t frame = 0;
        std::uint64_t hash = 0;
        bool valid = false;
    };
    std::size_t bytes_ = 0, count_ = 0;
    std::array<Slot, MaximumWindow + 1> slots_;
    std::unique_ptr<PagedStore> paged_;
    std::optional<std::uint32_t> staged_;
    std::uint32_t protected_until_ = 0, retired_frontier_ = 0;
    Slot* find(std::uint32_t frame) {
        const auto it =
            std::find_if(slots_.begin(), slots_.end(), [&](const auto& s) { return s.valid && s.frame == frame; });
        return it == slots_.end() ? nullptr : &*it;
    }
    const Slot* find(std::uint32_t frame) const {
        return const_cast<SnapshotHistory*>(this)->find(frame);
    }
    Slot* spare() {
        const auto it = std::find_if(slots_.begin(), slots_.end(), [](const auto& s) { return s.image && !s.valid; });
        return it == slots_.end() ? nullptr : &*it;
    }
    std::size_t images() const {
        return std::count_if(slots_.begin(), slots_.end(), [](const auto& s) { return bool(s.image); });
    }
    void trim_direct() {
        auto retained = images();
        const auto correction =
            protected_until_ >= retired_frontier_ ? std::size_t(protected_until_ - retired_frontier_) + 1 : 0;
        const auto required = std::max(count_, correction);
        for (auto it = slots_.rbegin(); it != slots_.rend() && retained > required; ++it)
            if (it->image && !it->valid) {
                it->image.reset();
                --retained;
            }
    }

  public:
    SnapshotHistory(std::size_t bytes, unsigned window, std::size_t budget) : bytes_(bytes), count_(window + 1U) {
        if (bytes && window <= MaximumWindow && budget >= sizeof(slots_) &&
            count_ <= (budget - sizeof(slots_)) / bytes) {
            for (unsigned i = 0; i < count_; ++i)
                slots_[i].image.reset(new std::uint8_t[bytes]);
        } else
            paged_ = std::make_unique<PagedStore>(bytes, MaximumWindow + 8U, 4U, 2U, true);
    }
    bool direct() const {
        return !paged_;
    }
    // Growth allocates only additional images. Existing images, hashes and
    // outstanding speculative history keep their identity. Failed growth
    // releases its staged allocations and leaves the active pool unchanged.
    bool reserve_window(unsigned window, std::size_t budget, std::uint32_t preserve_until = 0) {
        if (window < 2 || window > MaximumWindow || staged_)
            return false;
        if (paged_)
            return paged_->allocated_bytes() <= budget;
        const auto required = std::size_t(window) + 1, have = images();
        const auto extra = required > have ? required - have : 0;
        if (budget < allocated_bytes() || extra > (budget - allocated_bytes()) / bytes_)
            return false;
        std::array<std::unique_ptr<std::uint8_t[]>, MaximumWindow + 1> pending;
        try {
            for (std::size_t i = 0; i < extra; ++i)
                pending[i].reset(new std::uint8_t[bytes_]);
        } catch (const std::bad_alloc&) {
            return false;
        }
        std::size_t next = 0;
        for (auto& slot : slots_)
            if (!slot.image && next < extra)
                slot.image = std::move(pending[next++]);
        if (required < count_) {
            protected_until_ = std::max(protected_until_, preserve_until);
            for (const auto& slot : slots_)
                if (slot.valid)
                    protected_until_ = std::max(protected_until_, slot.frame);
        }
        count_ = required;
        trim_direct();
        return true;
    }
    std::span<std::uint8_t> staging(std::uint32_t frame) {
        staged_.reset();
        if (paged_ || find(frame))
            return {};
        auto* slot = spare();
        if (!slot)
            return {};
        staged_ = frame;
        return {slot->image.get(), bytes_};
    }
    bool save(std::uint32_t frame, std::span<const std::uint8_t> state, std::uint64_t hash) {
        if (paged_)
            return paged_->save(frame, state, hash);
        if (state.size() != bytes_)
            return false;
        if (find(frame))
            return false;
        auto* slot = spare();
        if (!slot)
            return false;
        auto* target = slot->image.get();
        if (state.data() == target) {
            if (staged_ != frame)
                return false;
        } else
            std::memcpy(target, state.data(), bytes_);
        slot->frame = frame;
        slot->hash = hash;
        slot->valid = true;
        staged_.reset();
        return true;
    }
    bool load(std::uint32_t frame, std::span<std::uint8_t> state, std::uint64_t* hash = nullptr) const {
        if (paged_)
            return paged_->load(frame, state, hash);
        const auto* slot = find(frame);
        if (!slot || state.size() != bytes_)
            return false;
        std::memcpy(state.data(), slot->image.get(), bytes_);
        if (hash)
            *hash = slot->hash;
        return true;
    }
    void discard_after(std::uint32_t frame) {
        staged_.reset();
        if (paged_) {
            paged_->discard_after(frame);
            return;
        }
        for (auto& slot : slots_)
            if (slot.valid && slot.frame > frame)
                slot.valid = false;
        trim_direct();
    }
    void retire_before(std::uint32_t frame) {
        if (paged_) {
            paged_->retire_before(frame);
            return;
        }
        retired_frontier_ = std::max(retired_frontier_, frame);
        for (auto& slot : slots_)
            if (slot.valid && slot.frame < frame)
                slot.valid = false;
        if (!staged_)
            trim_direct();
    }
    void trim_spares(std::size_t budget) {
        if (paged_)
            paged_->trim_spares(budget);
    }
    std::size_t allocated_bytes() const {
        return paged_ ? paged_->allocated_bytes() : bytes_ * images() + sizeof(slots_);
    }
};
} // namespace dkr::online_stability
