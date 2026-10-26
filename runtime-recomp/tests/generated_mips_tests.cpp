#include "render/rt64_generated_mips.h"
#include "render/rt64_generated_mip_scope.h"
#include "render/rt64_raster_shader.h"
#include <SDL.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace plume {
#ifdef _WIN32
std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
#ifdef PLUME_SDL_VULKAN_ENABLED
std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow window);
#else
std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif
} // namespace plume
static void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
#include "generated_mip_sampling_tests.hpp"
using namespace RT64;
static GeneratedMips::Level fixture(unsigned w, unsigned h, unsigned kind) {
    GeneratedMips::Level l{w, h, {}};
    l.pixels.resize(size_t(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            auto* p = &l.pixels[(size_t(y) * w + x) * 4];
            p[0] = (x * 29 + y * 71) % 256;
            p[1] = (x * 111 + y * 17) % 256;
            p[2] = (x * 13 + y * 47) % 256;
            p[3] = kind == 0 ? 255 : kind == 1 ? (x * 37 + y * 51) % 256 : kind == 2 ? ((x + y) % 3 ? 255 : 0) : 0;
        }
    return l;
}
static void cpu() {
    // Exercise the shipping Vulkan specialization path, not only the dynamic
    // compute fixture. re-spirv cannot sort loop-carried dependencies.
    {
        OptimizerCacheSPIRV cache;
        cache.initialize();
        respv::Shader* shaders[] = {&cache.rasterVS,   &cache.rasterVSFlat, &cache.rasterPS,
                                    &cache.rasterPSMS, &cache.rasterPSFlat, &cache.rasterPSFlatMS};
        unsigned cases = 0;
        for (auto* shader : shaders) {
            require(shader->sort(), "shipping SPIR-V shader graph must be acyclic");
            for (uint32_t mode : {0U, 0x00552078U, 0x00553078U, 0x005049d8U}) {
                std::vector<respv::SpecConstant> constants = {
                    {0, {mode}}, {1, {0x00082000U}}, {2, {0xfffcf279U}}, {3, {0x00127e03U}}, {4, {0U}}};
                std::vector<uint8_t> output;
                require(respv::Optimizer::run(*shader, constants.data(), uint32_t(constants.size()), output),
                        "shipping SPIR-V specialization succeeds");
                require(!output.empty(), "specialized shader is not empty");
                ++cases;
            }
        }
        std::printf("PASS %u shipping SPIR-V specializations (smooth/flat, MSAA/non-MSAA)\n", cases);
    }
    {
        std::vector<interop::float4x4> models{interop::float4x4::identity()};
        require(!generatedMipPerspectiveScope(false, models, 0, 0), "affine HUD scope excluded");
        models[0][3][0] = 120.0f;
        require(!generatedMipPerspectiveScope(false, models, 0, 0), "translated HUD remains affine");
        models[0][2][3] = -0.5f;
        models[0][3][3] = 42.0f;
        require(generatedMipPerspectiveScope(false, models, 0, 0),
                "DKR combined camera MVP recognized with identity projection");
        models.push_back(interop::float4x4::identity());
        require(!generatedMipPerspectiveScope(false, models, 0, 1), "mixed HUD/world draw conservatively excluded");
        require(!generatedMipPerspectiveScope(false, models, 0, 2), "invalid matrix range excluded");
        require(!generatedMipPerspectiveScope(false, models, 65535, 0), "empty matrix range excluded");
        require(generatedMipPerspectiveScope(true, models, 0, 0), "ordinary separate perspective projection supported");
    }
    beginGeneratedMipSession(false);
    setGeneratedMipSampling(true);
    require(!generatedMipSamplingEnabled(), "default/session off must disable sampling");
    beginGeneratedMipSession(true);
    setGeneratedMipSampling(false);
    {
        MipActivity pending(3);
        require(!mipLoadingVisible(), "quick uploads do not flash a modal");
        MipConfiguration::startedMs = mipClockMs() - 300;
        require(mipLoadingVisible(), "prolonged activity shows loading feedback");
        require(MipConfiguration::pending == 3, "pending work counter");
    }
    require(!mipLoadingVisible() && MipConfiguration::completed == 3, "completed activity dismisses feedback");
    unsigned callbacks = 0;
    {
        MipWaitFeedbackScope outer([&] { callbacks += 1; });
        {
            MipWaitFeedbackScope inner([&] { callbacks += 10; });
            MipConfiguration::waitFeedback();
        }
        MipConfiguration::waitFeedback();
    }
    require(callbacks == 11 && !MipConfiguration::waitFeedback, "thread-local wait callback scopes restore ownership");
    require(!generatedMipSamplingEnabled(), "Accurate gate");
    setGeneratedMipSampling(true);
    require(generatedMipSamplingEnabled(), "Modern active gate");
    recordMipUpload(true, 7, false);
    recordMipUpload(true, 7, true);
    recordMipUpload(false, 7, true);
    recordMipUpload(false, 1, false);
    auto stats = generatedMipStatistics();
    require(stats.originals == 1 && stats.replacements == 1 && stats.authored == 1 && stats.singleLevel == 1,
            "diagnostic classification");
    beginGeneratedMipSession(false);
    require(generatedMipStatistics().originals == 0, "session counters reset");
    {
        TextureMap map;
        auto* native = new Texture();
        native->width = 64;
        native->height = 32;
        native->mipmaps = 7;
        native->generatedMipmaps = true;
        map.add(123, 0, native);
        uint32_t index = 0;
        interop::float2 scale;
        interop::float3 dimensions;
        bool replaced = false, hasMips = false, shift = false, generated = false;
        require(map.use(123, 1, index, scale, dimensions, replaced, hasMips, shift, generated),
                "native texture lookup");
        require(!replaced && hasMips && generated && dimensions.z == 7 && scale.x == 1,
                "native identity independent of mip availability");
    }
    require(GeneratedMips::levelCount(0, 8) == 1, "zero size");
    require(GeneratedMips::levelCount(1, 1) == 1, "one texel");
    require(GeneratedMips::levelCount(1, 64) == 7, "skinny chain");
    require(GeneratedMips::levelCount(0xffffffffU, 0xffffffffU) == 1, "overflow guard");
    GeneratedMips::Level edge{2, 1, {255, 0, 0, 255, 0, 255, 0, 0}};
    auto mip = GeneratedMips::downsample(edge);
    require(mip.pixels == std::vector<uint8_t>({255, 0, 0, 128}), "alpha-weighted RGB/mean alpha");
    auto odd = fixture(3, 3, 0);
    auto reduced = GeneratedMips::downsample(odd);
    require(reduced.width == 1 && reduced.height == 1, "odd dimensions");
    unsigned total = 0;
    for (unsigned i = 0; i < 9; ++i)
        total += odd.pixels[i * 4];
    require(reduced.pixels[0] == (total + 4) / 9, "odd final row/column included");
    std::puts("PASS generated-mip CPU dimension, overflow, alpha and odd-edge policies");
}
static void gpu(const std::string& api, const char* path) {
    using namespace plume;
    std::unique_ptr<RenderInterface> rhi;
#ifdef _WIN32
    if (api == "dx12")
        rhi = CreateD3D12Interface();
    else
#endif
    {
#ifdef PLUME_SDL_VULKAN_ENABLED
        require(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, "SDL video for Linux Vulkan probe");
        static SDL_Window* probeWindow =
            SDL_CreateWindow("DKR mip probe", 0, 0, 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
        require(probeWindow != nullptr, "hidden Vulkan probe window");
        rhi = CreateVulkanInterface(probeWindow);
#else
        rhi = CreateVulkanInterface();
#endif
    }
    require(bool(rhi), "RHI");
    auto device = rhi->createDevice();
    require(bool(device), "device");
    auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
    auto cmd = queue->createCommandList();
    auto fence = device->createCommandFence();
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
    require(!bytes.empty(), "compiled generator shader");
    auto shader = device->createShader(bytes.data(), bytes.size(), "CSMain", rhi->getCapabilities().shaderFormat);
    TextureDecodeDescriptorSet desc;
    RenderPipelineLayoutBuilder builder;
    builder.begin();
    builder.addPushConstant(0, 0, 16, RenderShaderStageFlag::COMPUTE);
    builder.addDescriptorSet(desc);
    builder.end();
    ShaderRecord record;
    record.pipelineLayout = builder.create(device.get());
    record.pipeline =
        device->createComputePipeline(RenderComputePipelineDesc(record.pipelineLayout.get(), shader.get(), 8, 8, 1));
    require(bool(record.pipeline), "generation pipeline");
    std::ifstream readInput(std::string(path) + ".readback", std::ios::binary);
    std::vector<char> readBytes{std::istreambuf_iterator<char>(readInput), {}};
    require(!readBytes.empty(), "compiled readback shader");
    auto readShader =
        device->createShader(readBytes.data(), readBytes.size(), "CSMain", rhi->getCapabilities().shaderFormat);
    const RenderDescriptorRange ranges[]{{RenderDescriptorRangeType::TEXTURE, 1, 1},
                                         {RenderDescriptorRangeType::READ_WRITE_STRUCTURED_BUFFER, 2, 1}};
    const RenderDescriptorSetDesc readDesc(ranges, 2);
    const RenderPushConstantRange push(0, 0, 0, 16, RenderShaderStageFlag::COMPUTE);
    auto readLayout = device->createPipelineLayout(RenderPipelineLayoutDesc(&push, 1, &readDesc, 1));
    auto readPipeline =
        device->createComputePipeline(RenderComputePipelineDesc(readLayout.get(), readShader.get(), 8, 8, 1));
    unsigned checks = 0;
    for (auto dims : {std::pair<unsigned, unsigned>{1, 1}, {1, 31}, {31, 1}, {3, 5}, {7, 9}, {32, 32}, {64, 16}})
        for (unsigned kind = 0; kind < 4; ++kind) {
            std::vector<GeneratedMips::Level> levels{fixture(dims.first, dims.second, kind)};
            while (levels.back().width > 1 || levels.back().height > 1)
                levels.emplace_back(GeneratedMips::downsample(levels.back()));
            Texture texture;
            texture.width = dims.first;
            texture.height = dims.second;
            texture.mipmaps = 1;
            texture.format = RenderFormat::R8G8B8A8_UNORM;
            texture.texture = device->createTexture(
                RenderTextureDesc::Texture2D(texture.width, texture.height, 1, texture.format,
                                             RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS));
            const uint64_t basePitch = (texture.width * 4 + 255) & ~255U;
            auto upload = device->createBuffer(RenderBufferDesc::UploadBuffer(basePitch * texture.height));
            auto* p = static_cast<uint8_t*>(upload->map());
            for (unsigned y = 0; y < texture.height; ++y)
                std::memcpy(p + y * basePitch, levels[0].pixels.data() + y * texture.width * 4, texture.width * 4);
            upload->unmap();
            std::vector<uint64_t> offsets;
            uint64_t size = 0;
            for (auto& l : levels) {
                offsets.push_back(size);
                size += (GeneratedMips::levelBytes(l.width, l.height) + 511) & ~511ULL;
            }
            auto read = device->createBuffer(RenderBufferDesc::ReadbackBuffer(size));
            auto output = device->createBuffer(
                RenderBufferDesc::DefaultBuffer(size, RenderBufferFlag::STORAGE | RenderBufferFlag::UNORDERED_ACCESS));
            auto readSet = device->createDescriptorSet(readDesc);
            RenderBufferStructuredView outputView(4);
            readSet->setBuffer(1, output.get(), size, &outputView);
            GeneratedMips::Scratch scratch;
            cmd->begin();
            cmd->barriers(RenderBarrierStage::COPY,
                          RenderTextureBarrier(texture.texture.get(), RenderTextureLayout::COPY_DEST));
            cmd->copyTextureRegion(RenderTextureCopyLocation::Subresource(texture.texture.get()),
                                   RenderTextureCopyLocation::PlacedFootprint(upload.get(), texture.format,
                                                                              texture.width, texture.height, 1,
                                                                              unsigned(basePitch / 4)));
            const bool generated = GeneratedMips::record(texture, device.get(), cmd.get(), record, scratch);
            require(generated == (levels.size() > 1), "record generation fallback");
            require(texture.mipmaps == levels.size(), "published mip count");
            readSet->setTexture(0, texture.texture.get(), RenderTextureLayout::SHADER_READ);
            cmd->barriers(RenderBarrierStage::COMPUTE,
                          RenderTextureBarrier(texture.texture.get(), RenderTextureLayout::SHADER_READ));
            cmd->barriers(RenderBarrierStage::COMPUTE, RenderBufferBarrier(output.get(), RenderBufferAccess::WRITE));
            cmd->setComputePipelineLayout(readLayout.get());
            cmd->setPipeline(readPipeline.get());
            cmd->setComputeDescriptorSet(readSet.get(), 0);
            for (unsigned m = 0; m < levels.size(); ++m) {
                auto& l = levels[m];
                const unsigned params[]{l.width, l.height, m, unsigned(offsets[m])};
                cmd->setComputePushConstants(0, params);
                cmd->dispatch((l.width + 7) / 8, (l.height + 7) / 8, 1);
            }
            cmd->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(output.get(), RenderBufferAccess::READ));
            cmd->copyBuffer(read.get(), output.get());
            cmd->end();
            queue->executeCommandLists(cmd.get(), fence.get());
            queue->waitForCommandFence(fence.get());
            const auto* result = static_cast<const uint8_t*>(read->map());
            for (unsigned m = 0; m < levels.size(); ++m) {
                auto& l = levels[m];
                auto pitch = (l.width * 4 + 255) & ~255U;
                for (unsigned y = 0; y < l.height; ++y)
                    for (unsigned x = 0; x < l.width * 4; ++x) {
                        if (result[offsets[m] + y * pitch + x] != l.pixels[y * l.width * 4 + x]) {
                            std::fprintf(stderr, "Mismatch %ux%u kind%u mip%u row%u byte%u got%u expected%u\n",
                                         texture.width, texture.height, kind, m, y, x,
                                         result[offsets[m] + y * pitch + x], l.pixels[y * l.width * 4 + x]);
                            throw std::runtime_error("GPU pixel mismatch");
                        }
                        ++checks;
                    }
            }
            read->unmap();
        }
    std::printf("PASS %s generated mip GPU readback: %u exact byte comparisons (base included)\n", api.c_str(), checks);
    sampling_checks(rhi.get(), device.get(), record, path);
}
int main(int argc, char** argv) {
    try {
        cpu();
        if (argc == 3)
            gpu(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
