#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dkr::runtime::surgical {
// Dense samples plus an open-addressed index. No sample survives clear(), but
// storage does. At most 16384 entries, <= 50% index occupancy. No node allocator
// activity once the scene's high-water mark has been reached.
template <class Key, class Value> class ReusableFlowMap {
    static_assert(std::is_same_v<Key, std::uint32_t>);
    static constexpr std::size_t maximum = 16384;
    struct Entry {
        Key first;
        Value second;
    };
    std::vector<Entry> entries_;
    std::vector<std::uint32_t> slots_;
    static std::size_t bucket(Key key, std::size_t mask) noexcept {
        key ^= key >> 16;
        key *= 0x7feb352dU;
        key ^= key >> 15;
        return std::size_t(key) & mask;
    }
    void index(std::vector<std::uint32_t>& slots, std::size_t entry) const noexcept {
        const auto mask = slots.size() - 1;
        auto at = bucket(entries_[entry].first, mask);
        while (slots[at])
            at = (at + 1) & mask;
        slots[at] = std::uint32_t(entry + 1);
    }

  public:
    auto end() noexcept {
        return entries_.end();
    }
    std::size_t size() const noexcept {
        return entries_.size();
    }
    auto find(Key key) noexcept {
        if (slots_.empty())
            return end();
        const auto mask = slots_.size() - 1;
        auto at = bucket(key, mask);
        for (std::size_t probes = 0; probes < slots_.size(); ++probes) {
            const auto slot = slots_[at];
            if (!slot)
                return end();
            if (entries_[slot - 1].first == key)
                return entries_.begin() + (slot - 1);
            at = (at + 1) & mask;
        }
        return end();
    }
    void emplace(Key key, Value value) {
        if (find(key) != end())
            return;
        if (entries_.size() >= maximum)
            throw std::length_error("flow sample storage bound exceeded");
        if (slots_.empty() || (entries_.size() + 1) * 2 > slots_.size()) {
            const auto capacity = slots_.empty() ? 256U : slots_.size() * 2;
            std::vector<std::uint32_t> next(capacity, 0);
            for (std::size_t i = 0; i < entries_.size(); ++i)
                index(next, i);
            entries_.reserve(capacity / 2);
            slots_.swap(next);
        }
        entries_.push_back({key, std::move(value)});
        index(slots_, entries_.size() - 1);
    }
    void clear() noexcept {
        entries_.clear();
        std::fill(slots_.begin(), slots_.end(), 0);
    }
    void swap(ReusableFlowMap& other) noexcept {
        entries_.swap(other.entries_);
        slots_.swap(other.slots_);
    }
};
} // namespace dkr::runtime::surgical
