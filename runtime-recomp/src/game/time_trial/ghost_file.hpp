#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace dkr::runtime::time_trial {
inline constexpr std::string_view kPortableGhostSignature = "DKRPGH01";
inline constexpr unsigned kGhostPoseBytes = 20;
inline constexpr unsigned kMaxPortableGhostBytes = 136 + 54000 * kGhostPoseBytes;

// The replay digest covers the course, vehicle, racer, rules and recorded inputs.
inline std::string portable_ghost_prefix(std::string_view replay_digest) {
    return std::string(kPortableGhostSignature).append(replay_digest);
}

inline std::filesystem::path portable_ghost_file(std::filesystem::path replay) {
    return replay.replace_extension(".dkrghost");
}
} // namespace dkr::runtime::time_trial
