#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace dkr::runtime::surgical {
// Preserve the original FIRST cache occurrence, including invalid texture IDs.
// Raw pointers are deliberately not masked: the original comparison was exact.
class TextureBindingIndex {
    struct Binding {
        std::uint32_t pointer, ordinal;
        std::uint16_t id;
    };
    std::vector<Binding> entries_;

  public:
    template <class Memory, class Resolve>
    TextureBindingIndex(Memory memory, std::uint32_t cache, std::uint32_t count, Resolve resolve) {
        entries_.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto id = resolve(memory.u32(cache + i * 8));
            entries_.push_back({memory.u32(cache + i * 8 + 4), i, id ? std::uint16_t(*id) : std::uint16_t(65535)});
        }
        std::sort(entries_.begin(), entries_.end(), [](const Binding& a, const Binding& b) {
            return a.pointer < b.pointer || (a.pointer == b.pointer && a.ordinal < b.ordinal);
        });
    }
    std::uint16_t find(std::uint32_t pointer) const noexcept {
        const auto at = std::lower_bound(entries_.begin(), entries_.end(), pointer,
                                         [](const Binding& value, std::uint32_t key) { return value.pointer < key; });
        return at != entries_.end() && at->pointer == pointer ? at->id : 65535;
    }
};
} // namespace dkr::runtime::surgical
