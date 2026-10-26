#pragma once
#include "vehicle_library.hpp"
#include "legacy_character_materialize.hpp"

namespace dkr::mods::vehicles {
struct ModelProfile {
    unsigned model = 0, vertices = 0, triangles = 0;
    std::vector<std::uint8_t> hide;
    std::string identity;
};
struct DriverProfile {
    unsigned header = 0, base = 0;
    float native_scale = 1;
    std::string character_id, name, issue;
    std::array<ModelProfile, 5> lods;
    bool compatible() const {
        return issue.empty();
    }
};
struct MaterialPixels {
    Bytes bytes;
    bool rgba32 = false; // Exact PNG channels when the two TMEM banks fit.
};
struct Resources {
    Frozen library;
    std::vector<DriverProfile> drivers;
    std::vector<unsigned> portraits; // first ID of two consecutive 64x32 menu tiles
    AssetBank::Overrides artwork;
    std::vector<std::vector<MaterialPixels>> texture_pixels; // Prepared once; menu artwork remains RGBA16.
    std::vector<float> vertex_units;                         // fixed car precision, independent of driver scale
    std::string identity;
    const DriverProfile* driver(unsigned header) const;
    const DriverProfile* character(std::string_view id, unsigned base) const;
    std::shared_ptr<const AssetBank> apply(std::shared_ptr<const AssetBank>) const;
};
// Preparation is linear in driver models + car packages, not a cartesian
// character x car asset library. The driver animation files remain unchanged.
std::shared_ptr<const Resources> prepare_resources(std::shared_ptr<const AssetBank> original,
                                                   std::shared_ptr<const AssetBank> boot,
                                                   std::shared_ptr<const CharacterNamespace>, Frozen);
} // namespace dkr::mods::vehicles
