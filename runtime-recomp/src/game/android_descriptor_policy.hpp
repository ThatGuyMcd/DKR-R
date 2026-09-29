#pragma once
#include <cstdint>

namespace dkr::runtime::android_graphics {
// A failed small variable-count allocation may be retried with a pool sized
// for its unchanged layout maximum. Shader/layout compatibility and the actual
// allocated descriptor count are unchanged. Never retry memory/device loss.
constexpr bool retry_full_pool(std::int32_t result, std::uint32_t requested,
                               std::uint32_t declared) {
    return result != 0 && result != -1 && result != -2 && result != -4 &&
           requested > 0 && requested < declared;
}
constexpr bool descriptor_features(bool partial, bool variable, bool runtime_array,
                                    bool sampled_update, bool nonuniform) {
    return partial && variable && runtime_array && sampled_update && nonuniform;
}
}
