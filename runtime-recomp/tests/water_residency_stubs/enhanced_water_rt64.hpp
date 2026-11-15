#pragma once
#include "render/rt64_texture_cache.h"
#include <span>
#include <vector>
namespace dkr::runtime::revision_addresses {
// The residency header reads the selected table through current(). The stub's
// table deliberately differs from kUsV80 in CurrentMenuId, so the header takes
// its non-v80 branch.
struct Addresses {
    std::uint32_t NumberOfLoadedTextures;
    std::uint32_t CurrentMenuId;
};
inline constexpr Addresses kUsV80{0x100, 0x10C};
inline constexpr Addresses kStubTable{0x100, 0x108};
inline const Addresses& current() {
    return kStubTable;
}
} // namespace dkr::runtime::revision_addresses
namespace dkr::runtime::water::enhanced {
struct Memory {
    std::span<const std::uint8_t> data;
    explicit Memory(std::span<const std::uint8_t> bytes) : data(bytes) {}
    bool valid(std::uint32_t at, std::uint64_t bytes) const {
        at &= 0x1FFFFFFFU;
        return at <= data.size() && bytes <= data.size() - at;
    }
    std::uint8_t u8(std::uint32_t at) const {
        return data[(at & 0x1FFFFFFFU) ^ 3U];
    }
    std::uint16_t u16(std::uint32_t at) const {
        return (std::uint16_t(u8(at)) << 8) | u8(at + 1);
    }
    std::uint32_t u32(std::uint32_t at) const {
        return (std::uint32_t(u16(at)) << 16) | u16(at + 2);
    }
};
inline constexpr std::uint32_t level_model_symbol() {
    return 0x104;
}
struct Registry {
    std::vector<std::uint32_t> headers;
    const auto& liquid_headers() const {
        return headers;
    }
};
} // namespace dkr::runtime::water::enhanced
