#pragma once
#include "online_mod_bundle.hpp"
#include <functional>

namespace dkr::mods::online {
struct Profile {
    Manifest manifest;
    std::string digest;
    std::filesystem::path directory;
};
using ProfileProgress=std::function<void(std::string_view)>;
// Worker-only transactions. No offline activation files, ROM files or saves
// are written. Track Lab sources are an already selected frontend snapshot.
Bundle export_host(const std::filesystem::path& legacy_library,
    const std::vector<std::filesystem::path>& track_lab_sources,
    std::string revision,bool custom_ai,std::stop_token stop={});
void validate_profile(const Profile& profile);
// Background-only probe of the packaged helper, before downloading/replaying mods.
void verify_online_worker(const std::filesystem::path& worker,const std::filesystem::path& online_root,
    std::stop_token stop={});
// Keep complete verified copies for offline use. Existing content and all
// activation, visibility and save files are preserved; new copies are inactive.
void retain_profile(const Profile& profile,const std::filesystem::path& legacy_library,
    const std::filesystem::path& track_library,std::stop_token stop={});
std::shared_ptr<const Profile> prepare_profile(const Manifest& manifest,
    const std::filesystem::path& payload_directory,
    const std::vector<std::filesystem::path>& imported_roms,
    const std::filesystem::path& online_root,
    const std::filesystem::path& worker,std::stop_token stop={},
    const ProfileProgress& progress={});
}
