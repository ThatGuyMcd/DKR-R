#include "legacy_mod_geometry.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <set>

namespace dkr::mods {
namespace {
constexpr std::size_t TrackHeap = 0x82a00;
unsigned nonnegative16(View bytes, std::size_t offset) {
    const auto value = be16(bytes, offset);
    if (value > 32767)
        throw Error("Geometry contains a negative count or offset.");
    return value;
}
std::size_t array_at(View bytes, std::size_t field, std::size_t count, std::size_t stride, unsigned alignment) {
    const std::size_t offset = be32(bytes, field);
    if (offset % alignment || count > bytes.size() / stride)
        throw Error("Geometry array is misaligned or oversized.");
    if (count && offset < 0x4c)
        throw Error("Geometry array overlaps its header.");
    slice(bytes, offset, count * stride);
    return offset;
}
std::size_t align16(std::size_t size) {
    return (size + 15) & ~std::size_t(15);
}
} // namespace
GeometrySummary validate_geometry(View decoded, std::size_t compressed_size) {
    slice(decoded, 0, 0x4c);
    // generate_track inflates inside a fixed heap with the compressed input at
    // its aligned tail, then appends collision data before checking overflow.
    // Reject overlap up front; never depend on its late diagnostic.
    if (decoded.size() > TrackHeap || compressed_size > TrackHeap ||
        decoded.size() > ((TrackHeap - compressed_size) & ~std::size_t(15)))
        throw Error("Track data cannot be inflated safely inside the retail track heap.");
    const auto model_size = be32(decoded, 0x48);
    if (model_size < 0x4c || model_size > TrackHeap ||
        (model_size < decoded.size() && decoded.size() - model_size > 15))
        throw Error("Geometry modelSize does not reserve the full decoded file.");
    // Some legacy exporters append alignment bytes beyond modelSize. They
    // are safe only when EVERY file-backed array is inside modelSize; never
    // accept an array in the region the loader overwrites with collision data.
    if (model_size < decoded.size())
        decoded = decoded.first(model_size);
    GeometrySummary out;
    out.segments = nonnegative16(decoded, 0x1a);
    const auto texture_count = nonnegative16(decoded, 0x18);
    if (!out.segments || out.segments > 128 || texture_count > 255)
        throw Error("Geometry exceeds the segment or material index capacity.");
    // The decomp only tests field 0x1E > 0 to enable an all-texture update;
    // retail files also use negative values. It is not an array bound.
    const auto textures = array_at(decoded, 0, texture_count, 8, 4);
    const auto segments = array_at(decoded, 4, out.segments, 0x44, 4);
    const auto bounds = array_at(decoded, 8, out.segments, 12, 2);
    for (unsigned i = 0; i < texture_count; ++i) {
        const auto id = be32(decoded, textures + 8 * i);
        if (id >= 0x8000 || !decoded[textures + 8 * i + 4] || !decoded[textures + 8 * i + 5])
            throw Error("Invalid track texture reference or dimensions.");
        out.textures.push_back(id);
    }
    for (unsigned field : {0x28U, 0x2cU}) {
        if (!std::isfinite(std::bit_cast<float>(be32(decoded, field))))
            throw Error("Geometry has a non-finite minimap scale.");
    }
    const auto bitfields = be32(decoded, 0x10);
    out.constructed_bytes = model_size;
    for (unsigned n = 0; n < out.segments; ++n) {
        const auto seg = segments + n * 0x44;
        const auto nv = nonnegative16(decoded, seg + 0x1c), nt = nonnegative16(decoded, seg + 0x1e);
        const auto nb = nonnegative16(decoded, seg + 0x20);
        if (decoded[seg + 0x40] > nb)
            throw Error("Opaque batch count exceeds total batches.");
        const auto verts = array_at(decoded, seg, nv, 10, 2);
        const auto tris = array_at(decoded, seg + 4, nt, 16, 4);
        const auto batches = array_at(decoded, seg + 0xc, nb + 1, 12, 4);
        const auto facets = array_at(decoded, seg + 0x14, nt, 8, 2);
        (void)verts;
        slice(decoded, std::size_t(bitfields) + nonnegative16(decoded, seg + 0x28), (out.segments + 7) / 8);
        for (unsigned axis = 0; axis < 3; ++axis) {
            const auto lo = static_cast<std::int16_t>(be16(decoded, bounds + n * 12 + axis * 2));
            const auto hi = static_cast<std::int16_t>(be16(decoded, bounds + n * 12 + 6 + axis * 2));
            if (lo > hi)
                throw Error("Geometry segment bounding box is inverted.");
        }
        if (nonnegative16(decoded, batches + 2) != 0 || nonnegative16(decoded, batches + 4) != 0 ||
            nonnegative16(decoded, batches + nb * 12 + 2) != nv || nonnegative16(decoded, batches + nb * 12 + 4) != nt)
            throw Error("Geometry batch sentinel does not cover its vertex/triangle arrays.");
        unsigned plane_count = 0, special_batches = 0;
        std::vector<bool> collidable(nt, false);
        for (unsigned b = 0; b < nb; ++b) {
            const auto entry = batches + b * 12;
            const auto v = nonnegative16(decoded, entry + 2), vend = nonnegative16(decoded, entry + 14);
            const auto t = nonnegative16(decoded, entry + 4), tend = nonnegative16(decoded, entry + 16);
            const auto tex = decoded[entry];
            if (v > vend || vend > nv || t > tend || tend > nt || (tex != 255 && tex >= texture_count))
                throw Error("Geometry batch points outside its material/vertex/triangle arrays.");
            const auto flags = be32(decoded, entry + 8);
            if (flags & 0x2000) {
                if (v == nv)
                    throw Error("Special batch has no vertex to inspect.");
                ++special_batches;
            }
            for (unsigned tri = t; tri < tend; ++tri) {
                for (unsigned corner = 1; corner <= 3; ++corner)
                    if (decoded[tris + tri * 16 + corner] >= vend - v)
                        throw Error("Geometry triangle indexes outside its vertex batch.");
                if (!(decoded[tris + tri * 16] & 0x80)) {
                    ++plane_count;
                    collidable[tri] = !(flags & 0x200);
                }
            }
        }
        // Mirror the index bookkeeping in track_init_collision (not its float
        // math) to bound exactly how many edge planes the loader will append.
        std::vector<std::array<unsigned, 3>> edges(nt);
        for (unsigned t = 0; t < nt; ++t)
            for (unsigned e = 0; e < 3; ++e)
                edges[t][e] = be16(decoded, facets + t * 8 + 2 + e * 2);
        const auto base_planes = plane_count;
        for (unsigned t = 0; t < nt; ++t)
            if (collidable[t]) {
                const auto base = be16(decoded, facets + t * 8);
                if (base >= base_planes)
                    throw Error("Collision facet references an absent base plane.");
                for (unsigned e = 0; e < 3; ++e) {
                    const auto next = edges[t][e];
                    if (next < base_planes) {
                        if (next >= nt)
                            throw Error("Collision edge references an absent facet.");
                        if (next != base)
                            for (auto& reciprocal : edges[next])
                                if (reciprocal == base)
                                    reciprocal = plane_count | 0x8000;
                        edges[t][e] = plane_count++;
                    } else if ((next & 0x7fff) >= plane_count) {
                        throw Error("Collision edge references an unconstructed plane.");
                    }
                }
            }
        if (plane_count >= 0x8000)
            throw Error("Collision plane indices exceed their packed range.");
        out.constructed_bytes = align16(out.constructed_bytes + nt * 2);
        out.constructed_bytes += plane_count * 16;
        out.constructed_bytes = align16(out.constructed_bytes + special_batches * 2);
        if (out.constructed_bytes > TrackHeap)
            throw Error("Constructed track collision data would overflow the retail track heap.");
        out.triangles += nt;
    }
    if (out.segments > 1) {
        const auto tree = be32(decoded, 0x14);
        std::set<unsigned> visited;
        std::set<unsigned> leaves;
        std::function<void(unsigned, unsigned, unsigned)> walk = [&](unsigned node, unsigned lo, unsigned hi) {
            if (visited.size() >= 127 || !visited.insert(node).second)
                throw Error("Geometry BSP tree contains a cycle, shared child or too many nodes.");
            const auto row = slice(decoded, std::size_t(tree) + node * 8, 8);
            const auto split = row[5];
            if (row[4] > 2)
                throw Error("Geometry BSP axis is invalid.");
            for (unsigned side = 0; side < 2; ++side) {
                const auto child = be16(row, side * 2);
                const auto a = side ? split : lo, b = side ? hi : split - 1;
                if (child == 0xffff) {
                    // Native traversal emits lo for a left leaf and hi for a
                    // right leaf. The split field is unused on that branch.
                    const auto segment = side ? hi : lo;
                    if (segment >= out.segments || !leaves.insert(segment).second)
                        throw Error("Geometry BSP emits an absent or duplicate segment.");
                } else {
                    if (child > 32767)
                        throw Error("Geometry BSP child has a negative index.");
                    if (a >= out.segments || b >= out.segments || a > b)
                        throw Error("Geometry BSP child range is invalid.");
                    walk(child, a, b);
                }
            }
        };
        walk(0, 0, out.segments - 1);
        if (leaves.size() != out.segments)
            throw Error("Geometry BSP does not cover every segment exactly once.");
    }
    return out;
}
} // namespace dkr::mods
