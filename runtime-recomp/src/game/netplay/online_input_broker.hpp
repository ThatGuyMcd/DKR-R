#pragma once

#include "netplay_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace dkr::runtime::netplay {

enum class InputFrameSource : std::uint8_t {
    None,
    FrontendLockstep,
    GameplayLockstep,
    Rollback,
};

struct LocalInputSample {
    PackedInput input{};
    std::size_t profile = 0U;
    std::uint64_t generation = 0U;
    bool blocked = false;
    bool valid = false;
};

struct CommittedInputFrame {
    FrameInputs inputs{};
    std::uint32_t frame = 0U;
    std::uint32_t epoch = 0U;
    std::uint64_t generation = 0U;
    InputFrameSource source = InputFrameSource::None;
    bool valid = false;
};

struct OnlineInputBrokerView {
    bool enabled = false;
    std::uint8_t occupied_mask = 0U;
    std::uint8_t local_slot = 0xFFU;
    std::size_t local_profile = 0U;
    std::uint32_t epoch = 0U;
    LocalInputSample local{};
    CommittedInputFrame committed{};
};

std::int8_t pack_input_axis(float value);
float unpack_input_axis(std::int8_t value);

// The broker is the sole owner of online game-visible controller frames.
// Physical polling only updates LocalInputSample; DirectSession or Gekko then
// publishes one complete immutable FrameInputs value for DKR to consume.
class OnlineInputBroker final {
  public:
    void configure(bool enabled, std::uint8_t occupied_mask, std::uint8_t local_slot);
    void set_local_profile(std::size_t profile);
    void begin_epoch();
    void reset();

    void capture_local(std::size_t profile, std::uint16_t buttons, float stick_x, float stick_y, bool blocked);
    LocalInputSample local_sample() const;

    bool publish(std::uint32_t frame, InputFrameSource source, const FrameInputs& inputs);
    PackedInput input_for_port(std::size_t port) const;
    OnlineInputBrokerView view() const;

  private:
    void clear_committed_locked();

    mutable std::mutex mutex_;
    bool enabled_ = false;
    std::uint8_t occupied_mask_ = 0U;
    std::uint8_t local_slot_ = 0xFFU;
    std::size_t local_profile_ = 0U;
    std::uint32_t epoch_ = 0U;
    std::uint64_t next_local_generation_ = 1U;
    std::uint64_t next_commit_generation_ = 1U;
    LocalInputSample local_{};
    CommittedInputFrame committed_{};
};

OnlineInputBroker& online_input_broker();

} // namespace dkr::runtime::netplay
