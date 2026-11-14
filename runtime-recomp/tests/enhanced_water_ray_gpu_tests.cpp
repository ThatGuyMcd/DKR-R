#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include "gbi/rt64_f3d.h"
#include "xxHash/xxh3.h"
#include "common/rt64_load_types.h"
#include "common/rt64_tmem_hasher.h"
#include "render/WaterRayScene.h"
#include "render/WaterProjection.h"
#include "render/WaterBounds.h"
#include "render/LavaLightingBounds.h"
#include "render/rt64_water_renderer.h"
#include "render/rt64_render_target.h"
#include "render/rt64_descriptor_sets.h"
#include "render/rt64_rsp_processor.h"
#include "render/WaterPipelineSafety.h"
#include "shared/rt64_raster_params.h"
#include "shared/rt64_frame_params.h"
#include "shared/rt64_framebuffer_params.h"
#include "shared/rt64_render_params.h"
#include "shared/rt64_render_indices.h"
#include "shared/rt64_rdp_params.h"
#include "shared/rt64_rdp_tile.h"
#include "shared/rt64_gpu_tile.h"
#include "shaders/WaterRayProbeCS.hlsl.spirv.h"
#include "shaders/WaterFieldCS.hlsl.spirv.h"
#include "shaders/WaterFieldReferenceCS.hlsl.spirv.h"
#include "shaders/WaterFieldCompareCS.hlsl.spirv.h"
#include "shaders/WaterProjectionProbeCS.hlsl.spirv.h"
#include "shaders/WaterCaptureProbeCS.hlsl.spirv.h"
#include "shaders/WaterCaptureMSProbeCS.hlsl.spirv.h"
#ifdef _WIN32
#include "plume_d3d12.h"
#include <d3d12sdklayers.h>
#include "shaders/WaterRayProbeCS.hlsl.dxil.h"
#include "shaders/WaterFieldCS.hlsl.dxil.h"
#include "shaders/WaterFieldReferenceCS.hlsl.dxil.h"
#include "shaders/WaterFieldCompareCS.hlsl.dxil.h"
#include "shaders/WaterProjectionProbeCS.hlsl.dxil.h"
#include "shaders/WaterCaptureProbeCS.hlsl.dxil.h"
#include "shaders/WaterCaptureMSProbeCS.hlsl.dxil.h"
#endif
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <vector>
#include <stdexcept>

