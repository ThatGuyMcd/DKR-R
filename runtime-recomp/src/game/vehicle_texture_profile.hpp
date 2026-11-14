#pragma once
// The car texture profiles the package importer admits. An empty profile is
// the native 16-bit material limit, native-4kib-rgba32-safe-v1 the declared
// RGBA32-safe limit, and original-resolution-v1 the bounded tile preparation
// in package_compatibility.hpp.
//
// vehicle_package.cpp includes this header with angle brackets so the
// vehicle-original-textures Before control
// (tests/historical/vehicle_original_textures) can compile the production
// importer with the pre-v1.0.10 answer and prove that DKRVehicleOriginalAudit
// rejects it. Shipped builds only ever see this file.
#include <string_view>

namespace dkr::mods::vehicles {
inline bool texture_profile_admitted(std::string_view profile) noexcept {
    return profile.empty() || profile == "native-4kib-rgba32-safe-v1" || profile == "original-resolution-v1";
}
} // namespace dkr::mods::vehicles
