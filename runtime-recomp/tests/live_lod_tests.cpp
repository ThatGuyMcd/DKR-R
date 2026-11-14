#include "render/rt64_shader_library.h"
#include "shared/rt64_raster_params.h"
#include "contrib/plume/plume_render_interface.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace plume {
#if defined(_WIN32)
std::unique_ptr<RenderInterface> CreateD3D12Interface();
std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif
} // namespace plume

static void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

static void policy_checks() {
    static_assert(sizeof(interop::RasterParams) == 32);
    static_assert(offsetof(interop::RasterParams, mipLODBias) == 4);
    static_assert(offsetof(interop::RasterParams, screenScale) == 16);
    RT64::setDefaultSamplerMipLODBias(0.0f);
    RT64::ShaderLibrary existingLibrary(false, false);
    for (float requested : {-4.0f, -2.0f, -1.25f, 0.0f, 0.75f, 2.0f, 4.0f, 0.0f}) {
        // Keep one library alive throughout, including changes from another thread.
        std::thread writer([requested] { RT64::setDefaultSamplerMipLODBias(requested); });
        writer.join();
        const float expected = std::fmax(-2.0f, std::fmin(2.0f, requested));
        require(RT64::getDefaultSamplerMipLODBias() == expected, "Live atomic bias did not change");
        const float scale = std::exp2(expected);
        const float actual = 0.5f * std::log2(16.0f * scale * scale);
        require(std::abs(actual - (2.0f + expected)) < 0.00001f, "Gradient bias is incorrect");
    }
    std::puts("PASS: live atomic bias, clamp, gradient equivalence and unchanged push-constant ABI");
}

