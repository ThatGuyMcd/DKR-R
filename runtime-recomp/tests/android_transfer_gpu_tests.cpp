#include "android_transfer_qualification.hpp"
#include "performance_trace.hpp"
#include <cstdlib>
#include <stdexcept>

namespace plume {
#if PLUME_SDL_VULKAN_ENABLED
std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow window);
#else
std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif
}

int main() {
    using namespace dkr::runtime;
    graphics_health::reset();
    graphics_health::resource_failure_reporter.store([](graphics_health::Stage, std::int32_t) {
        std::fputs("FAIL: buffer transfer qualification reported a resource error\n", stderr);
        std::_Exit(1); // Isolated test only; no parked worker can hide a failure.
    });
    // Match the reported ordering: recording is enabled in the launcher before
    // the Vulkan device/workers are created. This isolates the native buffer
    // startup path; it does not exercise Android Activity/Java sampling.
    const auto baseline = performance_trace::snapshot();
    performance_capture::start();
#if PLUME_SDL_VULKAN_ENABLED
    auto renderer = plume::CreateVulkanInterface(nullptr); // Buffer-only; no surface needed.
#else
    auto renderer = plume::CreateVulkanInterface();
#endif
    if (!renderer) throw std::runtime_error("Vulkan interface unavailable");
    auto device = renderer->createDevice();
    if (!device) throw std::runtime_error("Vulkan device unavailable");
    for (unsigned launch = 0; launch < 3; ++launch) {
        RT64::RenderWorker worker(device.get(), "Android buffer qualification", plume::RenderCommandListType::COPY);
        android_graphics::qualify_transfer(worker);
    }
    if (!performance_capture::active()) throw std::runtime_error("Capture expired before startup test completed");
    const auto after = performance_trace::snapshot();
    const auto fence = static_cast<std::size_t>(performance_trace::Region::GpuFence);
    if (after[fence][0] <= baseline[fence][0]) throw std::runtime_error("Startup fences were not measured");
    const auto report = performance_capture::finish();
    if (report.find("unavailable") == std::string::npos)
        throw std::runtime_error("Buffer-only test incorrectly reported presentation timing");
    std::puts("PASS: capture active before device creation; actual Android buffer-only helper, three worker lifetimes / six GPU copies (desktop Vulkan, not Android Activity coverage)");
}
