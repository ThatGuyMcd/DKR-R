#pragma once

#include <cstddef>

namespace dkr::runtime::launcher {

// Exact equality is deliberate: glyphs, texture edges and antialiased line
// atlas coordinates must never be mistaken for the atlas's opaque white texel.
template <class Vertex, class Index, class Point>
bool is_solid_triangle(const Vertex* vertices, std::size_t vertex_count, const Index* indices,
                       std::size_t vertex_offset, Point white_uv) {
    for (int i = 0; i < 3; ++i) {
        const std::size_t index = static_cast<std::size_t>(indices[i]);
        if (vertex_offset >= vertex_count || index >= vertex_count - vertex_offset)
            return false;
        const auto uv = vertices[vertex_offset + index].uv;
        if (uv.x != white_uv.x || uv.y != white_uv.y)
            return false;
    }
    return true;
}

} // namespace dkr::runtime::launcher
