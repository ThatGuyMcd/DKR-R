#pragma once
#include "render/rt64_texture_cache.h"
#include <cmath>
// Real production upload/generation + actual TextureSampler.hlsli, immutable samplers.
static void sampling_checks(plume::RenderInterface* rhi, plume::RenderDevice* device,
                            const RT64::ShaderRecord& generator, const char* path) {
    using namespace plume;
    using namespace RT64;
    std::ifstream in(std::string(path) + ".sampling", std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
    require(!bytes.empty(), "compiled sampling probe");
    auto shader = device->createShader(bytes.data(), bytes.size(), "CSMain", rhi->getCapabilities().shaderFormat);
    auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
    auto cmd = queue->createCommandList();
    auto fence = device->createCommandFence();
    std::vector<uint8_t> pixels(64 * 64 * 4);
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x) {
            auto* p = &pixels[(y * 64 + x) * 4];
            p[0] = p[1] = p[2] = ((x / 4 + y / 4) % 2) ? 0 : 255;
            p[3] = 255;
        }
    Texture original, png, base;
    std::unique_ptr<RenderBuffer> upOriginal, upPNG, upBase;
    GeneratedMips::Scratch scratch;
    cmd->begin();
    TextureCache::setRGBA32(&original, device, cmd.get(), pixels.data(), pixels.size(), 64, 64, 256, upOriginal);
    require(GeneratedMips::record(original, device, cmd.get(), generator, scratch), "native generation for live probe");
    require(GeneratedMips::uploadRGBA32(png, device, cmd.get(), pixels.data(), pixels.size(), 64, 64, 256, upPNG),
            "PNG generation for live probe");
    TextureCache::setRGBA32(&base, device, cmd.get(), pixels.data(), pixels.size(), 64, 64, 256, upBase);
    for (auto* t : {original.texture.get(), png.texture.get(), base.texture.get()})
        cmd->barriers(RenderBarrierStage::COMPUTE, RenderTextureBarrier(t, RenderTextureLayout::SHADER_READ));
    cmd->end();
    queue->executeCommandLists(cmd.get(), fence.get());
    queue->waitForCommandFence(fence.get());
    unsigned checks = 0;
    for (unsigned af : {1U, 16U}) {
        std::array<std::unique_ptr<RenderSampler>, 18> samplers;
        std::array<const RenderSampler*, 18> pointers{};
        std::array<RenderDescriptorRange, 18> ranges{};
        const RenderTextureAddressMode modes[]{RenderTextureAddressMode::WRAP, RenderTextureAddressMode::MIRROR,
                                               RenderTextureAddressMode::CLAMP};
        for (unsigned i = 0; i < 18; ++i) {
            RenderSamplerDesc d{};
            d.minFilter = d.magFilter = i < 9 ? RenderFilter::LINEAR : RenderFilter::NEAREST;
            d.mipmapMode = RenderMipmapMode::LINEAR;
            d.addressU = modes[(i % 9) / 3];
            d.addressV = modes[i % 3];
            d.addressW = RenderTextureAddressMode::CLAMP;
            d.mipLODBias = 0;
            d.maxLOD = 1000;
            d.anisotropyEnabled = i < 9 && af > 1;
            d.maxAnisotropy = af;
            samplers[i] = device->createSampler(d);
            pointers[i] = samplers[i].get();
            ranges[i] = RenderDescriptorRange(RenderDescriptorRangeType::SAMPLER, 7 + i, 1, &pointers[i]);
        }
        const RenderDescriptorRange textureRange(RenderDescriptorRangeType::TEXTURE, 0, 8192),
            outputRange(RenderDescriptorRangeType::READ_WRITE_STRUCTURED_BUFFER, 0, 1);
        std::array<RenderDescriptorSetDesc, 5> descs{};
        descs[0] = RenderDescriptorSetDesc(ranges.data(), 18);
        descs[1] = RenderDescriptorSetDesc(&textureRange, 1);
        descs[4] = RenderDescriptorSetDesc(&outputRange, 1);
        const RenderPushConstantRange push(0, 5, 0, 16, RenderShaderStageFlag::COMPUTE);
        auto layout = device->createPipelineLayout(RenderPipelineLayoutDesc(&push, 1, descs.data(), 5));
        auto pipeline = device->createComputePipeline(RenderComputePipelineDesc(layout.get(), shader.get(), 1, 1, 1));
        auto samplerSet = device->createDescriptorSet(descs[0]), textureSet = device->createDescriptorSet(descs[1]),
             outputSet = device->createDescriptorSet(descs[4]);
        for (unsigned i = 0; i < 8192; ++i)
            textureSet->setTexture(i,
                                   i == 1   ? png.texture.get()
                                   : i == 2 ? base.texture.get()
                                            : original.texture.get(),
                                   RenderTextureLayout::SHADER_READ);
        auto output = device->createBuffer(
            RenderBufferDesc::DefaultBuffer(160, RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS));
        auto read = device->createBuffer(RenderBufferDesc::ReadbackBuffer(160));
        const RenderBufferStructuredView view(16);
        outputSet->setBuffer(0, output.get(), 160, &view);
        auto sample = [&](float bias, unsigned index, unsigned scenario) {
            const struct {
                float bias;
                unsigned index, scenario, pad;
            } p{bias, index, scenario, 0};
            cmd->begin();
            cmd->barriers(RenderBarrierStage::COMPUTE, RenderBufferBarrier(output.get(), RenderBufferAccess::WRITE));
            cmd->setComputePipelineLayout(layout.get());
            cmd->setPipeline(pipeline.get());
            cmd->setComputeDescriptorSet(samplerSet.get(), 0);
            cmd->setComputeDescriptorSet(textureSet.get(), 1);
            cmd->setComputeDescriptorSet(outputSet.get(), 4);
            cmd->setComputePushConstants(0, &p);
            cmd->dispatch(10, 1, 1);
            cmd->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(output.get(), RenderBufferAccess::READ));
            cmd->copyBuffer(read.get(), output.get());
            cmd->end();
            queue->executeCommandLists(cmd.get(), fence.get());
            queue->waitForCommandFence(fence.get());
            std::array<float, 40> result{};
            std::memcpy(result.data(), read->map(), 160);
            read->unmap();
            return result;
        };
        for (unsigned scenario = 0; scenario < 10; ++scenario) {
            auto baseline = sample(0, 2, scenario);
            for (float bias : {-2.f, -1.25f, 0.f, .75f, 2.f, 0.f}) {
                auto native = sample(bias, 0, scenario), replacement = sample(bias, 1, scenario);
                for (unsigned n = 0; n < 40; ++n) {
                    require(std::abs(native[n] - replacement[n]) < .006f, "native/PNG mip sampling mismatch");
                    if (scenario > 0)
                        require(std::abs(native[n] - baseline[n]) < .006f, "excluded draw changed base pixels");
                    ++checks;
                }
            }
        }
        auto sharp = sample(-2, 0, 0), soft = sample(2, 0, 0);
        for (unsigned mode = 0; mode < 10; ++mode)
            require(std::abs(sharp[mode * 4] - soft[mode * 4]) > .15f,
                    "generated chain has no visible live bias response");
    }
    std::printf(
        "PASS generated live sampling: %u channel checks, all 10 samplers at AF 1/16, 9 exclusions, fractional bias, native/PNG match\n",
        checks);
}
