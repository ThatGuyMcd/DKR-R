#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace dkr::runtime::presentation {
struct TrackSelectClip {
    int left, top, right, bottom;
};
inline unsigned track_select_cover_token(float cover) {
    return static_cast<unsigned>(std::lround(std::clamp(std::isfinite(cover) ? cover : 1.F, 1.F, 8.F) * 1024.F));
}
inline TrackSelectClip track_select_window_clip(unsigned token, int width = 320, int height = 240) {
    const float cover = std::clamp(token / 1024.F, 1.F, 8.F);
    const int gutter = static_cast<int>(std::lround(width * 2.F * (cover - 1.F)));
    return {-gutter, 0, width * 4 + gutter, height * 4};
}
inline TrackSelectClip track_select_preview_clip(std::array<int, 4> frame, unsigned token) {
    const auto window = track_select_window_clip(token);
    const int left = std::clamp(frame[0], window.left, window.right);
    const int top = std::clamp(frame[1], window.top, window.bottom);
    return {left, top, std::max(left, std::min(frame[2], window.right)),
            std::max(top, std::min(frame[3], window.bottom))};
}

// These are task-local framebuffer identities, not new render targets. The
// extended per-draw scissors must NOT change RT64's native-aspect inference.
// Only the producer-owned aggregate is corrected, before fullSync submits it.
struct TrackSelectFramebufferImages {
    std::array<std::uint32_t, 8> addresses{};
    unsigned count = 0;
    bool contains(std::uint32_t address) const {
        return std::find(addresses.begin(), addresses.begin() + count, address) != addresses.begin() + count;
    }
    bool remember(std::uint32_t address, unsigned width) {
        if (width != 320U)
            return false;
        if (contains(address))
            return true;
        if (count == addresses.size())
            return false;
        addresses[count++] = address;
        return true;
    }
};
template <class Pair> inline void normalise_track_select_framebuffer_extent(Pair& pair) {
    if (pair.colorImage.width != 320U || pair.scissorRect.isNull())
        return;
    pair.scissorRect.ulx = std::max(pair.scissorRect.ulx, 0);
    pair.scissorRect.lrx = std::min(pair.scissorRect.lrx, 320 * 4);
}
} // namespace dkr::runtime::presentation
