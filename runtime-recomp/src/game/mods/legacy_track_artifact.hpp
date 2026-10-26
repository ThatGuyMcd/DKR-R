#pragma once
#include "legacy_track_materialize.hpp"

namespace dkr::mods {
// Prepared data only: no executable bytes, absolute paths, mutable ROM image,
// runtime pointers, or permission to activate a scene are serialized here.
inline constexpr unsigned TrackArtifactSchema = 1;
inline constexpr unsigned TrackAdapterVersion = 1;
struct PreparedTrack {
    Root root;
    std::string artifact_digest, patch_digest, profile;
    std::shared_ptr<const AssetBank> bank;
};
std::string write_track_artifact(const std::filesystem::path& destination, const TrackMaterialization& track,
                                 const std::string& patch_digest, const std::string& profile);
PreparedTrack read_track_artifact(const std::filesystem::path& directory, const std::string& expected_digest,
                                  std::shared_ptr<const AssetBank> stock);
} // namespace dkr::mods
