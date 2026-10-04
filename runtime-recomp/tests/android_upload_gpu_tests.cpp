#include "render/rt64_buffer_uploader.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace plume {
#if PLUME_SDL_VULKAN_ENABLED
std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow window);
#else
std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif
}
int main() {
    using namespace RT64;
#if PLUME_SDL_VULKAN_ENABLED
    auto renderer = plume::CreateVulkanInterface(nullptr);
#else
    auto renderer = plume::CreateVulkanInterface();
#endif
    auto device = renderer->createDevice();
    if (!device) throw std::runtime_error("No Vulkan device");
    RenderWorker worker(device.get(), "Upload qualification", RenderCommandListType::DIRECT);
    BufferUploader uploader(device.get());
    BufferPair pair;
    const RenderRange none{0,0};
    // Tiny inline, exact boundary, large asynchronous, buffer growth, partial
    // changes and repeated reuse all use the actual staged uploader code.
    for (const std::size_t count : {16U, 16384U, 65536U, 131072U, 262144U}) {
        std::vector<std::uint8_t> expected(count);
        for (unsigned pass = 0; pass < 3; ++pass) {
            std::size_t first = pass == 1 ? count / 3 : 0;
            for (std::size_t i = first; i < count; ++i) expected[i] = std::uint8_t(i * 37 + pass * 91);
            uploader.submit(&worker, {{expected.data(), {first, count}, 1, RenderBufferFlag::STORAGE, {}, &pair}});
            uploader.wait();
            auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(count));
            worker.commandList->begin();
            uploader.commandListBeforeBarriers(&worker);
            uploader.commandListCopyResources(&worker);
            uploader.commandListAfterBarriers(&worker);
            RenderBufferBarrier barriers[] = {{pair.defaultBuffer.get(), RenderBufferAccess::READ},
                                              {readback.get(), RenderBufferAccess::WRITE}};
            worker.commandList->barriers(RenderBarrierStage::COPY, barriers, 2);
            worker.commandList->copyBufferRegion(readback->at(0), pair.defaultBuffer->at(0), count);
            worker.commandList->end(); worker.execute(); worker.wait();
            const RenderRange all{0, count};
            const auto* result = readback->map(0, &all);
            const bool matches = result && std::memcmp(result, expected.data(), count) == 0;
            readback->unmap(0, &none);
            if (!matches) throw std::runtime_error("Upload/readback mismatch");
        }
    }
    std::vector<std::uint8_t> bytes(16384, 71);
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 10000; ++i) {
        uploader.submit(&worker, {{bytes.data(), {0, bytes.size()}, 1, RenderBufferFlag::STORAGE, {}, &pair}});
        uploader.wait();
    }
    std::printf("PASS: small/large/partial/GPU readback, 10000 small submit/waits %.3f ms (microbenchmark, not gameplay FPS)\n",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
