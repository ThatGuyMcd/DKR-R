#include "replay_recorder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>

namespace dkr::runtime::netplay {
namespace {
struct ReplayHeader {
    std::array<char, 8> magic{'D', 'K', 'R', 'R', 'E', 'P', 'L', 'Y'};
    std::uint64_t manifest_hash = 0U;
    std::uint32_t frame_count = 0U;
    std::uint32_t version = 1U;
    std::uint8_t player_count = 0U;
    std::array<std::uint8_t, 7> reserved{};
};
static_assert(sizeof(ReplayHeader) == 32U);

std::string safe_name(std::string value) {
    for (char& character : value) {
        const bool safe = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '-' || character == '_';
        if (!safe)
            character = '_';
    }
    return value.empty() ? "session" : value;
}
} // namespace

void ReplayRecorder::configure(std::filesystem::path directory) {
    directory_ = std::move(directory);
}

void ReplayRecorder::begin(const Room& room) {
    frames_.clear();
    room_id_ = room.room_id;
    manifest_hash_ = manifest_hash(room.manifest);
    player_count_ = static_cast<std::uint8_t>(
        std::count_if(room.players.begin(), room.players.end(), [](const Player& player) { return player.occupied; }));
    active_ = room.rules.record_replay && !directory_.empty();
}

void ReplayRecorder::record(std::uint32_t frame, const FrameInputs& inputs) {
    if (!active_)
        return;
    if (!frames_.empty() && frame <= frames_.back().frame)
        return;
    frames_.push_back({frame, inputs});
}

bool ReplayRecorder::finalize(std::string& error) {
    if (!active_) {
        error.clear();
        return true;
    }
    active_ = false;
    if (frames_.empty()) {
        error.clear();
        return true;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(directory_, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the netplay replay directory.";
        return false;
    }
    const auto stamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    last_path_ = directory_ / (safe_name(room_id_) + "-" + std::to_string(stamp) + ".dkrreplay");
    std::ofstream stream(last_path_, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "Could not create the netplay replay file.";
        return false;
    }
    const ReplayHeader header{{'D', 'K', 'R', 'R', 'E', 'P', 'L', 'Y'},
                              manifest_hash_,
                              static_cast<std::uint32_t>(frames_.size()),
                              1U,
                              player_count_,
                              {}};
    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char*>(frames_.data()),
                 static_cast<std::streamsize>(frames_.size() * sizeof(ReplayFrame)));
    stream.flush();
    stream.close();
    if (!stream) {
        error = "The netplay replay could not be written completely.";
        return false;
    }
    error.clear();
    return true;
}

ReplayRecorder ReplayRecorder::detach() {
    ReplayRecorder pending;
    pending.directory_ = directory_;
    pending.room_id_ = room_id_;
    pending.manifest_hash_ = manifest_hash_;
    pending.player_count_ = player_count_;
    pending.active_ = active_;
    pending.frames_.swap(frames_);
    active_ = false;
    return pending;
}

void ReplayRecorder::reset() {
    active_ = false;
    frames_.clear();
    room_id_.clear();
}

} // namespace dkr::runtime::netplay
