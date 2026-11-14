#include "rollback_ring.hpp"

#include <algorithm>

namespace dkr::runtime::netplay {

RollbackRing::RollbackRing(std::size_t capacity, std::size_t state_size)
    : slots_(std::max<std::size_t>(capacity, 2U)), state_size_(state_size) {
    for (Slot& slot : slots_)
        slot.bytes.resize(state_size_);
}

bool RollbackRing::save(std::uint32_t frame, std::span<const std::uint8_t> state, std::uint64_t checksum) {
    if (state.size() != state_size_)
        return false;
    Slot& slot = slots_[frame % slots_.size()];
    slot.frame = frame;
    slot.checksum = checksum;
    slot.valid = true;
    std::copy(state.begin(), state.end(), slot.bytes.begin());
    return true;
}

bool RollbackRing::load(std::uint32_t frame, std::span<std::uint8_t> state, std::uint64_t* checksum) const {
    if (state.size() != state_size_)
        return false;
    const Slot& slot = slots_[frame % slots_.size()];
    if (!slot.valid || slot.frame != frame)
        return false;
    std::copy(slot.bytes.begin(), slot.bytes.end(), state.begin());
    if (checksum)
        *checksum = slot.checksum;
    return true;
}

bool RollbackRing::contains(std::uint32_t frame) const {
    const Slot& slot = slots_[frame % slots_.size()];
    return slot.valid && slot.frame == frame;
}

void RollbackRing::clear() {
    for (Slot& slot : slots_)
        slot.valid = false;
}

std::uint64_t state_checksum(std::span<const std::uint8_t> state) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : state) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace dkr::runtime::netplay
