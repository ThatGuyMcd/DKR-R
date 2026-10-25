#pragma once
#include "vehicle_seat_alignment.hpp"
#include <cmath>

namespace dkr::runtime::vehicles::original_textures {
inline mods::vehicles::Vec3 model_anchor(const mods::vehicles::Package& car,unsigned base,float native_scale) {
    if(!std::isfinite(native_scale)||native_scale<=0)throw mods::Error("Custom vehicle: invalid native driver scale.");
    // Only the explicit revision-1 contract marks raw native anchors. Older
    // per-driver exports and calibrated legacy shared seats retain their
    // accepted Studio-unit conversion. Never infer units from model bounds.
    if(car.per_driver() && car.authored_anchors)return car.driver_anchors.at(base);
    const auto old=seating::anchor(car,base);
    return {old.x/native_scale,old.y/native_scale,old.z/native_scale};
}
}
