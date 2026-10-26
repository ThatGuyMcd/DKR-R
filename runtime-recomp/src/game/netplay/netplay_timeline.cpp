#include "netplay_timeline.hpp"

#include <algorithm>

namespace dkr::runtime::netplay {

InputTimeline::InputTimeline(std::size_t capacity) : entries_(std::max<std::size_t>(capacity, 32U)) {}

void InputTimeline::reset(std::uint32_t frame) {
    entries_.assign(entries_.size(), {});
    newest_frame_ = frame;
}

bool InputTimeline::set_local(std::uint8_t slot, std::uint32_t frame, PackedInput input) {
    if (slot >= kMaximumPlayers)
        return false;
    Entry& current = entry(frame);
    current.inputs[slot] = input;
    current.present[slot] = true;
    current.predicted[slot] = false;
    newest_frame_ = std::max(newest_frame_, frame);
    return true;
}

std::optional<std::uint32_t> InputTimeline::set_remote(std::uint8_t slot, std::uint32_t frame, PackedInput input) {
    if (slot >= kMaximumPlayers)
        return std::nullopt;
    Entry& current = entry(frame);
    const bool mismatch = current.predicted[slot] && !(current.inputs[slot] == input);
    current.inputs[slot] = input;
    current.present[slot] = true;
    current.predicted[slot] = false;
    newest_frame_ = std::max(newest_frame_, frame);
    return mismatch ? std::optional<std::uint32_t>{frame} : std::nullopt;
}

FrameInputs InputTimeline::inputs_for(std::uint32_t frame) {
    Entry& current = entry(frame);
    for (std::uint8_t slot = 0U; slot < kMaximumPlayers; ++slot) {
        if (!current.present[slot]) {
            current.inputs[slot] = previous(slot, frame);
            current.predicted[slot] = true;
        }
    }
    newest_frame_ = std::max(newest_frame_, frame);
    return current.inputs;
}

std::uint8_t InputTimeline::predicted_mask(std::uint32_t frame, std::uint8_t occupied_mask) const {
    const Entry* current = find(frame);
    if (current == nullptr)
        return occupied_mask & 0x0FU;
    std::uint8_t mask = 0U;
    for (std::uint8_t slot = 0U; slot < kMaximumPlayers; ++slot) {
        if ((occupied_mask & static_cast<std::uint8_t>(1U << slot)) != 0U && current->predicted[slot]) {
            mask |= static_cast<std::uint8_t>(1U << slot);
        }
    }
    return mask;
}

bool InputTimeline::confirmed(std::uint32_t frame, std::uint8_t player_count) const {
    const Entry* current = find(frame);
    if (!current || player_count < 1U || player_count > kMaximumPlayers) {
        return false;
    }
    for (std::uint8_t slot = 0U; slot < player_count; ++slot) {
        if (!current->present[slot] || current->predicted[slot])
            return false;
    }
    return true;
}

bool InputTimeline::confirmed_mask(std::uint32_t frame, std::uint8_t occupied_mask) const {
    const Entry* current = find(frame);
    occupied_mask &= 0x0FU;
    if (!current || occupied_mask == 0U)
        return false;
    for (std::uint8_t slot = 0U; slot < kMaximumPlayers; ++slot) {
        if ((occupied_mask & static_cast<std::uint8_t>(1U << slot)) != 0U &&
            (!current->present[slot] || current->predicted[slot])) {
            return false;
        }
    }
    return true;
}

bool InputTimeline::slot_confirmed(std::uint8_t slot, std::uint32_t frame) const {
    if (slot >= kMaximumPlayers)
        return false;
    const Entry* current = find(frame);
    return current != nullptr && current->present[slot] && !current->predicted[slot];
}

std::uint32_t InputTimeline::unconfirmed_runway(std::uint32_t frame, std::uint8_t occupied_mask) const {
    std::uint32_t runway = 0U;
    const std::uint32_t limit =
        static_cast<std::uint32_t>((std::min<std::size_t>)(entries_.size(), static_cast<std::size_t>(frame) + 1U));
    for (std::uint32_t distance = 0U; distance < limit; ++distance) {
        if (confirmed_mask(frame - distance, occupied_mask))
            break;
        ++runway;
    }
    return runway;
}

InputTimeline::Entry& InputTimeline::entry(std::uint32_t frame) {
    Entry& current = entries_[frame % entries_.size()];
    if (!current.valid || current.frame != frame) {
        current = {};
        current.valid = true;
        current.frame = frame;
    }
    return current;
}

const InputTimeline::Entry* InputTimeline::find(std::uint32_t frame) const {
    const Entry& current = entries_[frame % entries_.size()];
    return current.valid && current.frame == frame ? &current : nullptr;
}

PackedInput InputTimeline::previous(std::uint8_t slot, std::uint32_t frame) const {
    const std::size_t search = std::min<std::size_t>(frame, entries_.size());
    for (std::size_t distance = 1U; distance <= search; ++distance) {
        const Entry* candidate = find(frame - static_cast<std::uint32_t>(distance));
        if (candidate && candidate->present[slot])
            return candidate->inputs[slot];
    }
    return {};
}

} // namespace dkr::runtime::netplay
