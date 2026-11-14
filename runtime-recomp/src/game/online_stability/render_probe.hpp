#pragma once
#include "hle/rt64_application.h"
#include "render/rt64_render_target.h"
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <stdexcept>
#ifdef _WIN32
#include "plume_d3d12.h"
#endif
#if defined(_WIN32) || defined(__linux__)
#include "plume_vulkan.h"
#endif

namespace dkr::online_stability {
// Intrusive opt-in developer readback. Not a timing measurement: the caller
// drains both queues before inspecting their target. No automatic capture.
inline bool render_probe_due(std::uint64_t epoch, unsigned frame) {
#if defined(_WIN32) || defined(__linux__)
    const char* path = std::getenv("DKR_ONLINE_RENDER_PROBE");
    const char* requested = std::getenv("DKR_ONLINE_RENDER_FRAME");
    const unsigned first = requested ? unsigned(std::strtoul(requested, nullptr, 10)) : 30;
    return path && *path && epoch == 92 && frame >= first && !std::filesystem::exists(std::filesystem::u8path(path));
#else
    (void)epoch;
    (void)frame;
    return false;
#endif
}
inline void render_probe(RT64::Application& app, unsigned address, std::span<const std::uint8_t> memory) {
#if defined(_WIN32) || defined(__linux__)
    using namespace RT64;
    if (app.chosenGraphicsAPI != UserConfiguration::GraphicsAPI::Vulkan
#ifdef _WIN32
        && app.chosenGraphicsAPI != UserConfiguration::GraphicsAPI::D3D12
#endif
    )
        return;
    const auto path = std::filesystem::u8path(std::getenv("DKR_ONLINE_RENDER_PROBE"));
    if (!path.is_absolute() || !std::filesystem::is_directory(path.parent_path()))
        return;
    auto& resources = *app.sharedQueueResources;
    std::scoped_lock lock(resources.managerMutex);
    const auto* fb = resources.framebufferManager.find(address & 0x7fffffU);
    if (!fb)
        return;
    auto& target = resources.renderTargetManager.get(
        RenderTargetKey(fb->addressStart, fb->width, fb->siz, Framebuffer::Type::Color), true);
    if (target.isEmpty() || target.width > 4096 || target.height > 4096)
        return;
    const unsigned pixel = target.usesHDR ? 8 : 4, pitch = (target.width * pixel + 255U) & ~255U;
    auto buffer = app.device->createBuffer(RenderBufferDesc::ReadbackBuffer(std::uint64_t(pitch) * target.height));
    if (!buffer)
        throw std::runtime_error("Online render probe readback allocation failed");
    auto& worker = *app.framebufferGraphicsWorker;
    worker.commandList->begin();
    target.resolveTarget(&worker, app.shaderLibrary.get());
    auto* source = target.getResolvedTexture();
    worker.commandList->barriers(RenderBarrierStage::COPY,
                                 RenderTextureBarrier(source, RenderTextureLayout::COPY_SOURCE));
    worker.commandList->barriers(RenderBarrierStage::COPY,
                                 RenderBufferBarrier(buffer.get(), RenderBufferAccess::WRITE));
#ifdef _WIN32
    if (app.chosenGraphicsAPI == UserConfiguration::GraphicsAPI::D3D12) {
        D3D12_TEXTURE_COPY_LOCATION destination{}, origin{};
        destination.pResource = static_cast<plume::D3D12Buffer*>(buffer.get())->d3d;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint.Footprint = {target.usesHDR ? DXGI_FORMAT_R16G16B16A16_UNORM
                                                                : DXGI_FORMAT_R8G8B8A8_UNORM,
                                                 target.width, target.height, 1, pitch};
        origin.pResource = static_cast<plume::D3D12Texture*>(source)->d3d;
        origin.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        static_cast<plume::D3D12CommandList*>(worker.commandList.get())
            ->d3d->CopyTextureRegion(&destination, 0, 0, 0, &origin, nullptr);
    } else
#endif
    {
        VkBufferImageCopy region{};
        region.bufferRowLength = pitch / pixel;
        region.bufferImageHeight = target.height;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {target.width, target.height, 1};
        vkCmdCopyImageToBuffer(static_cast<plume::VulkanCommandList*>(worker.commandList.get())->vk,
                               static_cast<plume::VulkanTexture*>(source)->vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               static_cast<plume::VulkanBuffer*>(buffer.get())->vk, 1, &region);
    }
    worker.commandList->barriers(RenderBarrierStage::GRAPHICS,
                                 RenderTextureBarrier(source, RenderTextureLayout::SHADER_READ));
    worker.commandList->end();
    worker.execute();
    worker.wait();
    const RenderRange range(0, std::uint64_t(pitch) * target.height);
    const auto* bytes = static_cast<const unsigned char*>(buffer->map(0, &range));
    if (!bytes)
        throw std::runtime_error("Online render probe readback mapping failed");
    std::ofstream image(path, std::ios::binary);
    image << "P6\n" << target.width << ' ' << target.height << "\n255\n";
    for (unsigned y = 0; y < target.height; ++y)
        for (unsigned x = 0; x < target.width; ++x)
            for (unsigned c = 0; c < 3; ++c) {
                const auto* value = bytes + y * pitch + x * pixel;
                const auto v = target.usesHDR ? reinterpret_cast<const std::uint16_t*>(value)[c] >> 8 : value[c];
                image.put(char(v));
            }
    const RenderRange none(0, 0);
    buffer->unmap(0, &none);
    // Private ROM-derived evidence beside the requested developer image.
    // Never part of a report upload or a distributable.
    std::ofstream ram(path.string() + ".ram", std::ios::binary);
    ram.write(reinterpret_cast<const char*>(memory.data()), memory.size());
    std::fprintf(stderr, "[online][render-probe] captured %ux%u actual GPU colour target; intrusive diagnostic\n",
                 target.width, target.height);
#else
    (void)app;
    (void)address;
    (void)memory;
#endif
}
} // namespace dkr::online_stability
