#pragma once
#include "legacy_mod_format.hpp"

namespace dkr::mods {
struct GeometrySummary {
    unsigned segments = 0;
    unsigned triangles = 0;
    std::size_t constructed_bytes = 0;
    std::vector<unsigned> textures;
};
// Structural checks only; this does not certify texture/object dependencies,
// gameplay semantics, collision quality, or runtime bank ownership.
GeometrySummary validate_geometry(View decoded, std::size_t compressed_size);
} // namespace dkr::mods
