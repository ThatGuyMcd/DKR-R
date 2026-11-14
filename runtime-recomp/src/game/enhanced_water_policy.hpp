#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

// Renderer-only policy. Never writes guest geometry, physics or replay state.
namespace dkr::runtime::water::enhanced {

enum class Quality : std::uint8_t { Original, Enhanced, High };
enum class Reflection : std::uint8_t { Sky, ScreenSpace, Hardware };
enum class Kind : std::uint8_t { None, Calm, Waves, Waterfall, Fountain, Lava };
enum class Hint : std::uint8_t { Automatic, Exclude, Calm, Waves, Waterfall, Fountain, Lava };

inline constexpr std::uint32_t kWater = 1U << 13U;
inline constexpr std::uint32_t kWaves = 1U << 22U;
inline constexpr std::uint32_t kHidden = 1U << 8U;

constexpr std::optional<std::uint16_t> texture_3d_id(std::uint32_t cache_id) {
    // load_texture stores its full 16-bit key: bit 15 selects the 3D asset
    // table. Track/object texture IDs in the decomp asset data omit that bit.
    // Never alias a 2D/UI texture with the same ordinal to a 3D water material,
    // or normalize a freed (-1) cache slot into a valid asset identity.
    if (cache_id > 0xFFFFU || !(cache_id & 0x8000U))
        return {};
    return static_cast<std::uint16_t>(cache_id & 0x7FFFU);
}

struct MaterialEvidence {
    std::uint32_t flags = 0U;
    std::uint16_t texture_id = 0U; // decoded 3D asset ordinal, not cache key
    std::uint8_t surface_type = 0U;
    bool texture_valid = false;
    bool geometry_valid = false;
    Hint hint = Hint::Automatic;
};

constexpr bool excluded_stock_texture(std::uint16_t id) {
    // Water flags alone are NOT a fluid type. These are lava, fog, flags,
    // finish signs, frozen water and solid shoreline/underwater terrain.
    switch (id) {
    case 16:
    case 58:
    case 158:
    case 221:
    case 265:
    case 300:
    case 320:
    case 503:
    case 871:
    case 898:
    case 927:
    case 1056:
        return true;
    default:
        return false;
    }
}

constexpr Kind classify(MaterialEvidence e) {
    if (!e.texture_valid || !e.geometry_valid || (e.flags & kHidden) || e.hint == Hint::Exclude)
        return Kind::None;
    // Explicit, validated author metadata can describe a repurposed material.
    switch (e.hint) {
    case Hint::Calm:
        return Kind::Calm;
    case Hint::Waves:
        return Kind::Waves;
    case Hint::Waterfall:
        return Kind::Waterfall;
    case Hint::Fountain:
        return Kind::Fountain;
    case Hint::Lava:
        return Kind::Lava;
    default:
        break;
    }
    // Reviewed Hot Top Volcano molten surface (including waves_render's
    // reference batch). Do not turn its rock, haze or collision-type 10 ice
    // into lava, and never treat molten geometry as water for craft wakes.
    if (e.texture_id == 16U && (e.flags & kWater))
        return Kind::Lava;
    if (excluded_stock_texture(e.texture_id) || e.surface_type == 10U)
        return Kind::None;
    switch (e.texture_id) {
    case 317:
    case 1177:
    case 1391:
        return Kind::Waterfall;
    case 1241:
        return Kind::Fountain;
    default:
        break;
    }
    // 11 is calm liquid, 14/15 are liquid collision classes. Unknown custom
    // textures need semantic evidence; resemblance or texture names are not
    // sufficient to replace an arbitrary transparent object.
    const bool liquid = e.surface_type == 11U || e.surface_type == 14U || e.surface_type == 15U;
    const bool known = e.texture_id == 5U || e.texture_id == 6U || e.texture_id == 12U || e.texture_id == 13U ||
                       e.texture_id == 15U || e.texture_id == 140U || e.texture_id == 1156U;
    if (!liquid && !(known && (e.flags & kWater)))
        return Kind::None;
    return (e.flags & kWaves) ? Kind::Waves : Kind::Calm;
}

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// A degenerate or corrupt triangle must never become an optical surface.
inline bool valid_triangle(Vec3 a, Vec3 b, Vec3 c) {
    if (!finite(a) || !finite(b) || !finite(c))
        return false;
    const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    const float area = n.x * n.x + n.y * n.y + n.z * n.z;
    return std::isfinite(area) && area > 0.0001F;
}

struct Profile {
    Vec3 deep{0.008F, 0.055F, 0.075F};
    Vec3 shallow{0.04F, 0.25F, 0.28F};
    Vec3 sky{0.25F, 0.45F, 0.72F};
    Vec3 sun{0.35F, 0.85F, 0.25F};
    float absorption = 0.006F;
    float roughness = 0.15F;
    float visibility = 650.0F;
    float ripple = 0.65F;
    float foam = 0.6F;
};

constexpr Profile lava_profile() {
    Profile p;
    p.deep = {0.028F, 0.006F, 0.002F};
    p.shallow = {1.0F, 0.18F, 0.015F};
    p.sky = {0.15F, 0.055F, 0.025F};
    p.roughness = 0.26F;
    p.ripple = 0.35F;
    p.foam = 0;
    return p;
}

inline bool valid_profile(const Profile& p) {
    const auto colour = [](Vec3 v) {
        return finite(v) && v.x >= 0 && v.y >= 0 && v.z >= 0 && v.x <= 16 && v.y <= 16 && v.z <= 16;
    };
    const float sun_length = p.sun.x * p.sun.x + p.sun.y * p.sun.y + p.sun.z * p.sun.z;
    return colour(p.deep) && colour(p.shallow) && colour(p.sky) && finite(p.sun) && std::isfinite(sun_length) &&
           sun_length > 0.00001F && sun_length <= 100 && std::isfinite(p.absorption) && p.absorption >= 0.00001F &&
           p.absorption <= 1 && std::isfinite(p.roughness) && p.roughness >= 0.02F && p.roughness <= 1 &&
           std::isfinite(p.visibility) && p.visibility >= 1 && p.visibility <= 100000 && std::isfinite(p.ripple) &&
           p.ripple >= 0 && p.ripple <= 4 && std::isfinite(p.foam) && p.foam >= 0 && p.foam <= 4;
}

// Optional Track Lab appearance. Texture numbers are model-table ordinals,
// not ROM IDs: appending custom textures cannot change their meaning.
struct Metadata {
    std::optional<Profile> profile;
    std::array<Hint, 256> materials{};
};

constexpr Profile stock_profile(std::int32_t map) {
    Profile p{};
    switch (map) {
    case 28: // Frosty Village: cold running water, not the ice materials.
        p.deep = {0.012F, 0.035F, 0.065F};
        p.shallow = {0.09F, 0.24F, 0.31F};
        break;
    case 8:
    case 10:
    case 4:
    case 14:
    case 40:
    case 53:
    case 61:
        p.deep = {0.004F, 0.06F, 0.08F};
        p.shallow = {0.03F, 0.3F, 0.28F};
        p.foam = 0.9F;
        p.visibility = 950;
        break;
    case 30:
    case 19:
    case 31:
        p.deep = {0.006F, 0.018F, 0.032F};
        p.shallow = {0.025F, 0.09F, 0.12F};
        p.sky = {0.045F, 0.075F, 0.14F};
        p.visibility = 420;
        break;
    default:
        break;
    }
    return p;
}

struct Capabilities {
    bool shaders = false;
    bool sampled_depth = false;
    bool floating_targets = false;
    bool sample_count_supported = false;
    bool hardware_rays = false;
    bool hardware_adapter_ready = false;
};
struct Selection {
    Quality quality;
    Reflection reflection;
};
constexpr Selection select(Quality q, Reflection r, Capabilities c) {
    if (q == Quality::Original || !c.shaders || !c.sampled_depth || !c.floating_targets || !c.sample_count_supported)
        return {Quality::Original, Reflection::Sky};
    if (r == Reflection::Hardware && (!c.hardware_rays || !c.hardware_adapter_ready))
        r = Reflection::ScreenSpace;
    return {q, r};
}

// Effects consume immutable presentation time, never wall-clock time. A
// rewind/cut/scene replacement clears history rather than emitting old wakes.
struct History {
    std::uint32_t scene = 0U;
    double time = 0;
    bool valid = false;
    bool observe(std::uint32_t next_scene, double next_time) {
        const bool reset =
            !valid || !std::isfinite(next_time) || scene != next_scene || next_time < time || next_time - time > 0.25;
        scene = next_scene;
        time = next_time;
        valid = std::isfinite(next_time);
        return reset;
    }
};

inline constexpr std::array<std::int32_t, 30> kStockWaterMaps{
    0,  3,  4,  5,  8,  10, 12, 14, 19, 20, 23, 25, 26, 28, 29,
    30, 31, 36, 37, 40, 42, 45, 47, 49, 53, 56, 60, 61, 63, 64 // All reachable stock geometry with liquid.
};
constexpr bool contains_stock_water(std::int32_t map) {
    return std::find(kStockWaterMaps.begin(), kStockWaterMaps.end(), map) != kStockWaterMaps.end();
}
} // namespace dkr::runtime::water::enhanced
