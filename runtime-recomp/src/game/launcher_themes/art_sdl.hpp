#pragma once
#include "art.hpp"
#include "art_data.hpp"
#include <SDL.h>
#include "stb/stb_image.h"
#include <cstdio>

namespace dkr::launcher_themes {
// One small atlas per launcher renderer. No 3D, worker, file/network lookup or
// per-frame upload is needed. Explicitly release before destroying SDL's renderer.
class ArtTexture {
    SDL_Texture* texture_ = nullptr;
public:
    explicit ArtTexture(SDL_Renderer* renderer) {
        if (!renderer) return;
        int width = 0, height = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(ArtPng, int(sizeof(ArtPng)), &width, &height, &channels, 4);
        if (pixels && width == ArtWidth && height == ArtHeight) {
            texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
            if (texture_ && (SDL_UpdateTexture(texture_,nullptr,pixels,width*4) != 0 ||
                             SDL_SetTextureBlendMode(texture_,SDL_BLENDMODE_BLEND) != 0)) release();
            if (texture_) SDL_SetTextureScaleMode(texture_,SDL_ScaleModeLinear);
        }
        if (pixels) stbi_image_free(pixels);
        std::fprintf(stderr,"[launcher][art] original seasonal sprites=%s; atlas=%dx%d; renderer-owned\n",
            texture_ ? "ready" : "unavailable",ArtWidth,ArtHeight);
    }
    ArtTexture(const ArtTexture&) = delete;
    ArtTexture& operator=(const ArtTexture&) = delete;
    ~ArtTexture() { release(); }
    SDL_Texture* get() const { return texture_; }
    void release() { if (texture_) SDL_DestroyTexture(texture_); texture_ = nullptr; }
};
} // namespace dkr::launcher_themes
