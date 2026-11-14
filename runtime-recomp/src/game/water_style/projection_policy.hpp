#pragma once
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::water::style {
// DKR supplies its own camera. Only native-camera draws need the view table;
// both paths still need the raster projection and viewport before indexing.
constexpr bool projection_ready(std::uint32_t index, std::size_t views, std::size_t view_projections,
                                std::size_t viewports, bool captured_camera = false) {
    return (captured_camera || index < views) && index < view_projections && index < viewports;
}
} // namespace dkr::runtime::water::style
