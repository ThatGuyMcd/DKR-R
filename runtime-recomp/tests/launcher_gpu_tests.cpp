#define SDL_MAIN_HANDLED
#include "launcher_gpu.hpp"
#include "library_ui/launcher_fade.hpp"
#include <cstdlib>
#include <iostream>

static void check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << ": " << SDL_GetError() << '\n';
        std::exit(1);
    }
}

int main(int argc, char**) {
    SDL_SetMainReady();
    check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video");
    SDL_setenv("DKR_LINUX_ACCELERATED_LAUNCHER", "", 1);
    check(dkr::runtime::launcher::linux_acceleration_requested(), "acceleration must be the default");
    SDL_setenv("DKR_LINUX_ACCELERATED_LAUNCHER", "0", 1);
    check(!dkr::runtime::launcher::linux_acceleration_requested(), "explicit CPU override");
    auto* window = SDL_CreateWindow("DKR launcher handoff test", 0, 0, 800, 600, SDL_WINDOW_HIDDEN);
    check(window != nullptr, "window");
    auto* renderer = dkr::runtime::launcher::create_linux_launcher_renderer(window);
    check(renderer && !dkr::runtime::launcher::accelerated_renderer(renderer), "CPU fallback");
    {
        dkr::library_ui::LauncherFade fade;
        SDL_SetRenderDrawColor(renderer, 24, 96, 160, 255);
        SDL_RenderClear(renderer);
        fade.capture(renderer, 10);
        check(fade.frame() && fade.opacity(10) == 1 && !fade.finished(10), "launcher snapshot");
        check(fade.opacity(10.175F) > .49F && fade.opacity(10.175F) < .51F && fade.finished(10.36), "crossfade timing");
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderClear(renderer);
        check(SDL_RenderCopy(renderer, fade.frame(), nullptr, nullptr) == 0, "snapshot copy");
        unsigned char pixel[4]{};
        SDL_Rect sample{20, 20, 1, 1};
        check(SDL_RenderReadPixels(renderer, &sample, SDL_PIXELFORMAT_RGBA32, pixel, 4) == 0 && pixel[0] == 24 &&
                  pixel[1] == 96 && pixel[2] == 160,
              "snapshot retained actual launcher pixels");
        fade.clear();
        check(!fade.frame() && !fade.started(), "snapshot retirement");
    }
    auto* original = window;
    check(!dkr::runtime::launcher::replace_surface_window(window, 0, "live-owner-check") && window == original,
          "must not retire a window with a live renderer");
    SDL_DestroyRenderer(renderer);
    check(!dkr::runtime::launcher::replace_surface_window(window, SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN,
                                                          "failed-allocation") &&
              window == original && SDL_GetWindowID(window),
          "failed allocation must preserve the original");
    check(dkr::runtime::launcher::replace_surface_window(window, 0, "CPU-window-roundtrip"), "software replacement");
    check(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN, "hidden state lost");
    if (argc > 1) {
        SDL_setenv("DKR_LINUX_ACCELERATED_LAUNCHER", "1", 1);
        for (unsigned i = 0; i < 4; ++i) {
            SDL_SetWindowSize(window, 900 + int(i) * 10, 650 + int(i) * 10);
            renderer = dkr::runtime::launcher::create_linux_launcher_renderer(window);
            check(dkr::runtime::launcher::accelerated_renderer(renderer), "accelerated API unavailable");
            SDL_SetRenderDrawColor(renderer, 10, 80, 140, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
            SDL_DestroyRenderer(renderer);
            check(dkr::runtime::launcher::replace_surface_window(window, SDL_WINDOW_VULKAN, "test-to-Vulkan"),
                  "Vulkan replacement");
            check(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN, "Vulkan flag missing");
            int width, height;
            SDL_GetWindowSize(window, &width, &height);
            check(width == 900 + int(i) * 10 && height == 650 + int(i) * 10, "window extent lost");
            check(dkr::runtime::launcher::replace_surface_window(window, 0, "test-to-launcher"),
                  "launcher replacement");
            check(!(SDL_GetWindowFlags(window) & (SDL_WINDOW_VULKAN | SDL_WINDOW_OPENGL)), "stale API ownership");
        }
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "Launcher renderer default, fallback, owner fence, failure retention and window handoffs passed\n";
}
