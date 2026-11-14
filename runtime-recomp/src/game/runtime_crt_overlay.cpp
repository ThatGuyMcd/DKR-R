#include "runtime_crt_overlay.hpp"

#include "hle/rt64_application.h"
#include "imgui/imgui.h"
#include "plume_render_interface.h"
// Volk must precede the ImGui Vulkan backend so Vulkan prototypes are not
// emitted twice on Windows and Linux.
#include "plume_vulkan.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "stb/stb_image.h"

#if defined(_WIN32)
#include "plume_d3d12.h"
#endif

#if defined(__APPLE__)
#include "plume_metal.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

using namespace plume;

struct GpuImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::unique_ptr<RenderTexture> texture;
    std::unique_ptr<RenderBuffer> upload_buffer;
    std::unique_ptr<RenderSampler> sampler;
    std::unique_ptr<RenderDescriptorSet> descriptor_set;
    ImTextureID image_id = nullptr;
};

std::unordered_map<std::string, std::unique_ptr<GpuImage>> g_images;

std::string PathKey(const std::filesystem::path& path) {
    const auto value = std::filesystem::weakly_canonical(path).u8string();
    return {value.begin(), value.end()};
}

std::unique_ptr<GpuImage> UploadImage(RT64::Application& application, const std::filesystem::path& path,
                                      std::string& status) {
    const std::string native_path = path.string();
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* source = stbi_load(native_path.c_str(), &width, &height, &channels, 4);
    if (source == nullptr || width <= 0 || height <= 0) {
        status = "Filter image could not be decoded: " + path.filename().string();
        if (source != nullptr)
            stbi_image_free(source);
        return nullptr;
    }

    auto image = std::make_unique<GpuImage>();
    image->width = static_cast<std::uint32_t>(width);
    image->height = static_cast<std::uint32_t>(height);
    image->texture = application.device->createTexture(
        RenderTextureDesc::Texture2D(image->width, image->height, 1, RenderFormat::R8G8B8A8_UNORM));
    if (image->texture == nullptr) {
        stbi_image_free(source);
        status = "The graphics device could not create the CRT filter texture.";
        return nullptr;
    }

    constexpr std::uint32_t pitch_alignment = 256;
    const std::uint32_t source_pitch = image->width * 4U;
    const std::uint32_t upload_pitch = (source_pitch + pitch_alignment - 1U) & ~(pitch_alignment - 1U);
    image->upload_buffer = application.device->createBuffer(
        RenderBufferDesc::UploadBuffer(static_cast<std::uint64_t>(upload_pitch) * image->height));
    if (image->upload_buffer == nullptr) {
        stbi_image_free(source);
        status = "The graphics device could not stage the CRT filter texture.";
        return nullptr;
    }
    auto* destination = static_cast<std::uint8_t*>(image->upload_buffer->map());
    if (destination == nullptr) {
        stbi_image_free(source);
        status = "The CRT filter staging buffer could not be mapped.";
        return nullptr;
    }
    for (std::uint32_t row = 0; row < image->height; ++row) {
        std::memcpy(destination + static_cast<std::size_t>(row) * upload_pitch,
                    source + static_cast<std::size_t>(row) * source_pitch, source_pitch);
    }
    image->upload_buffer->unmap();
    stbi_image_free(source);

    auto queue = application.device->createCommandQueue(RenderCommandListType::DIRECT);
    auto command_list = queue != nullptr ? queue->createCommandList() : nullptr;
    auto fence = application.device->createCommandFence();
    if (queue == nullptr || command_list == nullptr || fence == nullptr) {
        status = "The graphics device could not create a CRT upload transaction.";
        return nullptr;
    }
    command_list->begin();
    command_list->barriers(RenderBarrierStage::COPY,
                           RenderTextureBarrier(image->texture.get(), RenderTextureLayout::COPY_DEST));
    command_list->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(image->texture.get()),
        RenderTextureCopyLocation::PlacedFootprint(image->upload_buffer.get(), RenderFormat::R8G8B8A8_UNORM,
                                                   image->width, image->height, 1, upload_pitch / 4U));
    command_list->barriers(RenderBarrierStage::GRAPHICS,
                           RenderTextureBarrier(image->texture.get(), RenderTextureLayout::SHADER_READ));
    command_list->end();
    queue->executeCommandLists(command_list.get(), fence.get());
    queue->waitForCommandFence(fence.get());

    RenderSamplerDesc sampler_desc;
    sampler_desc.minFilter = RenderFilter::LINEAR;
    sampler_desc.magFilter = RenderFilter::LINEAR;
    sampler_desc.addressU = RenderTextureAddressMode::WRAP;
    sampler_desc.addressV = RenderTextureAddressMode::WRAP;
    image->sampler = application.device->createSampler(sampler_desc);
    if (image->sampler == nullptr) {
        status = "The graphics device could not create the CRT filter sampler.";
        return nullptr;
    }

    switch (application.chosenGraphicsAPI) {
    case RT64::UserConfiguration::GraphicsAPI::D3D12:
#if defined(_WIN32)
    {
        RenderDescriptorRange range(RenderDescriptorRangeType::TEXTURE, 0, 1);
        image->descriptor_set = application.device->createDescriptorSet(RenderDescriptorSetDesc(&range, 1));
        if (image->descriptor_set == nullptr) {
            status = "The Direct3D CRT descriptor could not be allocated.";
            return nullptr;
        }
        image->descriptor_set->setTexture(0, image->texture.get(), RenderTextureLayout::SHADER_READ);
        auto* device = static_cast<D3D12Device*>(application.device.get());
        auto* descriptor = static_cast<D3D12DescriptorSet*>(image->descriptor_set.get());
        const D3D12_GPU_DESCRIPTOR_HANDLE handle =
            device->viewHeapAllocator->getGPUHandleAt(descriptor->viewAllocation.offset);
        image->image_id = reinterpret_cast<ImTextureID>(handle.ptr);
        break;
    }
#else
        status = "Direct3D CRT filters are unavailable on this platform.";
        return nullptr;
#endif
    case RT64::UserConfiguration::GraphicsAPI::Vulkan: {
        auto* texture = static_cast<VulkanTexture*>(image->texture.get());
        auto* sampler = static_cast<VulkanSampler*>(image->sampler.get());
        const VkDescriptorSet descriptor =
            ImGui_ImplVulkan_AddTexture(sampler->vk, texture->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        image->image_id = reinterpret_cast<ImTextureID>(descriptor);
        break;
    }
    case RT64::UserConfiguration::GraphicsAPI::Metal:
#if defined(__APPLE__)
    {
        // Dear ImGui's Metal backend accepts the native MTLTexture directly.
        // The Plume RenderTexture remains owned by this cache for at least as
        // long as any submitted overlay frame can reference it.
        auto* texture = static_cast<MetalTexture*>(image->texture.get());
        image->image_id = reinterpret_cast<ImTextureID>(texture->mtl);
        break;
    }
#else
        status = "Metal CRT filters are unavailable on this platform.";
        return nullptr;
#endif
    default:
        status = "CRT filters are unavailable with the selected graphics API.";
        return nullptr;
    }

    status = "CRT filter active: " + path.stem().string();
    return image;
}

} // namespace

