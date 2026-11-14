#pragma once
#include "enhanced_water_policy.hpp"
#include "enhanced_water_ray_geometry.hpp"
#include "lava_lighting_policy.hpp"
#include "lava_glow_policy.hpp"
#include "flowing_water_mapping.hpp"
#include <bit>
#include <memory>
#include <map>
#include <vector>
#include <cstring>
#include "texture_binding_index.hpp"
#include "performance_observation.hpp"

namespace dkr::runtime::water::enhanced {

// All offsets are reviewed decomp struct offsets, shared by US v77/v80.
// Inputs are a graphics task's immutable, word-swapped RDRAM snapshot.
class Memory {
    std::span<const std::uint8_t> bytes_;

  public:
    explicit Memory(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool valid(std::uint32_t p, std::uint64_t size) const {
        if (p >= 0x80000000U && p < 0xA0000000U)
            p &= 0x1FFFFFFFU;
        return std::uint64_t(p) + size <= bytes_.size();
    }
    std::uint8_t u8(std::uint32_t p) const {
        p &= 0x1FFFFFFFU;
        return (p ^ 3U) < bytes_.size() ? bytes_[p ^ 3U] : 0;
    }
    std::uint16_t u16(std::uint32_t p) const {
        return (std::uint16_t(u8(p)) << 8U) | u8(p + 1);
    }
    std::int16_t s16(std::uint32_t p) const {
        return std::bit_cast<std::int16_t>(u16(p));
    }
    std::uint32_t u32(std::uint32_t p) const {
        return (std::uint32_t(u16(p)) << 16U) | u16(p + 2);
    }
    float f32(std::uint32_t p) const {
        return std::bit_cast<float>(u32(p));
    }
    Vec3 vertex(std::uint32_t p) const {
        return {float(s16(p)), float(s16(p + 2)), float(s16(p + 4))};
    }
};

struct TextureScroll {
    std::int32_t u = 0, v = 0;
    bool operator==(const TextureScroll&) const = default;
};
using TextureScrolls = std::array<TextureScroll, 256>;

// BHV_TEXTURE_SCROLL and Object/Object_TexScroll offsets are shared by both
// retail revisions. Observe the immutable task snapshot, never guest memory
// being updated by the simulation. Particle pointers have a different layout.
inline TextureScrolls texture_scrolls(Memory m, std::uint32_t list, std::uint32_t count) {
    TextureScrolls result{};
    if (!list || count > 512 || !m.valid(list, std::uint64_t(count) * 4))
        return result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto object = m.u32(list + i * 4);
        if (!object || !m.valid(object, 0x68) || (m.u16(object + 6) & 0x8000U) || m.s16(object + 0x48) != 29)
            continue;
        const auto state = m.u32(object + 0x64);
        if (!state || !m.valid(state, 12))
            continue;
        const auto texture = m.s16(state), u = m.s16(state + 4), v = m.s16(state + 6);
        if (texture < 0 || texture >= 256 || u < -128 || u > 127 || v < -128 || v > 127)
            continue;
        // More than one scroll object can affect a material: retail adds them.
        result[texture].u += u;
        result[texture].v += v;
    }
    return result;
}

inline float authored_flow_direction(const FlowMapping& mapping, const std::array<Vec3, 3>& p, TextureScroll scroll,
                                     bool scrollable) {
    if (!mapping.direction || !scrollable || scroll.u != 0 || scroll.v == 0)
        return mapping.direction;
    const Vec3 a{p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z};
    const Vec3 b{p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z};
    const Vec3 n{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    // Preserve working steep/vertical waterfalls. A river is a mostly
    // horizontal sheet using waterfall artwork. Increasing retail UV offsets
    // moves the visible pattern in -V, not +V. Honor that authored current.
    if (n.y * n.y < (n.x * n.x + n.y * n.y + n.z * n.z) * 0.3025F)
        return mapping.direction;
    return scroll.v > 0 ? -1.0F : 1.0F;
}

struct Surface {
    std::uint32_t begin = 0, end = 0, batch = 0;
    Kind kind = Kind::None;
    float height = 0;
    std::array<float, 2> scroll_period{};
};

struct FlowTriangle {
    std::uint32_t source = 0;
    std::array<Vec3, 3> positions{};
    std::array<std::array<float, 4>, 3> corners{}; // repeating UV, signed V, qualified
    std::array<float, 2> period{};
};

// Reviewed retail TextureHeader formats. Unsupported/paletted masks are not
// guessed opaque. Decode only immutable first-frame alpha; flow remains in the shader.
inline std::optional<RayOpacityMask> waterfall_opacity(Memory m, std::uint32_t texture, Kind kind) {
    if (!texture || !m.valid(texture, 32))
        return {};
    const auto w = m.u8(texture), h = m.u8(texture + 1);
    const auto f = m.u8(texture + 2) & 15U;
    const auto pixels = std::uint32_t(w) * h;
    const auto bytes = f == 0               ? pixels * 4
                       : (f == 1 || f == 4) ? pixels * 2
                       : (f == 2 || f == 5) ? pixels
                       : (f == 3 || f == 6) ? (pixels + 1) / 2
                                            : 0;
    const auto stride = m.u16(texture + 0x16);
    if (!w || !h || !bytes || stride < 32 || bytes > stride - 32 || !m.valid(texture + 32, bytes))
        return {};
    RayOpacityMask mask{};
    mask.params = {float(w), float(h), float(kind), 0};
    const auto alpha = [&](unsigned pixel) {
        const auto p = texture + 32;
        switch (f) {
        case 0:
            return m.u8(p + pixel * 4 + 3) / 255.0F;
        case 1:
            return float(m.u16(p + pixel * 2) & 1U);
        case 2:
            return m.u8(p + pixel) / 255.0F;
        case 3:
            return ((m.u8(p + pixel / 2) >> (pixel % 2 ? 0 : 4)) & 15U) / 15.0F;
        case 4:
            return m.u8(p + pixel * 2 + 1) / 255.0F;
        case 5:
            return (m.u8(p + pixel) & 15U) / 15.0F;
        case 6:
            return float((m.u8(p + pixel / 2) >> (pixel % 2 ? 0 : 4)) & 1U);
        default:
            return 0.0F;
        }
    };
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 32; ++x) {
            const auto opacity = alpha(std::min(unsigned(h) - 1, (2 * y + 1) * h / 64) * w +
                                       std::min(unsigned(w) - 1, (2 * x + 1) * w / 64));
            const auto cell = y * 32 + x;
            mask.alpha[cell / 4] |= std::uint32_t(std::round(opacity * 255)) << ((cell % 4) * 8);
        }
    return mask;
}

struct WaveSymbols {
    std::uint32_t controller, players, batch, triangles, fallback, texture, model;
};
constexpr WaveSymbols wave_symbols(bool us_v80) {
    // Reviewed waves_alloc/waves_free/waves_render addresses. The v80 data
    // arrays move by 0x590, while its BSS controller moves by 0x5C0.
    return us_v80
               ? WaveSymbols{0x8012A588U, 0x8012A638U, 0x8012A63CU, 0x800E3610U, 0x800E3620U, 0x800E3660U, 0x800E3668U}
               : WaveSymbols{0x80129FC8U, 0x8012A078U, 0x8012A07CU, 0x800E3080U, 0x800E3090U, 0x800E30D0U, 0x800E30D8U};
}

class Registry {
    std::vector<std::uint32_t> liquid_headers_;
    void add_liquid_header(std::uint32_t header) {
        if (liquid_headers_.size() < 256 &&
            std::find(liquid_headers_.begin(), liquid_headers_.end(), header) == liquid_headers_.end())
            liquid_headers_.push_back(header);
    }

