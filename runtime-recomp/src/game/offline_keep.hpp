#pragma once
#include "online_mod_profile.hpp"
namespace dkr::mods::online {
// Validated profile selections only. Does not change existing catalogue state.
void retain_legacy_sources(const ProfileSources&, const std::filesystem::path&, std::stop_token = {});
} // namespace dkr::mods::online
