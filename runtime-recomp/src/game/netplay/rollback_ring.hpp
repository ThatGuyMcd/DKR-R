#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dkr::runtime::netplay {

class RollbackRing final {
  public:
    RollbackRing(std::size_t capacity, std::size_t state_size);
    bool save(std::uint32_t frame, std::span<const std::uint8_t> state, std::uint64_t checksum);
    bool load(std::uint32_t frame, std::span<std::uint8_t> state, std::uint64_t* checksum = nullptr) const;
    bool contains(std::uint32_t frame) const;
    void clear();
    std::size_t capacity() const {
        return slots_.size();
    }
    std::size_t state_size() const {
        return state_size_;
    }

  private:
    struct Slot {
        std::uint32_t frame = 0U;
        std::uint64_t checksum = 0U;
        std::vector<std::uint8_t> bytes;
        bool valid = false;
    };
    std::vector<Slot> slots_;
    std::size_t state_size_ = 0U;
};

std::uint64_t state_checksum(std::span<const std::uint8_t> state);

} // namespace dkr::runtime::netplay
