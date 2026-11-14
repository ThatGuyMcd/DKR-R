#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace dkr::runtime::water::enhanced {
// Immutable renderer-only geometry. Vertices are in level/world coordinates,
// not a player's camera or the guest's mutable collision structures.
struct RayVertex {
    std::array<float, 4> position{};
    std::array<float, 4> colour{};
    std::array<float, 4> liquid{}; // UV in texels, immutable opacity-mask index, reserved
};
static_assert(sizeof(RayVertex) == 48);
struct RayOpacityMask {
    std::array<float, 4> params{};          // texture width/height, kind, opaque triangle offset
    std::array<std::uint32_t, 256> alpha{}; // four 8-bit alpha texels/word, bounded 32x32
};
static_assert(sizeof(RayOpacityMask) == 1040);
struct RayGeometry {
    std::vector<RayVertex> vertices;
    std::vector<RayOpacityMask> masks{1}; // index zero describes ordinary opaque geometry
    std::uint32_t opaque_vertices = 0;
};
inline constexpr std::size_t kMaximumRayTriangles = 65536;
inline constexpr std::uint64_t kMaximumRayBytes = 64ULL * 1024 * 1024;
// Cutouts, translucency, decals and hidden reference meshes must not turn into
// opaque ray occluders. Texture and batch flags are separate in retail DKR.
constexpr bool opaque_ray_material(std::uint32_t batch, std::uint16_t texture) {
    return !(batch & ((1U << 8) | (1U << 11) | (1U << 2) | (1U << 4))) && !(texture & ((1U << 2) | (1U << 4)));
}
} // namespace dkr::runtime::water::enhanced
