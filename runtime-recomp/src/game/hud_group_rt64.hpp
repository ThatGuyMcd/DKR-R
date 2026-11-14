#pragma once
#include "hud_group_layout.hpp"
#include "shared/rt64_rsp_viewport.h"
#include "common/rt64_hlslpp.h"

namespace dkr::runtime::hud::groups {
inline hlslpp::float4 untransform_anchor(hlslpp::float4 anchor, const interop::RSPViewport& viewport,
                                         const Transform& t, float scale_limit = 1.5F) {
    if (!t.valid(scale_limit) || std::abs(float(viewport.scale.x)) < .001F || std::abs(float(viewport.scale.y)) < .001F)
        return anchor;
    const float dx = ((t.scale - 1) * float(viewport.translate.x) + t.x) / float(viewport.scale.x);
    const float dy = -((t.y_scale() - 1) * float(viewport.translate.y) + t.y) / float(viewport.scale.y);
    anchor.x = (anchor.x - anchor.w * dx) / t.scale;
    anchor.y = (anchor.y - anchor.w * dy) / t.y_scale();
    return anchor;
}
// DKR supplies complete MVP matrices through its model-matrix command; the
// bridge's view/projection stacks are identity for the one/two-player HUD.
// Apply the affine in clip space BEFORE RT64's aspect correction, not through
// viewport translation (which would multiply offsets by the host aspect).
inline hlslpp::float4x4 transform_mvp(const hlslpp::float4x4& m, const interop::RSPViewport& viewport,
                                      const Transform& t, float scale_limit = 1.5F) {
    if (!t.valid(scale_limit) || std::abs(static_cast<float>(viewport.scale.x)) < .001F ||
        std::abs(static_cast<float>(viewport.scale.y)) < .001F)
        return m;
    const float dx =
        ((t.scale - 1) * static_cast<float>(viewport.translate.x) + t.x) / static_cast<float>(viewport.scale.x);
    const float dy =
        -((t.y_scale() - 1) * static_cast<float>(viewport.translate.y) + t.y) / static_cast<float>(viewport.scale.y);
    auto out = m;
    for (int row = 0; row < 4; ++row) {
        out[row][0] = m[row][0] * t.scale + m[row][3] * dx;
        out[row][1] = m[row][1] * t.y_scale() + m[row][3] * dy;
    }
    return out; // z and w, handedness, UVs and native animation are unchanged
}
} // namespace dkr::runtime::hud::groups
