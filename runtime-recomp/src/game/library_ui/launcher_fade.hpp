#pragma once
#include "handoff.hpp"
#include <SDL.h>
#include <vector>

namespace dkr::library_ui {
// One snapshot on an accepted button press, never a per-frame GPU readback.
// The caller clears this before destroying the owning SDL renderer.
class LauncherFade {
    SDL_Texture* frame_ = nullptr;
    double at_ = -1;

  public:
    ~LauncherFade() {
        clear();
    }
    void clear() {
        if (frame_)
            SDL_DestroyTexture(frame_);
        frame_ = nullptr;
        at_ = -1;
    }
    bool started() const {
        return at_ >= 0;
    }
    bool finished(double now) const {
        return !started() || now - at_ >= .35;
    }
    float opacity(double now) const {
        return started() ? 1.F - handoff_blend(now - at_) : 0;
    }
    SDL_Texture* frame() const {
        return frame_;
    }
    void capture(SDL_Renderer* renderer, double now) {
        if (started())
            return;
        at_ = now;
        int w = 0, h = 0;
        if (SDL_GetRendererOutputSize(renderer, &w, &h) || w < 1 || h < 1 || w > 8192 || h > 4320)
            return;
        std::vector<unsigned char> pixels(std::size_t(w) * h * 4);
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4))
            return;
        frame_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (frame_ && (SDL_UpdateTexture(frame_, nullptr, pixels.data(), w * 4) ||
                       SDL_SetTextureBlendMode(frame_, SDL_BLENDMODE_BLEND))) {
            SDL_DestroyTexture(frame_);
            frame_ = nullptr;
        }
    }
};
} // namespace dkr::library_ui
