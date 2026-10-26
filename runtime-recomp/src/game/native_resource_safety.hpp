#pragma once
// The DKR-owned optional mip extension must validate the underlying native
// allocation, not just its C++ wrapper. No submodule/interface edits required.
#include "plume_vulkan.h"
#if defined(_WIN32)
#include "plume_d3d12.h"
#endif
#include <atomic>
#include <cstdio>
namespace RT64::GeneratedMips {
inline bool resource_valid(const plume::RenderBuffer* value) {
    if (!value)
        return false;
#if defined(_WIN32)
    if (const auto* p = dynamic_cast<const plume::D3D12Buffer*>(value))
        return p->d3d != nullptr;
#endif
    if (const auto* p = dynamic_cast<const plume::VulkanBuffer*>(value))
        return p->vk != VK_NULL_HANDLE;
    return true;
}
inline bool resource_valid(const plume::RenderTexture* value) {
    if (!value)
        return false;
#if defined(_WIN32)
    if (const auto* p = dynamic_cast<const plume::D3D12Texture*>(value))
        return p->d3d != nullptr;
#endif
    if (const auto* p = dynamic_cast<const plume::VulkanTexture*>(value))
        return p->vk != VK_NULL_HANDLE;
    return true;
}
inline void upload_failure() {
    static std::atomic<unsigned> notices{0};
    if (notices.fetch_add(1, std::memory_order_relaxed) < 8)
        std::fprintf(
            stderr,
            "[texture-packs][upload] native allocation unavailable; PNG replacement skipped safely; original texture retained\n");
}
} // namespace RT64::GeneratedMips
