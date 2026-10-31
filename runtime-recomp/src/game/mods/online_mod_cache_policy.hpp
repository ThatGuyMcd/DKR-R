#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
namespace dkr::mods::online {
inline constexpr std::size_t DurablePayloadBatch = 1024U * 1024U;
// A single optional strong reference, not an accumulating library cache.
// Live lobbies always offer weak reuse without allocating a second collection.
inline std::uint64_t resource_cache_budget(std::uint64_t physical, std::uint64_t available, bool mobile) {
    constexpr std::uint64_t mib = 1024 * 1024;
    if (!physical || !available)
        return 0;
    return (std::min<std::uint64_t>)({physical / 16, available / 4, (mobile ? 256ULL : 768ULL) * mib});
}
} // namespace dkr::mods::online
