#pragma once
#include "rt64_buffer_uploader.h"
#include "enhanced_water_ray_geometry.hpp"

namespace RT64 {
struct WaterRaySet : RenderDescriptorSetBase {
    uint32_t world, vertices, masks;
    explicit WaterRaySet(RenderDevice* device = nullptr) {
        builder.begin();
        world = builder.addAccelerationStructure(1);
        vertices = builder.addStructuredBuffer(2);
        masks = builder.addStructuredBuffer(3);
        builder.end();
        if (device)
            create(device);
    }
};
// This static world is shared by all split-screen cameras, built only when
// immutable level geometry changes. No guest draw/replay pass is run twice.
class WaterRayScene {
    using Geometry = dkr::runtime::water::enhanced::RayGeometry;
    std::shared_ptr<const Geometry> geometry_;
    std::unique_ptr<RenderBuffer> vertices_, instances_, vertexUpload_, instanceUpload_, bottomBuffer_, topBuffer_,
        scratch_;
    std::unique_ptr<RenderBuffer> masks_, maskUpload_;
    std::unique_ptr<RenderAccelerationStructure> bottom_, top_;
    RenderBottomLevelASBuildInfo bottomInfo_;
    RenderTopLevelASBuildInfo topInfo_;
    bool pending_ = false, enabled_ = false;

  public:
    std::unique_ptr<WaterRaySet> set;
    void resetFrame() {
        // The owner calls this only after the preceding workload fence.
        // A recorded static build no longer needs upload or scratch storage.
        if (!pending_) {
            vertexUpload_.reset();
            instanceUpload_.reset();
            maskUpload_.reset();
            scratch_.reset();
        }
        enabled_ = false;
    }
    bool prepare(RenderWorker* worker, std::shared_ptr<const Geometry> geometry);
    void record(RenderWorker* worker);
    bool enabled() const {
        return enabled_;
    }
};
} // namespace RT64
