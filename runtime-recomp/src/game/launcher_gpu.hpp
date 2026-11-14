#pragma once

#include <SDL.h>
#include <cstdio>
#include <cstring>

namespace dkr::runtime::launcher {

inline bool linux_acceleration_requested() {
    const char* override = SDL_getenv("DKR_LINUX_ACCELERATED_LAUNCHER");
    return !override || std::strcmp(override, "0") != 0;
}

// Called on the window owner thread, after the previous renderer and all its
// workers have retired. Never convert a live GL/Vulkan surface in place.
inline bool replace_surface_window(SDL_Window*& window, Uint32 api, const char* reason) {
    if (!window || SDL_GetRenderer(window)) {
        std::fprintf(stderr, "[boot][window] refused %s with a live SDL renderer\n", reason);
        return false;
    }
    int x, y, width, height;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &width, &height);
    const Uint32 flags = SDL_GetWindowFlags(window);
    const Uint32 old_id = SDL_GetWindowID(window);
    auto* next = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled", x, y, width, height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN | api);
    if (!next) {
        std::fprintf(stderr, "[boot][window] %s allocation failed; original retained: %s\n", reason, SDL_GetError());
        return false;
    }
    if (flags & SDL_WINDOW_FULLSCREEN)
        SDL_SetWindowFullscreen(window, 0);
    SDL_DestroyWindow(window);
    window = next;
    SDL_SetWindowMinimumSize(window, 800, 600);
    if (flags & SDL_WINDOW_MAXIMIZED)
        SDL_MaximizeWindow(window);
    const auto fullscreen = flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (fullscreen && SDL_SetWindowFullscreen(window, fullscreen) != 0)
        std::fprintf(stderr, "[boot][window] %s fullscreen restore failed: %s\n", reason, SDL_GetError());
    if (!(flags & SDL_WINDOW_HIDDEN))
        SDL_ShowWindow(window);
    if (flags & SDL_WINDOW_MINIMIZED)
        SDL_MinimizeWindow(window);
    std::fprintf(stderr, "[boot][window] %s retired=%u replacement=%u flags=0x%08X\n", reason, unsigned(old_id),
                 unsigned(SDL_GetWindowID(window)), unsigned(SDL_GetWindowFlags(window)));
    return true;
}

inline SDL_Renderer* create_linux_launcher_renderer(SDL_Window* window) {
    SDL_Renderer* renderer = nullptr;
    const bool requested = linux_acceleration_requested();
    if (requested && !(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN)) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer)
            std::fprintf(stderr, "[boot][launcher] acceleration failed: %s; trying software\n", SDL_GetError());
    }
    if (!renderer) {
        std::fprintf(stderr, "[boot][launcher] CPU fallback reason=%s\n",
                     !requested                                         ? "explicit override"
                     : (SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN) ? "launcher surface unavailable"
                                                                        : "accelerated renderer unavailable");
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    // SDL's accelerated flag describes the API, not the physical GPU. Record
    // Mesa's rasterizer too, so llvmpipe is never mistaken for hardware proof.
    if (renderer && SDL_GL_GetCurrentContext()) {
        using GetString = const unsigned char* (*)(unsigned);
        const auto get_string = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
        const auto* device = get_string ? get_string(0x1F01) : nullptr; // GL_RENDERER
        std::fprintf(stderr, "[boot][launcher] GL rasterizer=%s\n",
                     device ? reinterpret_cast<const char*>(device) : "unknown");
    }
    return renderer;
}

inline bool accelerated_renderer(SDL_Renderer* renderer) {
    SDL_RendererInfo info{};
    return renderer && SDL_GetRendererInfo(renderer, &info) == 0 && (info.flags & SDL_RENDERER_ACCELERATED);
}

} // namespace dkr::runtime::launcher
