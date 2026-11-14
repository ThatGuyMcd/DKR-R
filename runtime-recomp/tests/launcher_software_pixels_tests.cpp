#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "launcher_panel_cache.hpp"
#include "imgui.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

static void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << ": " << SDL_GetError() << '\n';
        std::exit(1);
    }
}

int main() {
    // No window, graphics driver, display server or personal profile required.
    auto* surface = SDL_CreateRGBSurfaceWithFormat(0, 320, 200, 32, SDL_PIXELFORMAT_RGBA32);
    check(surface != nullptr, "surface");
    auto* renderer = SDL_CreateSoftwareRenderer(surface);
    check(renderer != nullptr, "renderer");
    const std::array<Uint32, 16> white = [] {
        std::array<Uint32, 16> p{};
        p.fill(0xFFFFFFFF);
        return p;
    }();
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 4, 4);
    check(texture != nullptr, "texture");
    check(SDL_UpdateTexture(texture, nullptr, white.data(), 16) == 0, "texture upload");
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    unsigned worst_difference = 0;
    for (int variant = 0; variant < 12; ++variant) {
        const Uint8 alpha = static_cast<Uint8>((variant % 4) * 76 + 1);
        SDL_Vertex vertices[] = {{{3.25F, 7.75F}, {9, 22, 31, alpha}, {.125F, .125F}},
                                 {{301.5F, 18.25F}, {9, 22, 31, alpha}, {.125F, .125F}},
                                 {{91.75F, 193.5F}, {9, 22, 31, alpha}, {.125F, .125F}}};
        if (variant >= 4 && variant < 8)
            vertices[1].color = {231, 144, 88, alpha};
        if (variant >= 8)
            vertices[2].color.a = 0;
        std::vector<SDL_Vertex> mesh(std::begin(vertices), std::end(vertices));
        if (variant < 4) {
            // The seam-protection underlay contains overlapping translucent
            // rectangles. Exercise repeated source-over, not only one layer.
            mesh.insert(mesh.end(), std::begin(vertices), std::end(vertices));
            mesh.insert(mesh.end(), std::begin(vertices), std::end(vertices));
        }
        std::vector<Uint32> reference(320 * 200), actual(320 * 200);
        auto* cached = dkr::runtime::launcher::PanelCache::rasterize(renderer, mesh, {0, 0, 320, 200});
        check((cached == nullptr) == (variant >= 4 && variant < 8), "cache gradient guard");
        for (int mode = 0; mode < (cached ? 3 : 2); ++mode) {
            SDL_SetRenderDrawColor(renderer, 111, 177, 243, 255);
            SDL_RenderClear(renderer);
            SDL_Rect clip{17, 11, 282, 174};
            SDL_RenderSetClipRect(renderer, &clip);
            if (mode == 2) {
                SDL_Rect dest{0, 0, 320, 200};
                check(SDL_RenderCopy(renderer, cached, nullptr, &dest) == 0, "cached geometry");
            } else {
                check(SDL_RenderGeometry(renderer, mode == 0 ? texture : nullptr, mesh.data(),
                                         static_cast<int>(mesh.size()), nullptr, 0) == 0,
                      "geometry");
            }
            SDL_RenderPresent(renderer);
            auto& pixels = mode == 0 ? reference : actual;
            check(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), 320 * 4) == 0,
                  "pixels");
            SDL_RenderSetClipRect(renderer, nullptr);
            if (mode == 0)
                continue;
            for (std::size_t i = 0; i < reference.size(); ++i) {
                // The launcher presents an opaque window; compare displayed
                // RGB, not destination alpha (SDL 2.26 and 2.30 accumulate
                // that unused channel differently on a software surface).
                for (unsigned shift : {0U, 8U, 16U}) {
                    const auto delta = static_cast<unsigned>(
                        std::abs(int((reference[i] >> shift) & 255) - int((actual[i] >> shift) & 255)));
                    worst_difference = std::max(worst_difference, delta);
                    if (delta > 3)
                        std::cerr << "variant=" << variant << " mode=" << mode << " pixel=" << i << " channel=" << shift
                                  << " expected=" << ((reference[i] >> shift) & 255)
                                  << " actual=" << ((actual[i] >> shift) & 255) << '\n';
                    check(delta <= 3, "solid/cached path changed triangle coverage/colour");
                }
            }
        }
        SDL_DestroyTexture(cached);
    }
    std::cout << "12 clipped solid/gradient/alpha comparisons passed; maximum channel difference=" << worst_difference
              << "\n";
    {
        dkr::runtime::launcher::PanelCache cache;
        std::vector<SDL_Vertex> mesh{
            {{0, 0}, {9, 22, 31, 230}, {}}, {{600, 0}, {9, 22, 31, 230}, {}}, {{0, 400}, {9, 22, 31, 230}, {}}};
        cache.next_frame();
        check(cache.find(renderer, mesh) == nullptr, "cache allocated on first observation");
        cache.next_frame();
        check(cache.find(renderer, mesh) != nullptr && cache.builds() == 1, "stable panel not cached");
        cache.next_frame();
        check(cache.find(renderer, mesh) != nullptr && cache.builds() == 1, "stable panel rebuilt");
        mesh[1].position.x += 1;
        cache.next_frame();
        check(cache.find(renderer, mesh) == nullptr, "resize reused stale panel");
        mesh[0].position.x = -1;
        check(cache.find(renderer, mesh) == nullptr, "negative-coordinate fallback missing");
        mesh[0].position.x = 0;
        mesh[1].position.x = 20000;
        check(cache.find(renderer, mesh) == nullptr, "oversized panel accepted");
        cache.clear();
        cache.next_frame();
        mesh[1].position.x = 600;
        check(cache.find(renderer, mesh) == nullptr, "clear retained renderer resource");
    }
    // Exercise the actual launcher backend at normal and Retina scales.
    ImGui::CreateContext();
    ImGui_ImplSDLRenderer2_Init(renderer);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    for (float scale : {1.0F, 2.0F}) {
        io.DisplaySize = ImVec2(320 / scale, 200 / scale);
        io.DisplayFramebufferScale = ImVec2(scale, scale);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui::NewFrame();
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->PushClipRect(ImVec2(40, 20), ImVec2(100, 60));
        draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(255, 0, 0, 255));
        draw->PopClipRect();
        ImGui::Render();
        SDL_RenderSetScale(renderer, scale, scale);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
        const auto red_at = [&](int x, int y) {
            Uint8 red, green, blue;
            const auto* row = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch;
            SDL_GetRGB(reinterpret_cast<const Uint32*>(row)[x], surface->format, &red, &green, &blue);
            return red == 255 && green == 0 && blue == 0;
        };
        check(red_at(int(99 * scale), int(59 * scale)), "scaled geometry missed clip edge");
        check(!red_at(int(39 * scale), int(40 * scale)), "scaled clip leaked left");
        check(!red_at(int(100 * scale), int(40 * scale)), "scaled clip leaked right");
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    return 0;
}
