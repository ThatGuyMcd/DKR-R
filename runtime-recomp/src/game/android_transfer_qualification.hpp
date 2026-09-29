#pragma once
#include "render/rt64_render_worker.h"
#include "graphics_health.hpp"
#include <array>
#include <cstdio>
#include <cstring>

namespace dkr::runtime::android_graphics {
inline void qualify_transfer(RT64::RenderWorker& worker) {
    using namespace RT64;
    constexpr std::size_t bytes = 16384;
    auto upload = worker.device->createBuffer(RenderBufferDesc::UploadBuffer(bytes));
    auto readback = worker.device->createBuffer(RenderBufferDesc::ReadbackBuffer(bytes));
    const RenderRange none{0, 0}, whole{0, bytes};
    // Two different patterns also exercise reuse; no shaders, ROM or user data.
    for (unsigned pass = 0; pass < 2; ++pass) {
        std::array<unsigned char, bytes> expected{};
        for (std::size_t i = 0; i < bytes; ++i) expected[i] = static_cast<unsigned char>((i * 37 + pass * 113) ^ (i >> 7));
        auto* dst = upload->map(0, &none);
        if (!dst) graphics_health::quarantine_resource(graphics_health::Stage::BufferMap, -5);
        std::memcpy(dst, expected.data(), bytes);
        upload->unmap(0, &whole);
        worker.commandList->begin();
        const RenderBufferBarrier barriers[] = {{upload.get(), RenderBufferAccess::READ}, {readback.get(), RenderBufferAccess::WRITE}};
        worker.commandList->barriers(RenderBarrierStage::COPY, barriers, 2);
        worker.commandList->copyBuffer(readback.get(), upload.get());
        worker.commandList->end();
        worker.execute(); worker.wait();
        if (graphics_health::failed()) graphics_health::quarantine_resource(graphics_health::Stage::Fence, -4);
        const auto* result = readback->map(0, &whole);
        if (!result) graphics_health::quarantine_resource(graphics_health::Stage::BufferMap, -5);
        const bool matched = std::memcmp(result, expected.data(), bytes) == 0;
        readback->unmap(0, &none);
        if (!matched) {
            std::fprintf(stderr, "[Android] GPU upload/readback qualification mismatch pass=%u\n", pass);
            graphics_health::quarantine_resource(graphics_health::Stage::BufferMap, -3);
        }
    }
    std::fprintf(stderr, "[Android] GPU upload/readback qualification passed: two 16 KiB patterns\n");
}
// Keep startup qualification buffer-only. Plume's current Vulkan backend does
// not implement texture-to-buffer copyTextureRegion; probing that direction
// caused the 1050022 startup crash. Rendering/presentation needs device testing.
}
