#pragma once

#include "netplay_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dkr::runtime::netplay {

class InputTimeline final {
  public:
    explicit InputTimeline(std::size_t capacity = 256U);
    void reset(std::uint32_t frame = 0U);
    bool set_local(std::uint8_t slot, std::uint32_t frame, PackedInput input);
    std::optional<std::uint32_t> set_remote(std::uint8_t slot, std::uint32_t frame, PackedInput input);
    FrameInputs inputs_for(std::uint32_t frame);
    std::uint8_t predicted_mask(std::uint32_t frame, std::uint8_t occupied_mask) const;
    bool confirmed(std::uint32_t frame, std::uint8_t player_count) const;
    bool confirmed_mask(std::uint32_t frame, std::uint8_t occupied_mask) const;
    bool slot_confirmed(std::uint8_t slot, std::uint32_t frame) const;
    std::uint32_t unconfirmed_runway(std::uint32_t frame, std::uint8_t occupied_mask) const;
    std::uint32_t newest_frame() const {
        return newest_frame_;
    }

  private:
    struct Entry {
        std::uint32_t frame = 0U;
        FrameInputs inputs{};
        std::array<bool, kMaximumPlayers> present{};
        std::array<bool, kMaximumPlayers> predicted{};
        bool valid = false;
    };

    Entry& entry(std::uint32_t frame);
    const Entry* find(std::uint32_t frame) const;
    PackedInput previous(std::uint8_t slot, std::uint32_t frame) const;

    std::vector<Entry> entries_;
    std::uint32_t newest_frame_ = 0U;
};

} // namespace dkr::runtime::netplay
