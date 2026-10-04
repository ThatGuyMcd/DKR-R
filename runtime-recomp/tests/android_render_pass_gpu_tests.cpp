#include "plume_vulkan.h"
#include "render_metrics.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main() {
    using namespace plume;
    using namespace dkr::runtime;
    CHECK(performance_trace::enabled());
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if PLUME_SDL_VULKAN_ENABLED
    VulkanInterface renderer(nullptr);
#else
    VulkanInterface renderer;
#endif
    auto device = renderer.createDevice("");
    CHECK(device);
    std::puts("Vulkan device created");
    auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
    auto list = queue->createCommandList();
    auto fence = device->createCommandFence();
    auto timing = device->createQueryPool(3);
    CHECK(timing && timing->getCount() == 3);
    constexpr unsigned W = 128, H = 64;
    auto color = device->createTexture(RenderTextureDesc::ColorTarget(W, H, RenderFormat::R8G8B8A8_UNORM));
    auto other = device->createTexture(RenderTextureDesc::ColorTarget(W, H, RenderFormat::R8G8B8A8_UNORM));
    auto depth = device->createTexture(RenderTextureDesc::DepthTarget(W, H, RenderFormat::D32_FLOAT));
    const RenderTexture* colors[] = {color.get()};
    const RenderTexture* others[] = {other.get()};
    auto writable = device->createFramebuffer(RenderFramebufferDesc(colors, 1, depth.get()));
    auto readonly = device->createFramebuffer(RenderFramebufferDesc(colors, 1, depth.get(), true));
    auto alternate = device->createFramebuffer(RenderFramebufferDesc(others, 1));
    auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(W * H * 4));
    auto depthback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(W * H * 4));
    auto* actual = static_cast<VulkanCommandList*>(list.get());
    std::puts("Color/depth framebuffers created");
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        list->begin();
        list->resetQueryPool(timing.get(), 0, 3);
        list->writeTimestamp(timing.get(), 0);
        list->barriers(RenderBarrierStage::GRAPHICS, {
            {color.get(), RenderTextureLayout::COLOR_WRITE},
            {other.get(), RenderTextureLayout::COLOR_WRITE},
            {depth.get(), RenderTextureLayout::DEPTH_WRITE}});
        list->setFramebuffer(alternate.get());
        list->clearColor(0, RenderColor(1, 1, 0, 1));
        list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(other.get(), RenderTextureLayout::SHADER_READ));
        list->setFramebuffer(writable.get());
        std::puts("Writable target bound");
        list->clearColor(0, RenderColor(1, 0, 0, 1));
        std::puts("Color cleared");
        list->clearDepth(true, 0.25f);
        std::puts("Depth cleared");
        list->writeTimestamp(timing.get(), 1);
        const auto pass = actual->activeRenderPass;
        CHECK(pass != VK_NULL_HANDLE);
        for (unsigned bind = 0; bind < 1000; ++bind) {
            list->setFramebuffer(writable.get());
            CHECK(actual->activeRenderPass == pass);
            list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(other.get(), RenderTextureLayout::SHADER_READ));
            CHECK(actual->activeRenderPass == pass);
        }
        // A new consumer stage needs a real dependency, even in the same layout.
        list->barriers(RenderBarrierStage::COMPUTE, RenderTextureBarrier(other.get(), RenderTextureLayout::SHADER_READ));
        CHECK(actual->activeRenderPass == VK_NULL_HANDLE);
        list->clearColor(0, RenderColor(1, 0, 0, 1));
        // Mixed calls retain buffer barriers even when the image part is redundant.
        RenderBufferBarrier bufferBarrier(readback.get(), RenderBufferAccess::WRITE);
        RenderTextureBarrier readBarrier(other.get(), RenderTextureLayout::SHADER_READ);
        list->barriers(RenderBarrierStage::COMPUTE, &bufferBarrier, 1, &readBarrier, 1);
        CHECK(actual->activeRenderPass == VK_NULL_HANDLE);
        RenderRect left(0, 0, W / 2, H);
        std::puts("Repeated bindings retained");
        list->clearColor(0, RenderColor(0, 1, 0, 1), &left, 1);
        // Real layout changes still end the pass and perform barriers.
        list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(depth.get(), RenderTextureLayout::DEPTH_READ));
        CHECK(actual->activeRenderPass == VK_NULL_HANDLE);
        list->setFramebuffer(readonly.get());
        std::puts("Read-only target bound");
        RenderRect topRight(W / 2, 0, W, H / 2);
        list->clearColor(0, RenderColor(0, 0, 1, 1), &topRight, 1);
        std::puts("Read-only color cleared");
        const auto readPass = actual->activeRenderPass;
        list->setFramebuffer(readonly.get());
        CHECK(actual->activeRenderPass == readPass);
        // Unbind and a different target must still flush the old pass.
        list->setFramebuffer(nullptr);
        std::puts("Explicitly unbound");
        CHECK(actual->activeRenderPass == VK_NULL_HANDLE && actual->targetFramebuffer == nullptr);
        list->setFramebuffer(readonly.get());
        list->clearColor(0, RenderColor(0, 0, 1, 1), &topRight, 1);
        list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(other.get(), RenderTextureLayout::COLOR_WRITE));
        list->setFramebuffer(alternate.get());
        CHECK(actual->activeRenderPass == VK_NULL_HANDLE);
        list->clearColor(0, RenderColor(1, 1, 0, 1));
        std::puts("Alternate target cleared");
        list->barriers(RenderBarrierStage::COPY, {
            {color.get(), RenderTextureLayout::COPY_SOURCE}, {depth.get(), RenderTextureLayout::COPY_SOURCE}});
        std::puts("Copy texture barriers recorded");
        list->barriers(RenderBarrierStage::COPY, {
            RenderBufferBarrier(readback.get(), RenderBufferAccess::WRITE),
            RenderBufferBarrier(depthback.get(), RenderBufferAccess::WRITE)});
        std::puts("Copy buffer barriers recorded");
        // Plume does not implement image -> buffer copyTextureRegion. Use the
        // Vulkan test harness for readback, not that unsupported production API.
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {W, H, 1};
        vkCmdCopyImageToBuffer(actual->vk, static_cast<VulkanTexture*>(color.get())->vk,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, static_cast<VulkanBuffer*>(readback.get())->vk, 1, &copy);
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCmdCopyImageToBuffer(actual->vk, static_cast<VulkanTexture*>(depth.get())->vk,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, static_cast<VulkanBuffer*>(depthback.get())->vk, 1, &copy);
        list->writeTimestamp(timing.get(), 2);
        list->end();
        std::printf("Submitting iteration %u\n", iteration);
        queue->executeCommandLists(list.get(), fence.get());
        queue->waitForCommandFence(fence.get());
        std::puts("GPU fence completed");
        timing->queryResults();
        const auto* timestamps = timing->getResults();
        CHECK(render_metrics::valid_gpu_times(timestamps[0], timestamps[1], timestamps[2]));
        const RenderRange all{0, W * H * 4}, none{0, 0};
        auto* rgba = static_cast<const unsigned char*>(readback->map(0, &all));
        auto* z = static_cast<const float*>(depthback->map(0, &all));
        CHECK(rgba && z);
        for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x) {
            const auto p = (y * W + x) * 4;
            CHECK(rgba[p] == ((x >= W / 2 && y >= H / 2) ? 255 : 0));
            CHECK(rgba[p + 1] == (x < W / 2 ? 255 : 0));
            CHECK(rgba[p + 2] == ((x >= W / 2 && y < H / 2) ? 255 : 0));
            CHECK(rgba[p + 3] == 255 && z[y * W + x] == 0.25f);
        }
        readback->unmap(0, &none); depthback->unmap(0, &none);
    }
    CHECK(render_metrics::counts[static_cast<std::size_t>(render_metrics::Event::RedundantBind)].load() == 3003);
    CHECK(render_metrics::counts[static_cast<std::size_t>(render_metrics::Event::ReadBarrierRemoved)].load() == 3003);
    CHECK(render_metrics::counts[static_cast<std::size_t>(render_metrics::Event::PassBreakRemoved)].load() == 3000);
    std::puts("PASS: actual staged Vulkan backend, 3003 redundant binds and read barriers removed, 3000 pass breaks avoided, exact color/depth readback; new stages, real barriers, null unbind and target changes retained");
}
