#include "legacy_mod_dependencies.hpp"
#include "legacy_mod_geometry.hpp"
#include <algorithm>
#include <functional>

namespace dkr::mods {
namespace {
unsigned count16(View data, std::size_t offset) {
    const auto count = be16(data, offset);
    if (count > 32767)
        throw Error("Asset contains a negative array count.");
    return count;
}
std::size_t array_at(View data, std::size_t field, unsigned count, unsigned stride, unsigned alignment) {
    const auto offset = be32(data, field);
    if (!count)
        return offset; // Empty arrays are never dereferenced by the loader.
    if (offset % alignment || offset < 0x58)
        throw Error("Model array is misaligned or overlaps its file header.");
    slice(data, offset, std::size_t(count) * stride);
    return offset;
}
bool equivalent(unsigned section, View a, View b) {
    if (std::ranges::equal(a, b))
        return true;
    if (section == 21 || section == 27 || section == 29 || section == 32)
        return inflate_asset(a, 4 * MiB) == inflate_asset(b, 4 * MiB);
    return false;
}
} // namespace
std::vector<unsigned> inspect_sprite_textures(View data) {
    if (data.size() > 512)
        throw Error("Sprite exceeds the retail scratch allocation.");
    slice(data, 0, 14);
    const auto base = count16(data, 0), frames = count16(data, 2);
    if (!frames || frames > 499)
        throw Error("Sprite has an invalid frame count.");
    slice(data, 12, frames + 1);
    if (data[12] != 0)
        throw Error("Sprite frame offsets do not start at zero.");
    for (unsigned i = 0; i < frames; ++i)
        if (data[12 + i] > data[13 + i])
            throw Error("Sprite frame offsets are unordered.");
    const unsigned textures = data[12 + frames];
    if (textures > 32768 - base)
        throw Error("Sprite texture range exceeds the 2D namespace.");
    std::vector<unsigned> result;
    for (unsigned i = 0; i < textures; ++i)
        result.push_back(base + i);
    return result;
}
std::vector<unsigned> inspect_model_textures(View packed) {
    const auto decoded = inflate_asset(packed, 4 * MiB);
    View data(decoded);
    slice(data, 0, 0x58);
    if (packed.size() > decoded.size() + 0x80)
        throw Error("Model compressed input overlaps its allocation.");
    const auto size = be32(data, 0x2c);
    if (size < 0x58 || size > data.size() || data.size() - size > 15)
        throw Error("Model file size is inconsistent with its decoded data.");
    data = data.first(size);
    const auto nv = count16(data, 0x24), nt = count16(data, 0x26), nb = count16(data, 0x28);
    const auto textures = count16(data, 0x22);
    if (!nv || textures > 255)
        throw Error("Model has no vertices or too many materials.");
    const auto tex = array_at(data, 0, textures, 8, 4);
    const auto vertices = array_at(data, 4, nv, 10, 2);
    const auto tri = array_at(data, 8, nt, 16, 4), batch = array_at(data, 0x38, nb + 1, 12, 4);
    const auto attachment_count = count16(data, 0x18);
    const auto attachments = array_at(data, 0x14, attachment_count, 2, 2);
    std::size_t vertex_region_end = data.size();
    for (unsigned field : {0U, 8U, 0x14U, 0x1cU, 0x38U, 0x4cU}) {
        const auto next = be32(data, field);
        if (next > vertices)
            vertex_region_end = std::min(vertex_region_end, std::size_t(next));
    }
    for (unsigned i = 0; i < attachment_count; ++i) {
        const auto index = count16(data, attachments + 2 * i);
        // Exporters append attachment-only vertices after the render vertex
        // count. File-array ordering varies, so bound by the next array, not
        // by an assumption that triangles always follow vertices.
        const auto end = std::size_t(vertices) + (index + 1) * 10;
        if (index >= nv + attachment_count || end > vertex_region_end)
            throw Error("Model attachment indexes an absent vertex.");
        slice(data, vertices + index * 10, 10);
    }
    const auto spheres = count16(data, 0x20);
    if (spheres % 2)
        throw Error("Collision sphere data must contain vertex/radius pairs.");
    const auto sphere_data = array_at(data, 0x1c, spheres, 2, 2);
    for (unsigned i = 0; i < spheres; i += 2)
        if (count16(data, sphere_data + 2 * i) >= nv)
            throw Error("Collision sphere indexes an absent vertex.");
    const auto animated = count16(data, 0x4a);
    // The on-disk mapping is s16, despite an s32 pointer in the decomp's
    // in-memory struct. buildModel.cpp writes one s16 per render vertex.
    const auto indices = array_at(data, 0x4c, animated ? nv : 0, 2, 2);
    for (unsigned i = 0; animated && i < nv; ++i) {
        const auto index = be16(data, indices + 2 * i);
        if (index != 0xffff && index >= animated)
            throw Error("Model animation index exceeds its animated vertices.");
    }
    if (count16(data, batch + 2) != 0 || count16(data, batch + 4) != 0 || count16(data, batch + nb * 12 + 2) != nv ||
        count16(data, batch + nb * 12 + 4) != nt)
        throw Error("Model batch sentinel does not cover its arrays.");
    for (unsigned b = 0; b < nb; ++b) {
        const auto entry = batch + b * 12;
        const auto v = count16(data, entry + 2), ve = count16(data, entry + 14);
        const auto t = count16(data, entry + 4), te = count16(data, entry + 16);
        if (v > ve || ve > nv || t > te || te > nt || (data[entry] != 255 && data[entry] >= textures))
            throw Error("Model batch references an absent material/vertex/triangle.");
        for (unsigned n = t; n < te; ++n)
            for (unsigned c = 1; c <= 3; ++c)
                if (data[tri + 16 * n + c] >= ve - v)
                    throw Error("Model triangle exceeds its vertex batch.");
    }
    std::vector<unsigned> result;
    for (unsigned i = 0; i < textures; ++i) {
        const auto id = be32(data, tex + 8 * i);
        if (id >= 32768)
            throw Error("Model texture exceeds the 3D namespace.");
        result.push_back(id);
    }
    return result;
}
void validate_texture_record(View packed) {
    slice(packed, 0, 32);
    const auto frames = be16(packed, 0x12) >> 8;
    if (!frames)
        throw Error("Texture has no frames.");
    Bytes inflated;
    View data = packed;
    if (packed[0x1d]) {
        inflated = inflate_asset(packed.subspan(32), 4 * MiB);
        if (packed.size() > ((inflated.size() + 32) & ~std::size_t(15)))
            throw Error("Texture compressed input overlaps its allocation.");
        data = inflated;
    }
    constexpr unsigned bits_per_pixel[] = {32, 16, 8, 4, 16, 8, 4};
    std::size_t offset = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const auto header = slice(data, offset, 32);
        const auto format = header[2] & 15;
        if ((header[2] >> 4) > 3)
            throw Error("Texture selects an unknown material render mode.");
        // The accepted Patch Pipeline already fixes retail CI display-list
        // allocation. Palette bank routing/ownership is a separate concern:
        // do not enable palette-bearing content before that adapter exists.
        if (format >= 7)
            throw Error("Colour-indexed or unknown texture format needs a palette adapter.");
        if (!header[0] || !header[1])
            throw Error("Texture dimensions cannot be zero.");
        const std::size_t pixels = (std::size_t(header[0]) * header[1] * bits_per_pixel[format] + 7) / 8;
        const auto stride = count16(header, 0x16);
        // Static 2D textures commonly store a zero stride: it is advanced
        // after the final frame, never used to address another frame.
        slice(data, offset + 32, pixels);
        if (frames == 1 && stride == 0)
            continue;
        if (stride < 32 + pixels)
            throw Error("Texture frame stride cannot hold its image.");
        // A last-frame stride may include exporter alignment beyond the
        // stored payload. No next frame is read there; validate pixels above.
        if (frame + 1 < frames) {
            slice(data, offset, stride);
            offset += stride;
        }
    }
}
DependencyReport inspect_track_dependencies(const AssetImage& base, const AssetImage& target, const Root& root) {
    DependencyReport report;
    std::set<std::pair<unsigned, unsigned>> seen;
    std::array<std::vector<View>, 50> before, after;
    for (unsigned s = 0; s < 50; ++s) {
        before[s] = base.records(s);
        after[s] = target.records(s);
    }
    auto record = [&](unsigned s, unsigned id) -> View {
        if (s >= 50 || id >= after[s].size())
            throw Error("A referenced asset is missing from the patch.");
        return after[s][id];
    };
    std::function<void(unsigned, unsigned)> visit;
    visit = [&](unsigned section, unsigned id) {
        if (!seen.emplace(section, id).second)
            return;
        if (seen.size() > 8192)
            throw Error("Static dependency graph exceeds its bounded capacity.");
        const auto bytes = record(section, id);
        const bool changed = id >= before[section].size() || !equivalent(section, before[section][id], bytes);
        report.records.push_back({section, id, sha256(bytes), changed});
        if (section == 2 || section == 4) {
            validate_texture_record(bytes);
            return;
        }
        if (section == 12) {
            for (auto texture : inspect_sprite_textures(bytes))
                visit(4, texture);
        } else if (section == 29) {
            for (auto texture : inspect_model_textures(bytes))
                visit(2, texture);
            const auto table = target.sections[30];
            const auto first = count16(table, 2 * id), end = count16(table, 2 * id + 2);
            if (end < first || end - first > 128)
                throw Error("Model animation range is invalid.");
            for (unsigned anim = first; anim < end; ++anim)
                visit(32, anim);
        } else if (section == 32) {
            const auto decoded = inflate_asset(bytes, 4 * MiB);
            slice(decoded, 0, 4);
            if (bytes.size() > decoded.size() + 0x80 || be32(decoded, 0) > 32767)
                throw Error("Animation allocation or frame count is invalid.");
            // The model-specific compact animation stream still needs its
            // own semantic validator before runtime certification.
        } else if (section == 34) {
            slice(bytes, 0, 0x78);
            const auto count = bytes[0x55], type = bytes[0x53];
            if (count > 127 || type > 4)
                throw Error("Object header model type/count is invalid.");
            const auto offset = be32(bytes, 0x10);
            if (offset % 4 || (count && offset < 0x78))
                throw Error("Object model ID array is invalid.");
            slice(bytes, offset, 4 * count);
            report.behaviours.insert(bytes[0x54]);
            for (unsigned i = 0; i < count; ++i) {
                const auto model = be32(bytes, offset + i * 4);
                if (model == 0xffffffff)
                    continue;
                if (type == 0)
                    visit(29, model);
                else if (type == 4)
                    visit((model & 0x8000) ? 2 : 4, model & 0x7fff);
                else
                    visit(12, model);
            }
        } else if (section == 21) {
            const auto decoded = inflate_asset(bytes, 0x3000);
            const auto end = 16 + std::size_t(be32(decoded, 0));
            slice(decoded, 16, end - 16);
            for (std::size_t pos = 16; pos < end;) {
                slice(decoded, pos, 8);
                const auto size = decoded[pos + 1] & 0x7f;
                if (size < 8 || size > end - pos || ++report.placed_objects > 512)
                    throw Error("Combined object maps exceed their entry or allocation bounds.");
                const unsigned object = decoded[pos] | ((decoded[pos + 1] & 0x80) << 1);
                visit(34, count16(target.sections[35], object * 2));
                pos += size;
            }
        } else if (section == 27) {
            const auto decoded = inflate_asset(bytes, 0x82a00);
            const auto geometry = validate_geometry(decoded, bytes.size());
            for (auto texture : geometry.textures)
                visit(2, texture);
        }
    };
    try {
        visit(23, root.carrier);
        visit(27, root.geometry);
        if (root.object_map != 65535)
            visit(21, root.object_map);
        if (root.collectables != 65535)
            visit(21, root.collectables);
    } catch (const Error& error) {
        report.blockers.push_back(error.what());
    }
    std::sort(report.records.begin(), report.records.end(),
              [](const auto& a, const auto& b) { return std::pair{a.section, a.id} < std::pair{b.section, b.id}; });
    return report;
}
} // namespace dkr::mods
