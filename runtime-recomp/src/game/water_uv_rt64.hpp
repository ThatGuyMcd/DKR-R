#pragma once

#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"

namespace dkr::runtime::water {

inline bool set_culling(RT64::RSP& rsp, std::uint32_t requested, bool allow_equal_skip) {
    const auto current = rsp.geometryModeStack[rsp.geometryModeStackSize - 1] & rsp.cullBothMask;
    if (allow_equal_skip && current == requested) return true;
    // Marking an unchanged mode dirty still FLUSHES RT64's current draw. Keep
    // the row's first triangle as a boundary, but not every identical triangle.
    rsp.clearGeometryMode(rsp.cullBothMask);
    if (requested != 0U) rsp.setGeometryMode(requested);
    return false;
}

// Only a proven procedural-water draw may use this path. Read the CURRENT
// RT64 vertex, not a cache keyed by the 32 reusable DKR slots. modifyVertex(ST)
// also clears texture generation: equality of UV alone is not sufficient.
inline bool same_texcoord(const RT64::RSP& rsp, const RT64::DrawData& draw,
                          unsigned slot, std::int16_t s, std::int16_t t) {
    if (slot >= 32U || !rsp.used[slot]) return false;
    const auto index = rsp.indices[slot];
    const auto offset = static_cast<std::size_t>(index) * 2U;
    return index < draw.lookAtIndices.size() && offset + 1U < draw.tcFloats.size() &&
           draw.lookAtIndices[index] == 0U &&
           draw.tcFloats[offset] == static_cast<float>(s) / 32.0F &&
           draw.tcFloats[offset + 1U] == static_cast<float>(t) / 32.0F;
}

inline bool set_texcoord(RT64::RSP& rsp, const RT64::DrawData& draw,
                         unsigned slot, std::int16_t s, std::int16_t t,
                         bool allow_equal_skip) {
    if (allow_equal_skip && same_texcoord(rsp, draw, slot, s, t)) return true;
    const auto packed = (std::uint32_t(std::uint16_t(s)) << 16U) | std::uint16_t(t);
    rsp.modifyVertex(slot, G_MWO_POINT_ST, packed);
    return false;
}
}