namespace plume {
#ifdef _WIN32
std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
#if PLUME_SDL_VULKAN_ENABLED
std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow window);
#else
std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif
} // namespace plume
namespace {
template <class T> std::unique_ptr<T> required(std::unique_ptr<T> value) {
    if (!value)
        throw std::runtime_error("Headless water-ray resource creation failed");
    return value;
}
void requireNativeGraphicsPipeline(plume::RenderPipeline* pipeline, plume::RenderShaderFormat format) {
    if (!pipeline)
        throw std::runtime_error("No waterfall pipeline wrapper");
#ifdef _WIN32
    if (format == plume::RenderShaderFormat::DXIL && !static_cast<plume::D3D12GraphicsPipeline*>(pipeline)->d3d) {
        ID3D12InfoQueue* messages = nullptr;
        auto* device = static_cast<plume::D3D12GraphicsPipeline*>(pipeline)->device;
        if (SUCCEEDED(device->d3d->QueryInterface(IID_PPV_ARGS(&messages)))) {
            for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
                SIZE_T size = 0;
                messages->GetMessage(i, nullptr, &size);
                std::vector<unsigned char> bytes(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                if (SUCCEEDED(messages->GetMessage(i, message, &size)))
                    std::fprintf(stderr, "D3D12: %s\n", message->pDescription);
            }
            messages->Release();
        }
        throw std::runtime_error("Waterfall pipeline wrapper contains a null native PSO");
    }
#endif
    if (!RT64::waterGraphicsPipelineValid(pipeline))
        throw std::runtime_error("No native waterfall pipeline");
        // Simulate the exact failed-creation contract without issuing an invalid
        // GPU call. Restore ownership before any possible exception/destruction.
#ifdef _WIN32
    if (format == plume::RenderShaderFormat::DXIL) {
        auto* native = static_cast<plume::D3D12GraphicsPipeline*>(pipeline);
        auto* saved = native->d3d;
        native->d3d = nullptr;
        const bool rejected = !RT64::waterGraphicsPipelineValid(native);
        native->d3d = saved;
        if (!rejected)
            throw std::runtime_error("Water guard accepted a failed D3D12 native PSO");
    } else
#endif
    {
        auto* native = static_cast<plume::VulkanGraphicsPipeline*>(pipeline);
        const auto saved = native->vk;
        native->vk = VK_NULL_HANDLE;
        const bool rejected = !RT64::waterGraphicsPipelineValid(native);
        native->vk = saved;
        if (!rejected)
            throw std::runtime_error("Water guard accepted a failed Vulkan native pipeline");
    }
}
void requireNoD3D12Errors(plume::RenderDevice* device, plume::RenderShaderFormat format) {
#ifdef _WIN32
    if (format != plume::RenderShaderFormat::DXIL)
        return;
    ID3D12InfoQueue* messages = nullptr;
    auto* native = static_cast<plume::D3D12Device*>(device)->d3d;
    const auto removed = native->GetDeviceRemovedReason();
    if (FAILED(removed))
        std::fprintf(stderr, "D3D12 device removed: 0x%08lX\n", static_cast<unsigned long>(removed));
    if (FAILED(native->QueryInterface(IID_PPV_ARGS(&messages)))) {
        if (FAILED(removed))
            throw std::runtime_error("Waterfall probe device removed");
        return;
    }
    bool errors = false;
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0;
        messages->GetMessage(i, nullptr, &size);
        std::vector<unsigned char> bytes(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if (SUCCEEDED(messages->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            std::fprintf(stderr, "D3D12: %s\n", message->pDescription);
            errors = true;
        }
    }
    messages->ClearStoredMessages();
    messages->Release();
    if (errors || FAILED(removed))
        throw std::runtime_error("D3D12 rejected the actual waterfall submission");
#endif
}
void waterfallDrawProbe(RT64::RenderWorker& worker, plume::RenderShaderFormat format, plume::RenderInterface* api) {
    using namespace RT64;
    unsigned draws = 0;
    // No game, ROM, window or user configuration: submit the real waterfall
    // shaders with the same five descriptor spaces/vertex streams as runtime.
    for (bool hdr : {false, true}) {
        ShaderLibrary library(hdr, false);
        library.setupCommonShaders(api, worker.device);
        WaterRenderer water(worker.device, format, hdr, &library);
        requireNoD3D12Errors(worker.device, format);
        FramebufferRendererDescriptorCommonSet common(library.samplerLibrary,
                                                      worker.device->getCapabilities().raytracing, worker.device);
        FramebufferRendererDescriptorTextureSet textures(worker.device, 1), tmem(worker.device, 1);
        FramebufferRendererDescriptorFramebufferSet framebufferParams(worker.device);
        std::vector<std::unique_ptr<RenderBuffer>> uploads;
        const RenderRange none(0, 0);
        auto upload = [&](const auto& value, RenderBufferFlags flags) {
            const uint64_t size = (sizeof(value) + 255) & ~uint64_t(255);
            auto buffer = required(worker.device->createBuffer(RenderBufferDesc::UploadBuffer(size, flags)));
            auto* mapped = buffer->map(0, &none);
            if (!mapped)
                throw std::runtime_error("No waterfall draw upload");
            std::memset(mapped, 0, size);
            std::memcpy(mapped, &value, sizeof(value));
            const RenderRange written(0, size);
            buffer->unmap(0, &written);
            auto* pointer = buffer.get();
            uploads.push_back(std::move(buffer));
            return pointer;
        };
        interop::FrameParams frame{};
        common.setBuffer(common.FrParams, upload(frame, RenderBufferFlag::CONSTANT), sizeof(frame));
        interop::RenderIndices indices{};
        common.setBuffer(common.instanceRenderIndices, upload(indices, RenderBufferFlag::STORAGE),
                         RenderBufferStructuredView(sizeof(indices)));
        interop::RDPParams rdp{};
        rdp.blendColor.w = 0.5f;
        common.setBuffer(common.instanceRDPParams, upload(rdp, RenderBufferFlag::STORAGE),
                         RenderBufferStructuredView(sizeof(rdp)));
        interop::RDPTile tile{};
        interop::GPUTile gpuTile{};
        common.setBuffer(common.RDPTiles, upload(tile, RenderBufferFlag::STORAGE),
                         RenderBufferStructuredView(sizeof(tile)));
        common.setBuffer(common.GPUTiles, upload(gpuTile, RenderBufferFlag::STORAGE),
                         RenderBufferStructuredView(sizeof(gpuTile)));
        interop::FramebufferParams fb{};
        fb.resolution = {16, 16};
        fb.resolutionScale = {1, 1};
        framebufferParams.setBuffer(framebufferParams.FbParams, upload(fb, RenderBufferFlag::CONSTANT), sizeof(fb));
        auto dummyColor =
            required(worker.device->createTexture(RenderTextureDesc::Texture2D(1, 1, 1, RenderFormat::R8G8B8A8_UNORM)));
        auto dummyTmem =
            required(worker.device->createTexture(RenderTextureDesc::Texture1D(1, 1, RenderFormat::R32_UINT)));
        textures.setTexture(textures.gTextures, dummyColor.get(), RenderTextureLayout::SHADER_READ);
        tmem.setTexture(tmem.gTMEM, dummyTmem.get(), RenderTextureLayout::SHADER_READ);
        framebufferParams.setTexture(framebufferParams.gBackgroundColor, dummyColor.get(),
                                     RenderTextureLayout::SHADER_READ);
        const std::array<interop::float4, 3> positions{{{0, 0, 0.5f, 1}, {32, 0, 0.5f, 1}, {0, 32, 0.5f, 1}}};
        const std::array<interop::float2, 3> uvs{};
        const std::array<interop::float4, 3> worlds{
            {{0, 0, 1, 1}, {1, 0, 1, 1}, {0, 1, 1, 1}}}; // authored film coordinates
        auto* positionBuffer = upload(positions, RenderBufferFlag::VERTEX);
        auto* uvBuffer = upload(uvs, RenderBufferFlag::VERTEX);
        auto* worldBuffer = upload(worlds, RenderBufferFlag::VERTEX);
        for (uint32_t samples : {1u, 4u}) {
            const auto colorFormat = RenderTarget::colorBufferFormat(hdr);
            if (!(worker.device->getSampleCountsSupported(colorFormat) & samples))
                continue;
            RenderMultisampling ms;
            ms.sampleCount = samples;
            auto output =
                required(worker.device->createTexture(RenderTextureDesc::ColorTarget(16, 16, colorFormat, ms)));
            auto depth = required(
                worker.device->createTexture(RenderTextureDesc::DepthTarget(16, 16, RenderFormat::D32_FLOAT, ms)));
            auto backgroundDepth = required(
                worker.device->createTexture(RenderTextureDesc::DepthTarget(16, 16, RenderFormat::R32_TYPELESS, ms)));
            auto backgroundDepthView =
                required(backgroundDepth->createTextureView(RenderTextureViewDesc::Texture2D(RenderFormat::R32_FLOAT)));
            const RenderTexture* attachment = output.get();
            auto target =
                required(worker.device->createFramebuffer(RenderFramebufferDesc(&attachment, 1, depth.get())));
            framebufferParams.setTexture(framebufferParams.gBackgroundDepth, backgroundDepth.get(),
                                         RenderTextureLayout::DEPTH_READ, backgroundDepthView.get());
            auto resolved =
                required(worker.device->createTexture(RenderTextureDesc::Texture2D(16, 16, 1, colorFormat)));
            auto readback = required(worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(4096)));
            for (bool compare : {false, true})
                for (bool write : {false, true})
                    for (bool coverage : {false, true}) {
                        auto* pipeline = water.waterfallPipeline(worker.device, ms, compare, write, coverage);
                        requireNoD3D12Errors(worker.device, format);
                        requireNativeGraphicsPipeline(pipeline, format);
                        std::array<float, 3> sums{};
                        for (unsigned scenario = 0; scenario < 3; scenario++) {
                            interop::RenderParams rp{};
                            // One-cycle SHADE combiner, threshold cutout, no texture lookups.
                            rp.ccL = (15u << 5) | 31;
                            rp.ccH = (15u << 24) | (4u << 6) | (7u << 21) | (7u << 18) | (7u << 3) | 4;
                            rp.omL = 1 | (compare ? 0x10 : 0) | (write ? 0x20 : 0) | (coverage ? 0x100 : 0);
                            rp.flags.value = 0;
                            rp.flags.smoothShade = 1;
                            rp.flags.usesHDR = hdr;
                            rp.flags.sampleCount = samples > 1 ? 2 : 0;
                            common.setBuffer(common.DynamicRenderParams, upload(rp, RenderBufferFlag::STORAGE),
                                             RenderBufferStructuredView(sizeof(rp)));
                            interop::WaterDrawParams draw{};
                            draw.camera.w = 1.25f;
                            draw.material.appearance.w = scenario ? 1.0f : 0.0f;
                            water.waterfallSet->setBuffer(water.waterfallSet->draws,
                                                          upload(draw, RenderBufferFlag::STORAGE),
                                                          RenderBufferStructuredView(sizeof(draw)));
                            std::array<interop::float4, 3> colors;
                            colors.fill({1, 1, 1, scenario == 2 ? 0.0f : 1.0f});
                            auto* colorBuffer = upload(colors, RenderBufferFlag::VERTEX);
                            const RenderVertexBufferView views[] = {{positionBuffer->at(0), sizeof(positions)},
                                                                    {uvBuffer->at(0), sizeof(uvs)},
                                                                    {colorBuffer->at(0), sizeof(colors)},
                                                                    {worldBuffer->at(0), sizeof(worlds)}};
                            const RenderInputSlot slots[] = {{0, 16}, {1, 8}, {2, 16}, {3, 16}};
                            interop::RasterParams constants{};
                            constants.screenScale = {1, 1};
                            const RenderViewport viewport(0, 0, 16, 16);
                            const RenderRect scissor(0, 0, 16, 16);
                            worker.commandList->begin();
                            worker.commandList->barriers(
                                RenderBarrierStage::GRAPHICS,
                                RenderTextureBarrier(dummyColor.get(), RenderTextureLayout::SHADER_READ));
                            worker.commandList->barriers(
                                RenderBarrierStage::GRAPHICS,
                                RenderTextureBarrier(dummyTmem.get(), RenderTextureLayout::SHADER_READ));
                            worker.commandList->barriers(
                                RenderBarrierStage::GRAPHICS,
                                RenderTextureBarrier(backgroundDepth.get(), RenderTextureLayout::DEPTH_READ));
                            worker.commandList->barriers(
                                RenderBarrierStage::GRAPHICS,
                                RenderTextureBarrier(output.get(), RenderTextureLayout::COLOR_WRITE));
                            worker.commandList->barriers(
                                RenderBarrierStage::GRAPHICS,
                                RenderTextureBarrier(depth.get(), RenderTextureLayout::DEPTH_WRITE));
                            worker.commandList->setFramebuffer(target.get());
                            worker.commandList->clearColor(0, RenderColor(0, 0, 0, 0));
                            worker.commandList->clearDepthStencil(true, false, 1, 0, nullptr, 0);
                            worker.commandList->setViewports(&viewport, 1);
                            worker.commandList->setScissors(&scissor, 1);
                            worker.commandList->setGraphicsPipelineLayout(water.waterfallLayout.get());
                            worker.commandList->setGraphicsDescriptorSet(common.get(), 0);
                            worker.commandList->setGraphicsDescriptorSet(textures.get(), 1);
                            worker.commandList->setGraphicsDescriptorSet(tmem.get(), 2);
                            worker.commandList->setGraphicsDescriptorSet(framebufferParams.get(), 3);
                            worker.commandList->setGraphicsDescriptorSet(water.waterfallSet->get(), 4);
                            worker.commandList->setGraphicsPushConstants(0, &constants);
                            worker.commandList->setVertexBuffers(0, views, 4, slots);
                            worker.commandList->setPipeline(pipeline);
                            worker.commandList->drawInstanced(3, 1, 0, 0);
                            worker.commandList->setFramebuffer(nullptr);
                            auto* source = output.get();
                            if (samples > 1) {
                                worker.commandList->barriers(
                                    RenderBarrierStage::COPY,
                                    RenderTextureBarrier(output.get(), RenderTextureLayout::RESOLVE_SOURCE));
                                worker.commandList->barriers(
                                    RenderBarrierStage::COPY,
                                    RenderTextureBarrier(resolved.get(), RenderTextureLayout::RESOLVE_DEST));
                                worker.commandList->resolveTexture(resolved.get(), output.get());
                                source = resolved.get();
                            }
                            worker.commandList->barriers(
                                RenderBarrierStage::COPY,
                                RenderTextureBarrier(source, RenderTextureLayout::COPY_SOURCE));
                            worker.commandList->barriers(
                                RenderBarrierStage::COPY,
                                RenderBufferBarrier(readback.get(), RenderBufferAccess::WRITE));
#ifdef _WIN32
                            if (format == RenderShaderFormat::DXIL) {
                                // Plume's texture-to-buffer helper dereferences the
                                // destination texture (absent for a placed footprint).
                                // Use native readback here; do not edit the submodule.
                                D3D12_TEXTURE_COPY_LOCATION destination{}, origin{};
                                destination.pResource = static_cast<plume::D3D12Buffer*>(readback.get())->d3d;
                                destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                destination.PlacedFootprint.Footprint = {
                                    hdr ? DXGI_FORMAT_R16G16B16A16_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 256};
                                origin.pResource = static_cast<plume::D3D12Texture*>(source)->d3d;
                                origin.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                static_cast<plume::D3D12CommandList*>(worker.commandList.get())
                                    ->d3d->CopyTextureRegion(&destination, 0, 0, 0, &origin, nullptr);
                            } else
#endif
                            {
                                // The Vulkan helper only implements buffer-to-image
                                // and image-to-image copies. Read back natively here.
                                VkBufferImageCopy region{};
                                region.bufferRowLength = hdr ? 32 : 64;
                                region.bufferImageHeight = 16;
                                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                region.imageSubresource.layerCount = 1;
                                region.imageExtent = {16, 16, 1};
                                vkCmdCopyImageToBuffer(
                                    static_cast<plume::VulkanCommandList*>(worker.commandList.get())->vk,
                                    static_cast<plume::VulkanTexture*>(source)->vk,
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    static_cast<plume::VulkanBuffer*>(readback.get())->vk, 1, &region);
                            }
                            worker.commandList->end();
                            requireNoD3D12Errors(worker.device, format);
                            worker.execute();
                            worker.wait();
                            requireNoD3D12Errors(worker.device, format);
                            const RenderRange all(0, 4096);
                            const auto* bytes = static_cast<const unsigned char*>(readback->map(0, &all));
                            if (!bytes)
                                throw std::runtime_error("No waterfall raster readback");
                            const auto* pixel = bytes + 4 * 256 + 4 * (hdr ? 8 : 4);
                            for (unsigned component = 0; component < 3; component++)
                                sums[scenario] += hdr ? reinterpret_cast<const uint16_t*>(pixel)[component] / 65535.0f
                                                      : pixel[component] / 255.0f;
                            readback->unmap(0, &none);
                            draws++;
                        }
                        if (sums[0] < 2.9f || sums[1] < 0.1f || std::abs(sums[0] - sums[1]) < 0.1f || sums[2] > 0.01f)
                            throw std::runtime_error("Actual waterfall RGB replacement/retail alpha cutout mismatch");
                    }
        }
    }
    std::printf(
        "PASS: %u actual waterfall raster draws/readbacks, SDR/HDR, 1x/4x, all depth/coverage variants, RGB replacement and transparent cutouts.\n",
        draws);
}
struct OutputSet : plume::RenderDescriptorSetBase {
    uint32_t output;
    explicit OutputSet(plume::RenderDevice* device) {
        builder.begin();
        output = builder.addReadWriteStructuredBuffer(1);
        builder.end();
        create(device);
        if (!get())
            throw std::runtime_error("Probe descriptor allocation failed");
    }
};
struct WakeSet : plume::RenderDescriptorSetBase {
    uint32_t draws, previous, next;
    WakeSet() {
        builder.begin();
        draws = builder.addStructuredBuffer(1);
        previous = builder.addTexture(2);
        next = builder.addReadWriteTexture(3);
        builder.end();
    }
};
struct FieldCompareSet : plume::RenderDescriptorSetBase {
    uint32_t reference, optimized, initial, errors;
    explicit FieldCompareSet(plume::RenderDevice* device) {
        builder.begin();
        reference = builder.addTexture(1);
        optimized = builder.addTexture(2);
        initial = builder.addReadWriteTexture(3);
        errors = builder.addReadWriteStructuredBuffer(4);
        builder.end();
        create(device);
        if (!get())
            throw std::runtime_error("No field comparison descriptors");
    }
};
void fieldProbe(RT64::RenderWorker& worker, plume::RenderShaderFormat format) {
    using namespace RT64;
    WakeSet baseline, optimized;
    baseline.create(worker.device);
    optimized.create(worker.device);
    RenderPipelineLayoutBuilder builder;
    builder.begin();
    builder.addPushConstant(0, 0, 32, RenderShaderStageFlag::COMPUTE);
    builder.addDescriptorSet(baseline);
    builder.end();
    auto layout = required(builder.create(worker.device));
    FieldCompareSet compare(worker.device);
    builder.begin();
    builder.addPushConstant(0, 0, 16, RenderShaderStageFlag::COMPUTE);
    builder.addDescriptorSet(compare);
    builder.end();
    auto compareLayout = required(builder.create(worker.device));
    std::unique_ptr<RenderShader> referenceShader, optimizedShader, compareShader;
#ifdef _WIN32
    if (format == RenderShaderFormat::DXIL) {
        referenceShader = required(worker.device->createShader(
            WaterFieldReferenceCSBlobDXIL, sizeof(WaterFieldReferenceCSBlobDXIL), "CSMain", format));
        optimizedShader =
            required(worker.device->createShader(WaterFieldCSBlobDXIL, sizeof(WaterFieldCSBlobDXIL), "CSMain", format));
        compareShader = required(worker.device->createShader(WaterFieldCompareCSBlobDXIL,
                                                             sizeof(WaterFieldCompareCSBlobDXIL), "CSMain", format));
    } else
#endif
    {
        referenceShader = required(worker.device->createShader(
            WaterFieldReferenceCSBlobSPIRV, sizeof(WaterFieldReferenceCSBlobSPIRV), "CSMain", format));
        optimizedShader = required(
            worker.device->createShader(WaterFieldCSBlobSPIRV, sizeof(WaterFieldCSBlobSPIRV), "CSMain", format));
        compareShader = required(worker.device->createShader(WaterFieldCompareCSBlobSPIRV,
                                                             sizeof(WaterFieldCompareCSBlobSPIRV), "CSMain", format));
    }
    auto referencePipeline = required(
        worker.device->createComputePipeline(RenderComputePipelineDesc(layout.get(), referenceShader.get(), 8, 8, 1)));
    auto optimizedPipeline = required(
        worker.device->createComputePipeline(RenderComputePipelineDesc(layout.get(), optimizedShader.get(), 8, 8, 1)));
    auto comparePipeline = required(worker.device->createComputePipeline(
        RenderComputePipelineDesc(compareLayout.get(), compareShader.get(), 8, 8, 1)));
    auto draws = required(worker.device->createBuffer(
        RenderBufferDesc::DefaultBuffer(sizeof(interop::WaterDrawParams), RenderBufferFlag::STORAGE)));
    auto upload =
        required(worker.device->createBuffer(RenderBufferDesc::UploadBuffer(sizeof(interop::WaterDrawParams))));
    baseline.setBuffer(baseline.draws, draws.get(), RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));
    optimized.setBuffer(optimized.draws, draws.get(), RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));
    unsigned cases = 0;
    float maximumError = 0;
    auto timings = worker.device->getCapabilities().queryPools ? worker.device->createQueryPool(4) : nullptr;
    std::array<double, 2> referenceMs{}, optimizedMs{};
    std::array<unsigned, 2> timedCases{};
    for (unsigned resolution : {1u, 128u, 256u, 512u}) {
        const unsigned width = resolution * 8, height = resolution, gx = (width + 7) / 8, gy = (height + 7) / 8;
        std::array<std::unique_ptr<RenderTexture>, 3> textures;
        for (auto& texture : textures)
            texture = required(worker.device->createTexture(
                RenderTextureDesc::Texture2D(width, height, 1, RenderFormat::R16G16B16A16_FLOAT,
                                             RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS)));
        baseline.setTexture(baseline.previous, textures[0].get(), RenderTextureLayout::SHADER_READ);
        optimized.setTexture(optimized.previous, textures[0].get(), RenderTextureLayout::SHADER_READ);
        baseline.setTexture(baseline.next, textures[1].get(), RenderTextureLayout::GENERAL);
        optimized.setTexture(optimized.next, textures[2].get(), RenderTextureLayout::GENERAL);
        compare.setTexture(compare.reference, textures[1].get(), RenderTextureLayout::SHADER_READ);
        compare.setTexture(compare.optimized, textures[2].get(), RenderTextureLayout::SHADER_READ);
        compare.setTexture(compare.initial, textures[0].get(), RenderTextureLayout::GENERAL);
        const uint64_t errorBytes = uint64_t(gx) * gy * 16;
        auto errors = required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(
            errorBytes, RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS)));
        auto readback = required(worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(errorBytes)));
        compare.setBuffer(compare.errors, errors.get(), RenderBufferStructuredView(16));
        for (unsigned scenario = 0; scenario < 8; scenario++) {
            interop::WaterDrawParams draw{};
            draw.material.animation = {2, 2.1f, 1, 1};
            draw.material.surface = {0, 0.7f, -0.2f, 0};
            for (unsigned i = 0; i < 8; i++) {
                draw.material.craftPrevious[i] = {float(i * 50 + 4), 12, float(i * 15 + 9),
                                                  scenario == 3 || scenario == 7 ? 0.0f : 1.0f};
                draw.material.craftCurrent[i] = {float(i * 50 + 24), 0, float(i * 15 + 12),
                                                 scenario == 4 && i > 1 ? -1.0f : 1.0f};
                const float shift = scenario == 2 ? 3.25f : 3.0f, cell = 4096.0f / resolution;
                draw.fieldOrigins[i] = {-1500, -1500, -1500 - shift * cell, -1500 + cell};
                if (scenario == 5) {
                    draw.material.shoreCounts[i / 4][i % 4] = 1;
                    draw.material.shoreSegments[i * 8] = {-1500, 100, 1500, 100};
                }
            }
            const RenderRange none(0, 0), written(0, sizeof(draw));
            auto* mapped = upload->map(0, &none);
            if (!mapped)
                throw std::runtime_error("No field upload");
            std::memcpy(mapped, &draw, sizeof(draw));
            upload->unmap(0, &written);
            interop::WaterFieldPushConstants constants{
                0, resolution, scenario == 0 ? 1u : 0u, 0, {1.0f / 120, 2.095f, 2.1033333f, 4096}};
            if (scenario == 6) {
                constants.reset = 1;
                constants.simulation = {0, 2, 2, 4096};
            }
            if (scenario == 7)
                constants.reset = 1;
            const uint32_t initialize[] = {width, height, 0, 0}, comparison[] = {width, height, 1, 0};
            worker.commandList->begin();
            worker.commandList->barriers(RenderBarrierStage::COPY,
                                         RenderBufferBarrier(draws.get(), RenderBufferAccess::WRITE));
            worker.commandList->copyBufferRegion(draws->at(0), upload->at(0), sizeof(draw));
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                         RenderBufferBarrier(draws.get(), RenderBufferAccess::READ));
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                         RenderTextureBarrier(textures[0].get(), RenderTextureLayout::GENERAL));
            for (unsigned i = 1; i < 3; i++)
                worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                             RenderTextureBarrier(textures[i].get(), RenderTextureLayout::SHADER_READ));
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                         RenderBufferBarrier(errors.get(), RenderBufferAccess::WRITE));
            worker.commandList->setComputePipelineLayout(compareLayout.get());
            worker.commandList->setPipeline(comparePipeline.get());
            worker.commandList->setComputeDescriptorSet(compare.get(), 0);
            worker.commandList->setComputePushConstants(0, initialize);
            worker.commandList->dispatch(gx, gy, 1);
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                         RenderTextureBarrier(textures[0].get(), RenderTextureLayout::SHADER_READ));
            for (unsigned i = 1; i < 3; i++)
                worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                             RenderTextureBarrier(textures[i].get(), RenderTextureLayout::GENERAL));
            worker.commandList->setComputePipelineLayout(layout.get());
            worker.commandList->setComputePushConstants(0, &constants);
            if (timings) {
                worker.commandList->resetQueryPool(timings.get(), 0, 4);
                worker.commandList->writeTimestamp(timings.get(), 0);
            }
            worker.commandList->setPipeline(referencePipeline.get());
            worker.commandList->setComputeDescriptorSet(baseline.get(), 0);
            worker.commandList->dispatch(gx, gy, 1);
            if (timings) {
                worker.commandList->writeTimestamp(timings.get(), 1);
                worker.commandList->writeTimestamp(timings.get(), 2);
            }
            worker.commandList->setPipeline(optimizedPipeline.get());
            worker.commandList->setComputeDescriptorSet(optimized.get(), 0);
            worker.commandList->dispatch(gx, gy, 1);
            if (timings)
                worker.commandList->writeTimestamp(timings.get(), 3);
            for (unsigned i = 1; i < 3; i++)
                worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                             RenderTextureBarrier(textures[i].get(), RenderTextureLayout::SHADER_READ));
            // The initial UAV remains bound by the comparison layout, though
            // phase 1 never accesses it. Restore its declared layout for Vulkan.
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                         RenderTextureBarrier(textures[0].get(), RenderTextureLayout::GENERAL));
            worker.commandList->setComputePipelineLayout(compareLayout.get());
            worker.commandList->setPipeline(comparePipeline.get());
            worker.commandList->setComputeDescriptorSet(compare.get(), 0);
            worker.commandList->setComputePushConstants(0, comparison);
            worker.commandList->dispatch(gx, gy, 1);
            worker.commandList->barriers(RenderBarrierStage::COPY,
                                         RenderBufferBarrier(errors.get(), RenderBufferAccess::READ));
            worker.commandList->barriers(RenderBarrierStage::COPY,
                                         RenderBufferBarrier(readback.get(), RenderBufferAccess::WRITE));
            worker.commandList->copyBufferRegion(readback->at(0), errors->at(0), errorBytes);
            worker.commandList->end();
            worker.execute();
            worker.wait();
            if (timings && resolution >= 256) {
                timings->queryResults();
                const auto* stamps = timings->getResults();
                const auto index = resolution == 512 ? 1 : 0;
                referenceMs[index] += double(stamps[1] - stamps[0]) / 1000000;
                optimizedMs[index] += double(stamps[3] - stamps[2]) / 1000000;
                timedCases[index]++;
            }
            const RenderRange all(0, errorBytes);
            mapped = readback->map(0, &all);
            if (!mapped)
                throw std::runtime_error("No wake readback");
            const auto* values = static_cast<const float*>(mapped);
            float error = 0;
            for (size_t i = 0; i < errorBytes / sizeof(float); i++)
                error = std::max(error, values[i]);
            readback->unmap(0, &none);
            maximumError = std::max(error, maximumError);
            cases++;
            if (!std::isfinite(error) || error > 0.002f) {
                std::fprintf(stderr, "Wake resolution=%u scenario=%u max-error=%g\n", resolution, scenario, error);
                throw std::runtime_error("Reference wake shader mismatch");
            }
        }
    }
    std::printf(
        "PASS: %u real GPU reference wake comparisons (1/128/256/512, reset, shifted, fractional, landing, inactive, shore); max-error=%g.\n",
        cases, maximumError);
    for (unsigned i = 0; i < 2; i++)
        if (timedCases[i])
            std::printf(
                "Synthetic wake-only mean: resolution=%u baseline=%.3fms optimized=%.3fms (not gameplay FPS).\n",
                i ? 512 : 256, referenceMs[i] / timedCases[i], optimizedMs[i] / timedCases[i]);
}
struct ProjectionSet : plume::RenderDescriptorSetBase {
    uint32_t input, output, draws;
    explicit ProjectionSet(plume::RenderDevice* device) {
        builder.begin();
        input = builder.addStructuredBuffer(1);
        output = builder.addReadWriteStructuredBuffer(2);
        draws = builder.addStructuredBuffer(3);
        builder.end();
        create(device);
        if (!get())
            throw std::runtime_error("Projection descriptor allocation failed");
    }
};
struct ProjectionCase {
    interop::WaterDrawParams draw;
    interop::float4 world, expectedPixel, rawWorld;
};
static_assert(sizeof(ProjectionCase) == sizeof(interop::WaterDrawParams) + 48, "CPU projection probe ABI");
void projectionProbe(RT64::RenderWorker& worker, plume::RenderShaderFormat format) {
    using namespace RT64;
    std::vector<ProjectionCase> cases;
    // Original, widescreen, ultra-wide, portrait and split-view dimensions.
    for (const auto size : std::array<std::array<unsigned, 2>, 6>{
             {{640, 480}, {2560, 1440}, {3440, 1440}, {5120, 1440}, {1080, 2400}, {1920, 1080}}})
        for (unsigned views : {1u, 2u, 4u})
            for (unsigned view = 0; view < views; view++)
                for (float fov : {45.0f, 90.0f, 120.0f})
                    for (float yaw : {-1.1f, 0.0f, 1.1f})
                        for (float nx : {-0.94f, 0.0f, 0.94f})
                            for (float ny : {-0.8f, 0.8f}) {
                                ProjectionCase c{};
                                auto& p = c.draw;
                                const float sx = views == 4 ? 80.0f : 160.0f, sy = views > 1 ? 60.0f : 120.0f;
                                const float tx = views == 4 ? 80.0f + 160.0f * (view % 2) : 160.0f;
                                const float ty = views > 1 ? 60.0f + 120.0f * (views == 4 ? view / 2 : view) : 120.0f;
                                p.screen = {320, 240, float(size[0]), float(size[1])};
                                p.viewportScale = {sx, sy, 0.5f, float(size[0])};
                                p.viewportTranslate = {tx, ty, 0.5f, float(size[1])};
                                p.screenTransform = {0.997f, 0.995f, 0.0003f,
                                                     -0.0004f}; // actual offset/non-unit mapping
                                const float actualAspect = (float(size[0]) * (views == 4 ? 0.5f : 1.0f)) /
                                                           (float(size[1]) * (views > 1 ? 0.5f : 1.0f));
                                const float sourceAspect = sx / sy;
                                const float f = 1.0f / std::tan(fov * 3.14159265359f / 360.0f), cameraNear = 4,
                                            cameraFar = 12000;
                                const auto projection = hlslpp::float4x4(
                                    f / sourceAspect, 0, 0, 0, 0, f, 0, 0, 0, 0, cameraFar / (cameraFar - cameraNear),
                                    1, 0, 0, -cameraFar * cameraNear / (cameraFar - cameraNear), 0);
                                // Non-origin/interpolated camera ensures the order is not commutative.
                                const float cosine = std::cos(yaw), sine = std::sin(yaw);
                                const auto viewMatrix = hlslpp::float4x4(cosine, 0, -sine, 0, 0, 1, 0, 0, sine, 0,
                                                                         cosine, 0, -13, -7, 41, 1);
                                const auto camera = hlslpp::mul(viewMatrix, projection);
                                auto raster = hlslpp::float4x4::identity();
                                raster[0][0] = sourceAspect / actualAspect;
                                p.material.cameraPosition.w = 1;
                                setWaterCameraProjection(p, camera, raster);
                                // Generate edge samples independently, then apply the same two stages
                                // as RSPProcessCS rather than the helper's composed projection.
                                auto world = hlslpp::mul(hlslpp::float4(nx, ny, 0.95f, 1),
                                                         hlslpp::inverse(hlslpp::mul(camera, raster)));
                                world = world / hlslpp::float4(float(world.w));
                                c.world = {float(world.x), float(world.y), float(world.z), 1};
                                const auto raw = hlslpp::mul(world, camera), clip = hlslpp::mul(raw, raster);
                                c.rawWorld = raw;
                                const float screenX = float(clip.x / clip.w) * sx + tx,
                                            screenY = -float(clip.y / clip.w) * sy + ty;
                                const float mappedX =
                                    (screenX / 320 * 2 - 1) * p.screenTransform.x + p.screenTransform.z;
                                const float mappedY =
                                    (1 - screenY / 240 * 2) * p.screenTransform.y + p.screenTransform.w;
                                c.expectedPixel = {(mappedX * 0.5f + 0.5f) * size[0], (0.5f - mappedY * 0.5f) * size[1],
                                                   float(clip.z / clip.w) * 0.5f + 0.5f, 1};
                                p.viewBounds = {(tx - sx) / 320 * size[0], (ty - sy) / 240 * size[1],
                                                (tx + sx) / 320 * size[0], (ty + sy) / 240 * size[1]};
                                // Qualify the CPU capture rectangle against the independent RSP
                                // projection also checked below on the GPU. Exercise both cold and
                                // interpolated matrices, including the per-vertex velocity path.
                                for (bool interpolated : {false, true}) {
                                    DrawData data;
                                    data.worldTransforms.push_back(camera);
                                    if (interpolated)
                                        data.lerpWorldTransforms.push_back(camera);
                                    data.modViewProjTransforms.push_back(raster);
                                    interop::RSPViewport viewport{};
                                    viewport.scale = {sx, sy, 0.5f};
                                    viewport.translate = {tx, ty, 0.5f};
                                    data.rspViewports.push_back(viewport);
                                    constexpr float weight = 0.375f;
                                    for (unsigned vertex = 0; vertex < 3; vertex++) {
                                        data.posFloats.insert(data.posFloats.end(),
                                                              {c.world.x + 0.125f * (1 - weight),
                                                               c.world.y - 0.0625f * (1 - weight), c.world.z});
                                        data.velFloats.insert(data.velFloats.end(), {0.125f, -0.0625f, 0.0f});
                                        data.worldIndices.push_back(0);
                                        data.viewProjIndices.push_back(0);
                                        data.faceIndices.push_back(vertex);
                                    }
                                    const auto region = waterOriginalRegion(data, 0, 3, p, weight);
                                    if (!region.width || !region.height || c.expectedPixel.x < region.x ||
                                        c.expectedPixel.y < region.y || c.expectedPixel.x >= region.x + region.width ||
                                        c.expectedPixel.y >= region.y + region.height)
                                        throw std::runtime_error(
                                            "Compact waterfall capture excluded an RSP-projected sample");
                                    const auto full =
                                        water_work::captureRegion(size[0], size[1], p.viewBounds.x, p.viewBounds.y,
                                                                  p.viewBounds.z, p.viewBounds.w);
                                    data.faceIndices[0] = UINT32_MAX;
                                    const auto guarded = waterOriginalRegion(data, 0, 3, p, weight);
                                    if (guarded.x != full.x || guarded.y != full.y || guarded.width != full.width ||
                                        guarded.height != full.height)
                                        throw std::runtime_error(
                                            "Uncertain waterfall geometry did not use conservative scissor capture");
                                }
                                cases.push_back(c);
                            }
    ProjectionSet set(worker.device);
    RenderPipelineLayoutBuilder builder;
    builder.begin();
    builder.addDescriptorSet(set);
    builder.end();
    auto layout = required(builder.create(worker.device));
    std::unique_ptr<RenderShader> shader;
#ifdef _WIN32
    if (format == RenderShaderFormat::DXIL)
        shader = required(worker.device->createShader(WaterProjectionProbeCSBlobDXIL,
                                                      sizeof(WaterProjectionProbeCSBlobDXIL), "CSMain", format));
    else
#endif
        shader = required(worker.device->createShader(WaterProjectionProbeCSBlobSPIRV,
                                                      sizeof(WaterProjectionProbeCSBlobSPIRV), "CSMain", format));
    auto pipeline =
        required(worker.device->createComputePipeline(RenderComputePipelineDesc(layout.get(), shader.get(), 1, 1, 1)));
    std::vector<interop::WaterDrawParams> drawInputs;
    std::vector<std::array<interop::float4, 3>> details;
    for (const auto& c : cases) {
        drawInputs.push_back(c.draw);
        details.push_back({c.world, c.expectedPixel, c.rawWorld});
    }
    const uint64_t inputBytes = cases.size() * 48, outputBytes = cases.size() * 16,
                   drawBytes = drawInputs.size() * sizeof(interop::WaterDrawParams);
    auto drawBuffer =
        required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(drawBytes, RenderBufferFlag::STORAGE)));
    auto drawUpload = required(worker.device->createBuffer(RenderBufferDesc::UploadBuffer(drawBytes)));
    const RenderRange drawWritten(0, drawBytes);
    auto drawMapped = drawUpload->map();
    if (!drawMapped)
        throw std::runtime_error("No projection draw upload");
    std::memcpy(drawMapped, drawInputs.data(), drawBytes);
    drawUpload->unmap(0, &drawWritten);
    set.setBuffer(set.draws, drawBuffer.get(), RenderBufferStructuredView(sizeof(interop::WaterDrawParams)));
    auto input =
        required(worker.device->createBuffer(RenderBufferDesc::DefaultBuffer(inputBytes, RenderBufferFlag::STORAGE)));
    auto upload = required(worker.device->createBuffer(RenderBufferDesc::UploadBuffer(inputBytes)));
    auto output = required(worker.device->createBuffer(
        RenderBufferDesc::DefaultBuffer(outputBytes, RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS)));
    auto readback = required(worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(outputBytes)));
    const RenderRange none(0, 0), written(0, inputBytes);
    auto mapped = upload->map(0, &none);
    if (!mapped)
        throw std::runtime_error("No projection upload");
    std::memcpy(mapped, details.data(), inputBytes);
    upload->unmap(0, &written);
    set.setBuffer(set.input, input.get(), RenderBufferStructuredView(48));
    set.setBuffer(set.output, output.get(), RenderBufferStructuredView(16));
    worker.commandList->begin();
    worker.commandList->barriers(RenderBarrierStage::COPY,
                                 RenderBufferBarrier(drawBuffer.get(), RenderBufferAccess::WRITE));
    worker.commandList->copyBufferRegion(drawBuffer->at(0), drawUpload->at(0), drawBytes);
    worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                 RenderBufferBarrier(drawBuffer.get(), RenderBufferAccess::READ));
    worker.commandList->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(input.get(), RenderBufferAccess::WRITE));
    worker.commandList->copyBufferRegion(input->at(0), upload->at(0), inputBytes);
    worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                 RenderBufferBarrier(input.get(), RenderBufferAccess::READ));
    worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                 RenderBufferBarrier(output.get(), RenderBufferAccess::WRITE));
    worker.commandList->setComputePipelineLayout(layout.get());
    worker.commandList->setPipeline(pipeline.get());
    worker.commandList->setComputeDescriptorSet(set.get(), 0);
    worker.commandList->dispatch(uint32_t(cases.size()), 1, 1);
    worker.commandList->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(output.get(), RenderBufferAccess::READ));
    worker.commandList->barriers(RenderBarrierStage::COPY,
                                 RenderBufferBarrier(readback.get(), RenderBufferAccess::WRITE));
    worker.commandList->copyBufferRegion(readback->at(0), output->at(0), outputBytes);
    worker.commandList->end();
    worker.execute();
    worker.wait();
    const RenderRange all(0, outputBytes);
    mapped = readback->map(0, &all);
    if (!mapped)
        throw std::runtime_error("No projection readback");
    std::vector<std::array<float, 4>> errors(cases.size());
    std::memcpy(errors.data(), mapped, outputBytes);
    readback->unmap(0, &none);
    for (unsigned i = 0; i < errors.size(); i++) {
        const auto& e = errors[i];
        if (!std::isfinite(e[0]) || !std::isfinite(e[1]) || !std::isfinite(e[2]) || e[0] > 0.05f || e[1] > 0.2f ||
            e[2] > 0.2f || e[3] != 0) {
            std::fprintf(stderr, "Projection case %u: pixel=%g world=%g geometry=%g outside=%g\n", i, e[0], e[1], e[2],
                         e[3]);
            throw std::runtime_error("GPU water projection mismatch");
        }
    }
    std::printf(
        "PASS: %zu real GPU projection cases, wide edges, split views, offsets, FOV and undistorted geometry.\n",
        cases.size());
    std::printf(
        "PASS: %zu conservative compact-capture bounds, cold/interpolated matrices, velocities and invalid-index fallback.\n",
        cases.size() * 2);
}
void resourceProbe(RT64::RenderWorker& worker, plume::RenderShaderFormat format, const RT64::ShaderLibrary* library) {
    using namespace RT64;
    RenderTarget color(0, Framebuffer::Type::Color, RenderMultisampling{}, false);
    RenderTarget depth(0, Framebuffer::Type::Depth, RenderMultisampling{}, false);
    color.resize(&worker, 2560, 1440);
    depth.resize(&worker, 2560, 1440);
    WaterRenderer water(worker.device, format, false, library);
    Workload flowTask;
    flowTask.drawData.worldIndices.resize(3);
    flowTask.drawData.faceIndices = {2, 0, 1, UINT32_MAX};
    flowTask.drawData.waterfallFlow[2] = {0.25f, 0.5f, -1, 1};
    flowTask.drawData.waterfallFlow[0] = {0.75f, 0.5f, -1, 1};
    water.collectWaterfall(flowTask.drawData, 0, UINT32_MAX);
    if (water.waterfallFlow.size() != 3 || water.waterfallFlow[2].x != 0.25f || water.waterfallFlow[0].x != 0.75f ||
        water.waterfallFlow[1].w != 0)
        throw std::runtime_error("Indexed waterfall flow metadata or missing-UV fallback failed");
    flowTask.resetDrawData();
    water.reset();
    if (!flowTask.drawData.waterfallFlow.empty() || !water.waterfallFlow.empty())
        throw std::runtime_error("Waterfall flow metadata survived task/frame reset");
    std::puts("PASS: waterfall flow follows real indexed vertices, bounded ranges and task/frame resets.");
    interop::WaterDrawParams moltenDraw{};
    moltenDraw.material.effects.z = 5;
    moltenDraw.material.identity.x = 3;
    moltenDraw.material.animation = {0, 1.0f / 30, 0.35f, 0};
    moltenDraw.camera.w = 1.0f / 60;
    for (auto& craft : moltenDraw.material.craftCurrent)
        craft.w = -1;
    for (auto& craft : moltenDraw.material.craftPrevious)
        craft.w = -1;
    Workload moltenTask;
    moltenTask.drawData.posFloats = {0, 0, 0, 100, 0, 0, 0, 0, 100};
    moltenTask.drawData.faceIndices = {0, 1, 2};
    water.draws.push_back(moltenDraw);
    water.collectNormals(moltenTask.drawData, 0, 3, 1);
    if (water.normalGroups.size() != 3)
        throw std::runtime_error("Molten animated geometry lost its surface normals");
    water.prepareField(&worker);
    if (water.fieldResolution != 1 || !water.particles.empty())
        throw std::runtime_error("Lava-only scene allocated water wakes or spray");
    water.reset();
    water.reset();
    std::puts(
        "PASS: molten geometry retains normals; lava-only scenes use a one-texel dummy field and no water spray.");
    bool hardware = false;
    if (worker.device->getCapabilities().rayQuery) {
        auto world = std::make_shared<dkr::runtime::water::enhanced::RayGeometry>();
        world->vertices = {{{-1, 0, 0, 1}, {1, 1, 1, 1}}, {{1, 0, 0, 1}, {1, 1, 1, 1}}, {{0, 0, 1, 1}, {1, 1, 1, 1}}};
        world->opaque_vertices = 3;
        hardware = water.prepareRays(&worker, format, world);
        if (!hardware)
            throw std::runtime_error("Molten hardware reflection pipeline preparation failed");
    }
    for (uint32_t samples : {1u, 4u}) {
        if (!(worker.device->getSampleCountsSupported(RenderTarget::colorBufferFormat(false)) & samples))
            continue;
        RenderMultisampling ms;
        ms.sampleCount = samples;
        if (hardware)
            requireNativeGraphicsPipeline(water.lavaLightingPipeline(worker.device, format, ms), format);
        for (bool compare : {false, true})
            for (bool write : {false, true}) {
                requireNativeGraphicsPipeline(
                    water.pipeline(worker.device, ms, compare, write, false, false, false, false, false), format);
                if (hardware)
                    requireNativeGraphicsPipeline(
                        water.pipeline(worker.device, ms, compare, write, false, false, false, false, true), format);
            }
    }
    requireNoD3D12Errors(worker.device, format);
    std::puts(
        "PASS: actual molten/water pixel shaders form valid native 1x/4x reflection pipelines across depth modes.");
    for (unsigned pass = 0; pass < 12; pass++) {
        water.reset();
        if (pass % 4 == 0) {
            color.setupColor(&worker, 2560, 1440);
            depth.setupDepth(&worker, 2560, 1440);
        }
        const auto modern = water.prepareScene(&worker, &color, &depth, 1, false, true);
        if (modern == water_policy::invalidScene)
            throw std::runtime_error("1440p modern water rejected");
        auto* framebuffer = water.scenes[modern].surfaceFramebuffer.get();
        if (water.prepareScene(&worker, &color, &depth, 1, false, true) != modern ||
            water.scenes[modern].surfaceFramebuffer.get() != framebuffer)
            throw std::runtime_error("Scratch key/framebuffer reuse failed");
        if (water.scenes[modern].sourceColorRevision != color.textureRevision ||
            water.scenes[modern].sourceDepthRevision != depth.textureRevision)
            throw std::runtime_error("Recreated source attachment revision was not adopted");
        if (hardware) {
            moltenDraw.material.shoreCounts[0].x = 4;
            if (!water.prepareLavaLighting(&worker, modern, moltenDraw) || water.scenes[modern].lavaWidth != 640 ||
                water.scenes[modern].lavaHeight != 360)
                throw std::runtime_error("1440p bounded lava lighting preparation failed");
            auto* prepared = water.scenes[modern].lavaLight.get();
            moltenDraw.material.shoreCounts[0].x = 16;
            if (water.prepareLavaLighting(&worker, modern, moltenDraw) ||
                water.scenes[modern].lavaLight.get() != prepared)
                throw std::runtime_error("Live lava quality change invalidated an earlier prepared batch");
        }
        if (pass % 3 != 2) {
            const auto classic = water.prepareScene(&worker, &color, &depth, 1, true, false, {0, 0, 1024, 768});
            if (classic == water_policy::invalidScene)
                throw std::runtime_error("1440p original-colour overlay rejected");
            if (water.prepareScene(&worker, &color, &depth, 2, false, true) == water_policy::invalidScene)
                throw std::runtime_error("Third 1440p scratch rejected within payload budget");
            // Deliberately exceed the budget without allocating a fourth
            // scratch. Existing accepted scenes must remain available.
            unsigned rejected = 0;
            for (unsigned key = 3; key < 20; key++)
                if (water.prepareScene(&worker, &color, &depth, key, false, true) == water_policy::invalidScene) {
                    rejected = key;
                    break;
                }
            if (!rejected ||
                water.prepareScene(&worker, &color, &depth, rejected, false, true) != water_policy::invalidScene ||
                water.prepareScene(&worker, &color, &depth, 1, false, true) != modern)
                throw std::runtime_error("Bounded target-local fallback invalidated an accepted scene");
        }
        if (water.allocationBytes() > water_policy::sceneBudget)
            throw std::runtime_error("Scratch pool exceeded bounded payload");
    }
    water.reset();
    water.reset();
    if (!water.scenes.empty())
        throw std::runtime_error("Unused scene pool retained");
    if (water.prepareScene(&worker, &color, &depth, 1, false, true) == water_policy::invalidScene)
        throw std::runtime_error("Water failed to recover after budget fallback");
    std::puts(
        "PASS: real 1440p water/lava allocations, prepared-light ownership, mixed modes, target-local budget rejection, pool reuse/reclaim and recovery.");
    if (worker.device->getSampleCountsSupported(RenderFormat::R32_FLOAT) & 4) {
        RenderMultisampling ms;
        ms.sampleCount = 4;
        RenderTarget msColor(0, Framebuffer::Type::Color, ms, false), msDepth(0, Framebuffer::Type::Depth, ms, false);
        msColor.resize(&worker, 2560, 1440);
        msDepth.resize(&worker, 2560, 1440);
        for (bool reversed : {false, true}) {
            water.reset(); // Preserve the previous workload's pool, including aliases.
            const auto first = water.prepareScene(&worker, &msColor, &msDepth, 1, false, reversed);
            const auto second = water.prepareScene(&worker, &msColor, &msDepth, 1, false, !reversed);
            const auto vertical = water.prepareScene(&worker, &msColor, &msDepth, 1, true, false, {0, 0, 2560, 1472});
            if (first == water_policy::invalidScene || second == water_policy::invalidScene ||
                vertical == water_policy::invalidScene)
                throw std::runtime_error("Reported 1440p/4x mixed-mode regression rejected");
            if (water.scenes[first].color != water.scenes[second].color ||
                water.scenes[first].depth != water.scenes[second].depth ||
                water.scenes[first].surface != water.scenes[second].surface || water.scenes[vertical].surface ||
                water.scenes[vertical].width != 1)
                throw std::runtime_error("Optical/mask sharing or vertical resource isolation failed");
            if (water.allocationBytes() > water_policy::sceneBudget)
                throw std::runtime_error("4x resource pool exceeds budget");
            const auto original = water.scenes[vertical].originalColor.get();
            if (original || water.scenes[vertical].originalSet)
                throw std::runtime_error("Waterfall allocated obsolete MSAA atlas");
            for (bool compare : {false, true})
                for (bool write : {false, true})
                    for (bool coverage : {false, true})
                        requireNativeGraphicsPipeline(
                            water.waterfallPipeline(worker.device, ms, compare, write, coverage), format);
            if (water.prepareScene(&worker, &msColor, &msDepth, 1, true, false, {0, 0, 900, 600}) != vertical ||
                original != water.scenes[vertical].originalColor.get())
                throw std::runtime_error("Direct waterfall resources were needlessly recreated");
        }
        std::puts(
            "PASS: real 1440p/4x reported fullscreen waterfall regression: no colour atlas, dual-source pipelines valid, both draw orders fit.");
        water.reset();
        water.reset(); // Retire bindings before the local source attachments.
    }
    interop::WaterDrawParams fieldDraw{};
    fieldDraw.material.identity = {3, 0, 0, 1};
    fieldDraw.material.animation = {0, 0.0333333f, 1, 1};
    fieldDraw.camera.w = 0.0166667f;
    fieldDraw.material.craftCurrent[0].w = 1;
    water.draws.push_back(fieldDraw);
    water.prepareField(&worker);
    const auto* retained = water.fieldTextures[0].get();
    if (water.fieldResolution != 512 || !retained)
        throw std::runtime_error("High wake atlas missing");
    for (unsigned transition = 0; transition < 8; transition++) {
        water.reset();
        fieldDraw.material.craftCurrent[0].w = transition % 2 ? 1.0f : 0.0f;
        fieldDraw.camera.w += 1.0f / 60;
        water.draws.push_back(fieldDraw);
        water.prepareField(&worker);
        if (water.fieldTextures[0].get() != retained || water.fieldResolution != 512)
            throw std::runtime_error("Wet/dry transition reallocated wake textures");
        if (water.fieldSteps.empty() || !water.fieldSteps.front().first.reset)
            throw std::runtime_error("Wet/dry transition failed to reset history");
    }
    std::puts("PASS: real high-quality wake resources retained across eight wet/dry transitions with history resets.");
}
struct CaptureProbeSet : plume::RenderDescriptorSetBase {
    uint32_t optical, original, depth, privateDepth, errors;
    explicit CaptureProbeSet(plume::RenderDevice* device) {
        builder.begin();
        optical = builder.addTexture(1);
        original = builder.addTexture(2);
        depth = builder.addTexture(3);
        privateDepth = builder.addTexture(4);
        errors = builder.addReadWriteStructuredBuffer(5);
        builder.end();
        create(device);
        if (!get())
            throw std::runtime_error("No capture probe descriptors");
    }
};
void captureProbe(RT64::RenderWorker& worker, plume::RenderShaderFormat format, const RT64::ShaderLibrary* library) {
    using namespace RT64;
    for (uint32_t samples : {1u, 4u}) {
        if (!(worker.device->getSampleCountsSupported(RenderFormat::R32_FLOAT) & samples))
            continue;
        RenderMultisampling ms;
        ms.sampleCount = samples;
        RenderTarget color(0, Framebuffer::Type::Color, ms, false), depth(0, Framebuffer::Type::Depth, ms, false);
        color.resize(&worker, 256, 192);
        depth.resize(&worker, 256, 192);
        WaterRenderer water(worker.device, format, false, library);
        const auto root = water.prepareScene(&worker, &color, &depth, 1, false, false);
        const auto isolated = water.prepareScene(&worker, &color, &depth, 1, false, true);
        const auto vertical = water.prepareScene(&worker, &color, &depth, 1, true, false, {64, 32, 128, 128});
        if (root == water_policy::invalidScene || isolated == water_policy::invalidScene ||
            vertical == water_policy::invalidScene)
            throw std::runtime_error("Capture probe preparation rejected");
        interop::WaterDrawParams draw{};
        draw.material.identity.x = 3;
        draw.viewBounds = {0, 0, 256, 192};
        draw.originalRegion = {64, 32, 128, 128};
        water.draws = {draw, draw, draw};
        water.drawScenes = {root, isolated, vertical};
        const RenderTexture* output = color.texture.get();
        auto framebuffer =
            required(worker.device->createFramebuffer(RenderFramebufferDesc(&output, 1, depth.texture.get())));
        auto privateView = required(water.scenes[isolated].waterDepth->createTextureView(
            RenderTextureViewDesc::Texture2D(RenderFormat::D32_FLOAT)));
        CaptureProbeSet set(worker.device);
        auto& shared = water.scenes[root];
        auto& compact = water.scenes[vertical];
        set.setTexture(set.optical, shared.color.get(), RenderTextureLayout::SHADER_READ, shared.colorView.get());
        set.setTexture(set.original, shared.color.get(), RenderTextureLayout::SHADER_READ, shared.colorView.get());
        set.setTexture(set.depth, shared.depth.get(), RenderTextureLayout::SHADER_READ, shared.depthMipViews[0].get());
        set.setTexture(set.privateDepth, water.scenes[isolated].waterDepth.get(), RenderTextureLayout::SHADER_READ,
                       privateView.get());
        RenderPipelineLayoutBuilder builder;
        builder.begin();
        builder.addPushConstant(0, 0, 16, RenderShaderStageFlag::COMPUTE);
        builder.addDescriptorSet(set);
        builder.end();
        auto layout = required(builder.create(worker.device));
        std::unique_ptr<RenderShader> shader;
#ifdef _WIN32
        if (format == RenderShaderFormat::DXIL)
            shader = samples == 1
                         ? required(worker.device->createShader(WaterCaptureProbeCSBlobDXIL,
                                                                sizeof(WaterCaptureProbeCSBlobDXIL), "CSMain", format))
                         : required(worker.device->createShader(
                               WaterCaptureMSProbeCSBlobDXIL, sizeof(WaterCaptureMSProbeCSBlobDXIL), "CSMain", format));
        else
#endif
            shader =
                samples == 1
                    ? required(worker.device->createShader(WaterCaptureProbeCSBlobSPIRV,
                                                           sizeof(WaterCaptureProbeCSBlobSPIRV), "CSMain", format))
                    : required(worker.device->createShader(WaterCaptureMSProbeCSBlobSPIRV,
                                                           sizeof(WaterCaptureMSProbeCSBlobSPIRV), "CSMain", format));
        auto pipeline = required(
            worker.device->createComputePipeline(RenderComputePipelineDesc(layout.get(), shader.get(), 8, 8, 1)));
        const uint64_t bytes = 128 * 128 * samples * 16;
        auto errors = required(worker.device->createBuffer(
            RenderBufferDesc::DefaultBuffer(bytes, RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS)));
        auto readback = required(worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(bytes)));
        set.setBuffer(set.errors, errors.get(), RenderBufferStructuredView(16));
        worker.commandList->begin();
        worker.commandList->barriers(RenderBarrierStage::GRAPHICS,
                                     {RenderTextureBarrier(color.texture.get(), RenderTextureLayout::COLOR_WRITE),
                                      RenderTextureBarrier(depth.texture.get(), RenderTextureLayout::DEPTH_WRITE)});
        worker.commandList->setFramebuffer(framebuffer.get());
        worker.commandList->clearColor(0, RenderColor(1, 0, 0, 1));
        worker.commandList->clearDepth(true, 0.25f);
        // Reuse the root's immutable optical image, while initializing the
        // second mode's private depth after the first capture has completed.
        if (!water.captureScene(&worker, 0, false) || !water.captureScene(&worker, 1, false) ||
            water.captureScene(&worker, 0, false))
            throw std::runtime_error("Shared optical capture/depth initialization state is incorrect");
        worker.commandList->setFramebuffer(framebuffer.get());
        worker.commandList->clearColor(0, RenderColor(0, 1, 0, 1));
        const RenderRect patch(64, 32, 128, 96);
        worker.commandList->clearColor(0, RenderColor(0, 0, 1, 1), &patch, 1);
        water.captureScene(&worker, 2, false);
        if (compact.originalColor)
            throw std::runtime_error("Direct waterfall unexpectedly retained original-colour copy");
        worker.commandList->barriers(
            RenderBarrierStage::COMPUTE,
            {RenderTextureBarrier(shared.color.get(), RenderTextureLayout::SHADER_READ),
             RenderTextureBarrier(shared.depth.get(), RenderTextureLayout::SHADER_READ),
             RenderTextureBarrier(water.scenes[isolated].waterDepth.get(), RenderTextureLayout::SHADER_READ)});
        worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                     RenderBufferBarrier(errors.get(), RenderBufferAccess::WRITE));
        const uint32_t params[] = {128, 128, samples, 0};
        worker.commandList->setComputePipelineLayout(layout.get());
        worker.commandList->setPipeline(pipeline.get());
        worker.commandList->setComputeDescriptorSet(set.get(), 0);
        worker.commandList->setComputePushConstants(0, params);
        worker.commandList->dispatch(16, 16, 1);
        worker.commandList->barriers(RenderBarrierStage::COPY,
                                     {RenderBufferBarrier(errors.get(), RenderBufferAccess::READ),
                                      RenderBufferBarrier(readback.get(), RenderBufferAccess::WRITE)});
        worker.commandList->copyBufferRegion(readback->at(0), errors->at(0), bytes);
        worker.commandList->end();
        worker.execute();
        worker.wait();
        const RenderRange all(0, bytes), none(0, 0);
        auto* mapped = static_cast<const float*>(readback->map(0, &all));
        if (!mapped)
            throw std::runtime_error("No capture readback");
        float error = 0;
        std::array<float, 4> componentErrors{};
        std::array<uint64_t, 4> worstPixels{};
        for (uint64_t i = 0; i < bytes / 4; i++) {
            if (!std::isfinite(mapped[i]))
                throw std::runtime_error("Non-finite compact capture result");
            error = std::max(error, mapped[i]);
            if (mapped[i] > componentErrors[i % 4]) {
                componentErrors[i % 4] = mapped[i];
                worstPixels[i % 4] = i / 4;
            }
        }
        readback->unmap(0, &none);
        if (error > 0.002f) {
            std::fprintf(
                stderr,
                "Capture %ux errors: optical=%g at %llu original=%g at %llu optical-depth=%g at %llu private-depth=%g at %llu\n",
                samples, componentErrors[0], (unsigned long long)worstPixels[0], componentErrors[1],
                (unsigned long long)worstPixels[1], componentErrors[2], (unsigned long long)worstPixels[2],
                componentErrors[3], (unsigned long long)worstPixels[3]);
            throw std::runtime_error("Compact original/immutable optics/private depth pixels mismatch");
        }
        std::printf("PASS: actual %ux GPU optical reuse and private depth sample readback; error=%g.\n", samples,
                    error);
    }
}
void lavaDrawProbe(RT64::RenderWorker& worker, plume::RenderShaderFormat format, const RT64::ShaderLibrary* library) {
    using namespace RT64;
    if (!worker.device->getCapabilities().rayQuery)
        return;
    for (uint32_t samples : {1u, 4u})
        for (uint32_t divisor : {4u, 8u, 16u})
            for (float receiverDepth : {0.25f, 0.99985f}) {
                if (!(worker.device->getSampleCountsSupported(RenderTarget::colorBufferFormat(false)) & samples))
                    continue;
                RenderMultisampling ms;
                ms.sampleCount = samples;
                RenderTarget color(0, Framebuffer::Type::Color, ms, false),
                    depth(0, Framebuffer::Type::Depth, ms, false);
                color.resize(&worker, 16, 16);
                depth.resize(&worker, 16, 16);
                WaterRenderer water(worker.device, format, false, library);
                auto geometry = std::make_shared<dkr::runtime::water::enhanced::RayGeometry>();
                geometry->vertices = {{{1000, 0, 10, 1}, {1, 1, 1, 1}},
                                      {{1010, 0, 10, 1}, {1, 1, 1, 1}},
                                      {{1000, 10, 10, 1}, {1, 1, 1, 1}}};
                geometry->opaque_vertices = 3;
                if (!water.prepareRays(&worker, format, geometry))
                    throw std::runtime_error("No lava draw ray scene");
                requireNativeGraphicsPipeline(water.lavaLightingPipeline(worker.device, format, ms), format);
                const auto scene = water.prepareScene(&worker, &color, &depth, 1, false, false);
                if (scene == water_policy::invalidScene)
                    throw std::runtime_error("No lava draw capture");
                interop::WaterDrawParams draw{};
                draw.material.effects = {0, 0, 5, 1};
                draw.material.sunDirection.w = 1;
                draw.material.skyColor.w = 1;
                draw.material.optics.y = 1;
                draw.material.shoreCounts[0].x = float(divisor);
                draw.material.shoreSegments[0] = {0, 0, 20, 100};
                draw.camera = {0, 0, 5, 0};
                draw.screen = {16, 16, 16, 16};
                draw.screenTransform = {1, 1, 0, 0};
                draw.viewBounds = {0, 0, 16, 16};
                draw.viewportScale = {8, 8, 1, 16};
                draw.viewportTranslate = {8, 8, 0, 16};
                draw.inverseViewProjection = hlslpp::float4x4::identity();
                draw.viewProjection = hlslpp::float4x4::identity();
                water.draws = {draw};
                water.drawScenes = {scene};
                if (!water.prepareLavaLighting(&worker, scene, draw))
                    throw std::runtime_error("No bounded lava lighting field");
                if (water.scenes[scene].lavaWidth != 16 / divisor || water.scenes[scene].lavaHeight != 16 / divisor)
                    throw std::runtime_error("Lava quality did not reduce the ray field dimensions");
                auto upload = required(worker.device->createBuffer(
                    RenderBufferDesc::UploadBuffer(sizeof(draw), RenderBufferFlag::STORAGE)));
                const RenderRange none(0, 0), written(0, sizeof(draw));
                auto* mapped = upload->map(0, &none);
                if (!mapped)
                    throw std::runtime_error("No lava draw upload");
                std::memcpy(mapped, &draw, sizeof(draw));
                upload->unmap(0, &written);
                auto& set = *water.scenes[scene].materialSet;
                set.setBuffer(set.draws, upload.get(), RenderBufferStructuredView(sizeof(draw)));
                for (auto index : {set.currentField, set.previousField, set.originalColor})
                    set.setTexture(index, water.scenes[scene].color.get(), RenderTextureLayout::SHADER_READ,
                                   water.scenes[scene].colorView.get());
                const RenderTexture* target = color.texture.get();
                auto framebuffer =
                    required(worker.device->createFramebuffer(RenderFramebufferDesc(&target, 1, depth.texture.get())));
                auto resolved = required(worker.device->createTexture(
                    RenderTextureDesc::Texture2D(16, 16, 1, RenderTarget::colorBufferFormat(false))));
                auto readback = required(worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(4096)));
                worker.commandList->begin();
                water.rays.record(&worker);
                worker.commandList->barriers(
                    RenderBarrierStage::GRAPHICS,
                    {RenderTextureBarrier(color.texture.get(), RenderTextureLayout::COLOR_WRITE),
                     RenderTextureBarrier(depth.texture.get(), RenderTextureLayout::DEPTH_WRITE)});
                worker.commandList->setFramebuffer(framebuffer.get());
                worker.commandList->clearColor(0, RenderColor(0.2f, 0.2f, 0.2f, 1));
                worker.commandList->clearDepth(true, receiverDepth);
                if (!water.drawLavaLighting(&worker, 0, false, RenderViewport(0, 0, 16, 16)))
                    throw std::runtime_error("Lava draw was not submitted");
                worker.commandList->setFramebuffer(framebuffer.get());
                worker.commandList->drawInstanced(3, 1, 0, 0);
                worker.commandList->setFramebuffer(nullptr);
                auto* source = color.texture.get();
                if (samples > 1) {
                    worker.commandList->barriers(RenderBarrierStage::COPY,
                                                 RenderTextureBarrier(source, RenderTextureLayout::RESOLVE_SOURCE));
                    worker.commandList->barriers(
                        RenderBarrierStage::COPY,
                        RenderTextureBarrier(resolved.get(), RenderTextureLayout::RESOLVE_DEST));
                    worker.commandList->resolveTexture(resolved.get(), source);
                    source = resolved.get();
                }
                worker.commandList->barriers(RenderBarrierStage::COPY,
                                             RenderTextureBarrier(source, RenderTextureLayout::COPY_SOURCE));
                worker.commandList->barriers(RenderBarrierStage::COPY,
                                             RenderBufferBarrier(readback.get(), RenderBufferAccess::WRITE));
#ifdef _WIN32
                if (format == RenderShaderFormat::DXIL) {
                    D3D12_TEXTURE_COPY_LOCATION destination{}, origin{};
                    destination.pResource = static_cast<plume::D3D12Buffer*>(readback.get())->d3d;
                    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 256};
                    origin.pResource = static_cast<plume::D3D12Texture*>(source)->d3d;
                    origin.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    static_cast<plume::D3D12CommandList*>(worker.commandList.get())
                        ->d3d->CopyTextureRegion(&destination, 0, 0, 0, &origin, nullptr);
                } else
