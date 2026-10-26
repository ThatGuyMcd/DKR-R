#pragma once
#include "presentation_identity.hpp"
#include "hle/rt64_rsp.h"

namespace dkr::runtime::water {

// A vertex load is the immutable unit of DKR's authored static flow geometry.
// Preserve all camera/aspect/component rules, changing only its pairing key.
// Do not re-emit geometry, change guest memory or enable optical effects.
class FlowVertexScope {
    RT64::RSP& rsp_;
    RT64::TransformGroup previous_{};
    bool active_ = false;

  public:
    FlowVertexScope(RT64::RSP& rsp, std::uint32_t source, std::uint32_t count, std::uint32_t destination, bool enabled,
                    bool explicit_uv = false)
        : rsp_(rsp) {
        if (!enabled || rsp.extended.modelMatrixIdStackSize < 1)
            return;
        auto& group = rsp.extended.modelMatrixIdStack[rsp.extended.modelMatrixIdStackSize - 1];
        if (group.matrixId == G_EX_ID_IGNORE || group.matrixId == G_EX_ID_AUTO)
            return;
        previous_ = group;
        group.matrixId =
            presentation::normalise_identity(group.matrixId ^ 0x464C4F57U ^ presentation::mix_identity(source) ^
                                             presentation::mix_identity((count << 8U) | destination));
        group.ordering = G_EX_ORDER_LINEAR;
        if (explicit_uv)
            group.texcoordInterpolation = G_EX_COMPONENT_SKIP;
        rsp.extended.modelMatrixIdStackChanged = true;
        rsp.modelViewProjChanged = true;
        active_ = true;
    }
    FlowVertexScope(const FlowVertexScope&) = delete;
    FlowVertexScope& operator=(const FlowVertexScope&) = delete;
    bool active() const {
        return active_;
    }
    void restore() {
        if (!active_)
            return;
        rsp_.extended.modelMatrixIdStack[rsp_.extended.modelMatrixIdStackSize - 1] = previous_;
        rsp_.extended.modelMatrixIdStackChanged = true;
        // The next ordinary vertex load must not be appended to the flow's
        // transform range after its ID has been restored.
        rsp_.modelViewProjChanged = true;
        active_ = false;
    }
    ~FlowVertexScope() {
        restore();
    }
};
} // namespace dkr::runtime::water
