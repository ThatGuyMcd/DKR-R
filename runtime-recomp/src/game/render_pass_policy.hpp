#pragma once
#include <array>
#include <cstdint>

namespace dkr::runtime::render_pass {
// A bounded, allocation-free cache of visibility established by real image
// barriers in ONE command-list recording. Collisions only lose optimisations.
// Caller permits SHADER_READ -> SHADER_READ only, never GENERAL/depth/buffers.
class ReadBarrierCache {
    struct Entry { std::uintptr_t resource = 0; std::uint32_t stages = 0; };
    std::array<Entry, 64> entries_{};
    static std::size_t slot(std::uintptr_t key) { return ((key >> 4) ^ (key >> 10)) % 64; }
public:
    void reset() { entries_ = {}; }
    bool covers(std::uintptr_t key, std::uint32_t current_stages, std::uint32_t requested_stages) const {
        const auto& entry = entries_[slot(key)];
        return key != 0 && requested_stages != 0 && entry.resource == key && entry.stages == current_stages &&
            (entry.stages & requested_stages) == requested_stages;
    }
    void record(std::uintptr_t key, std::uint32_t stages) { entries_[slot(key)] = {key, stages}; }
    void invalidate(std::uintptr_t key) {
        auto& entry = entries_[slot(key)];
        if (entry.resource == key) entry = {};
    }
};
// Only remove a depth READ -> WRITE -> READ excursion when the very next
// effective command is known to request READ. No depth write/clear/compute,
// empty draw, unknown command or framebuffer change is allowed in this proof.
template <typename Draw>
bool continue_read_only(bool read_only, bool has_color, const Draw* first, unsigned decal_mode) {
    if (!read_only || !has_color || !first) return false;
    using Type = typename Draw::Type;
    if (first->type != Type::IndexedTriangles && first->type != Type::RawTriangles &&
        first->type != Type::RegularRect) return false;
    return !first->triangles.scissor.isEmpty() &&
        first->triangles.shaderDesc.otherMode.zMode() == decal_mode;
}

template <typename Framebuffer>
bool redundant_binding(const Framebuffer* requested, const Framebuffer* current, bool pass_active) {
    // Null remains an explicit flush/unbind; never conceal a different target.
    return requested && requested == current && pass_active;
}
}
