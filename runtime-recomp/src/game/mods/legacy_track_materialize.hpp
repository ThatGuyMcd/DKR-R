#pragma once
#include "legacy_asset_directory.hpp"
#include "legacy_mod_dependencies.hpp"

namespace dkr::mods {
struct TrackMaterialization {
    Root root;
    std::shared_ptr<const AssetBank> bank;
    std::shared_ptr<const AssetDirectory> directory;
    std::vector<AssetKey> included;
    std::vector<AssetKey> excluded;
    std::vector<unsigned> music_sequences;
    // This is a preparation artifact for offline runtime tests, not a UI
    // enablement certificate. Cache/audio activation is still required.
    bool runtime_certified = false;
};
// Decode against the exact source ROM, then optionally prepare an asset-only
// variant for another verified native revision. No executable bytes migrate.
TrackMaterialization prepare_track_bank(Bytes original, View reconstructed, const std::string& patch_digest,
                                        unsigned carrier, Bytes runtime_original = {}, bool behaviour_enabled = true);
} // namespace dkr::mods
