#pragma once
#include "vehicle_binding.hpp"
#include "common/rt64_hlslpp.h"

namespace RT64 {
struct State;
struct RSP;
} // namespace RT64
namespace dkr::runtime::vehicles {
// All addresses refer to a task-owned snapshot/private decoder, never live RAM.
class RenderPass {
  public:
    using Select = void (*)(RT64::RSP&, std::uint32_t, bool);
    explicit RenderPass(RT64::State&, std::shared_ptr<const mods::vehicles::Resources>, Select);
    ~RenderPass();
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;
    bool marker(unsigned kind, unsigned value);
    void matrix(unsigned index);
    bool suppress(std::uint32_t triangle) const;
    // Presentation-only LOD0 redraws must use the HIGH model's ownership mask,
    // not the submitted low model's indices. Null means no custom car scope.
    const mods::vehicles::ModelProfile* detail_profile(unsigned object, unsigned instance) const;

  private:
    void begin(unsigned object);
    bool draw();
    void diagnostic(const char* message) const;
    RT64::State& state_;
    const RenderPass* previous_pass_ = nullptr;
    std::shared_ptr<const mods::vehicles::Resources> resources_;
    Select select_;
    unsigned scope_ = 0, object_ = 0, wheel_ = 0, triangles_ = 0, car_ = 0, original_lod_ = 0, vehicle_lod_ = 0,
             player_ = 0, view_ = 0;
    unsigned texture_cursor_ = 0x00e00000U;
    unsigned setup_ = 0, wheel_epoch_ = 0;
    const mods::vehicles::DriverProfile* driver_ = nullptr;
    const mods::vehicles::ModelProfile* profile_ = nullptr;
    std::array<std::vector<unsigned>, 16> texture_addresses_;
    hlslpp::float4x4 root_ = hlslpp::float4x4::identity(), adjustment_ = hlslpp::float4x4::identity();
    float scale_ = 1, draw_scale_ = 1, steer_ = 0;
    std::array<std::uint64_t, 4> spin_{};
    bool active_ = false, root_ready_ = false, replacement_submitted_ = false, hide_attachment_ = false;
    bool maximum_detail_ = false; // Local presentation policy, never checkpointed.
};
// Decoder-thread scope only; never simulation/checkpoint state.
const RenderPass* active_render_pass() noexcept;
} // namespace dkr::runtime::vehicles
