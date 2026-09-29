#pragma once
#include <algorithm>
#include <cstdint>

namespace dkr::runtime::render_resources {
constexpr std::uint32_t image_count(std::uint32_t requested, std::uint32_t minimum,
                                    std::uint32_t maximum) {
    const auto count = std::max(requested, minimum);
    return maximum == 0 ? count : std::min(count, std::max(minimum, maximum));
}
struct Workers { std::uint32_t raster, uber, texture; };
constexpr Workers workers(std::uint32_t available, bool mobile) {
    // These pools coexist with simulation, presentation, audio and networking.
    // Bound burst parallelism, but never remove an essential worker.
    available = std::max(available, 1U);
    return {std::clamp(available / 2, 1U, mobile ? 2U : 4U),
            std::clamp(available > 2 ? available - 2 : 1U, 1U, mobile ? 2U : 4U),
            std::clamp(available / 4, 1U, mobile ? 1U : 2U)};
}
}
