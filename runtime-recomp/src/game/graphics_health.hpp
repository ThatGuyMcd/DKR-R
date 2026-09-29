#pragma once
#include <atomic>
#include <cstdint>
#if defined(__ANDROID__) || defined(DKR_GRAPHICS_FAULT_TEST)
#include <condition_variable>
#include <mutex>
#endif

namespace dkr::runtime::graphics_health {
enum class Stage : std::uint32_t { None, Surface, Swapchain, ComputePipeline, GraphicsPipeline, Submit, Fence, Present, Acquire, DescriptorLayout, DescriptorPool, DescriptorAllocate, DescriptorUse, DeviceFeatures, BufferAllocation, ImageAllocation, BufferMap, BufferFlush, ResourceView, DeviceLimits, TextureCopy };
// Preserve the first GPU error across worker threads; later failures are often
// consequences and must not bury the useful error in a multi-megabyte log.
inline std::atomic<std::uint64_t> failure{0};
inline std::atomic<bool> resource_quarantined{false};
inline std::atomic<bool> first_presentation{false};
using FailureReporter = void (*)(Stage, std::int32_t);
inline std::atomic<FailureReporter> resource_failure_reporter{nullptr};
inline bool record(Stage stage, std::int32_t result) noexcept {
    const auto packed = (std::uint64_t(stage) << 32) | std::uint32_t(result);
    std::uint64_t expected = 0;
    return failure.compare_exchange_strong(expected, packed, std::memory_order_relaxed);
}
inline void reset() noexcept { failure.store(0, std::memory_order_relaxed); resource_quarantined.store(false); first_presentation.store(false); }
inline bool failed() noexcept { return failure.load(std::memory_order_relaxed) != 0; }
#if defined(__ANDROID__) || defined(DKR_GRAPHICS_FAULT_TEST)
// A failed allocation/map cannot return a usable RenderResource. Workers have
// no universal exception-safe cancellation boundary, and unwinding them can
// destroy resources still owned by a submitted GPU command. Quarantine ONLY a
// terminal resource failure, preserving ownership. Android's UI thread offers
// log export and an explicit process close; never auto-retry or fake a fence.
[[noreturn]] inline void quarantine_resource(Stage stage, std::int32_t result) {
    resource_quarantined.store(true, std::memory_order_release);
    if (record(stage, result)) {
        // Notify Android directly, before parking: the SDL loop may itself be
        // waiting on the failing worker. The callback posts native UI only.
        if (const auto report = resource_failure_reporter.load(std::memory_order_acquire)) report(stage, result);
    }
    std::mutex mutex;
    std::condition_variable condition;
    std::unique_lock lock(mutex);
    for (;;) condition.wait(lock);
}
#endif
// Only unwind a display-list parse at a DKR-owned, exception-safe boundary.
// Background renderer workers instead observe the shared failure flag.
struct TaskAborted {};
inline thread_local bool can_abort_task = false;
struct TaskBoundary {
    bool previous = can_abort_task;
    TaskBoundary() { can_abort_task = true; }
    ~TaskBoundary() { can_abort_task = previous; }
};
inline bool stop_failed_task() {
    if (!failed()) return false;
    if (can_abort_task) throw TaskAborted{};
    return true;
}
inline const char* stage_name(Stage stage) noexcept {
    switch (stage) {
    case Stage::Surface: return "surface";
    case Stage::Swapchain: return "swapchain";
    case Stage::ComputePipeline: return "compute pipeline";
    case Stage::GraphicsPipeline: return "graphics pipeline";
    case Stage::Submit: return "GPU submission";
    case Stage::Fence: return "GPU completion";
    case Stage::Present: return "presentation";
    case Stage::Acquire: return "image acquisition";
    case Stage::DescriptorLayout: return "descriptor layout";
    case Stage::DescriptorPool: return "descriptor pool";
    case Stage::DescriptorAllocate: return "descriptor allocation";
    case Stage::DescriptorUse: return "descriptor use";
    case Stage::DeviceFeatures: return "GPU feature compatibility";
    case Stage::BufferAllocation: return "buffer allocation";
    case Stage::ImageAllocation: return "image allocation";
    case Stage::BufferMap: return "buffer mapping/readback visibility";
    case Stage::BufferFlush: return "buffer upload visibility";
    case Stage::ResourceView: return "GPU resource view";
    case Stage::DeviceLimits: return "GPU layout/format limits";
    case Stage::TextureCopy: return "unsupported GPU texture copy";
    default: return "renderer startup";
    }
}
}
