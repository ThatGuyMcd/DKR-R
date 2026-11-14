#include "overlay_images.hpp"
#include "overlay_completion.hpp"
#include "hle/rt64_application.h"
#include "imgui/imgui.h"
#include "plume_render_interface.h"
#include "plume_vulkan.h"
#if defined(_WIN32)
#include "plume_d3d12.h"
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

namespace dkr::store::overlay {
namespace {
using namespace plume;
struct Texture {
    std::unique_ptr<RenderTexture> texture;
    std::unique_ptr<RenderSampler> sampler;
    std::unique_ptr<RenderDescriptorSet> descriptor;
    std::uint64_t used = 0;
    void* id = nullptr;
    float aspect = 1.8F;
    VkDescriptorSet vulkan = VK_NULL_HANDLE;
};
std::map<std::string, Texture> textures;
RT64::Application* application = nullptr;
std::uint64_t frame = 0;
unsigned uploads = 0;
VkDevice descriptor_device = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
bool descriptors(RT64::Application& app) {
    if (descriptor_pool != VK_NULL_HANDLE)
        return true;
    descriptor_device = static_cast<VulkanDevice*>(app.device.get())->vk;
    // Match ImGui's single combined-sampler layout, using a separate bounded
    // pool so browsing cannot exhaust the Inspector's font/CRT descriptors.
    VkDescriptorSetLayoutBinding binding{};
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(descriptor_device, &layout, nullptr, &descriptor_layout) != VK_SUCCESS)
        return false;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 32;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    if (vkCreateDescriptorPool(descriptor_device, &pool, nullptr, &descriptor_pool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(descriptor_device, descriptor_layout, nullptr);
        descriptor_layout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}
void submitted(const ImDrawList*, const ImDrawCmd* command) {
    timing.submitted.store(std::uint64_t(reinterpret_cast<std::uintptr_t>(command->UserCallbackData)),
                           std::memory_order_release);
}
void erase(std::map<std::string, Texture>::iterator entry) {
    if (entry->second.vulkan != VK_NULL_HANDLE)
        vkFreeDescriptorSets(descriptor_device, descriptor_pool, 1, &entry->second.vulkan);
    textures.erase(entry);
}
struct Pixels {
    unsigned width, height;
    std::span<const unsigned char> rgba;
};
Texture upload(const Pixels& pixels) {
    Texture out;
    auto& app = *application;
    out.texture = app.device->createTexture(
        RenderTextureDesc::Texture2D(pixels.width, pixels.height, 1, RenderFormat::R8G8B8A8_UNORM));
    if (!out.texture)
        return {};
    const unsigned pitch = (pixels.width * 4 + 255U) & ~255U;
    auto buffer = app.device->createBuffer(RenderBufferDesc::UploadBuffer(std::uint64_t(pitch) * pixels.height));
    if (!buffer)
        return {};
    auto* data = static_cast<std::uint8_t*>(buffer->map());
    if (!data)
        return {};
    for (unsigned row = 0; row < pixels.height; ++row)
        std::memcpy(data + std::size_t(row) * pitch, pixels.rgba.data() + std::size_t(row) * pixels.width * 4,
                    pixels.width * 4);
    buffer->unmap();
    // Only this small new image's upload transaction is awaited. No gameplay
    // worker or rendering queue is drained, and cached images do not upload.
    auto queue = app.device->createCommandQueue(RenderCommandListType::DIRECT);
    auto command = queue ? queue->createCommandList() : nullptr;
    auto fence = app.device->createCommandFence();
    if (!command || !fence)
        return {};
    command->begin();
    command->barriers(RenderBarrierStage::COPY,
                      RenderTextureBarrier(out.texture.get(), RenderTextureLayout::COPY_DEST));
    command->copyTextureRegion(RenderTextureCopyLocation::Subresource(out.texture.get()),
                               RenderTextureCopyLocation::PlacedFootprint(buffer.get(), RenderFormat::R8G8B8A8_UNORM,
                                                                          pixels.width, pixels.height, 1, pitch / 4));
    command->barriers(RenderBarrierStage::GRAPHICS,
                      RenderTextureBarrier(out.texture.get(), RenderTextureLayout::SHADER_READ));
    command->end();
    queue->executeCommandLists(command.get(), fence.get());
    queue->waitForCommandFence(fence.get());
    RenderSamplerDesc sampler;
    sampler.minFilter = RenderFilter::LINEAR;
    sampler.magFilter = RenderFilter::LINEAR;
    sampler.addressU = RenderTextureAddressMode::CLAMP;
    sampler.addressV = RenderTextureAddressMode::CLAMP;
    out.sampler = app.device->createSampler(sampler);
    if (!out.sampler)
        return {};
    if (app.chosenGraphicsAPI == RT64::UserConfiguration::GraphicsAPI::Vulkan) {
        if (!descriptors(app))
            return {};
        VkDescriptorSetAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate.descriptorPool = descriptor_pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &descriptor_layout;
        if (vkAllocateDescriptorSets(descriptor_device, &allocate, &out.vulkan) != VK_SUCCESS)
            return {};
        VkDescriptorImageInfo image{static_cast<VulkanSampler*>(out.sampler.get())->vk,
                                    static_cast<VulkanTexture*>(out.texture.get())->imageView,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = out.vulkan;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(descriptor_device, 1, &write, 0, nullptr);
        out.id = reinterpret_cast<void*>(out.vulkan);
    }
#if defined(_WIN32)
    else if (app.chosenGraphicsAPI == RT64::UserConfiguration::GraphicsAPI::D3D12) {
        RenderDescriptorRange range(RenderDescriptorRangeType::TEXTURE, 0, 1);
        out.descriptor = app.device->createDescriptorSet(RenderDescriptorSetDesc(&range, 1));
        if (!out.descriptor)
            return {};
        out.descriptor->setTexture(0, out.texture.get(), RenderTextureLayout::SHADER_READ);
        auto* device = static_cast<D3D12Device*>(app.device.get());
        const auto handle = device->viewHeapAllocator->getGPUHandleAt(
            static_cast<D3D12DescriptorSet*>(out.descriptor.get())->viewAllocation.offset);
        out.id = reinterpret_cast<void*>(handle.ptr);
    }
#endif
    out.aspect = float(pixels.width) / pixels.height;
    return out;
}
} // namespace
void begin(RT64::Application& app) {
    application = &app;
    ++frame;
    uploads = 0;
}
void end() {
    if (application && !textures.empty())
        ImGui::GetForegroundDrawList()->AddCallback(submitted, reinterpret_cast<void*>(std::uintptr_t(frame)));
    application = nullptr;
}
View image(const std::string& key, unsigned width, unsigned height, std::span<const unsigned char> pixels) {
    if (!application)
        return {};
    auto found = textures.find(key);
    if (found == textures.end()) {
        if (uploads || !width || !height || width > 512 || height > 512 ||
            pixels.size() != std::size_t(width) * height * 4)
            return {};
        if (textures.size() >= 32) {
            auto victim = textures.end();
            for (auto it = textures.begin(); it != textures.end(); ++it)
                if (timing.can_retire(it->second.used, frame) &&
                    (victim == textures.end() || it->second.used < victim->second.used))
                    victim = it;
            if (victim == textures.end())
                return {};
            static bool logged = false;
            if (!logged) {
                logged = true;
                std::fprintf(stderr, "[mod-store][overlay] cache eviction used=%llu completed=%llu drawing=%llu\n",
                             static_cast<unsigned long long>(victim->second.used),
                             static_cast<unsigned long long>(timing.completed.load()),
                             static_cast<unsigned long long>(frame));
            }
            erase(victim);
        }
        ++uploads;
        auto next = upload({width, height, pixels});
        if (!next.id)
            return {};
        found = textures.emplace(key, std::move(next)).first;
        static bool logged = false;
        if (!logged) {
            logged = true;
            std::fprintf(
                stderr, "[mod-store][overlay] native cover renderer ready; bounded cache, fence-observed retirement\n");
        }
    }
    found->second.used = frame;
    return {found->second.id, found->second.aspect};
}
void release() {
    // The Inspector pool and last completed overlay frame still exist here.
    // Each descriptor is released before its referenced texture/sampler.
    while (!textures.empty())
        erase(textures.begin());
    if (descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(descriptor_device, descriptor_pool, nullptr);
    if (descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(descriptor_device, descriptor_layout, nullptr);
    descriptor_device = VK_NULL_HANDLE;
    descriptor_pool = VK_NULL_HANDLE;
    descriptor_layout = VK_NULL_HANDLE;
    application = nullptr;
    timing.submitted = 0;
    timing.completed = 0;
    frame = 0;
}
} // namespace dkr::store::overlay
