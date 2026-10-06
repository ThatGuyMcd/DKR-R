#pragma once

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>

namespace dkr::mods { struct PreparedModLaunch; }
namespace dkr::mods::online { class RuntimeResources; }
namespace dkr::runtime::rom { struct Identity; }

struct SDL_Window;
typedef union SDL_Event SDL_Event;

namespace RT64 {
struct Application;
}

namespace dkr::runtime::ui {

enum class LifecycleRequest : std::uint8_t {
    None = 0,
    StopGame,
    Exit,
    Restart,
};

struct StartupResult {
    bool start_game = false;
    LifecycleRequest lifecycle_request = LifecycleRequest::None;
    std::filesystem::path rom_path;
    std::shared_ptr<const dkr::mods::PreparedModLaunch> mods;
};

void configure(const std::filesystem::path& config_directory);
void shutdown_online_mods();
// Game stopped / launch admission only. Empty means a vanilla manifest.
// Throws rather than silently launching if an accepted mod proof is missing.
std::shared_ptr<const dkr::mods::online::RuntimeResources> online_mod_resources();
void persist_settings();
// Called only after an explicitly selected renderer backend fails and RT64
// successfully recovers with Automatic. Persist the recovered choice so the
// next launch does not repeat the same failure loop.
void persist_graphics_api_fallback();
// The bounded software return path is used after experimental failure, and
// also after graceful experimental GPU retirement on Windows. It does not
// change the selected graphics API or settings for the next game launch.
StartupResult run_startup_screen(
    SDL_Window* window,
    const std::filesystem::path& preselected_rom = {},
    bool software_failure_recovery = false);

void attach(RT64::Application& application);
void detach(RT64::Application& application);
void draw(RT64::Application& application);
bool handle_runtime_event(SDL_Event* event);
bool input_capture_active();
void toggle_overlay();
bool overlay_visible();
LifecycleRequest lifecycle_request();
void reset_lifecycle_request();
void report_mod_error(std::string error);
// Main/event thread only, after both experimental game workers have joined.
void report_online_game_end(std::string message, bool lobby_retained);
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
// Isolated diagnostic entry: same manifest/save/host actions as the real UI.
bool configure_owned_online_check(const rom::Identity& identity,bool host,unsigned players,std::string& error);
#endif

} // namespace dkr::runtime::ui
