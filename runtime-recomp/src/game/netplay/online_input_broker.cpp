#include "online_input_broker.hpp"

#include <algorithm>
#include <cmath>

namespace dkr::runtime::netplay {
namespace {

OnlineInputBroker g_online_input_broker;

} // namespace

std::int8_t pack_input_axis(float value) {
    const float scaled = std::round(std::clamp(value, -1.0F, 1.0F) * 127.0F);
    return static_cast<std::int8_t>(scaled);
}

float unpack_input_axis(std::int8_t value) {
    return static_cast<float>(value) / 127.0F;
}

void OnlineInputBroker::clear_committed_locked() {
    committed_ = {};
    committed_.epoch = epoch_;
}

void OnlineInputBroker::configure(bool enabled, std::uint8_t occupied_mask, std::uint8_t local_slot) {
    std::scoped_lock lock(mutex_);
    occupied_mask &= 0x0FU;
    if (!enabled) {
        enabled_ = false;
        occupied_mask_ = 0U;
        local_slot_ = 0xFFU;
        local_ = {};
        clear_committed_locked();
        return;
    }

    const bool topology_changed = !enabled_ || occupied_mask_ != occupied_mask || local_slot_ != local_slot;
    enabled_ = true;
    occupied_mask_ = occupied_mask;
    local_slot_ = local_slot;
    if (topology_changed) {
        ++epoch_;
        local_ = {};
        clear_committed_locked();
    }
}

void OnlineInputBroker::set_local_profile(std::size_t profile) {
    std::scoped_lock lock(mutex_);
    local_profile_ = std::min(profile, kMaximumPlayers - 1U);
    if (local_.profile != local_profile_) {
        local_ = {};
        local_.profile = local_profile_;
    }
}

void OnlineInputBroker::begin_epoch() {
    std::scoped_lock lock(mutex_);
    ++epoch_;
    clear_committed_locked();
}

void OnlineInputBroker::reset() {
    std::scoped_lock lock(mutex_);
    enabled_ = false;
    occupied_mask_ = 0U;
    local_slot_ = 0xFFU;
    epoch_ = 0U;
    next_local_generation_ = 1U;
    next_commit_generation_ = 1U;
    local_ = {};
    committed_ = {};
}

void OnlineInputBroker::capture_local(std::size_t profile, std::uint16_t buttons, float stick_x, float stick_y,
                                      bool blocked) {
    std::scoped_lock lock(mutex_);
    if (!enabled_ || profile != local_profile_)
        return;
    local_.input = {buttons, pack_input_axis(stick_x), pack_input_axis(stick_y)};
    local_.profile = profile;
    local_.generation = next_local_generation_++;
    local_.blocked = blocked;
    local_.valid = true;
}

LocalInputSample OnlineInputBroker::local_sample() const {
    std::scoped_lock lock(mutex_);
    return local_;
}

bool OnlineInputBroker::publish(std::uint32_t frame, InputFrameSource source, const FrameInputs& inputs) {
    std::scoped_lock lock(mutex_);
    if (!enabled_ || source == InputFrameSource::None)
        return false;

    committed_.inputs = inputs;
    for (std::size_t slot = 0U; slot < committed_.inputs.size(); ++slot) {
        if ((occupied_mask_ & static_cast<std::uint8_t>(1U << slot)) == 0U) {
            committed_.inputs[slot] = {};
        }
    }
    committed_.frame = frame;
    committed_.epoch = epoch_;
    committed_.generation = next_commit_generation_++;
    committed_.source = source;
    committed_.valid = true;
    return true;
}

PackedInput OnlineInputBroker::input_for_port(std::size_t port) const {
    std::scoped_lock lock(mutex_);
    if (!enabled_ || !committed_.valid || port >= kMaximumPlayers ||
        (occupied_mask_ & static_cast<std::uint8_t>(1U << port)) == 0U) {
        return {};
    }
    return committed_.inputs[port];
}

OnlineInputBrokerView OnlineInputBroker::view() const {
    std::scoped_lock lock(mutex_);
    return {enabled_, occupied_mask_, local_slot_, local_profile_, epoch_, local_, committed_};
}

OnlineInputBroker& online_input_broker() {
    return g_online_input_broker;
}

} // namespace dkr::runtime::netplay