#endif
                {
                    VkBufferImageCopy region{};
                    region.bufferRowLength = 64;
                    region.bufferImageHeight = 16;
                    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    region.imageSubresource.layerCount = 1;
                    region.imageExtent = {16, 16, 1};
                    vkCmdCopyImageToBuffer(static_cast<plume::VulkanCommandList*>(worker.commandList.get())->vk,
                                           static_cast<plume::VulkanTexture*>(source)->vk,
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           static_cast<plume::VulkanBuffer*>(readback.get())->vk, 1, &region);
                }
                worker.commandList->end();
                worker.execute();
                worker.wait();
                requireNoD3D12Errors(worker.device, format);
                const RenderRange all(0, 4096);
                const auto* pixels = static_cast<const unsigned char*>(readback->map(0, &all));
                if (!pixels)
                    throw std::runtime_error("No lava lighting raster readback");
                const auto red = pixels[8 * 256 + 8 * 4], green = pixels[8 * 256 + 8 * 4 + 1];
                readback->unmap(0, &none);
                // Capture linearizes the 0.2 grey input. The bounded irradiance should
                // encode back to about 110 red, not the >140 of an unlinearized capture.
                if (red <= green + 20 || red <= 90)
                    throw std::runtime_error("Lava lighting did not illuminate the receiver pixels");
                std::printf(
                    "PASS: actual %ux MSAA, 1/%u lighting pixels, depth=%.5f capture/ray bindings and warm receiver readback (%u,%u).\n",
                    samples, divisor * divisor, receiverDepth, unsigned(red), unsigned(green));
            }
}
} // namespace
int main(int argc, char** argv) {
    using namespace RT64;
    try {
        const std::array<uint64_t, 6> expected{0x206d5f44069cbcadULL, 0x8cc110a2995e91cbULL, 0x5081f4671da18c3bULL,
                                               0x75445d2a4046a145ULL, 0xddb85956cfa1601dULL, 0x8aaec51a5a73156aULL};
        LoadTile proxy{};
        proxy.line = 4;
        proxy.siz = G_IM_SIZ_16b;
        proxy.fmt = G_IM_FMT_RGBA;
        for (unsigned i = 0; i < expected.size(); ++i) {
            std::array<uint8_t, 4096> tmem{};
            std::fill_n(tmem.begin(), 512, uint8_t(49 + i * 16));
            if (TMEMHasher::hash(tmem.data(), proxy, 16, 16, 0, TMEMHasher::CurrentHashVersion) != expected[i])
                throw std::runtime_error("Bundled custom-row texture hash differs from the real renderer");
        }
        std::puts("PASS: all six custom-row proxies match the actual RT64 TMEM hasher.");
        std::unique_ptr<RenderInterface> api;
        RenderShaderFormat format = RenderShaderFormat::SPIRV;
#ifdef _WIN32
        if (argc < 2 || std::strcmp(argv[1], "vulkan") != 0) {
            ID3D12Debug* debug = nullptr;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
                debug->Release();
            }
            api = plume::CreateD3D12Interface();
            format = RenderShaderFormat::DXIL;
        }
#endif
        if (!api) {
#if PLUME_SDL_VULKAN_ENABLED
            api = plume::CreateVulkanInterface(nullptr);
#else
            api = plume::CreateVulkanInterface();
#endif
        }
        if (!api)
            throw std::runtime_error("No headless graphics interface");
        auto device = required(api->createDevice());
        RenderWorker worker(device.get(), "Headless water probes", RenderCommandListType::DIRECT);
        if (!worker.commandQueue || !worker.commandList || !worker.commandFence)
            throw std::runtime_error("No headless command resources");
        waterfallDrawProbe(worker, format, api.get());
        projectionProbe(worker, format);
        {
            interop::WaterDrawParams d{};
            d.viewProjection = hlslpp::float4x4::identity();
            d.screen = {320, 240, 640, 480};
            d.viewportScale = {160, 120, 1, 640};
            d.viewportTranslate = {160, 120, 0, 480};
            d.screenTransform = {1, 1, 0, 0};
            d.viewBounds = {0, 0, 640, 480};
            d.material.skyColor.w = 1;
            d.material.shoreSegments[0] = {0, 0, 0, 100};
            auto bounds = lavaLightingBounds(d);
            if (bounds != std::array<float, 4>{0, 0, 640, 480})
                throw std::runtime_error("Nearby lava influence was clipped");
            d.material.shoreSegments[0].x = 100000;
            bounds = lavaLightingBounds(d);
            if (bounds[2] > bounds[0])
                throw std::runtime_error("Offscreen lava influence was not culled");
            d.material.shoreSegments[0].x = 0;
            d.screenTransform = {0.65F, 0.8F, 0.2F, -0.1F};
            d.viewportScale.w = 2560;
            d.viewportTranslate.w = 1440;
            d.viewBounds = {20, 30, 2540, 1400};
            bounds = lavaLightingBounds(d);
            if (bounds != std::array<float, 4>{20, 30, 2540, 1400})
                throw std::runtime_error("Wide/aspect-adjusted influence lost coverage");
            std::puts(
                "PASS: conservative local-light bounds: nearby, offscreen, viewport offsets and aspect transforms.");
        }
        ShaderLibrary library(false, false);
        library.setupCommonShaders(api.get(), device.get());
        resourceProbe(worker, format, &library);
        captureProbe(worker, format, &library);
        lavaDrawProbe(worker, format, &library);
        {
            // Exercise the actual production RSP colour shader and descriptor
            // ABI, not an isolated approximation of the new arithmetic.
            std::fprintf(stderr, "[test][glow] production RSP stream begin\n");
            if (!library.rspProcess.pipeline)
                throw std::runtime_error("Production RSP pipeline/constant ABI invalid");
            Workload workload{};
            workload.reset();
            auto& draw = workload.drawData;
            constexpr unsigned vertices = 65536;
            draw.posFloats.resize(vertices * 3, 0);
            draw.velFloats.resize(vertices * 3, 0);
            draw.tcFloats.resize(vertices * 2, 0);
            draw.tcVelFloats.resize(vertices * 2, 0);
            draw.normColBytes.resize(vertices * 4, 100);
            for (unsigned i = 0; i < vertices; ++i)
                draw.normColBytes[i * 4 + 3] = 255;
            draw.viewProjIndices.resize(vertices, 0);
            draw.worldIndices.resize(vertices, 0);
            draw.fogIndices.resize(vertices, 0);
            draw.lightIndices.resize(vertices, 0);
            draw.lightCounts.resize(vertices, 0);
            draw.lookAtIndices.resize(vertices, 0);
            draw.rspFog.emplace_back();
            draw.rspLights.emplace_back();
            draw.rspLookAt.emplace_back();
            draw.lavaGlow.resize(vertices, 0.14F);
            draw.lavaGlowActive = true;
            draw.lavaGlowStrength = 0;
            draw.lavaGlowIntensity = 1;
            BufferUploader uploader(device.get());
            workload.updateDrawDataRanges();
            workload.uploadDrawData(&worker, &uploader);
            uploader.wait();
            workload.updateOutputBuffers(&worker);
            std::fprintf(stderr, "[test][glow] uploads and output buffers prepared\n");
            RSPProcessor processor(device.get());
            processor.process({&worker, &draw, &workload.drawBuffers, &workload.outputBuffers, 0, 1});
            std::fprintf(stderr, "[test][glow] descriptors prepared\n");
            auto readback = required(device->createBuffer(RenderBufferDesc::ReadbackBuffer(16)));
            auto timer = device->getCapabilities().queryPools ? device->createQueryPool(4) : nullptr;
            worker.commandList->begin();
            uploader.commandListBeforeBarriers(&worker);
            uploader.commandListCopyResources(&worker);
            uploader.commandListAfterBarriers(&worker);
            processor.recordCommandList(&worker, &library, &workload.outputBuffers);
            worker.commandList->barriers(
                RenderBarrierStage::COPY,
                RenderBufferBarrier(workload.outputBuffers.shadedColBuffer.buffer.get(), RenderBufferAccess::READ));
            worker.commandList->copyBufferRegion(readback->at(0), workload.outputBuffers.shadedColBuffer.buffer->at(0),
                                                 16);
            worker.commandList->end();
            worker.execute();
            worker.wait();
            std::fprintf(stderr, "[test][glow] colour dispatch completed\n");
            requireNoD3D12Errors(device.get(), format);
            const auto* rgba = static_cast<const float*>(readback->map());
            if (!rgba || rgba[0] <= rgba[1] || rgba[1] <= rgba[2] || rgba[3] != 1)
                throw std::runtime_error("Production cached lava glow colour/fog stream failed");
            const float base = 100.0f / 255;
            if (rgba[0] > base * 1.141f || rgba[1] > base * 1.028f || rgba[2] > base * 1.003f)
                throw std::runtime_error("Default lava glow exceeds subtle colour budget");
            readback->unmap();
            for (unsigned state = 0; state < 3; ++state) {
                processor.processCB.lavaGlowScale = state == 2 ? 0 : 3;
                processor.processCB.lavaGlowCooling = state == 1 ? 1 : 0;
                worker.commandList->begin();
                processor.recordCommandList(&worker, &library, &workload.outputBuffers);
                worker.commandList->barriers(
                    RenderBarrierStage::COPY,
                    RenderBufferBarrier(workload.outputBuffers.shadedColBuffer.buffer.get(), RenderBufferAccess::READ));
                worker.commandList->copyBufferRegion(readback->at(0),
                                                     workload.outputBuffers.shadedColBuffer.buffer->at(0), 16);
                worker.commandList->end();
                worker.execute();
                worker.wait();
                const auto* pixel = static_cast<const float*>(readback->map());
                if (!pixel || pixel[3] != 1 || pixel[0] > base * 1.181f || pixel[1] > base * 1.035f ||
                    pixel[2] > base * 1.003f || (state == 1 && pixel[1] > base * 1.014f) ||
                    (state == 2 && (std::abs(pixel[0] - base) > 0.00001f || std::abs(pixel[1] - base) > 0.00001f ||
                                    std::abs(pixel[2] - base) > 0.00001f)))
                    throw std::runtime_error("Lava glow maximum intensity, cooling colour or exact off regression");
                readback->unmap();
            }
            processor.processCB.lavaGlowCooling = 0;
            double totals[2] = {};
            for (unsigned frame = 0; frame < 40 && timer; ++frame) {
                worker.commandList->begin();
                worker.commandList->resetQueryPool(timer.get(), 0, 4);
                for (unsigned mode = 0; mode < 2; ++mode) {
                    processor.processCB.lavaGlowScale = float(mode);
                    worker.commandList->writeTimestamp(timer.get(), mode * 2);
                    processor.recordCommandList(&worker, &library, &workload.outputBuffers);
                    worker.commandList->writeTimestamp(timer.get(), mode * 2 + 1);
                }
                worker.commandList->end();
                worker.execute();
                worker.wait();
                timer->queryResults();
                const auto* times = timer->getResults();
                if (frame >= 8)
                    for (unsigned mode = 0; mode < 2; ++mode)
                        totals[mode] += double(times[mode * 2 + 1] - times[mode * 2]) / 1000000.0;
            }
            std::printf(
                "PASS: actual RSP cached glow, unchanged alpha, zero lighting rays/passes; %u vertices GPU off=%.4f ms on=%.4f ms incremental=%.4f ms.\n",
                vertices, totals[0] / 32, totals[1] / 32, (totals[1] - totals[0]) / 32);
        }
        fieldProbe(worker, format);
        // Validate the actual wake shader/pipeline with the portable constant
        // range even when this device cannot execute hardware ray queries.
        // Production's shared CPU/shader header separately asserts this ABI.
        // Do not import its SIMD/GUI dependencies into the headless probe.
        constexpr uint32_t fieldConstantBytes = 32;
        WakeSet fieldSet;
        RenderPipelineLayoutBuilder fieldBuilder;
        fieldBuilder.begin();
        fieldBuilder.addPushConstant(0, 0, fieldConstantBytes, RenderShaderStageFlag::COMPUTE);
        fieldBuilder.addDescriptorSet(fieldSet);
        fieldBuilder.end();
        auto fieldLayout = required(fieldBuilder.create(device.get()));
        std::unique_ptr<RenderShader> fieldShader;
#ifdef _WIN32
        if (format == RenderShaderFormat::DXIL)
            fieldShader =
                required(device->createShader(WaterFieldCSBlobDXIL, sizeof(WaterFieldCSBlobDXIL), "CSMain", format));
        else
#endif
            fieldShader =
                required(device->createShader(WaterFieldCSBlobSPIRV, sizeof(WaterFieldCSBlobSPIRV), "CSMain", format));
        auto fieldPipeline = required(
            device->createComputePipeline(RenderComputePipelineDesc(fieldLayout.get(), fieldShader.get(), 8, 8, 1)));
        std::puts("PASS: eight-racer wake shader/pipeline accepts 32-byte constants.");
        std::printf("Water ray-query capability: %s\n", device->getCapabilities().rayQuery ? "enabled" : "unavailable");
        if (!device->getCapabilities().rayQuery)
            return 77;
        WaterRayScene rays;
        OutputSet output(device.get());
        WaterRaySet rayLayout;
        RenderPipelineLayoutBuilder builder;
        builder.begin();
        builder.addDescriptorSet(output);
        builder.addDescriptorSet(rayLayout);
        builder.end();
        auto layout = required(builder.create(device.get()));
        std::unique_ptr<RenderShader> shader;
#ifdef _WIN32
        if (format == RenderShaderFormat::DXIL)
            shader = required(
                device->createShader(WaterRayProbeCSBlobDXIL, sizeof(WaterRayProbeCSBlobDXIL), "CSMain", format));
        else
#endif
            shader = required(
                device->createShader(WaterRayProbeCSBlobSPIRV, sizeof(WaterRayProbeCSBlobSPIRV), "CSMain", format));
        auto pipeline =
            required(device->createComputePipeline(RenderComputePipelineDesc(layout.get(), shader.get(), 4, 1, 1)));
        constexpr uint32_t lavaSamples = 128 * 128;
        constexpr uint32_t bytes = (19 + lavaSamples * 6) * 4 * sizeof(uint32_t);
        auto gpu = required(device->createBuffer(
            RenderBufferDesc::DefaultBuffer(bytes, RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS)));
        auto cpu = required(device->createBuffer(RenderBufferDesc::ReadbackBuffer(bytes)));
        output.setBuffer(output.output, gpu.get(), RenderBufferStructuredView(16));
        std::shared_ptr<dkr::runtime::water::enhanced::RayGeometry> geometry;
        for (unsigned pass = 0; pass < 4; pass++) {
            // Repeated immutable reuse, followed by a complete scene change.
            const float z = pass < 2 ? 10.0f : 12.0f;
            if (pass % 2 == 0) {
                geometry = std::make_shared<dkr::runtime::water::enhanced::RayGeometry>();
                geometry->vertices = {
                    {{-1, -1, z, 1}, {1, 0, 0, 1}}, {{1, -1, z, 1}, {0, 1, 0, 1}}, {{0, 1, z, 1}, {0, 0, 1, 1}}};
                geometry->opaque_vertices = 3;
                if (pass >= 2) {
                    geometry->masks.resize(2);
                    geometry->masks[0].params[3] = 1;
                    geometry->masks[1].params = {32, 32, 3, 0};
                    for (unsigned y = 0; y < 32; y++)
                        for (unsigned x = 15; x < 32; x++) {
                            const auto cell = y * 32 + x;
                            geometry->masks[1].alpha[cell / 4] |= 255U << ((cell % 4) * 8);
                        }
                    geometry->vertices.insert(geometry->vertices.end(),
                                              {{{-1, -1, 10, 1}, {1, 1, 1, 1}, {0, 0, 1, 0}},
                                               {{1, -1, 10, 1}, {1, 1, 1, 1}, {32, 0, 1, 0}},
                                               {{0, 1, 10, 1}, {1, 1, 1, 1}, {16, 32, 1, 0}}});
                }
            }
            rays.resetFrame();
            if (!rays.prepare(&worker, geometry))
                throw std::runtime_error("Synthetic water AS preparation failed");
            worker.commandList->begin();
            rays.record(&worker);
            worker.commandList->barriers(RenderBarrierStage::COMPUTE,
                                         RenderBufferBarrier(gpu.get(), RenderBufferAccess::WRITE));
            worker.commandList->setComputePipelineLayout(layout.get());
            worker.commandList->setPipeline(pipeline.get());
            worker.commandList->setComputeDescriptorSet(output.get(), 0);
            worker.commandList->setComputeDescriptorSet(rays.set->get(), 1);
            worker.commandList->dispatch(1 + lavaSamples / 4, 1, 1);
            worker.commandList->barriers(RenderBarrierStage::COPY,
                                         RenderBufferBarrier(gpu.get(), RenderBufferAccess::READ));
            worker.commandList->barriers(RenderBarrierStage::COPY,
                                         RenderBufferBarrier(cpu.get(), RenderBufferAccess::WRITE));
            worker.commandList->copyBufferRegion(cpu->at(0), gpu->at(0), bytes);
            worker.commandList->end();
            worker.execute();
            worker.wait();
            std::vector<uint32_t> values(bytes / sizeof(uint32_t));
            const RenderRange all(0, bytes), none(0, 0);
            auto* mapped = cpu->map(0, &all);
            if (!mapped)
                throw std::runtime_error("No probe readback");
            std::memcpy(values.data(), mapped, bytes);
            cpu->unmap(0, &none);
            if (values[0] != 1 || values[1] != uint32_t((pass < 2 ? z : 10) * 1000) || values[2] != 0 ||
                values[3] != (pass < 2 ? 0U : 1U) || values[4] != 0 || values[8] != 0 || values[12] != 1 ||
                values[13] != uint32_t(z * 1000) || values[15] != 0) {
                std::fprintf(
                    stderr,
                    "Ray readback pass=%u: centre=%u,%u,%u,%u outside=%u,%u,%u,%u back=%u,%u,%u,%u cutout=%u,%u,%u,%u\n",
                    pass, values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
                    values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15]);
                throw std::runtime_error("Hardware ray intersection/readback mismatch");
            }
            for (unsigned i = 0; i < 3; ++i) {
                const float a = std::bit_cast<float>(values[16 + i]), b = std::bit_cast<float>(values[20 + i]);
                if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > 0.03f)
                    throw std::runtime_error("Waterfall film temporal continuity failed");
                for (unsigned result : {6u, 7u})
                    if (std::abs(std::bit_cast<float>(values[result * 4 + i]) - a) > 0.0001f)
                        throw std::runtime_error("Authored flow UV repeat/positive advection continuity failed");
                if (std::abs(std::bit_cast<float>(values[32 + i]) - std::bit_cast<float>(values[36 + i])) > 0.0001f)
                    throw std::runtime_error("Mirrored downhill flow direction failed");
                if (std::abs(std::bit_cast<float>(values[40 + i]) - (i + 1) * 0.2f) > 0.0001f)
                    throw std::runtime_error("Unqualified flowing-water mapping did not preserve original RGB");
            }
            for (unsigned i = 0; i < 4; ++i) {
                const float a = std::bit_cast<float>(values[44 + i]), b = std::bit_cast<float>(values[48 + i]);
                const float advected = std::bit_cast<float>(values[52 + i]),
                            filtered = std::bit_cast<float>(values[56 + i]);
                if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(filtered) || std::abs(a - b) > 0.08f ||
                    std::abs(a - advected) > 0.08f)
                    throw std::runtime_error("Molten lava continuity, flow or distant filtering failed");
            }
            if (std::bit_cast<float>(values[44]) <= std::bit_cast<float>(values[45]) ||
                std::bit_cast<float>(values[45]) <= std::bit_cast<float>(values[46]))
                throw std::runtime_error("Molten lava lost its emissive warm spectrum");
            const float blocked = std::bit_cast<float>(values[60]), open = std::bit_cast<float>(values[64]);
            const float pulsed = std::bit_cast<float>(values[68]), off = std::bit_cast<float>(values[72]);
            if (blocked != 0 || off != 0 || !std::isfinite(open) || open < 0.5f ||
                std::abs(pulsed / open - 0.7f) > 0.001f)
                throw std::runtime_error("Lava light occlusion, pulse or zero-intensity regression");
            float meanLight = 0, meanEvolution = 0, maxStep = 0, minGreen = 1, maxGreen = 0;
            unsigned bright = 0, hot = 0, dark = 0, worstSample = 0, worstComponent = 0;
            std::ofstream preview;
            if (pass == 0 && argc > 2) {
                preview.open(argv[2], std::ios::binary);
                if (!preview)
                    throw std::runtime_error("Cannot open headless lava preview");
                // Hot and cool endpoints at the SAME world positions/time,
                // followed by minimum-brightness cool lava.
                preview << "P6\n128 512\n255\n";
            }
            for (unsigned sample = 0; sample < lavaSamples; sample++) {
                const auto offset = (19 + sample * 6) * 4;
                std::array<float, 3> color{};
                for (unsigned i = 0; i < 3; i++) {
                    const float now = std::bit_cast<float>(values[offset + i]);
                    const float next = std::bit_cast<float>(values[offset + 4 + i]);
                    const float later = std::bit_cast<float>(values[offset + 8 + i]);
                    if (!std::isfinite(now) || !std::isfinite(next) || !std::isfinite(later) || now < 0 || now > 1)
                        throw std::runtime_error("Non-finite or out-of-range lava display output");
                    color[i] = now;
                    if (std::abs(now - next) > maxStep) {
                        maxStep = std::abs(now - next);
                        worstSample = sample;
                        worstComponent = i;
                    }
                    meanEvolution += std::abs(now - later) / (lavaSamples * 3);
                    if (preview.is_open())
                        preview.put(static_cast<char>(std::lround(now * 255)));
                }
                const float light = color[0] * 0.2126f + color[1] * 0.7152f + color[2] * 0.0722f;
                meanLight += light / lavaSamples;
                bright += light > 0.75f;
                dark += light < 0.35f;
                hot += color[1] > 0.90f && color[2] > 0.65f;
                minGreen = std::min(minGreen, color[1]);
                maxGreen = std::max(maxGreen, color[1]);
                if (color[0] < color[1] || color[1] < color[2])
                    throw std::runtime_error("Lava pool lost its hot yellow-orange palette");
                // Test the production palette function at the full cool
                // endpoint, including minimum surface brightness. The old
                // one-point/base-surface tests missed the fade-to-black bug.
                for (unsigned phase = 3; phase < 6; ++phase) {
                    const float r = std::bit_cast<float>(values[offset + phase * 4]);
                    const float g = std::bit_cast<float>(values[offset + phase * 4 + 1]);
                    const float b = std::bit_cast<float>(values[offset + phase * 4 + 2]);
                    const float linearHot = std::pow(color[0], 2.2f);
                    const float redEmission = linearHot / std::max(0.00001f, 1 - linearHot);
                    const float retainedRed = redEmission * (phase == 3 ? 0.972f : 0.92f) * (phase == 5 ? 0.25f : 1);
                    const float expectedRed = std::pow(retainedRed / (1 + retainedRed), 1.0f / 2.2f);
                    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || r < 0.25f || g <= 0 || b <= 0 ||
                        r > 1 || g > r || b > g || std::abs(r - expectedRed) > 0.0002f)
                        throw std::runtime_error("Lava palette endpoint goes black or loses warm self-emission");
                    if (phase == 4 && g >= color[1] * 0.85f)
                        throw std::runtime_error("Cooling lava fails to change from yellow-white to orange-red");
                }
            }
            if (preview.is_open())
                for (unsigned phase : {3u, 4u, 5u})
                    for (unsigned sample = 0; sample < lavaSamples; ++sample)
                        for (unsigned channel = 0; channel < 3; ++channel)
                            preview.put(static_cast<char>(std::lround(
                                std::bit_cast<float>(values[(19 + sample * 6 + phase) * 4 + channel]) * 255)));
            // The previous dark-crust material passed a one-point spectrum
            // test. Require molten coverage, bright cores and spatial detail.
            if (pass == 0)
                std::printf(
                    "Lava pool: mean brightness=%.3f bright=%.1f%% white-hot=%.1f%% dark=%.1f%% max-frame-step=%.4f evolution=%.4f\n",
                    meanLight, 100.0f * bright / lavaSamples, 100.0f * hot / lavaSamples, 100.0f * dark / lavaSamples,
                    maxStep, meanEvolution);
            if (maxStep > 0.06f)
                std::fprintf(stderr, "Lava worst step at grid %u,%u component=%u: now=%g next=%g\n", worstSample % 128,
                             worstSample / 128, worstComponent,
                             std::bit_cast<float>(values[(19 + worstSample * 6) * 4 + worstComponent]),
                             std::bit_cast<float>(values[(20 + worstSample * 6) * 4 + worstComponent]));
            if (meanLight < 0.78f || bright < lavaSamples * 0.80f || hot < lavaSamples * 0.02f ||
                dark > lavaSamples * 0.02f || maxGreen - minGreen < 0.12f || maxStep > 0.06f || meanEvolution < 0.015f)
                throw std::runtime_error(
                    "Lava pool brightness, molten coverage, detail or continuous convection regression");
        }
        std::puts(
            "PASS: real GPU molten lava shading: yellow-white/orange-red endpoints remain emissive even at minimum brightness; bright cores, relief and continuous convection preserved.");
        std::puts(
            "PASS: real GPU lava lighting: static occlusion, unblocked illumination, bounded smooth pulse and zero-intensity off.");
        std::puts(
            "PASS: real GPU ray hit/miss, transparent waterfall rejection, opaque-liquid hit, continuous film, static reuse and scene replacement; no gameplay or FPS claim.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
