#pragma once
#include "shared/rt64_water_params.h"

namespace RT64 {
// DKR's model matrices already contain its camera MVP. RT64 applies a second
// projection (aspect/interpolation policy) to those clip-space vertices. Keep
// that final raster mapping separate from decoding the original model output.
inline void setWaterCameraProjection(interop::WaterDrawParams& draw, const hlslpp::float4x4& camera,
                                     const hlslpp::float4x4& rasterProjection) {
    draw.inverseWorldViewProjection = hlslpp::inverse(camera);
    draw.viewProjection = hlslpp::mul(camera, rasterProjection);
    draw.inverseViewProjection = hlslpp::inverse(draw.viewProjection);
    const auto eye = hlslpp::mul(hlslpp::float4(0, 0, 1, 0), draw.inverseWorldViewProjection);
    draw.camera.x = float(eye.x / eye.w);
    draw.camera.y = float(eye.y / eye.w);
    draw.camera.z = float(eye.z / eye.w);
}
} // namespace RT64
