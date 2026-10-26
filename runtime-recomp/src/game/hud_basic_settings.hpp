#pragma once
#include "hud_layout_policy.hpp"

namespace dkr::runtime::hud {
// The workshop is suspended. Never reactivate custom placement or sizing from
// an existing profile, but retain its data for a future qualified editor.
constexpr LayoutMode basic_mode(LayoutMode stored) {
    return stored == LayoutMode::FitToViewport ? stored : LayoutMode::Original;
}
} // namespace dkr::runtime::hud
