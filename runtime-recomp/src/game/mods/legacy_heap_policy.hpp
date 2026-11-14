#pragma once
#include <cstddef>
#include <cstdint>
namespace dkr::mods {
// The runtime/renderer already expose 8 MiB, but retail's main pool stops at
// 4 MiB. Only an admitted offline legacy session may use the remaining RAM.
constexpr std::uint32_t legacy_heap_end(std::uint32_t retail_end, std::size_t rdram_bytes,
                                        bool offline_custom_session) {
    return offline_custom_session && retail_end == 0x80400000U && rdram_bytes >= 0x800000U ? 0x80800000U : retail_end;
}
} // namespace dkr::mods
