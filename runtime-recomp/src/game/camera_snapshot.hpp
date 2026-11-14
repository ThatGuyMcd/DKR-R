#pragma once
#include "camera_presentation.hpp"
#include "rom_revision.hpp"
#include <array>

namespace dkr::runtime::camera {
struct FrameStamp {
    std::uint32_t scene = 0, epoch = 0, frame = 0;
    float time = 0, dt = 0, aspect = 4.F / 3.F;
    std::uint32_t capabilities = 0;
    bool online = false, cut = false, inspection = false;
};
// Reads an ALREADY RETAINED image, never the live CPU's concurrent RAM. The
// normal CPU and rollback producer must publish at their own safe boundaries.
// Binding identities are presentation-local and reacquired from the roster.
class SnapshotReader final {
  public:
    std::optional<DkrViewSnapshot> capture(std::span<const std::uint8_t> retained, rom::Revision, unsigned view,
                                           const FrameStamp&);

  private:
    struct Binding {
        std::uint32_t object = 0, racer = 0, scene = 0, epoch = 0, generation = 0, frame = 0;
    };
    std::array<Binding, 4> bindings_{};
};
} // namespace dkr::runtime::camera
