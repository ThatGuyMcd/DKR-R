#pragma once

#include "netplay_types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkr::runtime::netplay {

struct ReplayFrame {
    std::uint32_t frame = 0U;
    FrameInputs inputs{};
};

class ReplayRecorder final {
  public:
    void configure(std::filesystem::path directory);
    void begin(const Room& room);
    void record(std::uint32_t frame, const FrameInputs& inputs);
    bool finalize(std::string& error);
    ReplayRecorder detach();
    void reset();
    bool active() const {
        return active_;
    }
    const std::filesystem::path& last_path() const {
        return last_path_;
    }

  private:
    std::filesystem::path directory_;
    std::filesystem::path last_path_;
    std::string room_id_;
    std::uint64_t manifest_hash_ = 0U;
    std::uint8_t player_count_ = 0U;
    bool active_ = false;
    std::vector<ReplayFrame> frames_;
};

} // namespace dkr::runtime::netplay
