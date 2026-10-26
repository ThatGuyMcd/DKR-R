#pragma once
#include "camera_presentation.hpp"
#include <memory>
#include <vector>

namespace dkr::runtime::camera {
// Immutable copied STATIC collision geometry. No VM or graphics decoder owns
// a reference to the producer's RAM. Dynamic doors/platforms are intentionally
// not claimed by this adapter; callers must not set the dynamic capability.
class CollisionScene final {
  public:
    static std::shared_ptr<const CollisionScene> capture(std::span<const std::uint8_t> snapshot,
                                                         std::uint32_t level_model, std::uint64_t scene_generation);
    DkrViewHit query(const DkrViewQuery&, unsigned face_budget = 8192) const;
    std::uint64_t generation() const {
        return generation_;
    }
    std::size_t bytes() const;

  private:
    struct Triangle {
        std::array<float, 3> a, b, c;
    };
    struct Chunk {
        std::array<float, 3> lo, hi;
        std::uint32_t first, count;
    };
    std::vector<Triangle> triangles_;
    std::vector<Chunk> chunks_;
    std::uint64_t generation_ = 0;
};
} // namespace dkr::runtime::camera
