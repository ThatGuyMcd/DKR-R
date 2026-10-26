#pragma once
#include "plume_render_interface.h"
#ifdef _WIN32
#include "plume_d3d12.h"
#endif
#ifndef __APPLE__
#include "plume_vulkan.h"
#endif

namespace RT64 {
// A backend may return an allocated wrapper even when native PSO creation
// failed. Never submit that wrapper to SetPipelineState/vkCmdBindPipeline.
// Keep this adapter in the authored patch pipeline, not in Plume/RT64.
inline bool waterGraphicsPipelineValid(const plume::RenderPipeline* pipeline) {
    if (!pipeline)
        return false;
#ifdef _WIN32
    if (const auto* d3d = dynamic_cast<const plume::D3D12GraphicsPipeline*>(pipeline))
        return d3d->d3d != nullptr;
#endif
#ifndef __APPLE__
    if (const auto* vk = dynamic_cast<const plume::VulkanGraphicsPipeline*>(pipeline))
        return vk->vk != VK_NULL_HANDLE;
#endif
    return true;
}
} // namespace RT64
