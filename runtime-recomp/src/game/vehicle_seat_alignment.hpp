#pragma once
#include "vehicle_package.hpp"

namespace dkr::runtime::vehicles::seating {
// The export's seat, offset AND calibrated neutral anchors share the same
// post-scale Studio coordinates. They must receive the same model-to-world
// conversion. Treating just the anchors as world units amplified the donor
// differences by four (Banjo/Waluigi versus Diddy/Rayman: 24 instead of 6).
inline constexpr float StudioToWorld=.25F;
constexpr mods::vehicles::Vec3 scaled(mods::vehicles::Vec3 value,float factor) {
    return {value.x*factor,value.y*factor,value.z*factor};
}
inline mods::vehicles::Vec3 anchor(const mods::vehicles::Package& car,unsigned base) {
    return scaled(car.driver_anchors.at(base),StudioToWorld);
}
inline mods::vehicles::Vec3 reference_placement(const mods::vehicles::Package& car) {
    // The fixed-reference contract maps directly onto the authored seat.
    // Its anchors are immutable per KART, not one global calibration table.
    if(car.per_driver())return {};
    // Retain the accepted reference placement at neutral scale while fixing
    // RELATIVE donor alignment. This one shared calibration applies to the
    // car, not a Waluigi/Rayman name, body bounds, character height or LOD.
    // Diddy is the neutral reference of this version-1 export convention.
    return scaled(car.driver_anchors[9],StudioToWorld-1.F);
}
inline mods::vehicles::Vec3 source_point(const mods::vehicles::Package& car,mods::vehicles::Vec3 value) {
    // Both mesh vertices and pivot positions start in the same source space.
    // Seats are already AFTER model scale/yaw and never pass through here.
    if(car.per_driver())return {value.x-car.origin.x,value.y-car.origin.y,value.z-car.origin.z};
    return value;
}
inline mods::vehicles::Vec3 destination(const mods::vehicles::Package& car,unsigned base) {
    return scaled(car.driver_seats.at(base),StudioToWorld);
}
static_assert(scaled({0,0,24},StudioToWorld).z==6.F);
} // namespace dkr::runtime::vehicles::seating
