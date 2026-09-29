#include "rollback_state_store.hpp"

#include <algorithm>
#include <cstring>

namespace dkr::runtime::netplay {

RollbackStateStore::RollbackStateStore(std::size_t state_bytes,
                                       std::size_t capacity,
                                       std::size_t checkpoint_interval)
    : state_bytes_(state_bytes), capacity_(capacity),
      checkpoint_interval_(checkpoint_interval), latest_state_(state_bytes) {}

bool RollbackStateStore::save(std::uint32_t frame,
                              std::span<const std::uint8_t> state,
                              std::uint64_t checksum) {
    if (state.size() != state_bytes_ || state_bytes_ == 0U || capacity_ == 0U ||
        checkpoint_interval_ == 0U) {
        return false;
    }
    if (!entries_.empty() && frame <= entries_.back().frame) {
        discard_after(frame == 0U ? 0U : frame - 1U);
        if (frame == 0U) clear();
    }
    Entry entry{};
    if (!spare_entries_.empty()) {
        entry = std::move(spare_entries_.back());
        spare_entries_.pop_back();
    }
    entry.frame = frame;
    entry.checksum = checksum;
    entry.checkpoint = entries_.empty() ||
                       frame % checkpoint_interval_ == 0U;
    entry.page_indices.clear();
    entry.page_bytes.clear();
    const std::size_t page_count =
        (state_bytes_ + kPageBytes - 1U) / kPageBytes;
    if (entry.page_indices.capacity() < page_count) {
        entry.page_indices.reserve(page_count);
    }
    if (entry.checkpoint && entry.page_bytes.capacity() < state_bytes_) {
        entry.page_bytes.reserve(state_bytes_);
    }
    for (std::size_t page = 0; page < page_count; ++page) {
        const std::size_t first = page * kPageBytes;
        const std::size_t size = (std::min)(kPageBytes, state_bytes_ - first);
        const bool changed = entry.checkpoint ||
            std::memcmp(state.data() + first, latest_state_.data() + first,
                        size) != 0;
        if (!changed) continue;
        entry.page_indices.push_back(static_cast<std::uint32_t>(page));
        entry.page_bytes.insert(
            entry.page_bytes.end(),
            state.begin() + static_cast<std::ptrdiff_t>(first),
            state.begin() + static_cast<std::ptrdiff_t>(first + size));
    }
    std::copy(state.begin(), state.end(), latest_state_.begin());
    entries_.push_back(std::move(entry));
    while (entries_.size() > capacity_) {
        // Never synthesize a new 16 MiB checkpoint while the game is running.
        // Retire the complete oldest checkpoint group instead. The runtime
        // provisions eight guard frames beyond the configured rollback window,
        // so discarding at most checkpoint_interval-1 extra history frames is
        // safe and removes a recurring full-state allocation/copy spike.
        do {
            if (spare_entries_.size() < checkpoint_interval_ + 1U) {
                spare_entries_.push_back(std::move(entries_.front()));
            }
            entries_.pop_front();
        } while (!entries_.empty() && !entries_.front().checkpoint);
    }
    ensure_front_is_checkpoint();
    return true;
}

bool RollbackStateStore::reconstruct(std::size_t entry_index,
                                     std::span<std::uint8_t> state) const {
    if (entry_index >= entries_.size() || state.size() != state_bytes_) {
        return false;
    }
    std::size_t checkpoint = entry_index;
    while (checkpoint > 0U && !entries_[checkpoint].checkpoint) --checkpoint;
    if (!entries_[checkpoint].checkpoint) return false;
    std::fill(state.begin(), state.end(), 0U);
    for (std::size_t index = checkpoint; index <= entry_index; ++index) {
        const Entry& entry = entries_[index];
        std::size_t byte_cursor = 0U;
        for (const std::uint32_t page : entry.page_indices) {
            const std::size_t first =
                static_cast<std::size_t>(page) * kPageBytes;
            if (first >= state.size()) return false;
            const std::size_t size =
                (std::min)(kPageBytes, state.size() - first);
            if (byte_cursor + size > entry.page_bytes.size()) return false;
            std::copy_n(entry.page_bytes.begin() +
                            static_cast<std::ptrdiff_t>(byte_cursor),
                        size,
                        state.begin() + static_cast<std::ptrdiff_t>(first));
            byte_cursor += size;
        }
        if (byte_cursor != entry.page_bytes.size()) return false;
    }
    return true;
}

bool RollbackStateStore::load(std::uint32_t frame,
                              std::span<std::uint8_t> state,
                              std::uint64_t* checksum) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [frame](const Entry& entry) { return entry.frame == frame; });
    if (found == entries_.end()) return false;
    const std::size_t index = static_cast<std::size_t>(
        std::distance(entries_.begin(), found));
    if (!reconstruct(index, state)) return false;
    if (checksum != nullptr) *checksum = found->checksum;
    return true;
}

void RollbackStateStore::discard_after(std::uint32_t frame) {
    while (!entries_.empty() && entries_.back().frame > frame) {
        if (spare_entries_.size() < checkpoint_interval_ + 1U) {
            spare_entries_.push_back(std::move(entries_.back()));
        }
        entries_.pop_back();
    }
    if (entries_.empty()) {
        std::fill(latest_state_.begin(), latest_state_.end(), 0U);
    } else {
        reconstruct(entries_.size() - 1U, latest_state_);
    }
}

void RollbackStateStore::clear() {
    while (!entries_.empty()) {
        if (spare_entries_.size() < checkpoint_interval_ + 1U) {
            spare_entries_.push_back(std::move(entries_.back()));
        }
        entries_.pop_back();
    }
    std::fill(latest_state_.begin(), latest_state_.end(), 0U);
}

void RollbackStateStore::ensure_front_is_checkpoint() {
    if (!entries_.empty() && !entries_.front().checkpoint) {
        entries_.clear();
        std::fill(latest_state_.begin(), latest_state_.end(), 0U);
    }
}

std::size_t RollbackStateStore::memory_bytes() const {
    std::size_t result = latest_state_.size();
    for (const Entry& entry : entries_) {
        result += entry.page_bytes.size();
        result += entry.page_indices.size() * sizeof(std::uint32_t);
    }
    return result;
}

bool RollbackStateStore::contains(std::uint32_t frame) const {
    return std::any_of(entries_.begin(), entries_.end(),
        [frame](const Entry& entry) { return entry.frame == frame; });
}

std::size_t RollbackStateStore::allocated_bytes() const {
    std::size_t result = latest_state_.capacity() +
        entries_.size() * sizeof(Entry) + spare_entries_.capacity() * sizeof(Entry);
    const auto add = [&result](const Entry& entry) {
        result += entry.page_bytes.capacity();
        result += entry.page_indices.capacity() * sizeof(std::uint32_t);
    };
    for (const auto& entry : entries_) add(entry);
    for (const auto& entry : spare_entries_) add(entry);
    return result;
}

} // namespace dkr::runtime::netplay
