#pragma once
// Test double for src/game/vehicle_texture_profile.hpp: the profile admission
// the importer shipped with before v1.0.10 (1.0.10-vehicle-playtest.1), which
// knew no original-resolution-v1 profile.
//
// Only DKRVehicleOriginalBeforeTests puts this directory ahead of src/game, so
// it compiles the production vehicle_package.cpp with this answer. That build
// must still reproduce every legacy package fingerprint, then FAIL
// DKRVehicleOriginalAudit with "unsupported car texture profile." on the first
// original-resolution package.
#include <string_view>

namespace dkr::mods::vehicles {
inline bool texture_profile_admitted(std::string_view profile) noexcept {
    return profile.empty() || profile == "native-4kib-rgba32-safe-v1";
}
} // namespace dkr::mods::vehicles
