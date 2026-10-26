#include "WaterRayScene.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace RT64 {
namespace {
template <class T> std::unique_ptr<T> requireRay(std::unique_ptr<T> value) {
    if (!value)
        throw std::runtime_error("water ray resource unavailable");
    return value;
}
void uploadRay(RenderBuffer* buffer, const void* data, size_t size) {
    auto* mapped = buffer->map();
    if (!mapped)
        throw std::runtime_error("water ray upload mapping failed");
    std::memcpy(mapped, data, size);
    RenderRange range(0, size);
    buffer->unmap(0, &range);
}
} // namespace
bool WaterRayScene::prepare(RenderWorker* worker, std::shared_ptr<const Geometry> geometry) {
    enabled_ = false;
    if (!worker->device->getCapabilities().rayQuery || !geometry || geometry->vertices.empty())
        return false;
    if (geometry == geometry_ && top_ && set) {
        enabled_ = true;
        return true;
    }
    using namespace dkr::runtime::water::enhanced;
    const auto size = geometry->vertices.size() * sizeof(RayVertex);
    const auto maskSize = geometry->masks.size() * sizeof(RayOpacityMask);
    if (geometry->vertices.size() % 3 || geometry->vertices.size() / 3 > kMaximumRayTriangles ||
        size > kMaximumRayBytes || geometry->masks.empty() || geometry->masks.size() > 257 ||
        geometry->opaque_vertices > geometry->vertices.size() || geometry->opaque_vertices % 3)
        return false;
    // Allocate transactionally. A failed optional build retains SSR, without
    // invalidating resources used by an earlier submitted workload.
    auto vertexUpload = requireRay(worker->device->createBuffer(RenderBufferDesc::UploadBuffer(size)));
    uploadRay(vertexUpload.get(), geometry->vertices.data(), size);
    auto vertices = requireRay(worker->device->createBuffer(RenderBufferDesc::DefaultBuffer(
        size, RenderBufferFlag::STORAGE | RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT)));
    auto maskUpload = requireRay(worker->device->createBuffer(RenderBufferDesc::UploadBuffer(maskSize)));
    uploadRay(maskUpload.get(), geometry->masks.data(), maskSize);
    auto masks =
        requireRay(worker->device->createBuffer(RenderBufferDesc::DefaultBuffer(maskSize, RenderBufferFlag::STORAGE)));
    // Keep ordinary world geometry opaque/fast. Only liquid cutouts invoke the
    // alpha-aware candidate shader; never turn a waterfall rectangle into a wall.
    std::vector<RenderBottomLevelASMesh> meshes;
    const uint32_t opaque = geometry->opaque_vertices     ? geometry->opaque_vertices
                            : geometry->masks.size() == 1 ? uint32_t(geometry->vertices.size())
                                                          : 0;
    if (opaque)
        meshes.emplace_back(RenderBufferReference{}, vertices->at(0), RenderFormat::UNKNOWN,
                            RenderFormat::R32G32B32_FLOAT, 0, opaque, sizeof(RayVertex), true);
    if (opaque < geometry->vertices.size())
        meshes.emplace_back(RenderBufferReference{}, vertices->at(uint64_t(opaque) * sizeof(RayVertex)),
                            RenderFormat::UNKNOWN, RenderFormat::R32G32B32_FLOAT, 0,
                            uint32_t(geometry->vertices.size()) - opaque, sizeof(RayVertex), false);
    RenderBottomLevelASBuildInfo bottomInfo;
    worker->device->setBottomLevelASBuildInfo(bottomInfo, meshes.data(), uint32_t(meshes.size()), false, true);
    if (!bottomInfo.accelerationStructureSize || bottomInfo.accelerationStructureSize > kMaximumRayBytes)
        return false;
    auto bottomBuffer = requireRay(worker->device->createBuffer(
        RenderBufferDesc::AccelerationStructureBuffer(bottomInfo.accelerationStructureSize)));
    auto bottom = requireRay(worker->device->createAccelerationStructure(RenderAccelerationStructureDesc(
        RenderAccelerationStructureType::BOTTOM_LEVEL, bottomBuffer->at(0), bottomInfo.accelerationStructureSize)));
    RenderTopLevelASInstance instance(bottomBuffer->at(0), 0, 0xFF, 0, true, RenderAffineTransform());
    RenderTopLevelASBuildInfo topInfo;
    worker->device->setTopLevelASBuildInfo(topInfo, &instance, 1, false, true);
    const auto scratchSize = (std::max)(bottomInfo.scratchSize, topInfo.scratchSize);
    const auto resident = (size + maskSize) * 2 + bottomInfo.accelerationStructureSize +
                          topInfo.accelerationStructureSize + scratchSize + topInfo.instancesBufferData.size() * 2;
    if (!scratchSize || !topInfo.accelerationStructureSize || resident > kMaximumRayBytes)
        return false;
    auto topBuffer = requireRay(
        worker->device->createBuffer(RenderBufferDesc::AccelerationStructureBuffer(topInfo.accelerationStructureSize)));
    auto top = requireRay(worker->device->createAccelerationStructure(RenderAccelerationStructureDesc(
        RenderAccelerationStructureType::TOP_LEVEL, topBuffer->at(0), topInfo.accelerationStructureSize)));
    auto scratch = requireRay(worker->device->createBuffer(RenderBufferDesc::DefaultBuffer(
        scratchSize, RenderBufferFlag::ACCELERATION_STRUCTURE_SCRATCH | RenderBufferFlag::UNORDERED_ACCESS)));
    auto instanceUpload =
        requireRay(worker->device->createBuffer(RenderBufferDesc::UploadBuffer(topInfo.instancesBufferData.size())));
    uploadRay(instanceUpload.get(), topInfo.instancesBufferData.data(), topInfo.instancesBufferData.size());
    auto instances = requireRay(worker->device->createBuffer(RenderBufferDesc::DefaultBuffer(
        topInfo.instancesBufferData.size(), RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT)));
    auto descriptors = std::make_unique<WaterRaySet>(worker->device);
    if (!descriptors->get())
        throw std::runtime_error("water ray descriptor unavailable");
    descriptors->setAccelerationStructure(descriptors->world, top.get());
    descriptors->setBuffer(descriptors->vertices, vertices.get(), RenderBufferStructuredView(sizeof(RayVertex)));
    descriptors->setBuffer(descriptors->masks, masks.get(), RenderBufferStructuredView(sizeof(RayOpacityMask)));
    // AS/descriptors must retire before their backing buffers. The renderer
    // owns these objects under the same completed-workload fence as water.
    set.reset();
    top_.reset();
    bottom_.reset();
    geometry_ = std::move(geometry);
    vertices_ = std::move(vertices);
    instances_ = std::move(instances);
    masks_ = std::move(masks);
    maskUpload_ = std::move(maskUpload);
    vertexUpload_ = std::move(vertexUpload);
    instanceUpload_ = std::move(instanceUpload);
    bottomBuffer_ = std::move(bottomBuffer);
    topBuffer_ = std::move(topBuffer);
    scratch_ = std::move(scratch);
    bottom_ = std::move(bottom);
    top_ = std::move(top);
    set = std::move(descriptors);
    bottomInfo_ = std::move(bottomInfo);
    topInfo_ = std::move(topInfo);
    pending_ = true;
    enabled_ = true;
    return true;
}
void WaterRayScene::record(RenderWorker* worker) {
    if (!enabled_ || !pending_)
        return;
    // Upload heaps stay in their required GENERIC_READ state on D3D12.
    // Static geometry lives in device-local memory for subsequent queries.
    worker->commandList->barriers(RenderBarrierStage::COPY,
                                  RenderBufferBarrier(vertices_.get(), RenderBufferAccess::WRITE));
    worker->commandList->barriers(RenderBarrierStage::COPY,
                                  RenderBufferBarrier(instances_.get(), RenderBufferAccess::WRITE));
    worker->commandList->copyBufferRegion(vertices_->at(0), vertexUpload_->at(0),
                                          geometry_->vertices.size() *
                                              sizeof(dkr::runtime::water::enhanced::RayVertex));
    worker->commandList->barriers(RenderBarrierStage::COPY,
                                  RenderBufferBarrier(masks_.get(), RenderBufferAccess::WRITE));
    worker->commandList->copyBufferRegion(masks_->at(0), maskUpload_->at(0),
                                          geometry_->masks.size() *
                                              sizeof(dkr::runtime::water::enhanced::RayOpacityMask));
    worker->commandList->copyBufferRegion(instances_->at(0), instanceUpload_->at(0),
                                          topInfo_.instancesBufferData.size());
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,
                                  RenderBufferBarrier(vertices_.get(), RenderBufferAccess::READ));
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,
                                  RenderBufferBarrier(instances_.get(), RenderBufferAccess::READ));
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,
                                  RenderBufferBarrier(bottomBuffer_.get(), RenderBufferAccess::WRITE));
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,
                                  RenderBufferBarrier(scratch_.get(), RenderBufferAccess::WRITE));
    worker->commandList->buildBottomLevelAS(bottom_.get(), scratch_->at(0), bottomInfo_);
    // AS write -> AS read, plus scratch reuse, before the TLAS build.
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,
                                  RenderBufferBarrier(bottomBuffer_.get(), RenderBufferAccess::READ));
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,
                                  RenderBufferBarrier(scratch_.get(), RenderBufferAccess::WRITE));
    worker->commandList->barriers(RenderBarrierStage::COMPUTE,
                                  RenderBufferBarrier(topBuffer_.get(), RenderBufferAccess::WRITE));
    worker->commandList->buildTopLevelAS(top_.get(), scratch_->at(0), instances_->at(0), topInfo_);
    // Pixel shading consumes these in-game; the headless probe (and future
    // compute water passes) consumes them from compute. Make both readers
    // explicit so Vulkan does not rely on incidental driver ordering.
    const auto readers = RenderBarrierStage::GRAPHICS | RenderBarrierStage::COMPUTE;
    worker->commandList->barriers(readers, RenderBufferBarrier(topBuffer_.get(), RenderBufferAccess::READ));
    worker->commandList->barriers(readers, RenderBufferBarrier(vertices_.get(), RenderBufferAccess::READ));
    worker->commandList->barriers(readers, RenderBufferBarrier(masks_.get(), RenderBufferAccess::READ));
    pending_ = false;
}
} // namespace RT64
