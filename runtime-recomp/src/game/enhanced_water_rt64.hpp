#pragma once
#include "enhanced_water_registry.hpp"
#include "enhanced_water_settings.hpp"
#include "revision_addresses.hpp"
#include "runtime_legacy_mods.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_rdp.h"
#include "hle/rt64_workload_queue.h"
#include <cstdio>
#include <cstring>

namespace dkr::runtime::water::enhanced {

inline std::uint32_t level_model_symbol() {
    return revision_addresses::selected_revision() == rom::Revision::UsV80 ? 0x800DCE88U : 0x800DC918U;
}
inline std::uint32_t wave_batch_symbol() {
    return wave_symbols(revision_addresses::selected_revision() == rom::Revision::UsV80).batch;
}

inline const Registry& task_registry(RT64::State& state, std::uint32_t scene, bool animation_only = false) {
    struct Cache {
        Registry registry;
        std::array<std::uint32_t, 6> key{};
        TextureScrolls scrolls{};
        rom::Revision revision = rom::Revision::Unsupported;
        unsigned trace_lines = 0;
    };
    static thread_local Cache cache;
    Memory m(std::span<const std::uint8_t>(state.RDRAM, 0x800000));
    const auto model = m.u32(level_model_symbol());
    const bool lighting = !animation_only && lava_lighting_enabled.load() != 0 && lava_lighting.load() > 0;
    const bool glow = lighting;
    const bool rays = !animation_only && hardware_ray_capability.load(std::memory_order_relaxed) == 1 &&
                      requested_reflection.load(std::memory_order_relaxed) == Reflection::Hardware &&
                      reflection_strength.load(std::memory_order_relaxed) > 0;
    const std::array<std::uint32_t, 6> key{scene,
                                           model,
                                           m.u32(revision_addresses::TextureCache),
                                           m.u32(revision_addresses::NumberOfLoadedTextures),
                                           m.u32(wave_batch_symbol()),
                                           (rays ? 1U : 0U) | (glow ? 2U : 0U) | (animation_only ? 4U : 0U)};
    const auto revision = revision_addresses::selected_revision();
    const auto scrolls = animation_only ? TextureScrolls{}
                                        : texture_scrolls(m, m.u32(revision_addresses::ObjectList),
                                                          m.u32(revision_addresses::ObjectCount));
    if (cache.key != key || cache.revision != revision || cache.scrolls != scrolls) {
        const bool preserve_flow_phase = cache.key[0] == scene && cache.key[1] == model && cache.revision == revision;
        cache.key = key;
        cache.revision = revision;
        cache.scrolls = scrolls;
        cache.registry.rebuild(
            m, key[1], key[2], key[3], key[4],
            legacy::renderer_water_for_level(static_cast<std::int32_t>(m.u32(revision_addresses::CurrentMapId))), rays,
            preserve_flow_phase, scrolls, glow, animation_only);
        if (!animation_only && key[4] && cache.trace_lines < 64) {
            ++cache.trace_lines;
            const auto symbols = wave_symbols(revision == rom::Revision::UsV80);
            std::fprintf(
                stderr,
                "[water][wave-reference] scene=%u map=%u batch=%08X liquid=%u subdivisions=%u players=%u buffer=%08X\n",
                scene, m.u32(revision_addresses::CurrentMapId), key[4],
                unsigned(cache.registry.procedural().has_value()), m.u32(symbols.controller), m.u32(symbols.players),
                m.u32(symbols.triangles));
        }
    }
    return cache.registry;
}

struct CraftFrame {
    std::array<hlslpp::float4, 8> previous{}, current{};
    float previous_time = 0;
};
inline const CraftFrame& task_crafts(Memory m, const Registry& registry, std::uint32_t scene, float time) {
    struct Cache {
        History history;
        CraftFrame frame;
        std::array<std::uint32_t, 8> objects{};
    };
    static thread_local Cache cache;
    if (cache.history.valid && cache.history.scene == scene && cache.history.time == time)
        return cache.frame;
    const float old_time = float(cache.history.time);
    const bool reset = cache.history.observe(scene, time);
    cache.frame.previous = cache.frame.current;
    cache.frame.previous_time = reset ? time : old_time;
    const auto racers = m.u32(revision_addresses::Racers);
    const auto count = (std::min)(m.u32(revision_addresses::NumberOfRacers), 8U);
    for (unsigned i = 0; i < 8; ++i) {
        const auto object =
            i < count && racers && m.valid(racers, std::uint64_t(count) * 4) ? m.u32(racers + i * 4) : 0;
        const auto racer = object && m.valid(object, 0x68) ? m.u32(object + 0x64) : 0;
        auto& current = cache.frame.current[i];
        current = {0, 0, 0, -1};
        if (racer && m.valid(racer, 0x224) && m.u8(racer + 0x1D6) <= 2) {
            const Vec3 p{m.f32(object + 0xC), m.f32(object + 0x10), m.f32(object + 0x14)};
            const auto height = finite(p) ? registry.liquid_height(p.x, p.z, p.y) : std::nullopt;
            // Contact is a bounded visual observation, not a physics write.
            // Dry terrain/bridges and distant aircraft cannot emit wakes.
            const float contact = height && p.y >= *height - 60 && p.y <= *height + 40
                                      ? std::clamp(1.0F - (p.y - *height) / 40.0F, 0.0F, 1.0F)
                                      : 0;
            if (finite(p))
                current = {p.x, p.y, p.z, contact};
        }
        const auto& old = cache.frame.previous[i];
        const float dx = float(current.x - old.x), dz = float(current.z - old.z);
        if (reset || object != cache.objects[i] || old.w < 0 || dx * dx + dz * dz > 150 * 150)
            cache.frame.previous[i] = current;
        cache.objects[i] = object;
    }
    return cache.frame;
}

inline void apply_material(RT64::State& state, const Registry& registry, std::uint32_t source, std::uint32_t count,
                           bool procedural, std::uint32_t scene, const hlslpp::float4x4& cameraVP, bool camera_valid,
                           float time, std::uint32_t camera_address) {
    interop::WaterMaterial material{};
    const auto q = requested_quality.load(std::memory_order_relaxed);
    Memory m(std::span<const std::uint8_t>(state.RDRAM, 0x800000));
    auto surface = registry.find(source, count);
    const auto dynamic =
        surface ? std::optional<Surface>{}
                : registry.find_dynamic(
                      m, wave_symbols(revision_addresses::selected_revision() == rom::Revision::UsV80), source, count);
    if (dynamic)
        surface = dynamic;
    if (!surface && camera_valid) {
        const auto kind = registry.vertical_image_kind(state.rdp->texture.address);
        if (kind != Kind::None)
            surface = Surface{0, 0, 0, kind, 0, registry.vertical_scroll_period(state.rdp->texture.address)};
    }
    // Vertical water preserves its authored combiner and opacity, with a
    // separate flowing-water shader branch. It never uses horizontal optics.
    if (q != Quality::Original && camera_valid && surface) {
        const auto map = static_cast<std::int32_t>(m.u32(revision_addresses::CurrentMapId));
        const bool lava = surface->kind == Kind::Lava;
        const auto profile = registry.metadata() && registry.metadata()->profile ? *registry.metadata()->profile
                             : lava                                              ? lava_profile()
                                                                                 : stock_profile(map);
        material.deepColor = {profile.deep.x, profile.deep.y, profile.deep.z, profile.absorption};
        material.shallowColor = {profile.shallow.x, profile.shallow.y, profile.shallow.z, profile.roughness};
        material.sunDirection = {profile.sun.x, profile.sun.y, profile.sun.z, 1.0F};
        material.sunColor = {1.0F, 0.9F, 0.7F, 0.5F};
        material.skyColor = {profile.sky.x, profile.sky.y, profile.sky.z, 0.4F};
        const auto scale = [](const std::atomic<int>& setting) {
            return float(setting.load(std::memory_order_relaxed)) * 0.01F;
        };
        material.shallowColor.w = std::clamp(profile.roughness * scale(roughness_scale), 0.02F, 1.0F);
        if (!use_track_colours.load(std::memory_order_relaxed)) {
            material.deepColor.x *= scale(tint_red);
            material.shallowColor.x *= scale(tint_red);
            material.deepColor.y *= scale(tint_green);
            material.shallowColor.y *= scale(tint_green);
            material.deepColor.z *= scale(tint_blue);
            material.shallowColor.z *= scale(tint_blue);
        }
        const auto& crafts = task_crafts(m, registry, scene, time);
        material.animation = {crafts.previous_time, time, profile.ripple, profile.foam};
        material.animation.z *= scale(ripple_scale);
        material.animation.w *= scale(foam_scale);
        const auto reflection = requested_reflection.load(std::memory_order_relaxed);
        const int automatic = q == Quality::High && reflection != Reflection::Sky ? 2 : 1;
        const auto detail = [&](const std::atomic<int>& setting) {
            const int v = setting.load();
            return v < 0 ? automatic : v;
        };
        material.performance = {float(detail(reflection_detail) == 0   ? 12
                                      : detail(reflection_detail) == 1 ? 24
                                                                       : 36),
                                float(detail(wake_detail) == 0   ? 128
                                      : detail(wake_detail) == 1 ? 256
                                                                 : 512),
                                6000.0F * scale(reflection_distance), float(detail(spray_detail))};
        material.appearance = {scale(reflection_strength), scale(refraction_scale), scale(wake_strength),
                               scale(waterfall_detail)};
        // Reference quality 1 = sky reflections, 2 = screen-space reflections.
        material.identity = {reflection == Reflection::Sky ? 1.0F
                             : q == Quality::High          ? 3.0F
                                                           : 2.0F,
                             0.0F, float(map), float(scene & 0xFFFFFFU)};
        material.surface = {surface->height, 0.8F, 0.2F, 0.0F};
        const bool vertical = surface->kind == Kind::Waterfall || surface->kind == Kind::Fountain;
        material.optics = {profile.visibility, vertical ? surface->scroll_period[0] : 0.0F,
                           vertical ? surface->scroll_period[1] : 0.35F, vertical ? 1.0F : 0.0F};
        material.optics.x *= scale(clarity_scale);
        material.effects = {!lava && spray_enabled.load(std::memory_order_relaxed) ? 1.0F : 0.0F, float(camera_address),
                            float(surface->kind), reflection == Reflection::Hardware && !vertical ? 1.0F : 0.0F};
        material.cameraViewProjection = cameraVP;
        // Water batches in the same task share a camera. Cache only exact
        // immutable matrix values, never a guest address that can be reused.
        struct CameraInverseCache {
            hlslpp::float4x4 matrix{}, inverse{};
            bool valid = false;
        };
        static thread_local CameraInverseCache camera_cache;
        if (!camera_cache.valid || std::memcmp(&camera_cache.matrix, &cameraVP, sizeof(cameraVP)) != 0) {
            camera_cache.matrix = cameraVP;
            camera_cache.inverse = hlslpp::inverse(cameraVP);
            camera_cache.valid = true;
        }
        const auto& inverse = camera_cache.inverse;
        // A perspective camera's optical centre is inverse(VP)*(0,0,1,0).
        // This recovers the eye from DKR's exact fixed-point camera matrix.
        const auto eye = hlslpp::mul(hlslpp::float4(0, 0, 1, 0), inverse);
        if (std::isfinite(float(eye.x)) && std::isfinite(float(eye.y)) && std::isfinite(float(eye.z)) &&
            std::isfinite(float(eye.w)) && std::abs(float(eye.w)) > 0.000001F) {
            material.cameraPosition = {float(eye.x / eye.w), float(eye.y / eye.w), float(eye.z / eye.w), 1};
        } else
            material = {};
        for (unsigned i = 0; i < 8; ++i) {
            material.craftPrevious[i] = crafts.previous[i];
            material.craftCurrent[i] = crafts.current[i];
            if (vertical || lava || !wakes_enabled.load(std::memory_order_relaxed)) {
                material.craftPrevious[i].w = -1;
                material.craftCurrent[i].w = -1;
            }
        }
        if (lava) {
            // Lava has no shoreline/wake field. Reuse its unused shoreline
            // vectors without changing the full 2048-byte shader ABI. Keep
            // craft W negative so emitters cannot accidentally generate wakes.
            const auto emitters = registry.lava_emitters();
            for (unsigned i = 0; i < 16; ++i) {
                const auto e = i < emitters.size() ? emitters[i] : LavaEmitter{};
                const interop::float4 light{e[0], e[1], e[2], e[3]};
                material.shoreSegments[i] = light;
            }
            // Nearby glow is cached on native scenery. Never schedule the
            // old viewport-wide ray/capture/composite lighting pass, even
            // when an older settings file requested ray-traced lighting.
            material.sunDirection.w = 0;
            material.shoreCounts[0].y = reflection == Reflection::Hardware ? 1.0F : 0.0F;
            material.shoreCounts[0].z = scale(lava_surface_brightness);
            if (reflection == Reflection::Sky)
                material.appearance.x = 0;
            material.sunColor.w = scale(lava_pulse_strength);
            material.skyColor.w = scale(lava_light_range);
            material.optics.y = scale(lava_pulse_speed);
            // Lava has no shoreline field. Carry the immutable per-draw
            // lighting sample divisor in an otherwise unused field.
            material.shoreCounts[0].x = float(16U >> std::clamp(lava_lighting_quality.load(), 0, 2));
        }
    }
    if (dynamic) {
        // One bounded diagnostic per scene/outcome, not per wave tile/frame.
        // Reports actual material coverage for user-run visual playtests.
        struct Trace {
            std::uint32_t scene = ~0U, mask = 0, lines = 0;
        };
        static thread_local Trace trace;
        if (trace.scene != scene) {
            trace.scene = scene;
            trace.mask = 0;
        }
        const unsigned outcome = material.identity.x > 0 ? 0U : camera_valid ? 1U : 2U;
        const auto bit = 1U << outcome;
        if (!(trace.mask & bit) && trace.lines < 64) {
            trace.mask |= bit;
            ++trace.lines;
            std::fprintf(
                stderr, "[water][waves] scene=%u source=%08X triangles=%u material=%s camera=%s interpolation-tag=%u\n",
                scene, source, count, material.identity.x > 0 ? "enhanced" : "original",
                camera_valid ? "valid" : "missing", unsigned(procedural));
        }
    }
    if (std::memcmp(&material, &state.waterMaterial, sizeof(material)) != 0) {
        state.flush();
        state.waterMaterial = material;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::WaterMaterial);
    }
    state.waterRayGeometry = material.effects.w > 0.5F ? registry.ray_geometry() : nullptr;
}
} // namespace dkr::runtime::water::enhanced
