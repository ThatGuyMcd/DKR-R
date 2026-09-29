#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace dkr::runtime::netplay {

// Stores fixed-size rollback states as periodic full checkpoints plus changed
// pages. A token is what GekkoNet saves; the large recomp state stays in this
// bounded store instead of being copied into GekkoNet for every frame.
class RollbackStateStore final {
public:
    static constexpr std::size_t kPageBytes = 4096U;

    RollbackStateStore(std::size_t state_bytes, std::size_t capacity,
                       std::size_t checkpoint_interval = 4U);

    bool save(std::uint32_t frame, std::span<const std::uint8_t> state,
              std::uint64_t checksum);
    bool load(std::uint32_t frame, std::span<std::uint8_t> state,
              std::uint64_t* checksum = nullptr) const;
    void discard_after(std::uint32_t frame);
    void clear();

    std::size_t state_bytes() const { return state_bytes_; }
    std::size_t capacity() const { return capacity_; }
    std::size_t memory_bytes() const;
    // Includes retained capacities/spare checkpoint entries, for callers with
    // a hard memory budget. memory_bytes() retains its existing live-byte meaning.
    std::size_t allocated_bytes() const;
    bool contains(std::uint32_t frame) const;

private:
    struct Entry {
        std::uint32_t frame = 0U;
        std::uint64_t checksum = 0U;
        bool checkpoint = false;
        // All changed pages share one allocation. The previous representation
        // allocated one vector per 4 KiB page (thousands of allocations for a
        // DKR checkpoint), which produced long frame-time spikes during
        // rollback and water-heavy scenes.
        std::vector<std::uint32_t> page_indices;
        std::vector<std::uint8_t> page_bytes;
    };

    bool reconstruct(std::size_t entry_index,
                     std::span<std::uint8_t> state) const;
    void ensure_front_is_checkpoint();

    std::size_t state_bytes_ = 0U;
    std::size_t capacity_ = 0U;
    std::size_t checkpoint_interval_ = 0U;
    std::deque<Entry> entries_;
    std::vector<Entry> spare_entries_;
    std::vector<std::uint8_t> latest_state_;
};

} // namespace dkr::runtime::netplay
