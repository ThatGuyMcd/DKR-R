#include "enhanced_water_policy.hpp"
#include "enhanced_water_registry.hpp"
#include "enhanced_water_metadata.hpp"
#include "WaterResourcePolicy.h"
#include "WaterWorkPolicy.h"
#include "enhanced_water_settings.hpp"
#include "waterfall_scroll_policy.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

using namespace dkr::runtime::water::enhanced;
void require(bool value, const char* what) {
    if (!value) {
        std::cerr << what << '\n';
        std::exit(1);
    }
}
// Optional local asset audit. Inputs are decompressed models extracted from a
// user's ROM; no copyrighted fixtures are shipped. Relocate exactly the fields
// consumed by the registry, then emulate load_texture's real 3D cache keys.
int audit_stock_model(const char* path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "stock audit input opens");
    const std::vector<std::uint8_t> blob{std::istreambuf_iterator<char>(file), {}};
    require(blob.size() >= 0x4C && blob.size() <= 0xC0000, "bounded stock model input");
    std::vector<std::uint8_t> ram(0x800000);
    constexpr std::uint32_t base = 0x80010000, cache = 0x80400000, headers = 0x80500000;
    const auto byte = [&](std::uint32_t p, std::uint8_t v) { ram[(p & 0x1FFFFFFFU) ^ 3U] = v; };
    const auto half = [&](std::uint32_t p, std::uint16_t v) {
        byte(p, v >> 8);
        byte(p + 1, v);
    };
    const auto word = [&](std::uint32_t p, std::uint32_t v) {
        half(p, v >> 16);
        half(p + 2, v);
    };
    for (std::size_t i = 0; i < blob.size(); ++i)
        byte(base + std::uint32_t(i), blob[i]);
    Memory m(ram);
    const auto relocate = [&](std::uint32_t field, std::uint64_t size) {
        const auto offset = m.u32(field);
        require(std::uint64_t(offset) + size <= blob.size(), "model field stays within input");
        word(field, base + offset);
        return base + offset;
    };
    const auto nt = m.u16(base + 0x18), ns = m.u16(base + 0x1A);
    require(nt <= 256 && ns <= 128, "bounded stock tables");
    const auto textures = relocate(base, nt * 8), segments = relocate(base + 4, ns * 0x44);
    std::vector<std::uint16_t> texture_ids(nt);
    for (unsigned t = 0; t < nt; ++t) {
        texture_ids[t] = std::uint16_t(m.u32(textures + t * 8));
        require(texture_ids[t] < 0x8000, "stock texture ordinal has no cache namespace bit");
        const auto ptr = headers + t * 64;
        word(textures + t * 8, ptr);
        word(cache + t * 8, 0x8000U | texture_ids[t]);
        word(cache + t * 8 + 4, ptr);
        byte(ptr, 4);
        byte(ptr + 1, 4);
        byte(ptr + 2, 1);
        half(ptr + 0x12, 1 << 8);
        half(ptr + 0x16, 64);
    }
    struct Expected {
        std::uint32_t source, count, batch;
        Kind kind;
    };
    std::vector<Expected> expected;
    std::uint32_t reference = 0;
    Kind reference_kind = Kind::None;
    for (unsigned s = 0; s < ns; ++s) {
        const auto segment = segments + s * 0x44;
        const auto nv = m.u16(segment + 0x1C), nf = m.u16(segment + 0x1E), nb = m.u16(segment + 0x20);
        const auto vertices = relocate(segment, std::uint64_t(nv) * 10);
        const auto triangles = relocate(segment + 4, std::uint64_t(nf) * 16);
        const auto batches = relocate(segment + 12, std::uint64_t(nb + 1) * 12);
        for (unsigned b = 0; b < nb; ++b) {
            const auto batch = batches + b * 12;
            const auto ordinal = m.u8(batch);
            if (ordinal == 255)
                continue;
            require(ordinal < nt, "stock texture table ordinal valid");
            const auto flags = m.u32(batch + 8);
            const auto kind =
                classify(MaterialEvidence{flags, texture_ids[ordinal], m.u8(textures + ordinal * 8 + 7), true, true});
            if (!reference && (flags & ((1U << 24) | kWater | kHidden)) == ((1U << 24) | kWater)) {
                reference = batch;
                reference_kind = kind;
            }
            if (kind == Kind::None)
                continue;
            const auto v = m.u16(batch + 2), f = m.u16(batch + 4), ve = m.u16(batch + 14), fe = m.u16(batch + 16);
            require(ve > v && ve <= nv && fe > f && fe <= nf, "stock liquid has bounded batch geometry");
            bool area = false;
            for (unsigned i = f; i < fe; ++i) {
                const auto tri = triangles + i * 16;
                const auto a = m.u8(tri + 1), bb = m.u8(tri + 2), c = m.u8(tri + 3);
                require(a < ve - v && bb < ve - v && c < ve - v, "stock liquid indices valid");
                area |= valid_triangle(m.vertex(vertices + (v + a) * 10), m.vertex(vertices + (v + bb) * 10),
                                       m.vertex(vertices + (v + c) * 10));
            }
            if (area)
                expected.push_back({triangles + f * 16, std::uint32_t(fe - f), batch, kind});
        }
    }
    Registry registry;
    registry.rebuild(m, base, cache, nt, reference, {}, false, false, {}, true);
    require(registry.surfaces().size() == expected.size(), "all stock liquid batches survive real cache encoding");
    for (const auto& draw : expected) {
        const auto surface = registry.find(draw.source, draw.count);
        require(surface && surface->kind == draw.kind, "stock material maps to its actual triangle range");
    }
    const bool animated = reference_kind == Kind::Waves || reference_kind == Kind::Lava;
    require(registry.procedural().has_value() == animated,
            "stock water and molten wave references resolve; fog remains excluded");
    if (animated)
        for (bool v80 : {false, true}) {
            const auto symbols = wave_symbols(v80);
            word(symbols.batch, reference);
            word(symbols.controller, 4);
            word(symbols.players, 2);
            word(symbols.model, 0x80470000);
            word(symbols.texture, headers);
            for (unsigned i = 0; i < 4; ++i)
                word(symbols.triangles + i * 4, 0x80600000 + i * 512);
            for (unsigned i = 0; i < 4; ++i)
                for (unsigned row = 0; row < 4; ++row)
                    require(registry.find_dynamic(m, symbols, 0x80600000 + i * 512 + row * 128, 8).has_value(),
                            "stock wave material reaches dynamic draws in both revision layouts");
        }
    unsigned flowing = 0, mapped_flow = 0;
    if (reference_kind == Kind::Lava) {
        require(bool(registry.glow_geometry()), "stock lava creates a complete native light cache input");
        const auto begin = std::chrono::steady_clock::now();
        LavaGlowCache glow;
        glow.prepare(registry.glow_geometry(), 100);
        const double prepare_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        unsigned steps = 0, lit = 0;
        double peak_step_ms = 0;
        while (!glow.ready() && steps < 4096) {
            const auto step = std::chrono::steady_clock::now();
            glow.step(1);
            ++steps;
            peak_step_ms =
                std::max(peak_step_ms,
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - step).count());
        }
        require(glow.ready(), "stock lava cached lighting completes without GPU queries");
        unsigned sources = 0;
        for (const auto& t : registry.glow_geometry()->triangles) {
            if (t.source) {
                ++sources;
                continue;
            }
            for (unsigned i = 0; i < 3; ++i)
                if (glow.energy(t.addresses[i], t.p[i], 2) > 0.005F)
                    ++lit;
        }
        require(sources > 16 && lit > 0,
                "real volcano extends beyond old point-light cap and illuminates native scenery");
        const auto stable = glow.ray_tests();
        for (unsigned i = 0; i < 120; ++i)
            glow.step(2);
        require(glow.ray_tests() == stable, "real volcano steady frames never recheck static occlusion");
        std::cout << "Lava cache: source-triangles=" << sources << " receivers=" << glow.receivers()
                  << " lit-corners=" << lit << " prepare-ms=" << prepare_ms << " bounded-steps=" << steps
                  << " peak-step-ms=" << peak_step_ms << " cached-occlusion-tests=" << stable
                  << " steady-frame-lighting-rays=0\n";
    }
    for (const auto& surface : registry.surfaces())
        if (surface.kind == Kind::Waterfall || surface.kind == Kind::Fountain) {
            for (auto source = surface.begin; source < surface.end; source += 16) {
                ++flowing;
                if (const auto* f = registry.flow_triangle(source); f && f->corners[0][3] > 0)
                    ++mapped_flow;
            }
        }
    std::cout << "PASS stock model " << path << ": liquid-batches=" << expected.size() << " flowing=" << flowing
              << " mapped-flow=" << mapped_flow << " wave-reference=" << animated
              << " lava=" << (reference_kind == Kind::Lava) << '\n';
    return 0;
}
int main(int argc, char** argv) {
    using namespace RT64::water_policy;
    {
        std::vector<LavaEmitter> emitters;
        for (unsigned i = 0; i < 4000; ++i)
            add_lava_emitter(emitters, {float(i), 0, 0, 20});
        require(emitters.size() == 16, "lava emitters remain bounded without dropping an entire river end");
        double area = 0;
        for (const auto& e : emitters) {
            area += e[3] * e[3];
            for (float v : e)
                require(std::isfinite(v), "merged lava lights finite");
        }
        require(std::abs(area - 1600000) < 10,
                "area-weighted emitter merge retains emission area within radius budget");
        add_lava_emitter(emitters, {0, 0, 0, 0});
        add_lava_emitter(emitters, {NAN, 0, 0, 1});
        require(emitters.size() == 16, "invalid emitters cannot enter lighting buffer");
        for (unsigned i = 0; i < 10000; ++i) {
            const auto p = lava_pulse(i * 0.01f, 0.35f, 1);
            require(p >= 0.65f && p <= 1, "smooth lava pulse is bounded");
        }
        require(lava_pulse(100, 0, 1) == 1, "zero pulse strength preserves steady lighting");
        reset_appearance();
        load_tuning("rtx_lava_lighting_intensity", 999);
        require(lava_lighting == 300, "persisted light intensity is clamped");
        load_tuning("rtx_lava_lighting_intensity", -1);
        require(lava_lighting == 0, "zero light intensity disables pass");
        reset_appearance();
        require(lava_lighting == 100 && lava_pulse_strength == 35 && lava_light_range == 100,
                "lighting defaults restored with appearance");
        const auto water_quality = requested_quality.load();
        const auto water_reflection = requested_reflection.load();
        load_tuning("rtx_lava_lighting_enabled", 0);
        require(lava_lighting_enabled == 0 && lava_lighting == 100 && requested_quality == water_quality &&
                    requested_reflection == water_reflection,
                "independent light switch preserves intensity, surface and reflection settings");
        load_tuning("rtx_lava_lighting_quality", -10);
        require(lava_lighting_quality == 0, "lighting quality lower clamp");
        load_tuning("rtx_lava_lighting_quality", 99);
        require(lava_lighting_quality == 2, "lighting quality upper clamp");
        load_tuning("rtx_lava_lighting_enabled", 99);
        require(lava_lighting_enabled == 1, "lighting enable upper clamp");
        reset_appearance();
        require(lava_lighting_enabled == 1 && lava_lighting_quality == 1,
                "balanced independent lighting defaults restored");
    }
    {
        const std::array<Vec3, 3> river{{{0, 20, 0}, {100, 20, 0}, {0, 0, 100}}};
        const std::array<std::array<float, 2>, 3> uv{{{0, 0}, {32, 0}, {0, 32}}};
        const std::array<float, 2> period{32, 32};
        const auto flow = flow_mapping(river, uv, period);
        require(flow.direction == 1 && flow_matches(flow, river[2], uv[2], period),
                "river film follows authored downhill V, not fixed world Y");
        const auto midpoint = flow.at({25, 10, 50});
        require(std::abs(midpoint[0] - 0.25F) < 1e-5F && std::abs(midpoint[1] - 0.5F) < 1e-5F,
                "world-to-flow mapping retains authored phase");
        auto mirrored = uv;
        for (auto& at : mirrored)
            at[1] = -at[1];
        require(flow_mapping(river, mirrored, period).direction == -1, "mirrored river UVs still flow downhill");
        const std::array<Vec3, 3> turned{{{0, 20, 0}, {0, 20, 100}, {-100, 0, 0}}};
        const auto rotated = flow_mapping(turned, uv, period);
        require(rotated.direction == 1 && rotated.at({-50, 10, 25}) == midpoint,
                "rotated channel retains local flow frame");
        auto shifted = uv[2];
        shifted[0] += 32;
        shifted[1] -= 64;
        require(flow_matches(flow, river[2], shifted, period), "adjacent UV repeats qualify without a film seam");
        shifted[1] += 8;
        require(!flow_matches(flow, river[2], shifted, period),
                "non-affine mapping cannot silently inherit an incorrect frame");
        auto invalid = river;
        invalid[2] = river[1];
        require(!flow_mapping(invalid, uv, period).direction && !flow_mapping(river, uv, {0, 32}).direction &&
                    !flow_mapping(river, {}, period).direction,
                "degenerate positions, periods and UV frames fail closed");
    }
    require(dkr::runtime::water::waterfall_uv_velocity(255.75F, 0.25F, 256) == 0.5F &&
                dkr::runtime::water::waterfall_uv_velocity(0.25F, 255.75F, 256) == -0.5F,
            "waterfall wrap never reverses or jumps");
    require(dkr::runtime::water::waterfall_uv_velocity(1023.75F, -1023.75F, 2048) == 0.5F &&
                dkr::runtime::water::waterfall_uv_velocity(31.75F, 0.25F, 32) == 0.5F,
            "large and small authored waterfall periods work");
    require(dkr::runtime::water::waterfall_uv_velocity(0, 1, 0) == 0 &&
                dkr::runtime::water::waterfall_uv_velocity(0, 1, std::numeric_limits<float>::infinity()) == 0,
            "invalid waterfall period fails closed");
    const auto modern = sceneBytes(2560, 1440, 1, false, true, 2560, 1440, 1);
    const auto classic = sceneBytes(2560, 1440, 1, true, false, 2560, 1440, 1);
    require(fits(modern, classic, 1), "1440p horizontal and vertical water fit together");
    require(!fits(2 * modern + classic, modern, 3), "active scratch targets remain bounded");
    require(!fits(0, sceneBytes(8193, 1440, 1, false, true, 8193, 1440, 1), 0),
            "oversized target rejected without allocation");
    require(!fits(0, UINT64_MAX, 0) && !fits(sceneBudget + 1, 1, 0), "budget arithmetic never wraps");
    for (const unsigned samples : {1U, 2U, 4U}) {
        const auto root = scratchBytes(2560, 1440, samples, false, false, false, 2560, 1440, samples);
        const auto isolated = scratchBytes(2560, 1440, samples, false, true, true, 2560, 1440, samples);
        const auto vertical = scratchBytes(2560, 1440, samples, true, false, false, 2560, 1440, samples, 2560, 1472);
        require(vertical == 12, "waterfalls never allocate a fullscreen MSAA colour atlas");
        require(fits(root, isolated, 1) && fits(root + isolated, vertical, 2),
                "reported 1440p mixed Z modes and fullscreen waterfall fit, including 4x MSAA");
        const auto isolated_first = scratchBytes(2560, 1440, samples, false, false, true, 2560, 1440, samples);
        const auto opaque_second = scratchBytes(2560, 1440, samples, false, true, false, 2560, 1440, samples);
        require(root + isolated == isolated_first + opaque_second,
                "scratch budget is independent of horizontal draw order");
        require(fits(vertical, isolated_first, 1), "vertical-first ordering fits identical quality");
    }
    require(bucketExtent(1) == 64 && bucketExtent(65) == 128 && bucketExtent(8192) == 8192 && !bucketExtent(8193),
            "atlas extents are bounded and bucketed");
    require(scratchBytes(2560, 1440, 4, true, false, false, 2560, 1440, 4, 0, 0) == 12,
            "vertical rendering does not depend on projected capture bounds");
    require(scratchBytes(8193, 1440, 4, true, false, false, 8193, 1440, 4) == UINT64_MAX,
            "direct waterfalls retain target validation");
    set_quality(2);
    reset_tuning();
    require(preset() == 3 && reflection_detail == -1 && wake_detail == -1,
            "old High settings retain automatic high detail");
    requested_reflection = Reflection::Hardware;
    apply_preset(1);
    require(preset() == 1 && requested_reflection == Reflection::Hardware,
            "Performance preset never silently changes reflection method");
    require(load_tuning("enhanced_water_reflection_distance", 10000) && reflection_distance == 200,
            "persisted tuning is clamped");
    require(load_tuning("enhanced_water_wake_detail", -200) && wake_detail == -1,
            "old automatic detail value remains supported");
    require(!load_tuning("unrelated_setting", 100), "water parser does not consume unrelated settings");
    require(preset() == 4, "individual tuning marks Custom");
    reset_tuning();
    set_quality(0);
    require(retainedScenes(8, 2) == 2 && retainedScenes(2, 0) == 0, "unused menu targets are reclaimed");
    Recovery recovery;
    require(recovery.ready(), "water initially available");
    recovery.fail();
    require(!recovery.ready() && recovery.failed, "failed workload reverts safely");
    for (unsigned i = 0; i < 119; i++) {
        recovery.beginFrame();
        require(!recovery.ready(), "allocation retries are bounded");
    }
    recovery.beginFrame();
    require(recovery.ready(), "transient failure cannot disable the session permanently");
    if (argc == 3 && std::string_view(argv[1]) == "--stock-model")
        return audit_stock_model(argv[2]);
    require(texture_3d_id(0x800FU) == std::optional<std::uint16_t>(15),
            "live 3D texture namespace decoded before water classification");
    require(!texture_3d_id(15) && !texture_3d_id(0xFFFFFFFFU) && !texture_3d_id(0x1800FU),
            "2D, freed and malformed cache identities never alias 3D textures");
    Metadata appearance;
    std::string error;
    require(
        parse_metadata(
            nlohmann::json::parse(
                R"({"version":1,"profile":{"roughness":0.4,"deep":[0,0.1,0.2]},"materials":[{"texture":7,"kind":"calm"}]})"),
            appearance, error),
        "valid optical metadata");
    require(appearance.profile && appearance.profile->roughness == 0.4F && appearance.materials[7] == Hint::Calm,
            "metadata mapped by ordinal");
    require(!parse_metadata(nlohmann::json::parse(R"({"version":1,"profile":{"visibility":-1}})"), appearance, error),
            "unsafe metadata rejected");
    require(!parse_metadata(
                nlohmann::json::parse(
                    R"({"version":1,"materials":[{"texture":7,"kind":"waves"},{"texture":7,"kind":"exclude"}]})"),
                appearance, error),
            "duplicate water material rejected");
    require(appearance.materials[7] == Hint::Calm, "failed metadata parse is transactional");
    require(!parse_metadata(nlohmann::json::parse(R"({"version":1,"profile":{"sun":[0,0,0]}})"), appearance, error),
            "zero light direction rejected");
    MaterialEvidence e{kWater | kWaves, 15, 14, true, true};
    require(classify(e) == Kind::Waves, "procedural water");
    e.flags = 0;
    e.surface_type = 11;
    e.texture_id = 5;
    require(classify(e) == Kind::Calm, "unflagged calm hub water");
    e.texture_id = 60000;
    require(classify(e) == Kind::Calm, "custom liquid material");
    e.surface_type = 0;
    require(classify(e) == Kind::None, "unknown transparent material");
    e.flags = kWater | kWaves;
    for (auto id : {58, 158, 221, 265, 300, 320, 503, 871, 898, 927, 1056}) {
        e.texture_id = static_cast<std::uint16_t>(id);
        require(classify(e) == Kind::None, "false-positive water flag");
    }
    e.texture_id = 16;
    require(classify(e) == Kind::Lava, "reviewed Hot Top molten wave texture is not water");
    e.flags = 0;
    require(classify(e) == Kind::None, "lava artwork without liquid evidence is not replaced");
    e.flags = kWater | kWaves;
    e.texture_id = 15;
    e.surface_type = 10;
    require(classify(e) == Kind::None, "ice collision type");
    e.surface_type = 11;
    e.texture_id = 317;
    require(classify(e) == Kind::Waterfall, "waterfall separate from horizontal surface");
    e.texture_id = 1241;
    require(classify(e) == Kind::Fountain, "fountain separate material");
    e.flags |= kHidden;
    require(classify(e) == Kind::None, "hidden reference triangles");
    e.flags = 0;
    e.texture_id = 16;
    e.hint = Hint::Calm;
    require(classify(e) == Kind::Calm, "explicit custom repurposed texture");
    e.geometry_valid = false;
    require(classify(e) == Kind::None, "metadata cannot permit corrupt geometry");
    require(valid_triangle({0, 0, 0}, {10, 0, 0}, {0, 0, 10}), "valid plane");
    require(!valid_triangle({0, 0, 0}, {0, 0, 0}, {0, 0, 10}), "degenerate plane");
    require(!valid_triangle({NAN, 0, 0}, {10, 0, 0}, {0, 0, 10}), "nonfinite plane");
    for (auto map : kStockWaterMaps)
        require(valid_profile(stock_profile(map)), "stock optics validated");
    require(valid_profile(lava_profile()), "bounded molten profile");
    Metadata lava_metadata;
    require(parse_metadata(nlohmann::json::parse(R"({"version":1,"materials":[{"texture":7,"kind":"lava"}]})"),
                           lava_metadata, error) &&
                lava_metadata.materials[7] == Hint::Lava,
            "custom lava metadata uses the same validated ordinal system");
    require(classify({0, 60000, 0, true, true, Hint::Lava}) == Kind::Lava &&
                classify({kHidden, 60000, 0, true, true, Hint::Lava}) == Kind::None,
            "explicit custom lava cannot bypass hidden geometry");
    require(contains_stock_water(40) && contains_stock_water(53), "both Bubbler races");
    for (auto map : {1, 2, 6, 7, 9, 11, 13, 15, 17, 18, 24, 27, 32, 33, 34, 35, 38, 41, 46, 52, 54, 55, 62})
        require(!contains_stock_water(map), "non-liquid scene coverage");
    Profile p;
    p.absorption = NAN;
    require(!valid_profile(p), "bad custom profile rejected");
    Capabilities caps{true, true, true, true, false, false};
    require(select(Quality::High, Reflection::Hardware, caps).reflection == Reflection::ScreenSpace,
            "honest hardware fallback");
    caps.hardware_rays = true;
    require(select(Quality::High, Reflection::Hardware, caps).reflection == Reflection::ScreenSpace,
            "hardware adapter required");
    caps.hardware_adapter_ready = true;
    require(select(Quality::High, Reflection::Hardware, caps).reflection == Reflection::Hardware,
            "qualified hardware path");
    caps.sampled_depth = false;
    require(select(Quality::High, Reflection::ScreenSpace, caps).quality == Quality::Original,
            "safe resource fallback");
    History h;
    require(h.observe(1, 1), "first presentation resets");
    require(!h.observe(1, 1.016), "continuous presentation");
    require(!h.observe(1, 1.016), "repeat presentation does not create events");
    require(h.observe(1, 1), "rollback resets effects");
    require(h.observe(2, 1.016), "scene reset");
    std::vector<std::uint8_t> ram(0x10000);
    const auto byte = [&](std::uint32_t p, std::uint8_t v) { ram[(p & 0x1FFFFFFFU) ^ 3U] = v; };
    const auto half = [&](std::uint32_t p, std::uint16_t v) {
        byte(p, v >> 8);
        byte(p + 1, v);
    };
    const auto word = [&](std::uint32_t p, std::uint32_t v) {
        half(p, v >> 16);
        half(p + 2, v);
    };
    constexpr auto base = 0x80001000U, tex = 0x80002000U, seg = 0x80003000U;
    constexpr auto verts = 0x80004000U, tris = 0x80005000U, batches = 0x80006000U, cache = 0x80007000U;
    constexpr auto scroll_list = 0x80009000U, scroll_object = 0x80009100U, scroll_state = 0x80009200U;
    word(scroll_list, scroll_object);
    half(scroll_object + 0x48, 29);
    word(scroll_object + 0x64, scroll_state);
    half(scroll_state, 29);
    half(scroll_state + 4, 0);
    half(scroll_state + 6, 47);
    auto scrolls = texture_scrolls(Memory(ram), scroll_list, 1);
    require(scrolls[29].u == 0 && scrolls[29].v == 47, "hub river authored positive V scroll captured");
    const std::array<Vec3, 3> channel{{{0, 0, 0}, {100, 0, 0}, {0, 0, 100}}};
    const std::array<Vec3, 3> fall{{{0, 0, 0}, {100, 0, 0}, {0, 100, 0}}};
    const std::array<std::array<float, 2>, 3> coords{{{0, 0}, {32, 0}, {0, 32}}};
    const auto channel_mapping = flow_mapping(channel, coords, {32, 32});
    const auto fall_mapping = flow_mapping(fall, coords, {32, 32});
    require(authored_flow_direction(channel_mapping, channel, scrolls[29], true) == -1 &&
                authored_flow_direction(channel_mapping, channel, {0, -47}, true) == 1,
            "river follows retail UV motion, not reversed pattern motion");
    require(authored_flow_direction(fall_mapping, fall, {0, 47}, true) == fall_mapping.direction,
            "working vertical waterfalls retain downhill direction");
    require(authored_flow_direction(channel_mapping, channel, {0, 47}, false) == channel_mapping.direction &&
                authored_flow_direction(channel_mapping, channel, {12, 47}, true) == channel_mapping.direction,
            "non-scrolling triangles and unsupported diagonal scroll do not receive a guessed override");
    word(scroll_list + 4, scroll_object);
    require(texture_scrolls(Memory(ram), scroll_list, 2)[29].v == 94, "multiple scroll objects add like retail");
    half(scroll_object + 6, 0x8000);
    require(texture_scrolls(Memory(ram), scroll_list, 1) == TextureScrolls{},
            "particle layout never interpreted as a scroll object");
    half(scroll_object + 6, 0);
    half(scroll_state, 256);
    require(texture_scrolls(Memory(ram), scroll_list, 1) == TextureScrolls{}, "bad scroll ordinal excluded");
    half(scroll_state, 29);
    half(scroll_state + 6, 128);
    require(texture_scrolls(Memory(ram), scroll_list, 1) == TextureScrolls{},
            "invalid signed-byte retail scroll speed excluded");
    half(scroll_state + 6, 47);
    word(scroll_object + 0x64, 0xFFFFFFF0);
    require(texture_scrolls(Memory(ram), scroll_list, 1) == TextureScrolls{} &&
                texture_scrolls(Memory(ram), scroll_list, 513) == TextureScrolls{} &&
                texture_scrolls(Memory(ram), 0xFFFFFFF0, 1) == TextureScrolls{},
            "bounded immutable scroll reads reject corrupt pointers and counts");
    word(scroll_object + 0x64, scroll_state);
    word(base, tex);
    word(base + 4, seg);
    half(base + 0x18, 1);
    half(base + 0x1A, 1);
    // Match Whale Bay/Pirate Lagoon: surface type 0, liquid identified by a
    // 3D texture cache key and batch flags, not the collision-type shortcut.
    word(tex, 0x80008000);
    byte(tex + 7, 0);
    word(cache, 0x800F);
    word(cache + 4, 0x80008000);
    word(seg, verts);
    word(seg + 4, tris);
    word(seg + 12, batches);
    half(seg + 0x1C, 3);
    half(seg + 0x1E, 1);
    half(seg + 0x20, 1);
    half(verts + 10, 100);
    half(verts + 20 + 4, 100);
    byte(tris + 1, 0);
    byte(tris + 2, 1);
    byte(tris + 3, 2);
    byte(batches, 0);
    half(batches + 12 + 2, 3);
    half(batches + 12 + 4, 1);
    word(batches + 8, kWater | kWaves);
    Registry registry;
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.surfaces().size() == 1 && registry.procedural().has_value(), "snapshot water registry");
    require(registry.find(0x5000, 1).has_value(), "physical triangle range lookup");
    require(registry.liquid_height(10, 10) == std::optional<float>(0), "visual wet contact inside liquid");
    require(!registry.liquid_height(100, 100) && !registry.liquid_height(NAN, 0),
            "dry terrain and invalid contact rejected");
    require(!registry.find(0x5000, 2) && !registry.find(0x4FF0, 1), "no range bleed into other materials");
    require(registry.find(tris, 1) && !registry.find(0x5001, 1) && !registry.find(0x5000, 0),
            "static material requires a complete aligned triangle draw");
    for (auto id : {5U, 6U, 12U, 13U, 15U, 140U, 1156U}) {
        word(cache, 0x8000U | id);
        registry.rebuild(Memory(ram), base, cache, 1, batches);
        require(registry.procedural().has_value(), "all reviewed 3D liquid IDs resolve from live cache keys");
        word(batches + 8, kWater);
        registry.rebuild(Memory(ram), base, cache, 1, 0);
        require(registry.find(tris, 1) && registry.find(tris, 1)->kind == Kind::Calm,
                "flagged flat water uses the same decoded IDs");
        word(batches + 8, kWater | kWaves);
    }
    word(cache, 15);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.surfaces().empty(), "2D ordinal cannot identify 3D ocean water");
    word(cache, 0xFFFFFFFFU);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.surfaces().empty(), "freed cache key cannot identify water");
    word(cache, 0x800F);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    ram.resize(0x800000);
    // Real wordswapped retail layouts, both revisions, with the same
    // row-at-a-time DMA pattern as Whale Bay/Pirate Lagoon. No interpolation
    // sidecar is supplied: optical coverage must not depend on that tag.
    for (bool v80 : {false, true}) {
        const auto symbols = wave_symbols(v80);
        word(symbols.controller, 4);
        word(symbols.players, 2);
        word(symbols.batch, batches);
        word(symbols.model, 0x8000B000);
        word(symbols.texture, 0x80008000);
        constexpr auto wave_base = 0x80010000U, buffer_bytes = 512U;
        const auto install = [&] {
            for (unsigned i = 0; i < 4; ++i)
                word(symbols.triangles + i * 4, wave_base + i * buffer_bytes);
        };
        install();
        for (unsigned buffer = 0; buffer < 4; ++buffer)
            for (unsigned row = 0; row < 4; ++row) {
                const auto p = wave_base + buffer * buffer_bytes + row * 128;
                const auto surface = registry.find_dynamic(Memory(ram), symbols, p, 8);
                require(surface && surface->kind == Kind::Waves && surface->batch == (batches & 0x1FFFFFFFU),
                        "both frame parities and both viewports receive wave material");
            }
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base + buffer_bytes - 16, 2),
                "dynamic draw cannot bleed across buffers");
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base - 16, 1) &&
                    !registry.find_dynamic(Memory(ram), symbols, wave_base + 1, 1) &&
                    !registry.find_dynamic(Memory(ram), symbols, wave_base, 0),
                "invalid dynamic draw excluded");
        for (unsigned parity = 0; parity < 2; ++parity)
            require(registry.find_dynamic(Memory(ram), symbols, symbols.fallback + parity * 32, 2).has_value(),
                    "low-density animated water retains optical material");
        require(!registry.find_dynamic(Memory(ram), symbols, symbols.fallback + 16, 2),
                "fallback quads cannot cross frame boundary");
        word(symbols.players, 1);
        require(registry.find_dynamic(Memory(ram), symbols, wave_base + buffer_bytes, 8) &&
                    !registry.find_dynamic(Memory(ram), symbols, wave_base + buffer_bytes * 2, 8),
                "single viewport ignores stale secondary viewport pointers");
        word(symbols.players, 4);
        require(registry.find_dynamic(Memory(ram), symbols, wave_base, 8).has_value(),
                "retail four-player layout uses two buffers");
        word(symbols.players, 2);
        word(symbols.triangles + 12, wave_base + buffer_bytes * 3 + 16);
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base, 8), "corrupt wave allocation excluded");
        install();
        word(symbols.triangles, 0);
        require(!registry.find_dynamic(Memory(ram), symbols, symbols.fallback, 2), "freed waves disable fallback too");
        install();
        word(symbols.controller, 0);
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base, 8), "zero subdivisions excluded");
        word(symbols.controller, 0xFFFFFFFF);
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base, 8), "negative subdivisions excluded");
        word(symbols.controller, 16);
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base, 8), "oversized vertex rows excluded");
        word(symbols.controller, 4);
        word(symbols.texture, 0);
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base, 8), "freed texture excludes stale waves");
        word(symbols.texture, 0x80008000);
        word(symbols.model, 0);
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base, 8), "freed model excludes stale waves");
        word(symbols.model, 0x8000B000);
        word(symbols.batch, batches + 12);
        require(!registry.find_dynamic(Memory(ram), symbols, wave_base, 8),
                "different scene reference cannot inherit optics");
        word(symbols.batch, batches);
        // A new allocation takes effect without rebuilding the static registry.
        for (unsigned i = 0; i < 4; ++i)
            word(symbols.triangles + i * 4, wave_base + 0x4000 + i * buffer_bytes);
        require(registry.find_dynamic(Memory(ram), symbols, wave_base + 0x4000, 8) &&
                    !registry.find_dynamic(Memory(ram), symbols, wave_base, 8),
                "live snapshot owns wave allocations");
        install();
    }
    for (auto id : {58U, 158U, 221U, 265U, 300U, 320U, 503U, 871U, 898U, 927U, 1056U}) {
        word(cache, 0x8000U | id);
        byte(tex + 7, 14);
        registry.rebuild(Memory(ram), base, cache, 1, batches);
        require(registry.surfaces().empty(), "encoded non-water IDs retain exclusion even with liquid collision types");
    }
    word(cache, 0x8010);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.find(tris, 1) && registry.find(tris, 1)->kind == Kind::Lava && registry.procedural() &&
                registry.procedural()->kind == Kind::Lava,
            "static and animated Hot Top lava share a distinct molten material");
    require(!registry.liquid_height(10, 10), "molten surfaces cannot emit water craft wakes");
    for (bool v80 : {false, true}) {
        const auto symbols = wave_symbols(v80);
        word(symbols.batch, batches);
        word(symbols.controller, 4);
        word(symbols.players, 1);
        word(symbols.texture, 0x80008000);
        word(symbols.model, base);
        for (unsigned i = 0; i < 2; ++i)
            word(symbols.triangles + i * 4, 0x8000A000 + i * 512);
        const auto molten = registry.find_dynamic(Memory(ram), symbols, 0x8000A000, 8);
        require(molten && molten->kind == Kind::Lava,
                "both retail revisions submit molten material for tessellated lava");
    }
    word(cache, 0x800F);
    byte(tris + 3, 9);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.surfaces().empty(), "malformed custom geometry excluded");
    registry.rebuild(Memory(ram), 0xFFFFFFF0U, cache, 1, batches);
    require(registry.surfaces().empty(), "corrupt model pointer safe fallback");
    byte(tris + 3, 2);
    word(cache, 0x8000U | 500);
    byte(tex + 7, 0);
    word(batches + 8, 0);
    for (auto p : {verts, verts + 10, verts + 20}) {
        byte(p + 6, 255);
        byte(p + 7, 128);
        byte(p + 8, 0);
        byte(p + 9, 255);
    }
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(registry.ray_geometry() && registry.ray_geometry()->vertices.size() == 3,
            "complete immutable static ray triangle");
    const auto held = registry.ray_geometry();
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(registry.ray_geometry() == held, "texture-only rebuild reuses exact immutable ray geometry");
    byte(verts + 6, 254);
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(registry.ray_geometry() != held, "changed ray colours invalidate reuse");
    byte(verts + 6, 255);
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(held->vertices[1].position[0] == 100 && held->vertices[0].colour[0] == 1,
            "ray geometry remains in world space");
    byte(batches, 255);
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(registry.ray_geometry() && registry.ray_geometry()->vertices.size() == 3,
            "untextured opaque world geometry occludes rays");
    byte(batches, 0);
    word(batches + 8, kWater | kWaves);
    word(cache, 0x800F);
    byte(tex + 7, 14);
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(!registry.ray_geometry(), "liquid excluded from reflection occluders");
    require(held->vertices.size() == 3, "previous submitted world survives registry replacement");
    require(!opaque_ray_material(1U << 8, 0) && !opaque_ray_material(0, 1U << 4) && !opaque_ray_material(0, 1U << 2) &&
                !opaque_ray_material(1U << 11, 0),
            "hidden cutout translucent and decal ray exclusion");
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, false);
    require(!registry.ray_geometry(), "non-hardware water allocates no ray world");
    word(cache, 0x8000U | 1241);
    byte(0x80008000, 4);
    byte(0x80008001, 4);
    byte(0x80008002, 1);
    half(0x80008012, 2 << 8);
    half(0x80008016, 64);
    byte(0x80008040, 4);
    byte(0x80008041, 4);
    byte(0x80008042, 1);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.vertical_image_kind(0x8020) == Kind::Fountain &&
                registry.vertical_image_kind(0x8060) == Kind::Fountain,
            "object liquid texture frames recognized");
    require(registry.vertical_image_kind(0x8000) == Kind::None && registry.vertical_image_kind(0x8080) == Kind::None,
            "object liquid never tags headers or adjacent memory");
    word(cache, 1241);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.vertical_image_kind(0x8020) == Kind::None, "2D fountain ordinal cannot tag world water");
    for (auto id : {317U, 1177U, 1391U}) {
        word(cache, 0x8000U | id);
        registry.rebuild(Memory(ram), base, cache, 1, batches);
        require(registry.vertical_image_kind(0x8020) == Kind::Waterfall &&
                    registry.vertical_image_kind(0x8060) == Kind::Waterfall,
                "encoded waterfall frames recognized");
    }
    word(cache, 0x8000U | 1241);
    word(batches + 8, 0);
    byte(tex + 7, 0);
    half(tris + 4, 0);
    half(tris + 6, 0);
    half(tris + 8, 128);
    half(tris + 10, 0);
    half(tris + 12, 0);
    half(tris + 14, 128);
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(registry.ray_geometry() && registry.ray_geometry()->masks.size() == 2 &&
                registry.ray_geometry()->opaque_vertices == 0 && registry.ray_geometry()->vertices[0].liquid[2] == 1,
            "qualified vertical geometry receives an alpha-aware ray material, not an opaque rectangle");
    const auto* authored_flow = registry.flow_triangle(tris);
    require(authored_flow && authored_flow->corners[0][3] == 1 && authored_flow->corners[1][0] == 1 &&
                registry.ray_geometry()->vertices[1].liquid[0] == 4 &&
                registry.ray_geometry()->vertices[1].liquid[3] != 0,
            "visible triangle and its hardware reflection share normalized authored flow coordinates");
    const auto original_corners = authored_flow->corners;
    TextureScrolls river_scrolls{};
    river_scrolls[0] = {0, 47};
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true, true, river_scrolls);
    require(registry.flow_triangle(tris)->corners[0][2] == -1 && registry.ray_geometry()->vertices[0].liquid[3] == -1,
            "river surface and hardware reflection both use authored direction");
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true, false);
    for (unsigned j = 0; j < 3; ++j)
        half(tris + 6 + j * 4, Memory(ram).s16(tris + 6 + j * 4) + 32);
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true, true);
    require(registry.flow_triangle(tris)->corners == original_corners,
            "texture-cache churn cannot reset an existing film's phase");
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true, false);
    require(registry.flow_triangle(tris)->corners != original_corners,
            "a new scene never inherits the preceding scene's film origin");
    const auto film = registry.ray_geometry();
    byte(0x80008021, 1);
    registry.rebuild(Memory(ram), base, cache, 1, batches, {}, true);
    require(registry.ray_geometry() != film, "authored waterfall opacity invalidates immutable ray reuse");
    require(registry.vertical_scroll_period(0x8020) == std::array<float, 2>{4, 4},
            "object waterfall captures texel periods after RSP 1/32 conversion");
    for (unsigned format = 0; format <= 6; ++format) {
        byte(0x80008002, format);
        half(0x80008016, 128);
        require(waterfall_opacity(Memory(ram), 0x80008000, Kind::Waterfall).has_value(),
                "all direct retail opacity formats supported");
    }
    byte(0x80008002, 7);
    require(!waterfall_opacity(Memory(ram), 0x80008000, Kind::Waterfall),
            "unknown paletted opacity cannot become an opaque ray wall");
    byte(0x80008002, 1);
    half(0x80008016, 64);
    half(0x80008016, 33);
    registry.rebuild(Memory(ram), base, cache, 1, batches);
    require(registry.vertical_image_kind(0x8020) == Kind::None, "truncated fountain image rejected");
    using namespace RT64::water_work;
    require(fieldResolution(false, true, 512, 3) == 512 && fieldResolution(false, false, 0, 3) == 1 &&
                fieldResolution(true, true, 256, 3) == 512,
            "wet/dry transitions retain bounded wake allocation");
    for (auto dimensions :
         std::array<std::array<unsigned, 2>, 4>{{{2560, 1440}, {2559, 1439}, {641, 481}, {1920, 1080}}}) {
        const auto width = dimensions[0], height = dimensions[1];
        const auto full = captureRegion(width, height, 0, 0, float(width), float(height));
        require(full.x == 0 && full.y == 0 && full.width == width && full.height == height,
                "full view capture unchanged");
        for (unsigned column = 0; column < 2; column++)
            for (unsigned row = 0; row < 2; row++) {
                const auto left = float(width) * column / 2, top = float(height) * row / 2;
                const auto right = float(width) * (column + 1) / 2, bottom = float(height) * (row + 1) / 2;
                auto region = captureRegion(width, height, left, top, right, bottom);
                require(region.x <= left && region.y <= top && region.x + region.width >= right &&
                            region.y + region.height >= bottom,
                        "split view remains fully captured");
                require(region.x % 32 == 0 && region.y % 32 == 0, "capture includes complete coarse-depth cells");
                unsigned mipWidth = width, mipHeight = height;
                for (unsigned mip = 1; mip < 6; mip++) {
                    mipWidth = std::max(1U, mipWidth / 2);
                    mipHeight = std::max(1U, mipHeight / 2);
                    region = reduceRegion(region, mipWidth, mipHeight);
                    require(region.width && region.height && region.x + region.width <= mipWidth &&
                                region.y + region.height <= mipHeight,
                            "odd-sized split view reduction stays in bounds");
                }
            }
    }
    std::cout << "Enhanced water policy checks passed\n";
}
