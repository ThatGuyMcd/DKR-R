#pragma once

#include "netplay_types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dkr::runtime::netplay {

// Everything the admission manifest is built from, gathered by the caller.
// The builder reads no globals, settings, files or compile-time defines, so the
// same inputs give the same manifest on every build and a test can pin it.
struct ManifestInputs {
    // The repository version the network ABI is named after (DKR_NETWORK_RELEASE_VERSION).
    std::string_view source_release_version;
    Revision revision = Revision::UsV77;
    std::uint64_t canonical_rom_hash = 0;
    // The text of the checked patch policy digests (DKR_PATCH_POLICY_SHA256), hashed whole.
    std::string_view patch_policy;
    // magic_codes::selected_mask() at the moment the manifest is built.
    std::uint32_t magic_codes = 0;
    // The canonical adventure save, when the profile has one; nullopt hashes to 0.
    std::optional<std::span<const std::uint8_t>> adventure_save;
    std::string_view architecture;
    std::string floating_point_mode;
    // Empty unless the build carries the owned engine.
    std::string owned_engine_hash;
};

// The manifest a build offers its peers. mod_manifest_hash and
// owned_resources_hash stay empty: the lobby fills them in once content is chosen.
CompatibilityManifest build_netplay_manifest(const ManifestInputs& inputs);

} // namespace dkr::runtime::netplay
