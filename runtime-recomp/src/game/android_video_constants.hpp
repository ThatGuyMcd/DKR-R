#pragma once
#include "shared/rt64_hlsl.h"
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::android_rotation {
struct VideoConstants {
    interop::float2 videoResolution;
    interop::float2 textureResolution;
    float gamma;
    std::uint32_t rotation;
};
static_assert(sizeof(VideoConstants) == 24);
static_assert(offsetof(VideoConstants, rotation) == 20);
} // namespace dkr::runtime::android_rotation
