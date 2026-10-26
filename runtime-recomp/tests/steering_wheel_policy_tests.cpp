#include "steering_wheel_policy.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace dkr::runtime::steering_wheel;

    // Zero is a valid physical RDRAM offset for low-level memory helpers, but
    // it is always a null sentinel for the model/material pointer chain used
    // by the steering-wheel render hook.
    assert(!is_valid_render_pointer(0U, 0x06U));
    assert(is_valid_render_pointer(0x00000004U, 0x06U));
    assert(is_valid_render_pointer(0x80000004U, 0x06U));
    assert(is_valid_render_pointer(0xA0000004U, 0x06U));
    assert(!is_valid_render_pointer(0x807FFFF8U, 0x08U));
    assert(!is_valid_render_pointer(0x80800000U, 0x00U));

    BatchIdentity wheel{};
    wheel.behaviour = kRacerBehaviour;
    wheel.vehicle = kCarVehicle;
    wheel.batch_count = 30U;
    wheel.batch_index = 29U;
    wheel.texture_index = 9U;
    wheel.vertex_count = 4;
    wheel.triangle_count = 2;
    wheel.authored_flags = kAuthoredBatchFlags;
    wheel.texture_width = kSteeringWheelWidth;
    wheel.texture_height = kSteeringWheelHeight;
    wheel.texture_format = kRgba32Format;
    wheel.texture_flags = kClampBothAxes;
    wheel.texture_asset_matches = true;

    constexpr std::uint32_t fog = 1U << 3U;
    const std::uint32_t effective = kAuthoredBatchFlags | fog;
    assert(is_cockpit_steering_wheel(wheel));
    assert(corrected_material_flags(wheel, effective) == (effective & ~kDecal));
    assert((corrected_material_flags(wheel, effective) & (kAntiAliasing | kDepthCompare | kSemiTransparent | fog)) ==
           (kAntiAliasing | kDepthCompare | kSemiTransparent | fog));

    // The title demo uses BHV_VEHICLE_ANIMATION and stores animated-object
    // state at Object+0x64, not Object_Racer. Its car uses the same exact
    // steering-wheel batch and asset, so admit it without reading that
    // unrelated pointer as a racer.
    auto title_wheel = wheel;
    title_wheel.behaviour = kTitleVehicleAnimationBehaviour;
    title_wheel.vehicle = kNoVehicle;
    assert(is_cockpit_steering_wheel(title_wheel));
    assert(corrected_material_flags(title_wheel, effective) == (effective & ~kDecal));

    // Every gate is intentional: do not rewrite another car batch, a lower
    // LOD batch, another vehicle, or a coincidentally similar decal texture.
    auto other = wheel;
    other.vehicle = 1;
    assert(!is_cockpit_steering_wheel(other));
    other = title_wheel;
    other.behaviour = 54U; // BHV_CHARACTER_SELECT
    assert(!is_cockpit_steering_wheel(other));
    other = title_wheel;
    other.behaviour = 53U; // BHV_CAR_ANIMATION
    assert(!is_cockpit_steering_wheel(other));
    other = title_wheel;
    other.vehicle = kCarVehicle;
    assert(!is_cockpit_steering_wheel(other));
    other = wheel;
    other.batch_index = 28U;
    assert(!is_cockpit_steering_wheel(other));
    other = wheel;
    other.vertex_count = 5;
    assert(!is_cockpit_steering_wheel(other));
    other = wheel;
    other.authored_flags |= 1U << 8U;
    assert(!is_cockpit_steering_wheel(other));
    other = wheel;
    other.texture_asset_matches = false;
    assert(!is_cockpit_steering_wheel(other));
    assert(corrected_material_flags(other, effective) == effective);

    std::puts("[test][steering-wheel-policy] PASS");
    return 0;
}
