#pragma once

#include "netplay/local_scenery.hpp"
#include "netplay/experimental_draw_events.h"
#include "local_scenery_policy.hpp"
#include "local_scenery_workload.hpp"
#include <array>
#include <span>

namespace RT64 { struct State; struct RSP; }
namespace dkr::runtime {
// Typed renderer-only submission. Not a native-pointer display-list escape.
// prepare() runs before decoding; draw_at() runs at an observed world-pass
// boundary. No resources or preferences are read by guest CPU/replay.
class LocalSceneryRT64Pass final {
public:
    using IdentitySelector=void (*)(RT64::RSP&,std::uint32_t);
    void prepare(const netplay::experimental::LocalSceneryScene* scene,
                 std::span<const dkr_owned_draw_event> observations,
                 std::span<std::uint8_t> decoder,std::uint32_t identity) noexcept;
    void draw_at(RT64::State& state,std::uint32_t command,IdentitySelector select);
private:
    const netplay::experimental::LocalSceneryScene* scene_=nullptr;
    std::uint32_t identity_=0;
    std::array<dkr_owned_draw_event,local_scenery::kMaxViewPlans> observations_{};
    std::array<local_scenery::ViewPlan,local_scenery::kMaxViewPlans> plans_{};
    std::array<bool,local_scenery::kMaxViewPlans> consumed_{};
    std::size_t observation_count_=0;
    std::vector<local_scenery::TextureBinding> textures_;
    local_scenery::Settings settings_;
};
}
