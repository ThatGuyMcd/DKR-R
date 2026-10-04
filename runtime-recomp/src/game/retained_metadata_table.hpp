#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dkr::runtime::presentation {

// Decoder-thread-local metadata only. Entries own their values; no span or
// guest pointer survives a task. begin() reserves from the validated input
// count, and keeps both arrays for the next frame instead of allocating and
// freeing an unordered_map node (and marker vector) for every draw identity.
// A load factor <= 1/2 bounds probes. No erase/rehash or nested task support.
template<class Key, class Value>
class RetainedMetadataTable {
    static_assert(std::is_unsigned_v<Key>);
    struct Slot { std::uint64_t generation = 0; std::size_t entry = 0; };
    struct Entry { Key key; Value value; };
    std::vector<Slot> slots_;
    std::vector<Entry> entries_;
    std::uint64_t generation_ = 1;
    std::size_t limit_ = 0;

    std::size_t bucket(Key key) const {
        // Matrix/command addresses are aligned. Mix all bits rather than
        // indexing with their low zero bits. Shadow keys include the scene.
        auto value = std::uint64_t(key);
        value ^= value >> 30; value *= UINT64_C(0xBF58476D1CE4E5B9);
        value ^= value >> 27; value *= UINT64_C(0x94D049BB133111EB);
        value ^= value >> 31;
        return std::size_t(value) & (slots_.size() - 1);
    }
public:
    void clear() {
        entries_.clear();
        limit_ = 0;
        if (++generation_ == 0) {
            std::fill(slots_.begin(), slots_.end(), Slot{});
            generation_ = 1;
        }
    }
    void begin(std::size_t maximum_entries) {
        clear();
        // Owned draw metadata is bounded at publication. This also makes a
        // future caller error fail before any unbounded reserve or probe.
        if (maximum_entries > 8192) throw std::length_error("Owned metadata table limit");
        std::size_t capacity = 2;
        while (capacity < maximum_entries * 2) capacity *= 2;
        if (capacity > slots_.size()) slots_.resize(capacity);
        entries_.reserve(maximum_entries);
        limit_ = maximum_entries;
    }
    const Value* find(Key key) const {
        if (entries_.empty()) return nullptr;
        auto index = bucket(key);
        // The table always has a vacant slot, even at its admitted limit.
        for (std::size_t count = 0; count < slots_.size(); ++count) {
            const auto& slot = slots_[index];
            if (slot.generation != generation_) return nullptr;
            const auto& entry = entries_[slot.entry];
            if (entry.key == key) return &entry.value;
            index = (index + 1) & (slots_.size() - 1);
        }
        return nullptr;
    }
    std::pair<Value*, bool> try_emplace(Key key, const Value& value = {}) {
        if (slots_.empty()) throw std::logic_error("Owned metadata table not begun");
        auto index = bucket(key);
        for (std::size_t count = 0; count < slots_.size(); ++count) {
            auto& slot = slots_[index];
            if (slot.generation != generation_) {
                if (entries_.size() >= limit_) throw std::length_error("Owned metadata table exhausted");
                entries_.push_back({key, value});
                slot = {generation_, entries_.size() - 1};
                return {&entries_.back().value, true};
            }
            auto& entry = entries_[slot.entry];
            if (entry.key == key) return {&entry.value, false};
            index = (index + 1) & (slots_.size() - 1);
        }
        throw std::logic_error("Owned metadata table has no vacant slot");
    }
    void insert_or_assign(Key key, const Value& value) {
        auto [entry, inserted] = try_emplace(key, value);
        if (!inserted) *entry = value;
    }
    std::size_t size() const { return entries_.size(); }
    std::size_t entry_capacity() const { return entries_.capacity(); }
    std::size_t slot_capacity() const { return slots_.size(); }
};
} // namespace dkr::runtime::presentation
