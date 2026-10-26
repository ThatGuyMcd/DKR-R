#pragma once
#include <cstdlib>

namespace dkr::runtime::surgical {
inline bool option(const char* name, bool fallback = true) noexcept {
    const char* value = std::getenv(name);
    if (!value)
        return fallback;
    if (value[0] == '0' && value[1] == '\0')
        return false;
    if (value[0] == '1' && value[1] == '\0')
        return true;
    return fallback;
}
inline bool flow_reuse() noexcept {
    static const bool value = option("DKR_PERF_FLOW_REUSE");
    return value;
}
inline bool texture_index() noexcept {
    static const bool value = option("DKR_PERF_TEXTURE_INDEX");
    return value;
}
inline bool mip_scratch() noexcept {
    static const bool value = option("DKR_PERF_MIP_SCRATCH");
    return value;
}
inline bool monotonic_pacer() noexcept {
    static const bool value = option("DKR_PERF_MONOTONIC_PACER");
    return value;
}
inline bool profile() noexcept {
    static const bool value = option("DKR_SURGICAL_PROFILE", false);
    return value;
}
} // namespace dkr::runtime::surgical