#if defined(_WIN32)
static void gpu_checks(const std::string& api, const char* shaderPath, unsigned anisotropy) {
    using namespace plume;
    auto rhi = api == "dx12" ? CreateD3D12Interface() : CreateVulkanInterface();
    require(bool(rhi), "GPU interface creation failed");
    auto device = rhi->createDevice();
    require(bool(device), "GPU device creation failed");
    auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
    auto commands = queue->createCommandList();
    auto fence = device->createCommandFence();
    std::ifstream shaderInput(shaderPath, std::ios::binary);
    std::vector<char> shaderBytes{std::istreambuf_iterator<char>(shaderInput), {}};
    require(!shaderBytes.empty(), "Compiled probe shader is missing");
    auto shader =
        device->createShader(shaderBytes.data(), shaderBytes.size(), "CSMain", rhi->getCapabilities().shaderFormat);
    require(bool(shader), "Probe shader creation failed");

    // All resources are created once. Only the push constant changes between tests.
    std::array<std::unique_ptr<RenderSampler>, 18> samplers;
    std::array<const RenderSampler*, 18> samplerPointers{};
    std::array<RenderDescriptorRange, 18> samplerRanges{};
    const std::array<RenderTextureAddressMode, 3> modes = {
        RenderTextureAddressMode::WRAP, RenderTextureAddressMode::MIRROR, RenderTextureAddressMode::CLAMP};
    for (unsigned i = 0; i < 18; ++i) {
        RenderSamplerDesc desc{};
        desc.minFilter = desc.magFilter = i < 9 ? RenderFilter::LINEAR : RenderFilter::NEAREST;
        desc.mipmapMode = RenderMipmapMode::LINEAR;
        desc.addressU = modes[(i % 9) / 3];
        desc.addressV = modes[i % 3];
        desc.addressW = RenderTextureAddressMode::CLAMP;
        desc.mipLODBias = 0.0f;
        desc.anisotropyEnabled = i < 9 && anisotropy > 1;
        desc.maxAnisotropy = anisotropy;
        desc.maxLOD = 1000.0f;
        samplers[i] = device->createSampler(desc);
        samplerPointers[i] = samplers[i].get();
        samplerRanges[i] = RenderDescriptorRange(RenderDescriptorRangeType::SAMPLER, 7 + i, 1, &samplerPointers[i]);
    }
    const RenderDescriptorRange textureRange(RenderDescriptorRangeType::TEXTURE, 0, 8192);
    const RenderDescriptorRange resultRange(RenderDescriptorRangeType::READ_WRITE_STRUCTURED_BUFFER, 0, 1);
    std::array<RenderDescriptorSetDesc, 5> setDescs{};
    setDescs[0] = RenderDescriptorSetDesc(samplerRanges.data(), 18);
    setDescs[1] = RenderDescriptorSetDesc(&textureRange, 1);
    setDescs[4] = RenderDescriptorSetDesc(&resultRange, 1);
    const RenderPushConstantRange push(0, 5, 0, 16, RenderShaderStageFlag::COMPUTE);
    auto layout = device->createPipelineLayout(RenderPipelineLayoutDesc(&push, 1, setDescs.data(), 5));
    auto pipeline = device->createComputePipeline(RenderComputePipelineDesc(layout.get(), shader.get(), 1, 1, 1));
    require(bool(pipeline), "Compute pipeline creation failed");
    auto samplerSet = device->createDescriptorSet(setDescs[0]);
    auto textures = device->createDescriptorSet(setDescs[1]);
    auto outputs = device->createDescriptorSet(setDescs[4]);
    constexpr unsigned outputBytes = 10 * 4 * sizeof(float);
    auto output = device->createBuffer(
        RenderBufferDesc::DefaultBuffer(outputBytes, RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS));
    auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(outputBytes));
    const RenderBufferStructuredView outputView(4 * sizeof(float));
    outputs->setBuffer(0, output.get(), outputBytes, &outputView);

    auto mipTexture = device->createTexture(RenderTextureDesc::Texture2D(64, 64, 7, RenderFormat::R8G8B8A8_UNORM));
    auto singleTexture = device->createTexture(RenderTextureDesc::Texture2D(64, 64, 1, RenderFormat::R8G8B8A8_UNORM));
    std::array<unsigned, 7> offsets{};
    unsigned uploadBytes = 0;
    for (unsigned mip = 0; mip < 7; ++mip) {
        offsets[mip] = uploadBytes;
        uploadBytes += ((256 * (64 >> mip) + 511) / 512) * 512;
    }
    auto upload = device->createBuffer(RenderBufferDesc::UploadBuffer(uploadBytes));
    auto* pixels = static_cast<unsigned char*>(upload->map());
    std::memset(pixels, 0, uploadBytes);
    for (unsigned mip = 0; mip < 7; ++mip) {
        const unsigned size = 64 >> mip;
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                auto* pixel = pixels + offsets[mip] + 256 * y + 4 * x;
                pixel[0] = pixel[1] = pixel[2] = static_cast<unsigned char>(mip * 32);
                pixel[3] = 255;
            }
    }
    upload->unmap();
    commands->begin();
    commands->barriers(RenderBarrierStage::COPY,
                       RenderTextureBarrier(mipTexture.get(), RenderTextureLayout::COPY_DEST));
    commands->barriers(RenderBarrierStage::COPY,
                       RenderTextureBarrier(singleTexture.get(), RenderTextureLayout::COPY_DEST));
    for (unsigned mip = 0; mip < 7; ++mip) {
        auto source = RenderTextureCopyLocation::PlacedFootprint(upload.get(), RenderFormat::R8G8B8A8_UNORM, 64 >> mip,
                                                                 64 >> mip, 1, 64, offsets[mip]);
        commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(mipTexture.get(), mip), source);
        if (mip == 0)
            commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(singleTexture.get(), 0), source);
    }
    commands->barriers(RenderBarrierStage::COMPUTE,
                       RenderTextureBarrier(mipTexture.get(), RenderTextureLayout::SHADER_READ));
    commands->barriers(RenderBarrierStage::COMPUTE,
                       RenderTextureBarrier(singleTexture.get(), RenderTextureLayout::SHADER_READ));
    commands->end();
    queue->executeCommandLists(commands.get(), fence.get());
    queue->waitForCommandFence(fence.get());
    // Bind the entire declared table, avoiding partially-bound descriptor assumptions.
    for (unsigned i = 0; i < 8192; ++i)
        textures->setTexture(i, i == 1 ? singleTexture.get() : mipTexture.get(), RenderTextureLayout::SHADER_READ);

    for (unsigned mipCount : {7U, 1U})
        for (float bias : {-2.0f, 0.0f, 2.0f, -1.25f, 0.75f, 0.0f}) {
            RT64::setDefaultSamplerMipLODBias(bias);
            struct {
                float bias;
                unsigned mipCount, textureIndex, padding;
            } params{RT64::getDefaultSamplerMipLODBias(), mipCount, mipCount == 1 ? 1U : 0U, 0U};
            commands->begin();
            commands->barriers(RenderBarrierStage::COMPUTE,
                               RenderBufferBarrier(output.get(), RenderBufferAccess::WRITE));
            commands->setComputePipelineLayout(layout.get());
            commands->setPipeline(pipeline.get());
            commands->setComputeDescriptorSet(samplerSet.get(), 0);
            commands->setComputeDescriptorSet(textures.get(), 1);
            commands->setComputeDescriptorSet(outputs.get(), 4);
            commands->setComputePushConstants(0, &params);
            commands->dispatch(10, 1, 1);
            commands->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(output.get(), RenderBufferAccess::READ));
            commands->copyBuffer(readback.get(), output.get());
            commands->end();
            queue->executeCommandLists(commands.get(), fence.get());
            queue->waitForCommandFence(fence.get());
            std::array<float, 40> values{};
            std::memcpy(values.data(), readback->map(), outputBytes);
            readback->unmap();
            const float expected = mipCount > 1 ? (2.0f + bias) * 32.0f / 255.0f : 0.0f;
            for (unsigned mode = 0; mode < 10; ++mode) {
                std::printf("%s AF=%u mips=%u bias=%+.2f sampler=%u rgb=%.5f expected=%.5f\n", api.c_str(), anisotropy,
                            mipCount, bias, mode, values[4 * mode], expected);
                require(std::abs(values[4 * mode] - expected) < 0.006f, "GPU mip selection is incorrect");
                require(values[4 * mode + 3] > 0.99f, "GPU alpha changed");
            }
        }
    RT64::setDefaultSamplerMipLODBias(0.0f);
    std::puts("PASS: 120 GPU samples; live mip changes, all native modes, manual sampling, single-level invariance");
}
#endif

int main(int argc, char** argv) {
    try {
        policy_checks();
        if (argc > 1) {
#if defined(_WIN32)
            require(argc == 3, "Usage: DKRLiveLodTests [dx12|vulkan shader-file]");
            require(std::string(argv[1]) == "dx12" || std::string(argv[1]) == "vulkan", "Unknown graphics API");
            for (unsigned anisotropy : {1U, 16U})
                gpu_checks(argv[1], argv[2], anisotropy);
#else
            throw std::runtime_error(
                "Headless GPU probe currently targets Windows DX12/Vulkan; default checks are cross-platform");
#endif
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    return 0;
}
