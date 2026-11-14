#pragma once
#include "hle/rt64_rsp.h"

namespace dkr::runtime::hud {
inline void push_hud_projection(RT64::RSP& rsp) {
    // Combined MVPs are MODEL matrices in F3DDKR. Only the independent
    // PROJECTION group's aspectMode is read by RT64::ProjectionProcessor.
    rsp.matrixId(G_EX_ID_IGNORE, true, true, false, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                 G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                 G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_ASPECT_ADJUST, G_EX_EDIT_NONE, false,
                 false);
    rsp.projectionMatrixChanged = rsp.modelViewProjChanged = true;
}
inline void pop_hud_projection(RT64::RSP& rsp) {
    rsp.popMatrixId(1, true);
    rsp.projectionMatrixChanged = rsp.modelViewProjChanged = true;
}
} // namespace dkr::runtime::hud
