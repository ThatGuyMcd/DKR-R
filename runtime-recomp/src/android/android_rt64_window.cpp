// Project-owned Android implementation of RT64's ApplicationWindow interface.
// RT64's pinned desktop implementation assumes X11 for __linux__ builds.
#include "hle/rt64_application_window.h"
#include <SDL_syswm.h>
#include <optional>
#include <string>
#include <cstdio>

namespace dkr::runtime::platform { void* sdl_window(); }

namespace RT64 {
namespace {
// DKR owns one RT64 ApplicationWindow at a time. The launcher uses SDL's
// OpenGL renderer, so restore its hint when the game renderer is retired.
ApplicationWindow* externalContextOwner = nullptr;
std::optional<std::string> previousExternalContextHint;
}
ApplicationWindow* ApplicationWindow::HookedApplicationWindow = nullptr;
ApplicationWindow::ApplicationWindow() : listener(nullptr) {}
ApplicationWindow::~ApplicationWindow() {
    if (externalContextOwner == this) {
        if (previousExternalContextHint) {
            SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, previousExternalContextHint->c_str());
        } else {
            SDL_ResetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT);
        }
        externalContextOwner = nullptr;
        previousExternalContextHint.reset();
    }
    SDL_EventFilter current = nullptr; void* data = nullptr;
    if (sdlEventFilterInstalled && SDL_GetEventFilter(&current, &data) &&
        current == sdlEventFilter && data == this)
        SDL_SetEventFilter(sdlEventFilterStored, sdlEventFilterUserdata);
    if (HookedApplicationWindow == this) HookedApplicationWindow = nullptr;
}
void ApplicationWindow::setup(RenderWindow window, Listener* sink, uint32_t) {
    if (!externalContextOwner) {
        const char* hint = SDL_GetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT);
        if (hint) previousExternalContextHint = hint;
        externalContextOwner = this;
        // RT64 owns a Vulkan surface. SDL must not back up/restore an EGL
        // context when Android transiently pauses the Activity.
        const auto changed = SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, "1");
        std::fprintf(stderr, "[android][graphics] external Vulkan context hint=%s\n", changed ? "enabled" : "not applied");
    }
    windowHandle = window; listener = sink;
    sdlWindow = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window());
    fullScreen = true; detectRefreshRate();
}
void ApplicationWindow::setup(const char*, Listener* sink) {
    SDL_Window* window = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window());
    SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
    if (!window || !SDL_GetWindowWMInfo(window, &info)) return;
    setup(info.info.android.window, sink, 0);
}
void ApplicationWindow::setFullScreen(bool) { fullScreen = true; }
void ApplicationWindow::makeResizable() {}
void ApplicationWindow::detectRefreshRate() {
    SDL_DisplayMode mode{};
    refreshRate = SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0
        ? static_cast<uint32_t>(mode.refresh_rate) : 60;
}
uint32_t ApplicationWindow::getRefreshRate() const { return refreshRate; }
bool ApplicationWindow::detectWindowMoved() { return false; }
void ApplicationWindow::sdlCheckFilterInstallation() {
    if (!listener || !listener->usesWindowMessageFilter() || sdlEventFilterInstalled) return;
    SDL_GetEventFilter(&sdlEventFilterStored, &sdlEventFilterUserdata);
    SDL_SetEventFilter(sdlEventFilter, this); sdlEventFilterInstalled = true;
}
int ApplicationWindow::sdlEventFilter(void* data, SDL_Event* event) {
    auto* window = static_cast<ApplicationWindow*>(data);
    if ((event->type >= SDL_APP_TERMINATING && event->type <= SDL_APP_DIDENTERFOREGROUND) ||
        (event->type == SDL_WINDOWEVENT &&
            (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
             event->window.event == SDL_WINDOWEVENT_MINIMIZED ||
             event->window.event == SDL_WINDOWEVENT_RESTORED))) {
        std::fprintf(stderr, "[android][lifecycle] event=%u window-event=%u\n",
            event->type, event->type == SDL_WINDOWEVENT ? event->window.event : 0U);
    }
    if (window->listener && window->listener->sdlEventFilter(event)) return 0;
    return window->sdlEventFilterStored
        ? window->sdlEventFilterStored(window->sdlEventFilterUserdata, event) : 1;
}
}
