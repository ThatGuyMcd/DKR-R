#pragma once

#include <cstddef>
#include <cstdint>

namespace dkr::runtime {

// Temporarily exposes one submission-owned RDRAM image to RT64 while F3DDKR
// decodes it. The image is an isolated decode workspace: RT64 may perform
// framebuffer readback into it and the bridge uses a private vertex scratch
// window. The original live-memory pointers are restored on every exit,
// including exceptions.
class RendererSnapshotScope {
public:
    RendererSnapshotScope(std::uint8_t*& core_rdram,
                          std::uint8_t*& state_rdram,
                          std::uint8_t* submission_rdram);
    ~RendererSnapshotScope();
    void restore() noexcept;

    RendererSnapshotScope(const RendererSnapshotScope&) = delete;
    RendererSnapshotScope& operator=(const RendererSnapshotScope&) = delete;

private:
    std::uint8_t*& core_rdram_;
    std::uint8_t*& state_rdram_;
    std::uint8_t* original_core_ = nullptr;
    std::uint8_t* original_state_ = nullptr;
    bool restored_=false;
};

} // namespace dkr::runtime
