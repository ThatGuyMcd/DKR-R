// Opt-in GPU/window test. No ROM, saves, network or game simulation is used.
#ifdef _WIN32
#include <Unknwn.h>
#include <oaidl.h>
#endif
#include "game/runtime_mipmap_loading.hpp"
#include "game/runtime_mipmap_modal.hpp"
#include <SDL.h>
#include <SDL_syswm.h>
#if defined(__APPLE__)
#include <SDL_metal.h>
#endif
#include <thread>
#include <stdexcept>
#include <cstdio>

namespace plume {
#if defined(__APPLE__)
std::unique_ptr<RenderInterface> CreateMetalInterface();
#endif
#ifdef _WIN32
std::unique_ptr<RenderInterface> CreateD3D12Interface();
#endif
#ifdef PLUME_SDL_VULKAN_ENABLED
std::unique_ptr<RenderInterface> CreateVulkanInterface(RenderWindow);
#else
std::unique_ptr<RenderInterface> CreateVulkanInterface();
#endif
} // namespace plume
static unsigned uiFrames = 0;
static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "CHECK FAILED: %s\n", message);
        throw std::runtime_error(message);
    }
}
// Replace only the outer application UI in this harness. The modal and the
// blocked-wait presenter below are the identical headers used by the game.
void dkr::runtime::ui::draw(RT64::Application& app) {
    SDL_Event event;
    while (SDL_PollEvent(&event))
        app.presentQueue->inspector->handleSdlEvent(&event);
    app.presentQueue->inspector->newFrame(app.presentGraphicsWorker.get());
    draw_mipmap_loading_modal();
    app.presentQueue->inspector->endFrame();
    ++uiFrames;
}
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    using namespace plume;
    using namespace RT64;
    try {
        check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video");
        SDL_Window* window =
            SDL_CreateWindow("DKR mipmap loading validation", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 900, 540,
#ifdef _WIN32
                             SDL_WINDOW_SHOWN
#elif defined(__APPLE__)
                             SDL_WINDOW_SHOWN | SDL_WINDOW_METAL
#else
                             SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN
#endif
            );
        check(window != nullptr, "test window");
        RenderWindow handle{};
#ifdef _WIN32
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        check(SDL_GetWindowWMInfo(window, &info) == SDL_TRUE, "window info");
        handle = info.info.win.window;
#elif defined(__APPLE__)
        SDL_MetalView view = SDL_Metal_CreateView(window);
        check(view != nullptr, "metal view");
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        check(SDL_GetWindowWMInfo(window, &info) == SDL_TRUE, "window info");
        handle = {info.info.cocoa.window, SDL_Metal_GetLayer(view)};
#else
        handle = window;
#endif
        ApplicationConfiguration config;
        config.detectDataPath = false;
        config.useConfigurationFile = false;
        Application app(Application::Core{}, config);
        struct Cleanup {
            Application& app;
            ~Cleanup() {
                app.presentQueue.reset();
                app.swapChain.reset();
                app.presentGraphicsWorker.reset();
                app.sharedQueueResources.reset();
                app.device.reset();
                app.renderInterface.reset();
            }
        } cleanup{app};
        const bool dx = std::string(argv[1]) == "dx12";
#ifdef _WIN32
        if (dx)
            app.renderInterface = CreateD3D12Interface();
        else
#endif
        {
#if defined(__APPLE__)
            app.renderInterface = CreateMetalInterface();
#elif defined(PLUME_SDL_VULKAN_ENABLED)
            app.renderInterface = CreateVulkanInterface(handle);
#else
            app.renderInterface = CreateVulkanInterface();
#endif
        }
        check(bool(app.renderInterface), "interface");
        app.device = app.renderInterface->createDevice();
        check(bool(app.device), "device");
        app.presentGraphicsWorker =
            std::make_unique<RenderWorker>(app.device.get(), "Loading UI", RenderCommandListType::DIRECT);
        RenderSwapChainDesc desc;
        desc.renderWindow = handle;
        desc.format = RenderFormat::B8G8R8A8_UNORM;
        desc.textureCount = 2;
        app.swapChain = app.presentGraphicsWorker->commandQueue->createSwapChain(desc);
        check(bool(app.swapChain), "swap chain");
        if (app.swapChain->needsResize())
            check(app.swapChain->resize(), "initial Vulkan swap-chain images");
        app.presentQueue = std::make_unique<PresentQueue>();
        auto& p = *app.presentQueue;
        std::fprintf(stderr, "probe: swap chain initialized\n");
        app.sharedQueueResources = std::make_unique<SharedQueueResources>();
        p.ext.device = app.device.get();
        p.ext.swapChain = app.swapChain.get();
        p.ext.presentGraphicsWorker = app.presentGraphicsWorker.get();
        p.acquiredSemaphore = app.device->createCommandSemaphore();
        for (unsigned i = 0; i < app.swapChain->getTextureCount(); ++i) {
            const RenderTexture* target = app.swapChain->getTexture(i);
            p.swapChainFramebuffers.emplace_back(app.device->createFramebuffer(RenderFramebufferDesc(&target, 1)));
            p.drawSemaphores.emplace_back(app.device->createCommandSemaphore());
        }
        const auto graphicsAPI =
#if defined(__APPLE__)
            UserConfiguration::GraphicsAPI::Metal;
#else
            dx ? UserConfiguration::GraphicsAPI::D3D12 : UserConfiguration::GraphicsAPI::Vulkan;
#endif
        p.inspector = std::make_unique<Inspector>(app.device.get(), app.swapChain.get(), graphicsAPI, window);
        ImGui::GetIO().IniFilename = nullptr;
        std::fprintf(stderr, "probe: inspector initialized\n");
        beginGeneratedMipSession(true);
        MipConfiguration::blockingWait = true;
        dkr::runtime::present_mipmap_loading(app);
        check(uiFrames == 0, "must not present before initialization");
        // The harness has manually initialized exactly the resources that a
        // completed normal presentation guarantees in the actual application.
        app.sharedQueueResources->totalPresentations = 1;
        auto copy = std::make_unique<RenderWorker>(app.device.get(), "Upload copy", RenderCommandListType::COPY);
        auto direct = std::make_unique<RenderWorker>(app.device.get(), "Upload decode", RenderCommandListType::DIRECT);
        ShaderLibrary shaders(false, false);
        shaders.setupCommonShaders(app.renderInterface.get(), app.device.get());
        std::fprintf(stderr, "probe: shaders initialized\n");
        {
            TextureCache cache(direct.get(), copy.get(), 0, &shaders);
            // Delay publication on the genuine cache worker without injecting
            // delays into shipping code or blocking the UI/GPU command queue.
            std::atomic<bool> locked{false};
            std::jthread slowWorker([&] {
                std::unique_lock lock(cache.textureMapMutex);
                locked = true;
                std::this_thread::sleep_for(std::chrono::seconds(3));
            });
            while (!locked.load())
                std::this_thread::yield();
            std::vector<uint8_t> tmem(4096, 128);
            LoadTile tile{};
            tile.fmt = 4;
            tile.siz = 1;
            tile.line = 4;
            cache.queueGPUUploadTMEM(101, 1, tmem.data(), int(tmem.size()), 32, 32, 0, tile, true);
            MipConfiguration::blockingWait = false;
            const auto started = mipClockMs();
            MipWaitFeedbackScope feedback([&] { dkr::runtime::present_mipmap_loading(app); });
            cache.waitForGPUUploads();
            slowWorker.join();
            check(mipClockMs() - started >= 2000, "genuine prolonged upload wait");
            check(uiFrames >= 15, "spinner must receive repeated UI frames during blocked upload");
            // Cache publication wakes its consumer before the worker's local
            // activity guard unwinds. Observe completion across UI frames, as
            // the application does, rather than racing that final destructor.
            const auto feedbackDeadline = mipClockMs() + 500;
            while (mipLoadingVisible() && mipClockMs() < feedbackDeadline) {
                dkr::runtime::ui::draw(app);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(!mipLoadingVisible(), "modal closes promptly after worker completion");
            check(app.sharedQueueResources->totalPresentations == 1 && p.writeCursor == 0 && p.threadCursor == 0 &&
                      p.barrierCursor == 0 && p.presentId == 0,
                  "UI must not advance game queue ownership");
            check(generatedMipStatistics().originals == 1, "real TMEM decode generated a mip chain");
            uint32_t index = 0;
            interop::float2 scale;
            interop::float3 dimensions;
            bool replaced = false, mips = false, shift = false, generated = false;
            check(cache.useTexture(101, 2, index, scale, dimensions, replaced, mips, shift, generated) && mips &&
                      generated && dimensions.z == 6,
                  "worker published all six original mip levels");
            dkr::runtime::ui::draw(app);
            check(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup), "completion dismisses modal");
        }
        std::printf(
            "PASS %s: %u loading UI frames during 3-second real texture-cache wait; six generated levels; auto-dismiss; queue cursors unchanged\n",
            argv[1], uiFrames);
        p.inspector.reset();
        app.presentQueue.reset();
        app.swapChain.reset();
        app.presentGraphicsWorker.reset();
#if defined(__APPLE__)
        SDL_Metal_DestroyView(view);
#endif
        SDL_DestroyWindow(window);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
