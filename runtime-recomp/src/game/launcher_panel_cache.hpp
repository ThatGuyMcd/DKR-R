#pragma once

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace dkr::runtime::launcher {

// Renderer-local cache of large, untextured UI meshes. It never owns a window
// or touches the game's renderer. Unsupported/unstable meshes use the ordinary
// draw path. A key must recur before allocation, preventing resize/scroll churn.
class PanelCache {
  public:
    struct Entry {
        SDL_Texture* texture = nullptr;
        std::vector<SDL_Vertex> vertices;
        SDL_Rect bounds{};
        std::uint64_t last_frame = 0;
        bool attempted = false;
    };
    ~PanelCache() {
        clear();
    }
    void clear() {
        for (auto& entry : entries_) {
            if (entry.texture)
                SDL_DestroyTexture(entry.texture);
            entry = {};
        }
    }
    void next_frame() {
        ++frame_;
    }
    std::uint64_t hits() const {
        return hits_;
    }
    std::uint64_t builds() const {
        return builds_;
    }
    Entry* find(SDL_Renderer* renderer, std::vector<SDL_Vertex> vertices) {
        if (vertices.empty() || vertices.size() > 4096)
            return nullptr;
        float left = vertices[0].position.x, right = left;
        float top = vertices[0].position.y, bottom = top;
        for (const auto& vertex : vertices) {
            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y))
                return nullptr;
            left = std::min(left, vertex.position.x);
            right = std::max(right, vertex.position.x);
            top = std::min(top, vertex.position.y);
            bottom = std::max(bottom, vertex.position.y);
        }
        // Real launcher coordinates are bounded by the viewport. Reject huge
        // coordinates before converting to integers or allocating surfaces.
        if (left < 0 || top < 0 || right > 16384 || bottom > 16384)
            return nullptr;
        SDL_Rect bounds{static_cast<int>(std::floor(left)), static_cast<int>(std::floor(top)),
                        static_cast<int>(std::ceil(right)) - static_cast<int>(std::floor(left)),
                        static_cast<int>(std::ceil(bottom)) - static_cast<int>(std::floor(top))};
        const auto pixels = static_cast<std::int64_t>(bounds.w) * bounds.h;
        if (pixels < 100000 || pixels > 4 * 1024 * 1024)
            return nullptr;
        for (auto& entry : entries_) {
            if (entry.vertices.size() == vertices.size() &&
                std::memcmp(entry.vertices.data(), vertices.data(), vertices.size() * sizeof(SDL_Vertex)) == 0) {
                const bool consecutive = entry.last_frame + 1 >= frame_;
                entry.last_frame = frame_;
                if (!entry.attempted && consecutive) {
                    entry.attempted = true;
                    std::int64_t total = pixels;
                    for (const auto& other : entries_)
                        if (other.texture)
                            total += static_cast<std::int64_t>(other.bounds.w) * other.bounds.h;
                    if (total <= 16 * 1024 * 1024)
                        entry.texture = rasterize(renderer, vertices, bounds);
                    if (entry.texture)
                        ++builds_;
                }
                if (entry.texture) {
                    ++hits_;
                    return &entry;
                }
                return nullptr;
            }
        }
        auto& slot = *std::min_element(entries_.begin(), entries_.end(),
                                       [](const Entry& a, const Entry& b) { return a.last_frame < b.last_frame; });
        // Do not evict a mesh referenced by this frame's pending callbacks.
        if (slot.last_frame == frame_)
            return nullptr;
        if (slot.texture)
            SDL_DestroyTexture(slot.texture);
        slot = {nullptr, std::move(vertices), bounds, frame_, false};
        return nullptr;
    }

    static SDL_Texture* rasterize(SDL_Renderer* destination, std::vector<SDL_Vertex> vertices, SDL_Rect bounds) {
        // Colour gradients multiply two interpolants when flattened through an
        // alpha surface; keep them on SDL's original path. Panel triangles have
        // constant RGB (their antialiased edges may vary only in alpha).
        if (vertices.size() % 3 != 0 || bounds.w <= 0 || bounds.h <= 0)
            return nullptr;
        for (std::size_t i = 0; i < vertices.size(); i += 3) {
            for (std::size_t j = 1; j < 3; ++j) {
                const auto a = vertices[i].color, b = vertices[i + j].color;
                if (a.r != b.r || a.g != b.g || a.b != b.b)
                    return nullptr;
            }
        }
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, bounds.w, bounds.h, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return nullptr;
        SDL_Renderer* software = SDL_CreateSoftwareRenderer(surface);
        if (!software) {
            SDL_FreeSurface(surface);
            return nullptr;
        }
        SDL_SetRenderDrawColor(software, 0, 0, 0, 0);
        SDL_RenderClear(software);
        SDL_SetRenderDrawBlendMode(software, SDL_BLENDMODE_BLEND);
        for (auto& vertex : vertices) {
            vertex.position.x -= bounds.x;
            vertex.position.y -= bounds.y;
        }
        const int status =
            SDL_RenderGeometry(software, nullptr, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
        SDL_RenderFlush(software);
        SDL_DestroyRenderer(software);
        SDL_Texture* texture = nullptr;
        if (status == 0) {
            // Source-over onto transparent stores premultiplied colour. SDL's
            // normal texture blit expects straight alpha: convert once here.
            for (int y = 0; y < surface->h; ++y) {
                auto* row = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
                for (int x = 0; x < surface->w; ++x) {
                    auto* pixel = row + x * 4;
                    const unsigned alpha = pixel[3];
                    if (alpha != 0)
                        for (int channel = 0; channel < 3; ++channel)
                            pixel[channel] = static_cast<Uint8>(
                                std::min(255U, (unsigned(pixel[channel]) * 255U + alpha / 2) / alpha));
                }
            }
            texture = SDL_CreateTextureFromSurface(destination, surface);
            if (texture)
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        SDL_FreeSurface(surface);
        return texture;
    }

  private:
    std::array<Entry, 8> entries_{};
    std::uint64_t frame_ = 0, hits_ = 0, builds_ = 0;
};

} // namespace dkr::runtime::launcher
