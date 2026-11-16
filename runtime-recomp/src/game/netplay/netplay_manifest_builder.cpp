#include "netplay_manifest_builder.hpp"

#include "netplay_build_identity.hpp"

namespace dkr::runtime::netplay {

CompatibilityManifest build_netplay_manifest(const ManifestInputs& inputs) {
    CompatibilityManifest manifest{};
    // Network compatibility comes from the repository VERSION and network ABI,
    // not a platform-specific package/RC label. The human-facing launcher can
    // still display DKR_RELEASE_VERSION independently.
    manifest.release_version = canonical_network_release(inputs.source_release_version);
    manifest.build_fingerprint = canonical_network_build_fingerprint(inputs.source_release_version);
    manifest.revision = inputs.revision;
    manifest.canonical_rom_hash = inputs.canonical_rom_hash;
    // CMake derives this digest directly from the checked Patch Pipeline
    // policy used for this revision. Admission therefore fails before launch
    // whenever authored hook or instruction policy differs between builds.
    manifest.patch_policy_hash = stable_hash(inputs.patch_policy);
    manifest.magic_codes_hash = inputs.magic_codes;
    manifest.gameplay_settings_hash = stable_hash(std::to_string(manifest.magic_codes_hash) + ":retail-simulation-30");
    if (inputs.adventure_save) {
        manifest.session_save_hash = stable_hash(std::string_view(
            reinterpret_cast<const char*>(inputs.adventure_save->data()), inputs.adventure_save->size()));
    }
    manifest.simulation_rate = 30U;
    manifest.architecture = std::string(inputs.architecture);
    manifest.floating_point_mode = inputs.floating_point_mode;
    manifest.owned_engine_hash = inputs.owned_engine_hash;
    return manifest;
}

} // namespace dkr::runtime::netplay