  public:
    const auto& liquid_headers() const {
        return liquid_headers_;
    }

  private:
    struct WetTriangle {
        Vec3 a, b, c;
    };
    std::vector<Surface> surfaces_;
    struct FlowVertices {
        std::uint32_t end;
        std::array<float, 2> period;
    };
    std::map<std::uint32_t, FlowVertices> flow_vertices_;
    std::vector<FlowTriangle> flow_triangles_;
    std::vector<WetTriangle> wet_triangles_;
    struct VerticalImage {
        std::uint32_t begin, end;
        Kind kind;
        std::array<float, 2> scroll_period;
    };
    std::vector<VerticalImage> vertical_images_;
    std::optional<Surface> procedural_;
    std::optional<Metadata> metadata_;
    std::shared_ptr<const RayGeometry> ray_geometry_;
    std::vector<LavaEmitter> lava_emitters_;
    std::shared_ptr<const GlowGeometry> glow_geometry_;

  public:
    std::array<float, 2> flow_vertex_period(std::uint32_t source, std::uint32_t count) const {
        source &= 0x1FFFFFFFU;
        if (!count)
            return {};
        auto it = flow_vertices_.upper_bound(source);
        if (it == flow_vertices_.begin())
            return {};
        --it;
        if ((source - it->first) % 10 || std::uint64_t(source) + std::uint64_t(count) * 10 > it->second.end)
            return {};
        return it->second.period;
    }
    const std::shared_ptr<const GlowGeometry>& glow_geometry() const {
        return glow_geometry_;
    }
    std::span<const LavaEmitter> lava_emitters() const {
        return lava_emitters_;
    }
    const FlowTriangle* flow_triangle(std::uint32_t source) const {
        source &= 0x1FFFFFFFU;
        const auto it = std::lower_bound(flow_triangles_.begin(), flow_triangles_.end(), source,
                                         [](const FlowTriangle& f, std::uint32_t p) { return f.source < p; });
        return it != flow_triangles_.end() && it->source == source ? &*it : nullptr;
    }
    std::array<float, 2> vertical_scroll_period(std::uint32_t image) const {
        image &= 0x1FFFFFFFU;
        for (const auto& entry : vertical_images_)
            if (image >= entry.begin && image < entry.end)
                return entry.scroll_period;
        return {};
    }
    const std::shared_ptr<const RayGeometry>& ray_geometry() const {
        return ray_geometry_;
    }
    const std::optional<Metadata>& metadata() const {
        return metadata_;
    }
    std::span<const Surface> surfaces() const {
        return surfaces_;
    }
    const std::optional<Surface>& procedural() const {
        return procedural_;
    }
    Kind vertical_image_kind(std::uint32_t physical) const {
        physical &= 0x1FFFFFFFU;
        for (const auto& image : vertical_images_)
            if (physical >= image.begin && physical < image.end)
                return image.kind;
        return Kind::None;
    }
    std::optional<float> liquid_height(float x, float z, float near_y = 0) const {
        if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(near_y))
            return {};
        std::optional<float> best;
        for (const auto& t : wet_triangles_) {
            if (x < (std::min)({t.a.x, t.b.x, t.c.x}) || x > (std::max)({t.a.x, t.b.x, t.c.x}) ||
                z < (std::min)({t.a.z, t.b.z, t.c.z}) || z > (std::max)({t.a.z, t.b.z, t.c.z}))
                continue;
            const float denominator = (t.b.z - t.c.z) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.z - t.c.z);
            if (std::abs(denominator) < 0.0001F)
                continue;
            const float a = ((t.b.z - t.c.z) * (x - t.c.x) + (t.c.x - t.b.x) * (z - t.c.z)) / denominator;
            const float b = ((t.c.z - t.a.z) * (x - t.c.x) + (t.a.x - t.c.x) * (z - t.c.z)) / denominator;
            if (a >= -0.0001F && b >= -0.0001F && a + b <= 1.0001F) {
                const float height = a * t.a.y + b * t.b.y + (1 - a - b) * t.c.y;
                if (!best || std::abs(height - near_y) < std::abs(*best - near_y))
                    best = height;
            }
        }
        return best;
    }
    std::optional<Surface> find(std::uint32_t physical, std::uint32_t triangles) const {
        physical &= 0x1FFFFFFFU;
        if (!triangles)
            return {};
        const auto it = std::upper_bound(surfaces_.begin(), surfaces_.end(), physical,
                                         [](std::uint32_t p, const Surface& s) { return p < s.begin; });
        if (it == surfaces_.begin())
            return {};
        const auto& s = *std::prev(it);
        if (physical < s.begin || (physical - s.begin) % 16 ||
            std::uint64_t(physical) + std::uint64_t(triangles) * 16 > s.end)
            return {};
        return s;
    }
    std::optional<Surface> find_dynamic(Memory m, WaveSymbols symbols, std::uint32_t physical,
                                        std::uint32_t triangles) const {
        // waves_render submits rows from gWaveTriangles, NOT level triangles.
        // Its interpolation identity is optional and cannot own material
        // classification. Read this task's snapshot each time: allocations
        // can change without the cached static level model changing.
        if (!procedural_ || !triangles || !m.valid(symbols.controller, 4) || !m.valid(symbols.players, 4) ||
            !m.valid(symbols.batch, 4) || !m.valid(symbols.triangles, 16) || !m.valid(symbols.texture, 4) ||
            !m.valid(symbols.model, 4))
            return {};
        if ((m.u32(symbols.batch) & 0x1FFFFFFFU) != procedural_->batch)
            return {};
        const auto model = m.u32(symbols.model), texture = m.u32(symbols.texture);
        if (!model || !texture || !m.valid(model, 0x1C) || !m.valid(texture, 32))
            return {};
        const auto n = m.u32(symbols.controller), players = m.u32(symbols.players);
        // Two vertex rows must fit the retail 32-entry RSP vertex cache.
        if (n < 1 || n > 15 || players < 1 || players > 4)
            return {};
        const auto bytes = 32U * n * n, buffers = players == 2 ? 4U : 2U;
        const auto base = m.u32(symbols.triangles);
        if (!base || (base & 7U) || !m.valid(base, std::uint64_t(bytes) * buffers))
            return {};
        for (unsigned i = 1; i < buffers; ++i)
            if (std::uint64_t(m.u32(symbols.triangles + i * 4)) != std::uint64_t(base) + bytes * i)
                return {};
        physical &= 0x1FFFFFFFU;
        const auto bounded = [&](std::uint32_t begin, std::uint32_t end) -> std::optional<Surface> {
            if (physical < begin || (physical - begin) % 16 ||
                std::uint64_t(physical) + std::uint64_t(triangles) * 16 > end)
                return {};
            auto surface = *procedural_;
            surface.begin = begin;
            surface.end = end;
            return surface;
        };
        for (unsigned i = 0; i < buffers; ++i) {
            const auto begin = (base & 0x1FFFFFFFU) + bytes * i;
            if (auto surface = bounded(begin, begin + bytes))
                return surface;
        }
        // Distant/low-density tiles use one of two authored two-triangle
        // quads. Do not let a draw span the opposite frame's quad.
        if (m.valid(symbols.fallback, 64))
            for (unsigned i = 0; i < 2; ++i) {
                const auto begin = (symbols.fallback & 0x1FFFFFFFU) + i * 32;
                if (auto surface = bounded(begin, begin + 32))
                    return surface;
            }
        return {};
    }
    void rebuild(Memory m, std::uint32_t model, std::uint32_t cache, std::uint32_t texture_count,
                 std::uint32_t procedural_batch, std::optional<Metadata> metadata = {}, bool collect_rays = false,
                 bool preserve_flow_phase = false, const TextureScrolls& scrolls = {}, bool collect_glow = false,
                 bool animation_only = false) {
        surgical::Scope registry_timing(surgical::Region::Registry);
        const auto previous_flows = preserve_flow_phase ? std::move(flow_triangles_) : std::vector<FlowTriangle>{};
        flow_triangles_.clear();
        flow_vertices_.clear();
        surfaces_.clear();
        wet_triangles_.clear();
        vertical_images_.clear();
        procedural_.reset();
        lava_emitters_.clear();
        const auto previous_rays = std::move(ray_geometry_);
        const auto previous_glow = std::move(glow_geometry_);
        liquid_headers_.clear();
        auto glow = collect_glow && !animation_only ? std::make_shared<GlowGeometry>() : nullptr;
        auto rays = collect_rays && !animation_only ? std::make_shared<RayGeometry>() : nullptr;
        std::vector<RayVertex> falling_vertices;
        bool ray_limit = false;
        std::array<std::uint32_t, 256> opacity_indices{};
        metadata_ = std::move(metadata);
        if (!m.valid(model, 0x4C) || !model)
            return;
        const auto textures = m.u32(model), segments = m.u32(model + 4);
        const auto nt = m.u16(model + 0x18), ns = m.u16(model + 0x1A);
        if (nt > 256 || ns > 127 || !m.valid(textures, std::uint64_t(nt) * 8) ||
            !m.valid(segments, std::uint64_t(ns) * 0x44) || texture_count > 2048 ||
            !m.valid(cache, std::uint64_t(texture_count) * 8))
            return;
        // Waterfall/fountain artwork can be carried by an object model, not
        // the level segment table. Recognize only reviewed liquid texture IDs
        // and validated texel ranges, never a sprite name or arbitrary alpha.
        // Camera provenance at the draw bridge excludes the ordinary 2D HUD.
        for (std::uint32_t c = 0; c < texture_count; ++c) {
            const auto id = texture_3d_id(m.u32(cache + c * 8));
            const auto ptr = m.u32(cache + c * 8 + 4);
            const Kind kind = id == 1241                                ? Kind::Fountain
                              : (id == 317 || id == 1177 || id == 1391) ? Kind::Waterfall
                                                                        : Kind::None;
            if (kind == Kind::None || !ptr || !m.valid(ptr, 32))
                continue;
            add_liquid_header(ptr);
            const auto frames = (std::max)(1U, std::uint32_t(m.u16(ptr + 0x12) >> 8));
            const auto stride = m.u16(ptr + 0x16);
            if (frames > 256 || stride < 32 || !m.valid(ptr, std::uint64_t(frames) * stride))
                continue;
            for (std::uint32_t frame = 0; frame < frames && vertical_images_.size() < 2048; ++frame) {
                const auto header = ptr + frame * stride;
                const auto pixels = std::uint32_t(m.u8(header)) * m.u8(header + 1), format = m.u8(header + 2) & 15U;
                const auto bytes = format == 0                                   ? pixels * 4
                                   : (format == 1 || format == 4)                ? pixels * 2
                                   : (format == 2 || format == 5 || format == 8) ? pixels
                                   : (format == 3 || format == 6 || format == 7) ? (pixels + 1) / 2
                                                                                 : 0;
                if (!bytes || bytes > stride - 32 || !m.valid(header + 32, bytes))
                    continue;
                vertical_images_.push_back({(header + 32) & 0x1FFFFFFFU,
                                            (header + 32 + bytes) & 0x1FFFFFFFU,
                                            kind,
                                            {float(m.u8(header)), float(m.u8(header + 1))}});
            }
        }
        std::optional<surgical::TextureBindingIndex> binding_index;
        if (surgical::texture_index() && nt >= 8 && texture_count >= 32) {
            try {
                binding_index.emplace(m, cache, texture_count, texture_3d_id);
            } catch (const std::bad_alloc&) {
                binding_index.reset();
            } // Exact original lookup is the fallback.
        }
        std::vector<MaterialEvidence> materials(nt);
        std::uint32_t total_batches = 0, total_triangles = 0;
        for (std::uint32_t t = 0; t < nt; ++t) {
            auto& e = materials[t];
            const auto info = textures + t * 8, ptr = m.u32(info);
            e.texture_valid = ptr && m.valid(ptr, 0x20);
            e.texture_id = 65535;
            e.surface_type = m.u8(info + 7);
            if (metadata_)
                e.hint = metadata_->materials[t];
            if (binding_index)
                e.texture_id = binding_index->find(ptr);
            else
                for (std::uint32_t c = 0; c < texture_count; ++c)
                    if (m.u32(cache + c * 8 + 4) == ptr) {
                        if (const auto id = texture_3d_id(m.u32(cache + c * 8)))
                            e.texture_id = *id;
                        break;
                    }
        }
        for (std::uint32_t s = 0; s < ns; ++s) {
            const auto seg = segments + s * 0x44;
            const auto verts = m.u32(seg), tris = m.u32(seg + 4), batches = m.u32(seg + 12);
            const auto nv = m.u16(seg + 0x1C), nf = m.u16(seg + 0x1E), nb = m.u16(seg + 0x20);
            if (nv > 32767 || nf > 32767 || nb > 4096 || !m.valid(verts, std::uint64_t(nv) * 10) ||
                !m.valid(tris, std::uint64_t(nf) * 16) || !m.valid(batches, std::uint64_t(nb + 1) * 12))
                continue;
            for (std::uint32_t b = 0; b < nb; ++b) {
                if (++total_batches > 8192) {
                    surfaces_.clear();
                    flow_triangles_.clear();
                    flow_vertices_.clear();
                    wet_triangles_.clear();
                    procedural_.reset();
                    return;
                }
                const auto batch = batches + b * 12;
                const auto texture = m.u8(batch);
                const auto v = m.u16(batch + 2), f = m.u16(batch + 4);
                const auto ve = m.u16(batch + 14), fe = m.u16(batch + 16);
                if ((texture >= nt && texture != 255) || ve <= v || ve > nv || fe <= f || fe > nf)
                    continue;
                total_triangles += fe - f;
                if (total_triangles > 262144) {
                    surfaces_.clear();
                    flow_triangles_.clear();
                    flow_vertices_.clear();
                    wet_triangles_.clear();
                    procedural_.reset();
                    return;
                }
                auto e = texture == 255 ? MaterialEvidence{} : materials[texture];
                e.flags = m.u32(batch + 8);
                e.geometry_valid = true;
                auto kind = classify(e);
                if (kind != Kind::None && texture < nt && e.texture_valid)
                    add_liquid_header(m.u32(textures + texture * 8));
                if ((kind == Kind::Waterfall || kind == Kind::Fountain) && texture < nt && e.texture_valid) {
                    const auto header = m.u32(textures + texture * 8);
                    const std::array<float, 2> period{float(m.u8(header)) * 8.F, float(m.u8(header + 1)) * 8.F};
                    if (period[0] >= 8 && period[1] >= 8) {
                        const auto begin = (verts + v * 10) & 0x1FFFFFFFU, end = (verts + ve * 10) & 0x1FFFFFFFU;
                        auto [at, inserted] = flow_vertices_.emplace(begin, FlowVertices{end, period});
                        if (!inserted && (at->second.end != end || at->second.period != period))
                            at->second.period = {};
                    }
                }
                // Vanilla animation needs only validated batch ranges and
                // texture periods. Do not build flow meshes, shoreline data,
                // opacity masks, emitters, ray geometry or lighting caches.
                if (animation_only) {
                    Surface animation{(tris + f * 16) & 0x1FFFFFFFU,
                                      (tris + fe * 16) & 0x1FFFFFFFU,
                                      batch & 0x1FFFFFFFU,
                                      kind,
                                      0,
                                      {}};
                    if ((kind == Kind::Waterfall || kind == Kind::Fountain) && texture < nt && e.texture_valid) {
                        const auto header = m.u32(textures + texture * 8);
                        const std::array<float, 2> period{float(m.u8(header)), float(m.u8(header + 1))};
                        if (period[0] >= 1 && period[1] >= 1)
                            animation.scroll_period = period;
                    }
                    // Retain non-flowing ranges too: an excluded/reused level
                    // texture must not inherit the object-image fallback.
                    surfaces_.push_back(animation);
                    continue;
                }
                // Native, immutable level geometry only. Never collect racers,
                // billboards, transparent overlays or a truncated blocker mesh.
                const auto glow_texture_flags = texture == 255 ? 0 : m.u16(m.u32(textures + texture * 8) + 6);
                if (glow && glow->complete &&
                    (kind == Kind::Lava || (kind == Kind::None && (texture == 255 || e.texture_valid) &&
                                            opaque_ray_material(e.flags, glow_texture_flags)))) {
                    for (std::uint32_t i = f; i < fe; ++i) {
                        const auto tri = tris + i * 16;
                        const std::array<std::uint32_t, 3> indices{m.u8(tri + 1), m.u8(tri + 2), m.u8(tri + 3)};
                        if (std::any_of(indices.begin(), indices.end(), [&](auto n) { return n >= ve - v; })) {
                            glow->complete = false;
                            glow->triangles.clear();
                            break;
                        }
                        GlowTriangle triangle{};
                        triangle.source = kind == Kind::Lava;
                        bool opaque = true;
                        for (unsigned j = 0; j < 3; ++j) {
                            const auto p = verts + (v + indices[j]) * 10;
                            triangle.addresses[j] = p & 0x1FFFFFFFU;
                            triangle.p[j] = m.vertex(p);
                            opaque &= m.u8(p + 9) == 255;
                        }
                        if (!valid_triangle(triangle.p[0], triangle.p[1], triangle.p[2]))
                            continue;
                        if (!triangle.source && !opaque)
                            continue;
                        if (glow->triangles.size() == kGlowTriangleLimit) {
                            glow->complete = false;
                            glow->triangles.clear();
                            break;
                        }
                        glow->triangles.push_back(triangle);
                    }
                }
                if (kind == Kind::None) {
                    const auto texture_flags = texture == 255 ? 0 : m.u16(m.u32(textures + texture * 8) + 6);
                    if (!rays || (texture != 255 && !e.texture_valid) || !opaque_ray_material(e.flags, texture_flags))
                        continue;
                    for (std::uint32_t i = f; i < fe; ++i) {
                        const auto tri = tris + i * 16;
                        const std::array<std::uint32_t, 3> indices{m.u8(tri + 1), m.u8(tri + 2), m.u8(tri + 3)};
                        if (std::any_of(indices.begin(), indices.end(), [&](auto n) { return n >= ve - v; }))
                            continue;
                        std::array<RayVertex, 3> vertices{};
                        bool opaque = true;
                        for (unsigned j = 0; j < 3; ++j) {
                            const auto p = verts + (v + indices[j]) * 10;
                            const auto at = m.vertex(p);
                            vertices[j].position = {at.x, at.y, at.z, 1};
                            vertices[j].colour = {m.u8(p + 6) / 255.0F, m.u8(p + 7) / 255.0F, m.u8(p + 8) / 255.0F, 1};
                            opaque &= m.u8(p + 9) == 255;
                        }
                        const auto p = [&](unsigned j) {
                            return Vec3{vertices[j].position[0], vertices[j].position[1], vertices[j].position[2]};
                        };
                        if (opaque && valid_triangle(p(0), p(1), p(2))) {
                            if (rays->vertices.size() / 3 >= kMaximumRayTriangles) {
                                // Never trace a silently truncated world: it
                                // would produce holes. This level uses SSR.
                                rays.reset();
                                break;
                            }
                            rays->vertices.insert(rays->vertices.end(), vertices.begin(), vertices.end());
                        }
                    }
                    continue;
                }
                std::uint32_t mask_index = 0;
                if (rays && (kind == Kind::Waterfall || kind == Kind::Fountain) && texture < nt && e.texture_valid) {
                    mask_index = opacity_indices[texture];
                    if (!mask_index)
                        if (auto mask = waterfall_opacity(m, m.u32(textures + texture * 8), kind)) {
                            mask_index = std::uint32_t(rays->masks.size());
                            rays->masks.push_back(*mask);
                            opacity_indices[texture] = mask_index;
                        }
                }
                double height = 0;
                std::uint32_t points = 0;
                std::array<float, 2> period{};
                const bool flowing = kind == Kind::Waterfall || kind == Kind::Fountain;
                if (flowing && texture < nt && e.texture_valid) {
                    const auto header = m.u32(textures + texture * 8);
                    period = {float(m.u8(header)), float(m.u8(header + 1))};
                }
                const auto wet_begin = wet_triangles_.size();
                const auto falling_begin = falling_vertices.size();
                const auto flow_begin = flow_triangles_.size();
                std::vector<LavaEmitter> batch_lava;
                for (std::uint32_t i = f; i < fe && e.geometry_valid; ++i) {
                    const auto tri = tris + i * 16;
                    const auto a = m.u8(tri + 1), bb = m.u8(tri + 2), c = m.u8(tri + 3);
                    if (a >= ve - v || bb >= ve - v || c >= ve - v) {
                        e.geometry_valid = false;
                        break;
                    }
                    const auto pa = m.vertex(verts + (v + a) * 10), pb = m.vertex(verts + (v + bb) * 10),
                               pc = m.vertex(verts + (v + c) * 10);
                    if (!valid_triangle(pa, pb, pc))
                        continue; // Zero-area separators carry no surface.
                    if (kind == Kind::Lava) {
                        const float ux = pb.x - pa.x, uy = pb.y - pa.y, uz = pb.z - pa.z;
                        const float vx = pc.x - pa.x, vy = pc.y - pa.y, vz = pc.z - pa.z;
                        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                        // Only upward/molten beds emit area lighting. Existing
                        // vertical lava artwork remains its original material.
                        if (std::abs(ny) > std::sqrt(nx * nx + ny * ny + nz * nz) * 0.5F)
                            add_lava_emitter(batch_lava,
                                             {(pa.x + pb.x + pc.x) / 3, (pa.y + pb.y + pc.y) / 3 + 4,
                                              (pa.z + pb.z + pc.z) / 3,
                                              std::min(600.0F, std::sqrt(std::abs(ny) * 0.5F / 3.14159265F))});
                    }
                    FlowTriangle flow_triangle{};
                    if (flowing) {
                        std::array<std::array<float, 2>, 3> uv{};
                        const std::array<Vec3, 3> p{pa, pb, pc};
                        for (unsigned j = 0; j < 3; ++j)
                            uv[j] = {m.s16(tri + 4 + j * 4) / 32.0F, m.s16(tri + 6 + j * 4) / 32.0F};
                        auto flow = flow_mapping(p, uv, period);
                        flow.direction = authored_flow_direction(flow, p, scrolls[texture], (m.u8(tri) & 0x80U) == 0);
                        flow_triangle.source = tri & 0x1FFFFFFFU;
                        flow_triangle.positions = p;
                        flow_triangle.period = period;
                        if (flow.direction) {
                            for (unsigned j = 0; j < 3; ++j)
                                flow_triangle.corners[j] = {uv[j][0] / period[0], uv[j][1] / period[1], flow.direction,
                                                            1};
                            const auto old =
                                std::lower_bound(previous_flows.begin(), previous_flows.end(), flow_triangle.source,
                                                 [](const FlowTriangle& f, std::uint32_t at) { return f.source < at; });
                            if (old != previous_flows.end() && old->source == flow_triangle.source &&
                                old->period == period && old->corners[0][2] == flow.direction &&
                                std::memcmp(old->positions.data(), p.data(), sizeof(p)) == 0)
                                flow_triangle.corners = old->corners;
                        }
                        flow_triangles_.push_back(flow_triangle);
                    }
                    height += pa.y + pb.y + pc.y;
                    points += 3;
                    if ((kind == Kind::Calm || kind == Kind::Waves) && wet_triangles_.size() < 8192)
                        wet_triangles_.push_back({pa, pb, pc});
                    if (rays && mask_index &&
                        falling_vertices.size() / 3 + rays->vertices.size() / 3 >= kMaximumRayTriangles)
                        ray_limit = true;
                    if (rays && mask_index && !ray_limit) {
                        const std::array<std::uint32_t, 3> indices{a, bb, c};
                        for (unsigned j = 0; j < 3; ++j) {
                            const auto p = verts + (v + indices[j]) * 10;
                            const auto at = m.vertex(p);
                            RayVertex rv{};
                            rv.position = {at.x, at.y, at.z, 1};
                            rv.colour = {m.u8(p + 6) / 255.0F, m.u8(p + 7) / 255.0F, m.u8(p + 8) / 255.0F,
                                         m.u8(p + 9) / 255.0F};
                            rv.liquid = {m.s16(tri + 4 + j * 4) / 32.0F, m.s16(tri + 6 + j * 4) / 32.0F,
                                         float(mask_index), 0};
                            if (flow_triangle.corners[j][3] > 0) {
                                rv.liquid[0] = flow_triangle.corners[j][0] * period[0];
                                rv.liquid[1] = flow_triangle.corners[j][1] * period[1];
                                rv.liquid[3] = flow_triangle.corners[j][2];
                            }
                            falling_vertices.push_back(rv);
                        }
                    }
                }
                if (!e.geometry_valid || !points) {
                    wet_triangles_.resize(wet_begin);
                    falling_vertices.resize(falling_begin);
                    flow_triangles_.resize(flow_begin);
                    continue;
                }
                Surface surface{(tris + f * 16) & 0x1FFFFFFFU, (tris + fe * 16) & 0x1FFFFFFFU, batch & 0x1FFFFFFFU,
                                kind, static_cast<float>(height / points)};
                for (const auto& emitter : batch_lava)
                    add_lava_emitter(lava_emitters_, emitter);
                if (texture < nt && e.texture_valid) {
                    const auto header = m.u32(textures + texture * 8);
                    surface.scroll_period = {float(m.u8(header)), float(m.u8(header + 1))};
                }
                surfaces_.push_back(surface);
                if ((procedural_batch & 0x1FFFFFFFU) == surface.batch &&
                    (kind == Kind::Waves || (kind == Kind::Lava && (e.flags & kWaves))))
                    procedural_ = surface;
            }
        }
        std::sort(surfaces_.begin(), surfaces_.end(),
                  [](const Surface& a, const Surface& b) { return a.begin < b.begin; });
        if (glow && glow->complete &&
            std::any_of(glow->triangles.begin(), glow->triangles.end(),
                        [](const GlowTriangle& t) { return t.source; })) {
            bool same = previous_glow && previous_glow->triangles.size() == glow->triangles.size();
            for (std::size_t i = 0; same && i < glow->triangles.size(); ++i) {
                const auto& a = previous_glow->triangles[i];
                const auto& b = glow->triangles[i];
                same = a.source == b.source && a.addresses == b.addresses;
                for (unsigned j = 0; same && j < 3; ++j) {
                    const auto delta = glow_sub(a.p[j], b.p[j]);
                    same = glow_dot(delta, delta) == 0;
                }
            }
            glow_geometry_ = same ? previous_glow : std::move(glow);
        }
        std::sort(flow_triangles_.begin(), flow_triangles_.end(),
                  [](const FlowTriangle& a, const FlowTriangle& b) { return a.source < b.source; });
        if (ray_limit || (rays && rays->vertices.size() + falling_vertices.size() > kMaximumRayTriangles * 3))
            rays.reset();
        if (rays) {
            rays->opaque_vertices = std::uint32_t(rays->vertices.size());
            rays->masks[0].params[3] = float(rays->opaque_vertices / 3);
            rays->vertices.insert(rays->vertices.end(), falling_vertices.begin(), falling_vertices.end());
        }
        if (rays && !rays->vertices.empty()) {
            // Texture-cache churn is not a new static world. Exact comparison
            // keeps the submitted GPU scene alive without hash collisions or
            // accidentally reusing a different custom track at the same address.
            if (previous_rays && previous_rays->vertices.size() == rays->vertices.size() &&
                previous_rays->masks.size() == rays->masks.size() &&
                std::memcmp(previous_rays->vertices.data(), rays->vertices.data(),
                            rays->vertices.size() * sizeof(RayVertex)) == 0 &&
                std::memcmp(previous_rays->masks.data(), rays->masks.data(),
                            rays->masks.size() * sizeof(RayOpacityMask)) == 0)
                ray_geometry_ = previous_rays;
            else
                ray_geometry_ = std::move(rays);
        }
    }
};
} // namespace dkr::runtime::water::enhanced
