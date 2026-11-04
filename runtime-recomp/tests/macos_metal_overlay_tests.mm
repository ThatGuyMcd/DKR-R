#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_metal.h>
#include <SDL_syswm.h>
#include "gui/rt64_inspector.h"
#include "imgui/imgui.h"
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cmath>
#include <cstdio>
#include <thread>

namespace plume {
    std::unique_ptr<RenderInterface> CreateMetalInterface();
}
extern "C" void dkr_set_window_fullscreen(Uint32 window_id, bool fullscreen);
extern "C" bool dkr_handle_fullscreen_event(const SDL_Event* event);

int main() {
    @autoreleasepool {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
        SDL_Window* window = SDL_CreateWindow("Metal overlay test", 0, 0, 640, 480,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) return 1;
        SDL_MetalView view = SDL_Metal_CreateView(window);
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (!view || !SDL_GetWindowWMInfo(window, &info)) return 1;
        bool passed = true;
        {
            auto interface = plume::CreateMetalInterface();
            auto device = interface->createDevice();
            RT64::RenderWorker worker(device.get(), "overlay test", plume::RenderCommandListType::DIRECT);
            plume::RenderWindow native{};
            native.window = (__bridge void*)info.info.cocoa.window;
            native.view = SDL_Metal_GetLayer(view);
            auto swapchain = worker.commandQueue->createSwapChain(plume::RenderSwapChainDesc(
                native, plume::RenderFormat::B8G8R8A8_UNORM, 2));
            if (!swapchain || !swapchain->resize()) return 1;
            RT64::Inspector inspector(device.get(), swapchain.get(),
                RT64::UserConfiguration::GraphicsAPI::Metal, window);
            ImGui::GetIO().IniFilename = nullptr;
            auto check_frame = [&] {
                SDL_PumpEvents();
                // RT64 presents off the main thread. needsResize/resize must see
                // the live Cocoa size there — main-thread-only checks miss the bug.
                bool present_resized = false;
                std::thread([&] {
                    if (swapchain->needsResize()) present_resized = swapchain->resize();
                    else present_resized = true;
                }).join();
                passed &= present_resized;
                int pixels_w = 0, pixels_h = 0;
                SDL_GL_GetDrawableSize(window, &pixels_w, &pixels_h);
                CAMetalLayer* layer = (__bridge CAMetalLayer*)native.view;
                // Both owners must agree. Matching only ImGui to the cached
                // swapchain dimensions misses SDL resizing the layer later.
                passed &= swapchain->getWidth() == unsigned(pixels_w);
                passed &= swapchain->getHeight() == unsigned(pixels_h);
                passed &= std::abs(layer.drawableSize.width - pixels_w) < 0.5;
                passed &= std::abs(layer.drawableSize.height - pixels_h) < 0.5;
                inspector.newFrame(&worker);
                const ImGuiIO& io = ImGui::GetIO();
                std::printf("mode=%s logical=%.0fx%.0f SDL=%dx%d target=%ux%u layer=%.0fx%.0f\n",
                    (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? "fullscreen" : "windowed",
                    io.DisplaySize.x, io.DisplaySize.y, pixels_w, pixels_h,
                    swapchain->getWidth(), swapchain->getHeight(),
                    layer.drawableSize.width, layer.drawableSize.height);
                passed &= std::abs(io.DisplaySize.x * io.DisplayFramebufferScale.x
                    - float(swapchain->getWidth())) < 0.5f;
                passed &= std::abs(io.DisplaySize.y * io.DisplayFramebufferScale.y
                    - float(swapchain->getHeight())) < 0.5f;
                inspector.endFrame();
            };
            for (const auto size : {SDL_Point{640, 480}, SDL_Point{800, 600}}) {
                SDL_SetWindowSize(window, size.x, size.y);
                check_frame();
            }
            auto wait_for_size = [&](int width, int height) {
                const auto deadline = SDL_GetTicks64() + 5000;
                while (SDL_GetTicks64() < deadline) {
                    SDL_PumpEvents();
                    SDL_Event event{};
                    while (SDL_PollEvent(&event)) dkr_handle_fullscreen_event(&event);
                    int actual_w, actual_h;
                    SDL_GetWindowSize(window, &actual_w, &actual_h);
                    NSWindow* native_window = info.info.cocoa.window;
                    // A notched display's fullscreen content height differs
                    // from its desktop bounds. Wait for native fullscreen and
                    // for SDL to agree with the actual Cocoa content bounds.
                    const bool height_ready = height < 0
                        ? (native_window.styleMask & NSWindowStyleMaskFullScreen) != 0
                            && actual_h == int(std::round(native_window.contentView.bounds.size.height))
                        : actual_h == height
                            && (native_window.styleMask & NSWindowStyleMaskFullScreen) == 0;
                    if (actual_w == width && height_ready) return true;
                    SDL_Delay(10);
                }
                int actual_w, actual_h;
                SDL_GetWindowSize(window, &actual_w, &actual_h);
                std::fprintf(stderr, "Fullscreen transition timeout: got %dx%d, expected %dx%d\n",
                    actual_w, actual_h, width, height);
                return false;
            };
            SDL_Rect display{};
            if (SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(window), &display) != 0) return 1;
            SDL_ShowWindow(window);
            const Uint32 window_id = SDL_GetWindowID(window);
            // Graphics changes originate on the renderer thread in the game.
            std::thread([=] { dkr_set_window_fullscreen(window_id, true); }).join();
            if (!wait_for_size(display.w, -1)) return 1;
            dkr_set_window_fullscreen(window_id, true); // Setting, not toggling.
            if (!wait_for_size(display.w, -1)) return 1;
            check_frame();
            std::thread([=] { dkr_set_window_fullscreen(window_id, false); }).join();
            if (!wait_for_size(800, 600)) return 1;
            dkr_set_window_fullscreen(window_id, false);
            if (!wait_for_size(800, 600)) return 1;
            check_frame();
            SDL_HideWindow(window);
        }
        SDL_Metal_DestroyView(view);
        SDL_DestroyWindow(window);
        SDL_Quit();
        if (!passed) return 1;
        std::puts("PASS: SDL, Metal, swapchain and overlay agree across resize and fullscreen");
        return 0;
    }
}
