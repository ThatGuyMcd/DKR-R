#pragma once
#include <cstdint>

namespace dkr::runtime::android_graphics {
struct MappedRange {
    std::uint64_t offset, size;
    bool valid;
};
// RenderRange is half-open, allocation-relative. Null means the entire buffer;
// an explicit empty range means no CPU access. Never clamp a malformed range.
constexpr MappedRange mapped_range(std::uint64_t allocation_size, bool supplied,
                                  std::uint64_t begin, std::uint64_t end) {
    if (!supplied) return {0, allocation_size, true};
    if (begin > end || end > allocation_size) return {0, 0, false};
    return {begin, end - begin, true};
}
}