bool dkr::runtime::crt::draw(RT64::Application& application, const std::filesystem::path& image_path,
                             ScaleMode scale_mode, float strength, std::string& status) {
    if (image_path.empty() || application.device == nullptr)
        return false;
    std::error_code error;
    if (!std::filesystem::is_regular_file(image_path, error)) {
        status = "CRT filter is missing: " + image_path.filename().string();
        return false;
    }
    const std::string key = PathKey(image_path);
    auto found = g_images.find(key);
    if (found == g_images.end()) {
        auto uploaded = UploadImage(application, image_path, status);
        if (uploaded == nullptr)
            return false;
        found = g_images.emplace(key, std::move(uploaded)).first;
    }
    const GpuImage& image = *found->second;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x <= 0.0F || display.y <= 0.0F)
        return false;
    const ImU32 tint = ImGui::ColorConvertFloat4ToU32({1.0F, 1.0F, 1.0F, std::clamp(strength, 0.0F, 1.0F)});
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    if (scale_mode == ScaleMode::Stretch) {
        draw_list->AddImage(image.image_id, {0.0F, 0.0F}, display, {0.0F, 0.0F}, {1.0F, 1.0F}, tint);
    } else {
        const float tile_width = std::max(1.0F, static_cast<float>(image.width));
        const float tile_height = std::max(1.0F, static_cast<float>(image.height));
        for (float y = 0.0F; y < display.y; y += tile_height) {
            for (float x = 0.0F; x < display.x; x += tile_width) {
                const ImVec2 end{std::min(x + tile_width, display.x), std::min(y + tile_height, display.y)};
                const ImVec2 uv_end{(end.x - x) / tile_width, (end.y - y) / tile_height};
                draw_list->AddImage(image.image_id, {x, y}, end, {0.0F, 0.0F}, uv_end, tint);
            }
        }
    }
    return true;
}

void dkr::runtime::crt::release() {
    // Vulkan descriptors are owned by the Inspector's descriptor pool. Keep
    // them allocated until that pool is destroyed; release the referenced GPU
    // resources immediately before the Inspector/device teardown.
    g_images.clear();
}
