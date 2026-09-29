#pragma once
// Included only by the checked Android plume stage, after plume_vulkan.h.
#include <array>
#include <cstdio>
#include "graphics_health.hpp"

namespace plume::dkr_android {
using Counts = std::array<std::uint64_t, 7>;
enum Count { Sampler, Uniform, Storage, SampledImage, StorageImage, Input, Resources };
inline void count_binding(Counts& c, const VkDescriptorSetLayoutBinding& b) {
    const auto n = b.descriptorCount;
    switch (b.descriptorType) {
    case VK_DESCRIPTOR_TYPE_SAMPLER: c[Sampler] += n; return;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: c[Sampler] += n; c[SampledImage] += n; break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: c[SampledImage] += n; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: c[StorageImage] += n; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: c[Uniform] += n; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: c[Storage] += n; break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: c[Input] += n; break;
    default: break;
    }
    c[Resources] += n;
}
inline bool within(const Counts& actual, const Counts& limit, const char* kind) {
    constexpr const char* names[] = {"samplers", "uniform buffers", "storage buffers",
        "sampled images/texel buffers", "storage images/texel buffers", "input attachments", "resources"};
    for (unsigned i = 0; i < actual.size(); ++i) {
        if (actual[i] > limit[i]) {
            std::fprintf(stderr, "[Android] Unsupported %s %s: declared=%llu limit=%llu\n", kind, names[i],
                static_cast<unsigned long long>(actual[i]), static_cast<unsigned long long>(limit[i]));
            return false;
        }
    }
    return true;
}
inline void qualify_pipeline(VulkanDevice* device,
    const std::vector<VulkanDescriptorSetLayout*>& layouts, const RenderPipelineLayoutDesc& desc) {
    VkPhysicalDeviceDescriptorIndexingProperties indexing{};
    indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &indexing;
    vkGetPhysicalDeviceProperties2(device->physicalDevice, &properties);
    Counts regular{}, all{};
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        for (const auto& binding : layouts[i]->setBindings) {
            count_binding(all, binding);
            if (!desc.descriptorSetDescs[i].lastRangeIsBoundless) count_binding(regular, binding);
        }
    }
    // Plume declares VK_SHADER_STAGE_ALL on every binding: these exact totals
    // apply to every stage, not just the fragment shader. Update-after-bind
    // limits include regular sets as well. Never count one table in isolation.
    const auto& l = properties.properties.limits;
    constexpr std::uint64_t unlimited = UINT64_MAX;
    const bool valid = layouts.size() <= l.maxBoundDescriptorSets &&
        within(regular, {l.maxPerStageDescriptorSamplers, l.maxPerStageDescriptorUniformBuffers,
            l.maxPerStageDescriptorStorageBuffers, l.maxPerStageDescriptorSampledImages,
            l.maxPerStageDescriptorStorageImages, l.maxPerStageDescriptorInputAttachments,
            l.maxPerStageResources}, "per-stage") &&
        within(regular, {l.maxDescriptorSetSamplers, l.maxDescriptorSetUniformBuffers,
            l.maxDescriptorSetStorageBuffers, l.maxDescriptorSetSampledImages,
            l.maxDescriptorSetStorageImages, l.maxDescriptorSetInputAttachments, unlimited}, "pipeline") &&
        within(all, {indexing.maxPerStageDescriptorUpdateAfterBindSamplers,
            indexing.maxPerStageDescriptorUpdateAfterBindUniformBuffers,
            indexing.maxPerStageDescriptorUpdateAfterBindStorageBuffers,
            indexing.maxPerStageDescriptorUpdateAfterBindSampledImages,
            indexing.maxPerStageDescriptorUpdateAfterBindStorageImages,
            indexing.maxPerStageDescriptorUpdateAfterBindInputAttachments,
            indexing.maxPerStageUpdateAfterBindResources}, "update-after-bind per-stage") &&
        within(all, {indexing.maxDescriptorSetUpdateAfterBindSamplers,
            indexing.maxDescriptorSetUpdateAfterBindUniformBuffers,
            indexing.maxDescriptorSetUpdateAfterBindStorageBuffers,
            indexing.maxDescriptorSetUpdateAfterBindSampledImages,
            indexing.maxDescriptorSetUpdateAfterBindStorageImages,
            indexing.maxDescriptorSetUpdateAfterBindInputAttachments, unlimited}, "update-after-bind pipeline");
    if (!valid) dkr::runtime::graphics_health::quarantine_resource(
        dkr::runtime::graphics_health::Stage::DeviceLimits, VK_ERROR_FEATURE_NOT_PRESENT);
}
inline void qualify_image(VulkanDevice* device, const VkImageCreateInfo& image) {
    struct Entry { VkPhysicalDevice device; VkFormat format; VkImageType type;
        VkImageTiling tiling; VkImageUsageFlags usage; VkImageCreateFlags flags;
        VkImageFormatProperties properties; };
    // Formats/usages recur for thousands of textures. Cache only capability
    // queries, not resources; reset on device identity and cap retained entries.
    thread_local std::vector<Entry> entries;
    const Entry* found = nullptr;
    for (const auto& e : entries) if (e.device == device->physicalDevice && e.format == image.format &&
        e.type == image.imageType && e.tiling == image.tiling && e.usage == image.usage && e.flags == image.flags) {
        found = &e; break;
    }
    if (!found) {
        Entry e{device->physicalDevice, image.format, image.imageType, image.tiling, image.usage, image.flags, {}};
        const auto result = vkGetPhysicalDeviceImageFormatProperties(e.device, e.format, e.type, e.tiling, e.usage, e.flags, &e.properties);
        if (result != VK_SUCCESS) {
            std::fprintf(stderr, "[Android] Unsupported image format=%u usage=0x%X flags=0x%X result=%d\n",
                unsigned(e.format), e.usage, e.flags, result);
            dkr::runtime::graphics_health::quarantine_resource(dkr::runtime::graphics_health::Stage::DeviceLimits, result);
        }
        if (entries.size() == 128) entries.clear();
        entries.push_back(e); found = &entries.back();
    }
    const auto& p = found->properties;
    if (image.extent.width > p.maxExtent.width || image.extent.height > p.maxExtent.height ||
        image.extent.depth > p.maxExtent.depth || image.mipLevels > p.maxMipLevels ||
        image.arrayLayers > p.maxArrayLayers || !(image.samples & p.sampleCounts)) {
        std::fprintf(stderr, "[Android] Unsupported image extent/mips/layers/sample count\n");
        dkr::runtime::graphics_health::quarantine_resource(dkr::runtime::graphics_health::Stage::DeviceLimits, VK_ERROR_FORMAT_NOT_SUPPORTED);
    }
}
}
