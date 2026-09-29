#include "runtime_platform.hpp"
#include "audio_equalizer.hpp"
#include "controller_snapshot.hpp"
#include "controller_mapping_policy.hpp"
#include "netplay/online_input_broker.hpp"
#include "online_input_policy.hpp"
#include "runtime_input.hpp"
#include "runtime_texture_packs.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_netplay.hpp"
#include "startup_performance.hpp"
#include "runtime_telemetry.hpp"
#include "sdl3_input_client.hpp"
#include "ultramodern/ultramodern.hpp"
#include "virtual_pak_policy.hpp"

#if DKR_RUNTIME_HAS_RT64
#include "runtime_ui.hpp"
#include "imgui/imgui.h"
#include <SDL.h>
#if defined(_WIN32)
#include <SDL_syswm.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace {

std::atomic<std::uint64_t> g_audio_buffers{0};
constexpr std::size_t kControllerCount = 4;
std::array<std::atomic<std::uint16_t>, kControllerCount> g_buttons{};
std::array<std::atomic<float>, kControllerCount> g_stick_x{};
std::array<std::atomic<float>, kControllerCount> g_stick_y{};
std::array<std::atomic<std::uint16_t>, kControllerCount> g_physical_buttons{};
std::array<std::atomic<float>, kControllerCount> g_physical_stick_x{};
std::array<std::atomic<float>, kControllerCount> g_physical_stick_y{};
std::atomic<bool> g_online_input_routing{false};
std::atomic<std::uint8_t> g_online_occupied_mask{0U};
std::atomic<std::uint8_t> g_online_local_slot{
    dkr::runtime::netplay::kNoOnlinePlayerSlot};
std::atomic<std::size_t> g_online_input_profile{0U};
std::atomic<float> g_master_volume{1.0F};
std::atomic<float> g_bass_gain{0.0F};
std::atomic<float> g_mid_gain{0.0F};
std::atomic<float> g_treble_gain{0.0F};
std::atomic<std::uint64_t> g_ui_tone_generation{0U};
std::atomic<float> g_ui_tone_frequency_hz{0.0F};
std::atomic<std::uint32_t> g_ui_tone_duration_ms{0U};
std::atomic<bool> g_rumble_enabled{true};
std::atomic<float> g_rumble_strength{1.0F};
std::atomic<dkr::runtime::platform::InputBackend> g_requested_input_backend{
    dkr::runtime::platform::InputBackend::Automatic};
std::atomic<dkr::runtime::platform::InputBackend> g_active_input_backend{
    dkr::runtime::platform::InputBackend::SDL2Compatibility};
std::atomic<bool> g_input_backend_switch_pending{false};
std::atomic<bool> g_input_backend_switch_in_progress{false};

#if DKR_RUNTIME_HAS_RT64
std::mutex g_platform_mutex;
std::mutex g_input_backend_transition_mutex;
SDL_AudioDeviceID g_audio_device = 0;
SDL_AudioDeviceID g_ui_audio_device = 0;
std::array<SDL_GameController*, kControllerCount> g_controllers{};
struct ControllerRecord {
    SDL_GameController* controller = nullptr;
    SDL_Joystick* joystick = nullptr;
    SDL_JoystickID instance = -1;
    std::string persistent_key;
    std::string name;
    bool rumble = false;
    bool gyro = false;
    bool mapped = false;
    std::string mapping_source;
};
std::vector<ControllerRecord> g_controller_devices;
dkr::runtime::controllers::AssignmentMode g_controller_assignment_mode =
    dkr::runtime::controllers::AssignmentMode::Automatic;
dkr::runtime::controllers::DesiredAssignments g_controller_assignments{};
std::array<SDL_GameController*, kControllerCount> g_gyro_controllers{};
dkr::runtime::sdl3_input::Client g_sdl3_input_client;
dkr::runtime::sdl3_input::State g_sdl3_input_state{};
std::array<int, kControllerCount> g_sdl3_player_instances{-1, -1, -1, -1};
std::array<int, kControllerCount> g_sdl3_gyro_instances{-1, -1, -1, -1};
std::unordered_map<int, dkr::runtime::controllers::ControllerSnapshot>
    g_sdl3_previous_event_state;
enum class Sdl3ProbeState : std::uint8_t {
    Idle,
    Running,
    Succeeded,
    Failed,
};
std::atomic<Sdl3ProbeState> g_sdl3_probe_state{Sdl3ProbeState::Idle};
std::mutex g_sdl3_probe_result_mutex;
std::string g_sdl3_probe_error;
std::jthread g_sdl3_probe_worker;
std::string g_input_backend_detail = "Native SDL2 compatibility input.";
bool g_sdl2_controller_subsystems_initialized = false;
std::filesystem::path g_input_config_directory;
std::filesystem::path g_user_mapping_path;
std::unordered_set<std::string> g_database_mapping_guids;
std::unordered_set<std::string> g_user_mapping_guids;
struct ControllerMappingSession {
    enum class State : std::uint8_t { Idle, Capturing, Complete };
    State state = State::Idle;
    SDL_JoystickID instance = -1;
    std::size_t player = 0;
    std::size_t step = 0;
    dkr::runtime::controllers::ControllerMappingDefinition definition{};
    dkr::runtime::controllers::PhysicalInput release_input{};
    std::vector<Sint16> axis_neutral{};
    bool waiting_for_release = false;
    bool success = false;
    std::string controller_name;
    std::string message;
};
ControllerMappingSession g_mapping_session;
SDL_Window* g_window = nullptr;
std::uint32_t g_audio_frequency = 0;
std::vector<std::int16_t> g_audio_swap_buffer;
dkr::runtime::audio::StereoEqualizer g_audio_equalizer;
std::uint32_t g_audio_callback_frames = 0;
bool g_audio_playback_started = false;
std::size_t g_audio_nominal_block_frames = 0;
std::size_t g_audio_prime_target_frames = 0;
std::uint32_t g_audio_cushion_blocks = 2;
std::uint64_t g_ui_tone_seen_generation = 0U;
std::size_t g_ui_tone_frames_remaining = 0U;
std::size_t g_ui_tone_total_frames = 0U;
double g_ui_tone_phase = 0.0;
float g_ui_tone_active_frequency_hz = 0.0F;
std::vector<std::int16_t> g_ui_tone_buffer;
constexpr std::uint64_t kFullscreenCursorIdleMs = 3'000U;
std::uint64_t g_cursor_last_activity_ms = 0;
bool g_cursor_hidden = false;
bool g_cursor_was_fullscreen = false;

constexpr double kPi = 3.14159265358979323846;

float SynthesiseCartridgeTone(double phase, float progress,
                              float remaining_fraction) {
    const double sine = std::sin(phase);
    const float triangle = static_cast<float>(
        (2.0 / kPi) * std::asin(sine));
    const float pulse = sine >= 0.0 ? 1.0F : -1.0F;
    const float octave = static_cast<float>(std::sin(phase * 2.0));
    const float voice = triangle * 0.58F + pulse * 0.20F + octave * 0.22F;

    const float attack_position = std::clamp(progress / 0.055F, 0.0F, 1.0F);
    const float attack = attack_position * attack_position *
                         (3.0F - 2.0F * attack_position);
    const float release_position = std::clamp(
        remaining_fraction / 0.28F, 0.0F, 1.0F);
    const float release = release_position * release_position *
                          (3.0F - 2.0F * release_position);
    const float pluck = 1.0F - progress * 0.30F;
    return voice * 5000.0F * attack * release * pluck;
}

bool QueueStandaloneUiToneLocked(float frequency_hz,
                                 std::uint32_t duration_ms) {
    constexpr std::uint32_t kUiSampleRate = 48'000U;
    if (g_ui_audio_device == 0) {
        SDL_AudioSpec desired{};
        SDL_AudioSpec obtained{};
        desired.freq = static_cast<int>(kUiSampleRate);
        desired.format = AUDIO_S16SYS;
        desired.channels = 2;
        desired.samples = 512;
        g_ui_audio_device = SDL_OpenAudioDevice(
            nullptr, 0, &desired, &obtained, 0);
        if (g_ui_audio_device == 0) {
            std::fprintf(stderr,
                         "[boot][audio] launcher cue device open failed: %s\n",
                         SDL_GetError());
            return false;
        }
    }

    const std::size_t frame_count = std::max<std::size_t>(
        (static_cast<std::size_t>(kUiSampleRate) * duration_ms) / 1000U, 1U);
    g_ui_tone_buffer.resize(frame_count * 2U);
    double phase = 0.0;
    const double phase_step = (2.0 * kPi * static_cast<double>(frequency_hz)) /
                              static_cast<double>(kUiSampleRate);
    const float master = g_master_volume.load(std::memory_order_relaxed);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const float progress = static_cast<float>(frame) /
                               static_cast<float>(frame_count);
        const float remaining = 1.0F - progress;
        const long sample = std::lround(
            SynthesiseCartridgeTone(phase, progress, remaining) * master);
        const auto clamped = static_cast<std::int16_t>(
            std::clamp(sample, -32768L, 32767L));
        g_ui_tone_buffer[frame * 2U] = clamped;
        g_ui_tone_buffer[frame * 2U + 1U] = clamped;
        phase += phase_step;
        if (phase >= 2.0 * kPi) {
            phase = std::fmod(phase, 2.0 * kPi);
        }
    }

    SDL_ClearQueuedAudio(g_ui_audio_device);
    const auto byte_count = static_cast<Uint32>(
        g_ui_tone_buffer.size() * sizeof(std::int16_t));
    if (SDL_QueueAudio(g_ui_audio_device, g_ui_tone_buffer.data(),
                       byte_count) != 0) {
        std::fprintf(stderr, "[boot][audio] launcher cue queue failed: %s\n",
                     SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(g_ui_audio_device, 0);
    return true;
}

float NormaliseAxis(Sint16 value, Sint16 deadzone = 7849) {
    const int magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= deadzone) {
        return 0.0F;
    }
    const float scaled = static_cast<float>(magnitude - deadzone) /
                         static_cast<float>(32767 - deadzone);
    return std::copysign(std::min(scaled, 1.0F), static_cast<float>(value));
}

float NormalisePreviewAxis(Sint16 value) {
    return value >= 0
        ? static_cast<float>(value) / 32767.0F
        : static_cast<float>(value) / 32768.0F;
}

std::string HashControllerIdentity(const std::string& identity) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : identity) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    char encoded[17]{};
    std::snprintf(encoded, sizeof(encoded), "%016llx",
                  static_cast<unsigned long long>(hash));
    return encoded;
}

std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::filesystem::path RuntimeAssetPath(const std::filesystem::path& relative) {
    if (char* base = SDL_GetBasePath(); base != nullptr) {
        const std::filesystem::path candidate =
            std::filesystem::path(base) / relative;
        SDL_free(base);
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    const std::filesystem::path candidate =
        std::filesystem::current_path() / relative;
    std::error_code error;
    return std::filesystem::is_regular_file(candidate, error)
        ? candidate : std::filesystem::path{};
}

std::string LowerText(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

dkr::runtime::platform::InputBackend ParseInputBackend(
    const std::string& value) {
    const std::string normalized = LowerText(value);
    if (normalized == "sdl3" || normalized == "sdl3_native" ||
        normalized == "2") {
        return dkr::runtime::platform::InputBackend::SDL3Native;
    }
    if (normalized == "sdl2" || normalized == "sdl2_compatibility" ||
        normalized == "1") {
        return dkr::runtime::platform::InputBackend::SDL2Compatibility;
    }
    return dkr::runtime::platform::InputBackend::Automatic;
}

bool IsSteamDeckHost() {
#if defined(_WIN32)
    return false;
#else
    if (const char* deck = std::getenv("SteamDeck");
        deck != nullptr && std::string(deck) == "1") {
        return true;
    }
    for (const std::filesystem::path path : {
             std::filesystem::path("/sys/class/dmi/id/board_vendor"),
             std::filesystem::path("/sys/class/dmi/id/product_name")}) {
        std::ifstream input(path);
        std::string text;
        std::getline(input, text);
        text = LowerText(text);
        if (text.find("valve") != std::string::npos ||
            text.find("jupiter") != std::string::npos ||
            text.find("galileo") != std::string::npos) {
            return true;
        }
    }
    return false;
#endif
}

bool ShouldTrySdl3Input() {
    return dkr::runtime::platform::resolve_input_backend(
               g_requested_input_backend.load(std::memory_order_acquire),
               IsSteamDeckHost(), DKR_RUNTIME_HAS_SDL3_INPUT_HOST != 0) ==
           dkr::runtime::platform::InputBackend::SDL3Native;
}

std::filesystem::path RuntimeInputHostPath() {
    if (const char* override_path = std::getenv("DKR_SDL3_INPUT_HOST");
        override_path != nullptr && *override_path != '\0') {
        std::filesystem::path candidate(override_path);
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
#if defined(_WIN32)
    constexpr const char* kHostName = "DKR-R-InputHost.exe";
#else
    constexpr const char* kHostName = "DKR-R-InputHost";
#endif
    std::vector<std::filesystem::path> candidates;
    if (char* base = SDL_GetBasePath(); base != nullptr) {
        const std::filesystem::path directory(base);
        SDL_free(base);
        candidates.push_back(directory / "libexec" / "dkr-r" / kHostName);
        candidates.push_back(directory / ".." / "libexec" / "dkr-r" /
                             kHostName);
        candidates.push_back(directory / kHostName);
    }
    candidates.push_back(std::filesystem::current_path() / "libexec" /
                         "dkr-r" / kHostName);
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return std::filesystem::weakly_canonical(candidate, error);
        }
    }
    return {};
}

std::string JoystickGuidText(int device_index) {
    char guid_text[33]{};
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device_index),
                              guid_text, sizeof(guid_text));
    return guid_text;
}

std::string ControllerPersistentKey(int device_index, SDL_Joystick* joystick) {
    std::string identity;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (const char* serial = SDL_JoystickGetSerial(joystick);
        serial != nullptr && *serial != '\0') {
        identity = "serial:" + std::string(serial);
#if SDL_VERSION_ATLEAST(2, 24, 0)
    } else if (const char* path = SDL_JoystickPathForIndex(device_index);
               path != nullptr && *path != '\0') {
        identity = "path:" + std::string(path);
#endif
    }
#endif
    if (identity.empty()) {
        identity = "guid:" + JoystickGuidText(device_index);
    }
    identity += ":" + std::to_string(SDL_JoystickGetVendor(joystick));
    identity += ":" + std::to_string(SDL_JoystickGetProduct(joystick));
    identity += ":" + std::to_string(SDL_JoystickGetProductVersion(joystick));
    return HashControllerIdentity(identity);
}

void CollectMappingGuids(const std::filesystem::path& path,
                         std::unordered_set<std::string>& output) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t comma = line.find(',');
        if (comma != std::string::npos && comma > 0U) {
            output.insert(line.substr(0, comma));
        }
    }
}

int LoadControllerMappings(const std::filesystem::path& path,
                           std::unordered_set<std::string>& guid_set,
                           const char* label) {
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(path, error)) {
        std::fprintf(stderr, "[boot][input] %s mapping file not found\n", label);
        return 0;
    }
    const int count = SDL_GameControllerAddMappingsFromFile(PathUtf8(path).c_str());
    if (count < 0) {
        std::fprintf(stderr, "[boot][input] %s mappings failed: %s\n",
                     label, SDL_GetError());
        return 0;
    }
    CollectMappingGuids(path, guid_set);
    std::fprintf(stderr, "[boot][input] %s mappings loaded=%d\n", label, count);
    return count;
}

std::string MappingSourceForGuid(const std::string& guid) {
    if (g_user_mapping_guids.contains(guid)) {
        return "Custom DKR-R mapping";
    }
    if (g_database_mapping_guids.contains(guid)) {
        return "DKR-R controller database";
    }
    return "SDL built-in mapping";
}

bool ControllerRecordAttached(const ControllerRecord& record) {
    return record.mapped
        ? record.controller != nullptr &&
              SDL_GameControllerGetAttached(record.controller) == SDL_TRUE
        : record.joystick != nullptr &&
              SDL_JoystickGetAttached(record.joystick) == SDL_TRUE;
}

bool IsNativeSteamDeckGyro(const ControllerRecord& record) {
    if (!record.mapped || record.controller == nullptr || !record.gyro ||
        !ControllerRecordAttached(record)) {
        return false;
    }

    constexpr char kSteamDeckName[] = "steam deck";
    return std::search(
               record.name.begin(), record.name.end(),
               kSteamDeckName,
               kSteamDeckName + sizeof(kSteamDeckName) - 1U,
               [](char left, char right) {
                   return std::tolower(static_cast<unsigned char>(left)) ==
                          std::tolower(static_cast<unsigned char>(right));
               }) != record.name.end();
}

void CloseControllerRecord(ControllerRecord& record) {
    if (record.controller != nullptr) {
        SDL_GameControllerClose(record.controller);
        record.controller = nullptr;
        record.joystick = nullptr;
    } else if (record.joystick != nullptr) {
        SDL_JoystickClose(record.joystick);
        record.joystick = nullptr;
    }
}

void ReconcileControllers() {
    std::vector<dkr::runtime::controllers::Device> devices;
    devices.reserve(g_controller_devices.size());
    for (const ControllerRecord& record : g_controller_devices) {
        if (!record.mapped || record.controller == nullptr) {
            continue;
        }
        devices.push_back({static_cast<int>(record.instance),
                           record.persistent_key});
        SDL_GameControllerSetPlayerIndex(record.controller, -1);
    }
    if (g_controller_assignment_mode ==
        dkr::runtime::controllers::AssignmentMode::Automatic) {
        dkr::runtime::controllers::populate_automatic_claims(
            g_controller_assignments, devices);
    }
    const auto resolved = dkr::runtime::controllers::resolve(
        g_controller_assignments, devices);
    const auto previous = g_controllers;
    g_controllers.fill(nullptr);
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        const auto match = std::find_if(
            g_controller_devices.begin(), g_controller_devices.end(),
            [&](const ControllerRecord& record) {
                return record.mapped &&
                       record.instance == resolved[player];
            });
        if (match != g_controller_devices.end()) {
            g_controllers[player] = match->controller;
            SDL_GameControllerSetPlayerIndex(match->controller,
                                             static_cast<int>(player));
        }
        if (previous[player] != g_controllers[player]) {
            g_buttons[player].store(0, std::memory_order_release);
            g_stick_x[player].store(0.0F, std::memory_order_release);
            g_stick_y[player].store(0.0F, std::memory_order_release);
        }
    }

    for (std::size_t player = 0; player < kControllerCount; ++player) {
        SDL_GameController* selected_gyro = nullptr;
        if (g_controllers[player] != nullptr &&
            SDL_GameControllerHasSensor(g_controllers[player],
                                        SDL_SENSOR_GYRO) == SDL_TRUE) {
            selected_gyro = g_controllers[player];
        }
        // SteamOS can expose gameplay through Steam Input's virtual pad while
        // SDL's HIDAPI exposes the Deck's physical IMU on a separate native
        // "Steam Deck" controller. Keep gameplay assignments unchanged and
        // use that native sensor only as Player 1's gyro source when the
        // assigned virtual pad has no gyro of its own.
        if (player == 0U && selected_gyro == nullptr) {
            const auto native_deck = std::find_if(
                g_controller_devices.begin(), g_controller_devices.end(),
                IsNativeSteamDeckGyro);
            if (native_deck != g_controller_devices.end()) {
                selected_gyro = native_deck->controller;
            }
        }
        if (selected_gyro != g_gyro_controllers[player]) {
            g_gyro_controllers[player] = selected_gyro;
            if (selected_gyro != nullptr) {
                if (SDL_GameControllerSetSensorEnabled(
                        selected_gyro, SDL_SENSOR_GYRO, SDL_TRUE) != 0) {
                    std::fprintf(stderr,
                                 "[boot][input] player %zu gyro enable failed: %s\n",
                                 player + 1U, SDL_GetError());
                } else {
                    const char* source_name =
                        SDL_GameControllerName(selected_gyro);
                    const float sensor_rate =
                        SDL_GameControllerGetSensorDataRate(
                            selected_gyro, SDL_SENSOR_GYRO);
                    std::fprintf(stderr,
                                 "[boot][input] player %zu gyro source=%s "
                                 "rate=%.1fHz timestamped=yes\n",
                                 player + 1U,
                                 source_name != nullptr ? source_name
                                                        : "unnamed SDL controller",
                                 sensor_rate);
                }
            }
            dkr::runtime::input::recenter_gyro(player);
        }
    }
}

std::atomic<bool> g_controller_inventory_dirty{true};
int SDLCALL ControllerInventoryEvent(void*, SDL_Event* event) {
    if (event->type == SDL_JOYDEVICEADDED || event->type == SDL_JOYDEVICEREMOVED ||
        event->type == SDL_CONTROLLERDEVICEADDED || event->type == SDL_CONTROLLERDEVICEREMOVED ||
        event->type == SDL_CONTROLLERDEVICEREMAPPED) {
        g_controller_inventory_dirty.store(true, std::memory_order_relaxed);
    }
    return 1;
}

void RefreshControllers(bool force = true) {
    // All callers own g_platform_mutex. Sampling still happens every poll;
    // only enumeration and assignment reconciliation are cached.
    static auto next_inventory_check = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const bool dirty = g_controller_inventory_dirty.exchange(false, std::memory_order_relaxed);
    if (!force && !dirty && now < next_inventory_check) return;
    next_inventory_check = now + std::chrono::seconds(1);
    for (auto iterator = g_controller_devices.begin();
         iterator != g_controller_devices.end();) {
        if (!ControllerRecordAttached(*iterator)) {
            if (g_mapping_session.state ==
                    ControllerMappingSession::State::Capturing &&
                g_mapping_session.instance == iterator->instance) {
                g_mapping_session.state = ControllerMappingSession::State::Complete;
                g_mapping_session.success = false;
                g_mapping_session.message =
                    "The controller disconnected before setup finished.";
            }
            CloseControllerRecord(*iterator);
            iterator = g_controller_devices.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
        const bool already_open = std::any_of(
            g_controller_devices.begin(), g_controller_devices.end(),
            [instance](const ControllerRecord& record) {
                return record.instance == instance;
            });
        if (already_open) {
            continue;
        }
        const bool mapped = SDL_IsGameController(index) == SDL_TRUE;
        ControllerRecord record{};
        record.instance = instance;
        record.mapped = mapped;
        const std::string guid = JoystickGuidText(index);
        if (mapped) {
            record.controller = SDL_GameControllerOpen(index);
            if (record.controller == nullptr) {
                std::fprintf(stderr,
                             "[boot][input] failed to open mapped controller: %s\n",
                             SDL_GetError());
                continue;
            }
            record.joystick = SDL_GameControllerGetJoystick(record.controller);
            const char* name = SDL_GameControllerName(record.controller);
            record.name = name != nullptr && *name != '\0'
                ? name : "Game controller";
            record.persistent_key = ControllerPersistentKey(index, record.joystick);
#if SDL_VERSION_ATLEAST(2, 0, 18)
            record.rumble =
                SDL_GameControllerHasRumble(record.controller) == SDL_TRUE;
#else
            record.rumble = true;
#endif
            record.gyro = SDL_GameControllerHasSensor(
                record.controller, SDL_SENSOR_GYRO) == SDL_TRUE;
            record.mapping_source = MappingSourceForGuid(guid);
        } else {
            record.joystick = SDL_JoystickOpen(index);
            if (record.joystick == nullptr) {
                std::fprintf(stderr,
                             "[boot][input] failed to open raw controller: %s\n",
                             SDL_GetError());
                continue;
            }
            const char* name = SDL_JoystickName(record.joystick);
            record.name = name != nullptr && *name != '\0'
                ? name : "Unmapped controller";
            record.persistent_key = ControllerPersistentKey(index, record.joystick);
#if SDL_VERSION_ATLEAST(2, 0, 18)
            record.rumble = SDL_JoystickHasRumble(record.joystick) == SDL_TRUE;
#endif
            record.mapping_source = "Setup required";
        }
        {
            // A path or serial normally distinguishes identical pads. Retain a
            // deterministic session suffix for drivers which expose neither.
            const std::string base_key = record.persistent_key;
            int duplicate = 1;
            while (std::any_of(g_controller_devices.begin(),
                               g_controller_devices.end(),
                               [&](const ControllerRecord& existing) {
                                   return existing.persistent_key ==
                                       record.persistent_key;
                               })) {
                record.persistent_key = base_key + "-" +
                                        std::to_string(++duplicate);
            }
            std::fprintf(stderr,
                         "[boot][input] connected controller=%s mapping=%s\n",
                         record.name.c_str(), record.mapping_source.c_str());
            g_controller_devices.push_back(std::move(record));
        }
    }
    ReconcileControllers();
}

const dkr::runtime::sdl3_input::Device* Sdl3DeviceForInstance(int instance) {
    const auto match = std::find_if(
        g_sdl3_input_state.devices.begin(), g_sdl3_input_state.devices.end(),
        [instance](const dkr::runtime::sdl3_input::Device& device) {
            return device.instance == instance && device.snapshot.connected;
        });
    return match != g_sdl3_input_state.devices.end() ? &*match : nullptr;
}

bool Sdl3NativeDeckGyro(const dkr::runtime::sdl3_input::Device& device) {
    return device.snapshot.connected && device.mapped && device.gyro &&
           LowerText(device.name).find("steam deck") != std::string::npos;
}

void ReconcileSdl3Controllers() {
    std::vector<dkr::runtime::controllers::Device> devices;
    devices.reserve(g_sdl3_input_state.devices.size());
    for (const auto& device : g_sdl3_input_state.devices) {
        if (device.snapshot.connected && device.mapped) {
            devices.push_back({
                device.instance,
                dkr::runtime::platform::canonical_controller_key(
                    device.persistent_key)});
        }
    }
    if (g_controller_assignment_mode ==
        dkr::runtime::controllers::AssignmentMode::Automatic) {
        dkr::runtime::controllers::populate_automatic_claims(
            g_controller_assignments, devices);
    }
    const auto resolved = dkr::runtime::controllers::resolve(
        g_controller_assignments, devices);
    const auto previous_players = g_sdl3_player_instances;
    const auto previous_gyro = g_sdl3_gyro_instances;
    for (std::size_t player = 0U; player < kControllerCount; ++player) {
        g_sdl3_player_instances[player] = resolved[player];
        int gyro_instance = -1;
        if (const auto* assigned = Sdl3DeviceForInstance(resolved[player]);
            assigned != nullptr && assigned->gyro) {
            gyro_instance = assigned->instance;
        }
        if (player == 0U && gyro_instance < 0) {
            const auto native_deck = std::find_if(
                g_sdl3_input_state.devices.begin(),
                g_sdl3_input_state.devices.end(), Sdl3NativeDeckGyro);
            if (native_deck != g_sdl3_input_state.devices.end()) {
                gyro_instance = native_deck->instance;
            }
        }
        g_sdl3_gyro_instances[player] = gyro_instance;
        if (previous_players[player] != g_sdl3_player_instances[player]) {
            g_buttons[player].store(0U, std::memory_order_release);
            g_stick_x[player].store(0.0F, std::memory_order_release);
            g_stick_y[player].store(0.0F, std::memory_order_release);
        }
        if (previous_gyro[player] != g_sdl3_gyro_instances[player]) {
            dkr::runtime::input::recenter_gyro(player);
        }
    }
}

void RefreshSdl3Controllers() {
    g_sdl3_input_state = g_sdl3_input_client.state();
    ReconcileSdl3Controllers();
}

void PushSdl3Events() {
    RefreshSdl3Controllers();
    std::unordered_set<int> present;
    for (const auto& device : g_sdl3_input_state.devices) {
        if (!device.snapshot.connected) continue;
        present.insert(device.instance);
        const auto previous = g_sdl3_previous_event_state.find(device.instance);
        const dkr::runtime::controllers::ControllerSnapshot empty{};
        const auto& before = previous != g_sdl3_previous_event_state.end()
            ? previous->second : empty;
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
            const bool was_pressed = before.buttons[
                static_cast<std::size_t>(button)] != 0U;
            const bool pressed = device.snapshot.buttons[
                static_cast<std::size_t>(button)] != 0U;
            if (was_pressed == pressed) continue;
            SDL_Event event{};
            event.type = pressed ? SDL_CONTROLLERBUTTONDOWN
                                 : SDL_CONTROLLERBUTTONUP;
            event.cbutton.timestamp = SDL_GetTicks();
            event.cbutton.which = static_cast<SDL_JoystickID>(device.instance);
            event.cbutton.button = static_cast<Uint8>(button);
            event.cbutton.state = pressed ? SDL_PRESSED : SDL_RELEASED;
            SDL_PushEvent(&event);
        }
        for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
            const Sint16 before_value = before.axes[
                static_cast<std::size_t>(axis)];
            const Sint16 value = device.snapshot.axes[
                static_cast<std::size_t>(axis)];
            if (before_value == value) continue;
            SDL_Event event{};
            event.type = SDL_CONTROLLERAXISMOTION;
            event.caxis.timestamp = SDL_GetTicks();
            event.caxis.which = static_cast<SDL_JoystickID>(device.instance);
            event.caxis.axis = static_cast<Uint8>(axis);
            event.caxis.value = value;
            SDL_PushEvent(&event);
        }
        g_sdl3_previous_event_state[device.instance] = device.snapshot;
    }
    for (auto iterator = g_sdl3_previous_event_state.begin();
         iterator != g_sdl3_previous_event_state.end();) {
        if (!present.contains(iterator->first)) {
            iterator = g_sdl3_previous_event_state.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

std::vector<std::string> ReadMappingLines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line.front() != '#') {
            lines.push_back(line);
        }
    }
    return lines;
}

std::string MappingLineKey(const std::string& line) {
    const std::size_t comma = line.find(',');
    if (comma == std::string::npos || comma == 0U) {
        return {};
    }
    std::string key = line.substr(0, comma);
    const std::size_t platform = line.find(",platform:");
    if (platform != std::string::npos) {
        const std::size_t value = platform + 10U;
        const std::size_t end = line.find(',', value);
        key += "|" + line.substr(value, end == std::string::npos
            ? std::string::npos : end - value);
    }
    return key;
}

bool WriteUserMappings(const std::vector<std::string>& lines,
                       std::string& status) {
    if (g_user_mapping_path.empty()) {
        status = "The controller mapping folder has not been configured.";
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(g_user_mapping_path.parent_path(), error);
    if (error) {
        status = "Could not create the controller mapping folder.";
        return false;
    }
    const std::filesystem::path temporary =
        g_user_mapping_path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            status = "Could not create the controller mapping file.";
            return false;
        }
        output << "# DKR-R user controller mappings\n";
        output << "# Generated by the in-app controller setup wizard.\n";
        for (const std::string& line : lines) {
            output << line << '\n';
        }
        if (!output) {
            status = "Could not finish writing the controller mapping file.";
            return false;
        }
    }
    std::filesystem::remove(g_user_mapping_path, error);
    error.clear();
    std::filesystem::rename(temporary, g_user_mapping_path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        status = "Could not replace the controller mapping file.";
        return false;
    }
    return true;
}

bool MergeUserMappings(const std::vector<std::string>& additions,
                       std::string& status) {
    std::vector<std::string> merged = ReadMappingLines(g_user_mapping_path);
    for (const std::string& addition : additions) {
        const std::string key = MappingLineKey(addition);
        if (key.empty()) {
            continue;
        }
        merged.erase(std::remove_if(merged.begin(), merged.end(),
            [&](const std::string& existing) {
                return MappingLineKey(existing) == key;
            }), merged.end());
        merged.push_back(addition);
    }
    if (!WriteUserMappings(merged, status)) {
        return false;
    }
    g_user_mapping_guids.clear();
    CollectMappingGuids(g_user_mapping_path, g_user_mapping_guids);
    return true;
}

int DeviceIndexForInstance(SDL_JoystickID instance) {
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        if (SDL_JoystickGetDeviceInstanceID(index) == instance) {
            return index;
        }
    }
    return -1;
}

void FinishControllerMapping() {
    const auto record = std::find_if(
        g_controller_devices.begin(), g_controller_devices.end(),
        [](const ControllerRecord& candidate) {
            return candidate.instance == g_mapping_session.instance;
        });
    const int device_index = DeviceIndexForInstance(g_mapping_session.instance);
    if (record == g_controller_devices.end() || record->joystick == nullptr ||
        device_index < 0) {
        g_mapping_session.state = ControllerMappingSession::State::Complete;
        g_mapping_session.success = false;
        g_mapping_session.message =
            "The controller disconnected before its mapping could be saved.";
        return;
    }

    const std::string guid = JoystickGuidText(device_index);
    const std::string mapping = dkr::runtime::controllers::build_sdl_mapping(
        guid, record->name, SDL_GetPlatform(), g_mapping_session.definition);
    if (mapping.empty() || SDL_GameControllerAddMapping(mapping.c_str()) < 0) {
        g_mapping_session.state = ControllerMappingSession::State::Complete;
        g_mapping_session.success = false;
        g_mapping_session.message = std::string(
            "SDL rejected this mapping: ") + SDL_GetError();
        return;
    }

    std::string status;
    if (!MergeUserMappings({mapping}, status)) {
        g_mapping_session.state = ControllerMappingSession::State::Complete;
        g_mapping_session.success = false;
        g_mapping_session.message = status;
        return;
    }

    const std::string persistent_key = record->persistent_key;
    dkr::runtime::controllers::claim(g_controller_assignments,
                                     g_mapping_session.player,
                                     persistent_key);
    g_controller_assignment_mode =
        dkr::runtime::controllers::AssignmentMode::Manual;
    CloseControllerRecord(*record);
    g_controller_devices.erase(record);
    RefreshControllers();

    g_mapping_session.state = ControllerMappingSession::State::Complete;
    g_mapping_session.success = true;
    g_mapping_session.message =
        "Controller setup complete. The mapping is active and saved locally.";
}

void ClearPublishedControllerInput(bool recenter_gyro) {
    const bool online_routing =
        g_online_input_routing.load(std::memory_order_acquire);
    for (std::size_t player = 0U; player < kControllerCount; ++player) {
        g_physical_buttons[player].store(0U, std::memory_order_release);
        g_physical_stick_x[player].store(0.0F, std::memory_order_release);
        g_physical_stick_y[player].store(0.0F, std::memory_order_release);
        if (!online_routing) {
            g_buttons[player].store(0U, std::memory_order_release);
            g_stick_x[player].store(0.0F, std::memory_order_release);
            g_stick_y[player].store(0.0F, std::memory_order_release);
        }
        if (recenter_gyro) {
            dkr::runtime::input::recenter_gyro(player);
        }
    }
}

void StopSdl2ControllerBackend() {
    if (!g_sdl2_controller_subsystems_initialized) return;

    SDL_GameControllerEventState(SDL_DISABLE);
    SDL_JoystickEventState(SDL_DISABLE);
    {
        std::scoped_lock lock(g_platform_mutex);
        for (ControllerRecord& record : g_controller_devices) {
            if (record.controller != nullptr) {
                SDL_GameControllerRumble(record.controller, 0U, 0U, 0U);
            } else if (record.joystick != nullptr) {
                SDL_JoystickRumble(record.joystick, 0U, 0U, 0U);
            }
            CloseControllerRecord(record);
        }
        g_controller_devices.clear();
        g_controllers.fill(nullptr);
        g_gyro_controllers.fill(nullptr);
        if (g_mapping_session.state ==
            ControllerMappingSession::State::Capturing) {
            g_mapping_session.state =
                ControllerMappingSession::State::Complete;
            g_mapping_session.success = false;
            g_mapping_session.message =
                "Controller setup was cancelled when the input backend changed.";
        }
    }
    SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_JOYDEVICEREMOVED);
    SDL_FlushEvents(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERSENSORUPDATE);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC |
                      SDL_INIT_SENSOR);
    g_sdl2_controller_subsystems_initialized = false;
}

void ClearSdl3ControllerState() {
    std::scoped_lock lock(g_platform_mutex);
    g_sdl3_input_state = {};
    g_sdl3_player_instances.fill(-1);
    g_sdl3_gyro_instances.fill(-1);
    g_sdl3_previous_event_state.clear();
}

void StopSdl3InputBackend() {
    std::vector<int> instances;
    {
        std::scoped_lock lock(g_platform_mutex);
        instances.reserve(g_sdl3_input_state.devices.size());
        for (const auto& device : g_sdl3_input_state.devices) {
            if (device.snapshot.connected) instances.push_back(device.instance);
        }
    }
    for (const int instance : instances) {
        g_sdl3_input_client.rumble(instance, 0U, 0U, 0U);
    }
    g_sdl3_input_client.stop();
    ClearSdl3ControllerState();
}

bool InitialiseSdl2ControllerBackend(bool subsystems_already_initialised) {
    if (!subsystems_already_initialised &&
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC |
                          SDL_INIT_SENSOR) != 0) {
        g_input_backend_detail = std::string(
            "SDL2 controller fallback failed: ") + SDL_GetError();
        std::fprintf(stderr, "[boot][input] %s\n",
                     g_input_backend_detail.c_str());
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC |
                          SDL_INIT_SENSOR);
        return false;
    }
    g_sdl2_controller_subsystems_initialized = true;
    g_database_mapping_guids.clear();
    g_user_mapping_guids.clear();
    LoadControllerMappings(
        RuntimeAssetPath("assets/controllers/gamecontrollerdb.txt"),
        g_database_mapping_guids, "DKR-R database");
    LoadControllerMappings(g_user_mapping_path, g_user_mapping_guids, "user");
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    {
        std::scoped_lock lock(g_platform_mutex);
        RefreshControllers();
    }
    g_active_input_backend.store(
        dkr::runtime::platform::InputBackend::SDL2Compatibility,
        std::memory_order_release);
    g_input_backend_detail = "Native SDL2 compatibility input active.";
    return true;
}

bool StartSdl3InputClient(std::string& error,
                          std::stop_token stop_token = {}) {
#if !DKR_RUNTIME_HAS_SDL3_INPUT_HOST
    error = "This build does not contain the SDL3 input host.";
    return false;
#else
    const std::filesystem::path host = RuntimeInputHostPath();
    const std::filesystem::path mappings = RuntimeAssetPath(
        "assets/controllers/gamecontrollerdb.txt");
    if (host.empty()) {
        error = "The private SDL3 input host was not found beside DKR-R.";
        return false;
    }
    return g_sdl3_input_client.start(host, mappings, error, stop_token);
#endif
}

void ActivateStartedSdl3InputBackend(const char* prefix = nullptr) {
    {
        std::scoped_lock lock(g_platform_mutex);
        g_sdl3_input_state = g_sdl3_input_client.state();
        ReconcileSdl3Controllers();
        g_sdl3_previous_event_state.clear();
        for (const auto& device : g_sdl3_input_state.devices) {
            if (device.snapshot.connected) {
                g_sdl3_previous_event_state.emplace(device.instance,
                                                     device.snapshot);
            }
        }
    }
    g_active_input_backend.store(
        dkr::runtime::platform::InputBackend::SDL3Native,
        std::memory_order_release);
    g_input_backend_detail = prefix != nullptr
        ? std::string(prefix) + g_sdl3_input_client.detail()
        : g_sdl3_input_client.detail();
    std::fprintf(stderr, "[boot][input] %s\n",
                 g_input_backend_detail.c_str());
}

bool StartSdl3InputBackend() {
    std::string error;
    if (!StartSdl3InputClient(error)) {
        g_input_backend_detail = error;
        std::fprintf(stderr,
                     "[boot][input] SDL3 native host unavailable: %s\n",
                     error.c_str());
        return false;
    }
    ActivateStartedSdl3InputBackend();
    return true;
}

void CancelSdl3InputProbe() {
    if (g_sdl3_probe_worker.joinable()) {
        g_sdl3_probe_worker.request_stop();
        g_sdl3_probe_worker.join();
    }
    if (g_sdl3_probe_state.load(std::memory_order_acquire) ==
        Sdl3ProbeState::Succeeded) {
        g_sdl3_input_client.stop();
    }
    {
        std::scoped_lock lock(g_sdl3_probe_result_mutex);
        g_sdl3_probe_error.clear();
    }
    g_sdl3_probe_state.store(Sdl3ProbeState::Idle,
                             std::memory_order_release);
}

bool BeginSdl3InputProbe() {
#if !DKR_RUNTIME_HAS_SDL3_INPUT_HOST
    g_input_backend_detail = "This build does not contain the SDL3 input host.";
    return false;
#else
    if (g_sdl3_probe_state.load(std::memory_order_acquire) ==
        Sdl3ProbeState::Running) {
        return true;
    }
    if (g_sdl3_probe_worker.joinable()) g_sdl3_probe_worker.join();

    const std::filesystem::path host = RuntimeInputHostPath();
    if (host.empty()) {
        g_input_backend_detail =
            "The private SDL3 input host was not found beside DKR-R.";
        return false;
    }
    const std::filesystem::path mappings = RuntimeAssetPath(
        "assets/controllers/gamecontrollerdb.txt");
    {
        std::scoped_lock lock(g_sdl3_probe_result_mutex);
        g_sdl3_probe_error.clear();
    }
    g_sdl3_probe_state.store(Sdl3ProbeState::Running,
                             std::memory_order_release);
    g_input_backend_switch_in_progress.store(true, std::memory_order_release);
    g_input_backend_detail =
        "SDL2 compatibility input is active while SDL3 initializes.";
    g_sdl3_probe_worker = std::jthread(
        [host, mappings](std::stop_token stop_token) {
            dkr::runtime::startup_performance::ScopedPhase phase(
                "SDL3 input host probe");
            std::string error;
            const bool started = g_sdl3_input_client.start(
                host, mappings, error, stop_token);
            {
                std::scoped_lock lock(g_sdl3_probe_result_mutex);
                g_sdl3_probe_error = std::move(error);
            }
            g_sdl3_probe_state.store(
                started ? Sdl3ProbeState::Succeeded
                        : Sdl3ProbeState::Failed,
                std::memory_order_release);
        });
    return true;
#endif
}

void CompleteSdl3InputProbe() {
    const Sdl3ProbeState state =
        g_sdl3_probe_state.load(std::memory_order_acquire);
    if (state == Sdl3ProbeState::Idle || state == Sdl3ProbeState::Running) {
        return;
    }
    if (g_sdl3_probe_worker.joinable()) g_sdl3_probe_worker.join();

    std::unique_lock transition_lock(g_input_backend_transition_mutex);
    const auto target = dkr::runtime::platform::resolve_input_backend(
        g_requested_input_backend.load(std::memory_order_acquire),
        IsSteamDeckHost(), DKR_RUNTIME_HAS_SDL3_INPUT_HOST != 0);
    if (state == Sdl3ProbeState::Succeeded &&
        target == dkr::runtime::platform::InputBackend::SDL3Native) {
        ClearPublishedControllerInput(true);
        StopSdl2ControllerBackend();
        ActivateStartedSdl3InputBackend("Live switch complete. ");
        ClearPublishedControllerInput(true);
        dkr::runtime::startup_performance::mark("SDL3 input active");
    } else {
        if (state == Sdl3ProbeState::Succeeded) {
            g_sdl3_input_client.stop();
        }
        std::string error;
        {
            std::scoped_lock lock(g_sdl3_probe_result_mutex);
            error = g_sdl3_probe_error;
        }
        if (state == Sdl3ProbeState::Failed &&
            error != "SDL3 input initialization was cancelled.") {
            if (error.empty()) error = "SDL3 input initialization failed.";
            g_input_backend_detail = error +
                " SDL2 compatibility fallback is active.";
            std::fprintf(stderr, "[boot][input] %s\n",
                         g_input_backend_detail.c_str());
        }
    }
    g_sdl3_probe_state.store(Sdl3ProbeState::Idle,
                             std::memory_order_release);
    g_input_backend_switch_in_progress.store(false, std::memory_order_release);
}

void EnsureInputBackendHealthy() {
    if (g_active_input_backend.load(std::memory_order_acquire) !=
            dkr::runtime::platform::InputBackend::SDL3Native ||
        g_sdl3_input_client.healthy()) {
        return;
    }
    std::scoped_lock transition_lock(g_input_backend_transition_mutex);
    if (g_active_input_backend.load(std::memory_order_acquire) !=
            dkr::runtime::platform::InputBackend::SDL3Native ||
        g_sdl3_input_client.healthy()) {
        return;
    }
    g_input_backend_switch_in_progress.store(true, std::memory_order_release);
    ClearPublishedControllerInput(true);
    const std::string failure = g_sdl3_input_client.detail();
    std::fprintf(stderr,
                 "[input] SDL3 native host stopped (%s); activating SDL2 fallback\n",
                 failure.c_str());
    StopSdl3InputBackend();
    if (InitialiseSdl2ControllerBackend(false)) {
        g_input_backend_detail = failure +
            " SDL2 compatibility fallback is active.";
    } else {
        g_active_input_backend.store(
            dkr::runtime::platform::InputBackend::SDL2Compatibility,
            std::memory_order_release);
    }
    g_input_backend_switch_in_progress.store(false, std::memory_order_release);
}

void ApplyPendingInputBackendSwitch() {
    if (!g_input_backend_switch_pending.load(std::memory_order_acquire)) return;

    std::unique_lock transition_lock(g_input_backend_transition_mutex);
    if (!g_input_backend_switch_pending.exchange(false,
                                                  std::memory_order_acq_rel)) {
        return;
    }
    const auto target = dkr::runtime::platform::resolve_input_backend(
        g_requested_input_backend.load(std::memory_order_acquire),
        IsSteamDeckHost(), DKR_RUNTIME_HAS_SDL3_INPUT_HOST != 0);
    const auto previous = g_active_input_backend.load(std::memory_order_acquire);
    if (target == dkr::runtime::platform::InputBackend::SDL2Compatibility &&
        g_sdl3_probe_state.load(std::memory_order_acquire) !=
            Sdl3ProbeState::Idle) {
        // Do not hold the transition mutex while joining the cancellable
        // worker: it never touches controller state, but cancellation may
        // briefly wait for a socket poll to finish.
        transition_lock.unlock();
        CancelSdl3InputProbe();
        g_input_backend_switch_in_progress.store(false,
                                                  std::memory_order_release);
        return;
    }
    if (target == previous) return;

    bool switched = false;
    if (target == dkr::runtime::platform::InputBackend::SDL3Native) {
        if (!BeginSdl3InputProbe()) {
            g_input_backend_detail +=
                " SDL2 compatibility input remains active.";
            g_input_backend_switch_in_progress.store(
                false, std::memory_order_release);
            std::fprintf(stderr, "[input] %s\n",
                         g_input_backend_detail.c_str());
        }
        return;
    } else {
        g_input_backend_switch_in_progress.store(true,
                                                 std::memory_order_release);
        ClearPublishedControllerInput(true);
        StopSdl3InputBackend();
        switched = InitialiseSdl2ControllerBackend(false);
        if (!switched) {
            const std::string activation_failure = g_input_backend_detail;
            if (StartSdl3InputBackend()) {
                g_input_backend_detail = activation_failure +
                    " The previous SDL3 backend was restored without "
                    "restarting DKR-R.";
            } else {
                g_active_input_backend.store(
                    dkr::runtime::platform::InputBackend::SDL2Compatibility,
                    std::memory_order_release);
                g_input_backend_detail = activation_failure +
                    " SDL3 restoration also failed; controller input is "
                    "currently unavailable.";
            }
        }
    }
    if (switched) {
        g_input_backend_detail = "Live switch complete. " +
            g_input_backend_detail;
    }
    ClearPublishedControllerInput(true);
    g_input_backend_switch_in_progress.store(false, std::memory_order_release);
    std::fprintf(stderr, "[input] %s\n", g_input_backend_detail.c_str());
}
#endif

} // namespace

void dkr::runtime::platform::configure_input(
    const std::filesystem::path& config_directory) {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    g_input_config_directory = config_directory;
    g_user_mapping_path = config_directory / "controllers" /
                          "gamecontrollerdb.txt";
    InputBackend requested = InputBackend::Automatic;
    std::ifstream settings(config_directory / "dkr-port-settings.ini");
    std::string line;
    while (std::getline(settings, line)) {
        constexpr const char* kPrefix = "input_backend=";
        if (line.rfind(kPrefix, 0U) == 0U) {
            requested = ParseInputBackend(line.substr(std::strlen(kPrefix)));
        }
    }
    if (const char* override_backend = std::getenv("DKR_INPUT_BACKEND");
        override_backend != nullptr && *override_backend != '\0') {
        requested = ParseInputBackend(override_backend);
    }
    g_requested_input_backend.store(requested, std::memory_order_release);
    g_input_backend_switch_pending.store(false, std::memory_order_release);
    g_input_backend_switch_in_progress.store(false, std::memory_order_release);
#else
    (void)config_directory;
#endif
}

dkr::runtime::platform::InputBackend
dkr::runtime::platform::requested_input_backend() {
    return g_requested_input_backend.load(std::memory_order_acquire);
}

dkr::runtime::platform::InputBackend
dkr::runtime::platform::active_input_backend() {
    return g_active_input_backend.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_requested_input_backend(InputBackend backend) {
    backend = sanitise_input_backend(backend);
    const auto previous = g_requested_input_backend.exchange(
        backend, std::memory_order_acq_rel);
#if DKR_RUNTIME_HAS_RT64
    const bool switch_required = input_backend_switch_required(
        backend, active_input_backend(), IsSteamDeckHost(),
        DKR_RUNTIME_HAS_SDL3_INPUT_HOST != 0);
#else
    const bool switch_required = false;
#endif
    g_input_backend_switch_pending.store(
        previous != backend && switch_required, std::memory_order_release);
}

const char* dkr::runtime::platform::input_backend_name(InputBackend backend) {
    switch (backend) {
    case InputBackend::Automatic: return "Automatic";
    case InputBackend::SDL2Compatibility: return "SDL2 compatibility";
    case InputBackend::SDL3Native: return "SDL3 native";
    }
    return "Automatic";
}

std::string dkr::runtime::platform::input_backend_detail() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    return g_input_backend_detail;
#else
    return "Diagnostic input backend.";
#endif
}

bool dkr::runtime::platform::input_backend_switch_pending() {
    return g_input_backend_switch_pending.load(std::memory_order_acquire) ||
           g_input_backend_switch_in_progress.load(std::memory_order_acquire);
}

bool dkr::runtime::platform::initialise() {
#if DKR_RUNTIME_HAS_RT64
    dkr::runtime::startup_performance::ScopedPhase platform_phase(
        "SDL platform and input initialization");
    // Request SDL's direct HID paths before controller discovery. String
    // literals retain source compatibility with the bundled Windows SDL while
    // allowing newer Linux/SteamOS SDL builds to expose the built-in Deck pad.
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_STEAMDECK", "1");
    // SDL may continue sampling connected game controllers while the native
    // window is unfocused. DKR-R still gates those samples independently for
    // every N64 port in poll_input, so this hint grants capability rather than
    // globally opting every controller into background gameplay.
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    const bool try_sdl3 = ShouldTrySdl3Input();
    const Uint32 sdl_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO |
        SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC | SDL_INIT_SENSOR;
    if (SDL_Init(sdl_flags) != 0) {
        std::fprintf(stderr, "[boot][platform] SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_version linked_version{};
    SDL_AddEventWatch(ControllerInventoryEvent, nullptr);
    g_controller_inventory_dirty.store(true, std::memory_order_relaxed);
    SDL_GetVersion(&linked_version);
    std::fprintf(stderr, "[boot][platform] SDL linked=%u.%u.%u\n",
                 linked_version.major, linked_version.minor,
                 linked_version.patch);
    const char* sdl3_version = SDL_GetHint("SDL3_VERSION");
    std::fprintf(stderr, "[boot][platform] SDL3 backend=%s\n",
                 sdl3_version != nullptr && *sdl3_version != '\0'
                     ? sdl3_version : "not reported");
    if (!InitialiseSdl2ControllerBackend(true)) {
        SDL_DelEventWatch(ControllerInventoryEvent, nullptr);
        SDL_Quit();
        return false;
    }
    if (try_sdl3 && !BeginSdl3InputProbe()) {
        g_input_backend_detail +=
            " SDL2 compatibility fallback is active.";
        std::fprintf(stderr, "[boot][input] %s\n",
                     g_input_backend_detail.c_str());
    }
#endif
    std::fprintf(stderr,
                 "[boot][input] keyboard: WASD=stick arrows=d-pad Space=A Shift=B "
                 "Z=Z Enter=Start IJKL=C Q=L E=R\n");
    return true;
}

void dkr::runtime::platform::shutdown() {
#if DKR_RUNTIME_HAS_RT64
    CancelSdl3InputProbe();
    g_sdl3_input_client.stop();
    std::scoped_lock lock(g_platform_mutex);
    if (g_cursor_hidden) {
        SDL_ShowCursor(SDL_ENABLE);
        g_cursor_hidden = false;
    }
    if (g_audio_device != 0) {
        SDL_ClearQueuedAudio(g_audio_device);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    if (g_ui_audio_device != 0) {
        SDL_ClearQueuedAudio(g_ui_audio_device);
        SDL_CloseAudioDevice(g_ui_audio_device);
        g_ui_audio_device = 0;
    }
    for (ControllerRecord& record : g_controller_devices) {
        CloseControllerRecord(record);
    }
    g_controller_devices.clear();
    g_controllers.fill(nullptr);
    g_gyro_controllers.fill(nullptr);
    g_sdl3_input_state = {};
    g_sdl3_player_instances.fill(-1);
    g_sdl3_gyro_instances.fill(-1);
    g_sdl3_previous_event_state.clear();
    g_sdl2_controller_subsystems_initialized = false;
    g_mapping_session = {};
    if (g_window != nullptr) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_DelEventWatch(ControllerInventoryEvent, nullptr);
    SDL_Quit();
#endif
}

#if DKR_RUNTIME_HAS_RT64
ultramodern::renderer::WindowHandle dkr::runtime::platform::create_window() {
    if (g_window == nullptr) {
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(__linux__) && !defined(__ANDROID__)
        flags |= SDL_WINDOW_VULKAN;
#endif
        g_window = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled",
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, 1440, 900, flags);
        if (g_window == nullptr) {
            std::fprintf(stderr, "[boot][window] SDL creation failed: %s\n", SDL_GetError());
        } else {
            // Both the launcher and the transparent in-game overlay are fully
            // responsive down to this size. Prevent a window-manager resize
            // from making controller targets or binding cards unusably small.
            SDL_SetWindowMinimumSize(g_window, 800, 600);
        }
    }

#if defined(_WIN32)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (g_window == nullptr || SDL_GetWindowWMInfo(g_window, &info) != SDL_TRUE) {
        std::fprintf(stderr, "[boot][window] native handle failed: %s\n", SDL_GetError());
        return {};
    }
    return {.window = info.info.win.window, .thread_id = GetCurrentThreadId()};
#else
    return g_window;
#endif
}

ultramodern::renderer::WindowHandle dkr::runtime::platform::prepare_window_for_game() {
#if defined(__linux__)
    if (g_window != nullptr) {
        const Uint32 existing_flags = SDL_GetWindowFlags(g_window);
        std::fprintf(stderr,
                     "[boot][window] launcher handoff flags=0x%08X vulkan=%s\n",
                     static_cast<unsigned>(existing_flags),
                     (existing_flags & SDL_WINDOW_VULKAN) != 0 ? "yes" : "no");
        if ((existing_flags & SDL_WINDOW_VULKAN) == 0) {
            int x = SDL_WINDOWPOS_CENTERED;
            int y = SDL_WINDOWPOS_CENTERED;
            int width = 1440;
            int height = 900;
            SDL_GetWindowPosition(g_window, &x, &y);
            SDL_GetWindowSize(g_window, &width, &height);
            const bool was_hidden = (existing_flags & SDL_WINDOW_HIDDEN) != 0;
            const bool was_maximized =
                (existing_flags & SDL_WINDOW_MAXIMIZED) != 0;
            const Uint32 fullscreen_mode = existing_flags &
                (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
            SDL_DestroyWindow(g_window);
            g_window = nullptr;

            Uint32 replacement_flags =
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN;
            if (was_hidden) {
                replacement_flags |= SDL_WINDOW_HIDDEN;
            }
            g_window = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled", x, y, width, height,
                                        replacement_flags);
            if (g_window == nullptr) {
                std::fprintf(stderr,
                             "[boot][window] Vulkan handoff recreation failed: %s\n",
                             SDL_GetError());
                return {};
            }
            SDL_SetWindowMinimumSize(g_window, 800, 600);
            if (was_maximized) {
                SDL_MaximizeWindow(g_window);
            }
            if (fullscreen_mode != 0U &&
                SDL_SetWindowFullscreen(g_window, fullscreen_mode) != 0) {
                std::fprintf(stderr,
                             "[boot][window] fullscreen handoff restore failed: %s\n",
                             SDL_GetError());
            }
            std::fprintf(stderr,
                         "[boot][window] recreated Vulkan-capable game window "
                         "flags=0x%08X\n",
                         static_cast<unsigned>(SDL_GetWindowFlags(g_window)));
        }
    }
#endif
    return create_window();
}

void* dkr::runtime::platform::prepare_window_for_launcher() {
#if defined(__linux__) && !defined(__ANDROID__)
    // Qualification switch: never allow SDL/OpenGL to convert a live Vulkan
    // window. Both renderers have been destroyed at this launcher boundary.
    const char* opt_in = SDL_getenv("DKR_LINUX_ACCELERATED_LAUNCHER");
    if (g_window && opt_in && std::string_view(opt_in) == "1" &&
        (SDL_GetWindowFlags(g_window) & SDL_WINDOW_VULKAN)) {
        int x, y, width, height;
        SDL_GetWindowPosition(g_window, &x, &y);
        SDL_GetWindowSize(g_window, &width, &height);
        const Uint32 flags = SDL_GetWindowFlags(g_window);
        // Create before destroying so a failed allocation keeps the proven
        // software path and its original window intact.
        auto* replacement = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled",
            x, y, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
        if (replacement) {
            SDL_DestroyWindow(g_window);
            g_window = replacement;
            SDL_SetWindowMinimumSize(g_window, 800, 600);
            if (flags & SDL_WINDOW_MAXIMIZED) SDL_MaximizeWindow(g_window);
            const auto fullscreen = flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
            if (fullscreen) SDL_SetWindowFullscreen(g_window, fullscreen);
            if (!(flags & SDL_WINDOW_HIDDEN)) SDL_ShowWindow(g_window);
        }
    }
#endif
    return g_window;
}

void dkr::runtime::platform::pump_window_events(void*) {
    pump_input_backend_events();
    if (dkr::runtime::ui::lifecycle_request() !=
        dkr::runtime::ui::LifecycleRequest::None) {
        ultramodern::quit();
        return;
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        update_fullscreen_cursor(&event);
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            ultramodern::quit();
            return;
        }

        // Overlay lifecycle commands are host-global. Process them before
        // forwarding the event to ImGui so a focused widget cannot swallow
        // Escape, F1 or Back/View and leave the player trapped in the UI.
        const bool keyboard_toggle = event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
            (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE ||
             event.key.keysym.scancode == SDL_SCANCODE_F1);
        if (dkr::runtime::ui::input_capture_active()) {
            dkr::runtime::ui::handle_runtime_event(&event);
            continue;
        }
        if (handle_window_shortcut(&event, true)) {
            continue;
        }
        if (keyboard_toggle) {
            dkr::runtime::ui::toggle_overlay();
            continue;
        }

        dkr::runtime::ui::handle_runtime_event(&event);
    }
    if (dkr::runtime::input::consume_shortcut_request(
            dkr::runtime::input::ShortcutAction::ToggleOverlay)) {
        dkr::runtime::ui::toggle_overlay();
    }
    if (dkr::runtime::input::consume_shortcut_request(
            dkr::runtime::input::ShortcutAction::ToggleTexturePack)) {
        std::string status;
        dkr::runtime::texture_packs::toggle_last_selected(status);
    }
    if (dkr::runtime::input::consume_shortcut_request(
            dkr::runtime::input::ShortcutAction::ToggleFullscreen)) {
        toggle_fullscreen(true);
    }
    if (dkr::runtime::input::consume_shortcut_request(
            dkr::runtime::input::ShortcutAction::RecenterGyro)) {
        dkr::runtime::input::recenter_gyro(online_input_profile());
    }
    if (dkr::runtime::ui::lifecycle_request() !=
        dkr::runtime::ui::LifecycleRequest::None) {
        ultramodern::quit();
    }
    update_fullscreen_cursor();
}

void dkr::runtime::platform::pump_input_backend_events() {
    CompleteSdl3InputProbe();
    ApplyPendingInputBackendSwitch();
    EnsureInputBackendHealthy();
    if (active_input_backend() != InputBackend::SDL3Native) return;
    std::scoped_lock lock(g_platform_mutex);
    PushSdl3Events();
}

void* dkr::runtime::platform::sdl_window() {
    return g_window;
}

bool dkr::runtime::platform::handle_window_shortcut(
    const void* raw_event, bool renderer_active) {
    const auto* event = static_cast<const SDL_Event*>(raw_event);
    if (event == nullptr || event->type != SDL_KEYDOWN || event->key.repeat != 0) {
        return false;
    }

    const bool f11 = event->key.keysym.scancode == SDL_SCANCODE_F11;
    const bool alt_enter = event->key.keysym.scancode == SDL_SCANCODE_RETURN &&
        (event->key.keysym.mod & KMOD_ALT) != 0;
    if (!f11 && !alt_enter) {
        return false;
    }

    toggle_fullscreen(renderer_active);
    return true;
}

void dkr::runtime::platform::toggle_fullscreen(bool renderer_active) {
    auto config = ultramodern::renderer::get_graphics_config();
    const bool fullscreen =
        config.wm_option == ultramodern::renderer::WindowMode::Fullscreen;
    config.wm_option = fullscreen
        ? ultramodern::renderer::WindowMode::Windowed
        : ultramodern::renderer::WindowMode::Fullscreen;
    ultramodern::renderer::set_graphics_config(config);
    dkr::runtime::ui::persist_settings();

    // The launcher has no RT64 swap chain yet, so SDL owns this transition.
    // In game, publishing GraphicsConfig lets RT64 switch at its safe update
    // boundary instead of changing the native window underneath a present.
    if (!renderer_active && g_window != nullptr) {
        const Uint32 mode = fullscreen ? 0U : SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (SDL_SetWindowFullscreen(g_window, mode) != 0) {
            std::fprintf(stderr, "[window] fullscreen toggle failed: %s\n",
                         SDL_GetError());
        }
    }

    g_cursor_last_activity_ms = SDL_GetTicks64();
    if (g_cursor_hidden) {
        SDL_ShowCursor(SDL_ENABLE);
        g_cursor_hidden = false;
    }
}

void dkr::runtime::platform::update_fullscreen_cursor(const void* raw_event) {
    const auto* event = static_cast<const SDL_Event*>(raw_event);
    const std::uint64_t now = SDL_GetTicks64();
    if (g_cursor_last_activity_ms == 0U) {
        g_cursor_last_activity_ms = now;
    }

    bool pointer_activity = false;
    if (event != nullptr) {
        pointer_activity = event->type == SDL_MOUSEMOTION ||
            event->type == SDL_MOUSEBUTTONDOWN ||
            event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEWHEEL;
        if (event->type == SDL_WINDOWEVENT &&
            (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
             event->window.event == SDL_WINDOWEVENT_ENTER)) {
            pointer_activity = true;
        }
    }

    const auto& config = ultramodern::renderer::get_graphics_config();
    const Uint32 flags = g_window != nullptr ? SDL_GetWindowFlags(g_window) : 0U;
    const bool fullscreen =
        config.wm_option == ultramodern::renderer::WindowMode::Fullscreen ||
        (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0U;
    const bool focused = g_window != nullptr &&
        (flags & SDL_WINDOW_INPUT_FOCUS) != 0U;

    if (pointer_activity || fullscreen != g_cursor_was_fullscreen) {
        g_cursor_last_activity_ms = now;
        if (g_cursor_hidden) {
            SDL_ShowCursor(SDL_ENABLE);
            g_cursor_hidden = false;
        }
    }
    g_cursor_was_fullscreen = fullscreen;

    const bool should_hide = fullscreen && focused &&
        now - g_cursor_last_activity_ms >= kFullscreenCursorIdleMs;
    if (should_hide != g_cursor_hidden) {
        SDL_ShowCursor(should_hide ? SDL_DISABLE : SDL_ENABLE);
        g_cursor_hidden = should_hide;
    }
}

void dkr::runtime::platform::update_ui_gamepad_navigation() {
    ImGuiIO& io = ImGui::GetIO();
    if (active_input_backend() == InputBackend::SDL3Native) {
        std::scoped_lock lock(g_platform_mutex);
        // pump_input_backend_events() refreshes and reconciles the SDL3 state
        // immediately before each launcher/overlay UI frame. Reusing that
        // snapshot here avoids a second locked copy of every controller and
        // sensor sample in the same frame.
        const auto* device = Sdl3DeviceForInstance(g_sdl3_player_instances[0]);
        const auto* controller = device != nullptr ? &device->snapshot : nullptr;
        const bool connected = controller != nullptr && controller->connected;
        if (connected) {
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        } else {
            io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        }
        const auto button = [&](ImGuiKey key, std::size_t source) {
            io.AddKeyEvent(key, connected &&
                source < controller->buttons.size() &&
                controller->buttons[source] != 0U);
        };
        const auto axis = [&](ImGuiKey negative_key, ImGuiKey positive_key,
                              std::size_t source, bool invert = false) {
            float value = connected && source < controller->axes.size()
                ? NormaliseAxis(controller->axes[source]) : 0.0F;
            if (invert) value = -value;
            io.AddKeyAnalogEvent(negative_key, value < 0.0F,
                                 std::max(-value, 0.0F));
            io.AddKeyAnalogEvent(positive_key, value > 0.0F,
                                 std::max(value, 0.0F));
        };
        using dkr::runtime::controllers::StandardAxis;
        using dkr::runtime::controllers::StandardButton;
        button(ImGuiKey_GamepadStart,
               static_cast<std::size_t>(StandardButton::Start));
        button(ImGuiKey_GamepadBack,
               static_cast<std::size_t>(StandardButton::Back));
        button(ImGuiKey_GamepadFaceDown,
               static_cast<std::size_t>(StandardButton::South));
        button(ImGuiKey_GamepadFaceRight,
               static_cast<std::size_t>(StandardButton::East));
        button(ImGuiKey_GamepadFaceLeft,
               static_cast<std::size_t>(StandardButton::West));
        button(ImGuiKey_GamepadFaceUp,
               static_cast<std::size_t>(StandardButton::North));
        button(ImGuiKey_GamepadDpadLeft,
               static_cast<std::size_t>(StandardButton::DpadLeft));
        button(ImGuiKey_GamepadDpadRight,
               static_cast<std::size_t>(StandardButton::DpadRight));
        button(ImGuiKey_GamepadDpadUp,
               static_cast<std::size_t>(StandardButton::DpadUp));
        button(ImGuiKey_GamepadDpadDown,
               static_cast<std::size_t>(StandardButton::DpadDown));
        button(ImGuiKey_GamepadL1,
               static_cast<std::size_t>(StandardButton::LeftShoulder));
        button(ImGuiKey_GamepadR1,
               static_cast<std::size_t>(StandardButton::RightShoulder));
        button(ImGuiKey_GamepadL3,
               static_cast<std::size_t>(StandardButton::LeftStick));
        button(ImGuiKey_GamepadR3,
               static_cast<std::size_t>(StandardButton::RightStick));
        axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
             static_cast<std::size_t>(StandardAxis::LeftX));
        axis(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
             static_cast<std::size_t>(StandardAxis::LeftY));
        axis(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight,
             static_cast<std::size_t>(StandardAxis::RightX));
        axis(ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown,
             static_cast<std::size_t>(StandardAxis::RightY));
        const float left_trigger = connected
            ? std::max(static_cast<float>(controller->axes[
                  static_cast<std::size_t>(StandardAxis::LeftTrigger)]) /
                  32767.0F, 0.0F)
            : 0.0F;
        const float right_trigger = connected
            ? std::max(static_cast<float>(controller->axes[
                  static_cast<std::size_t>(StandardAxis::RightTrigger)]) /
                  32767.0F, 0.0F)
            : 0.0F;
        io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, left_trigger > 0.10F,
                             left_trigger);
        io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, right_trigger > 0.10F,
                             right_trigger);
        return;
    }
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameControllerUpdate();
    RefreshControllers(false);

    SDL_GameController* controller = g_controllers[0];
    const bool connected = controller != nullptr;
    if (connected) {
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    } else {
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }

    const auto button = [&](ImGuiKey key, SDL_GameControllerButton source) {
        io.AddKeyEvent(key, connected &&
            SDL_GameControllerGetButton(controller, source) != 0);
    };
    const auto axis = [&](ImGuiKey negative_key, ImGuiKey positive_key,
                          SDL_GameControllerAxis source, bool invert = false) {
        float value = connected
            ? NormaliseAxis(SDL_GameControllerGetAxis(controller, source))
            : 0.0F;
        if (invert) {
            value = -value;
        }
        io.AddKeyAnalogEvent(negative_key, value < 0.0F, std::max(-value, 0.0F));
        io.AddKeyAnalogEvent(positive_key, value > 0.0F, std::max(value, 0.0F));
    };

    button(ImGuiKey_GamepadStart, SDL_CONTROLLER_BUTTON_START);
    button(ImGuiKey_GamepadBack, SDL_CONTROLLER_BUTTON_BACK);
    button(ImGuiKey_GamepadFaceDown, SDL_CONTROLLER_BUTTON_A);
    button(ImGuiKey_GamepadFaceRight, SDL_CONTROLLER_BUTTON_B);
    button(ImGuiKey_GamepadFaceLeft, SDL_CONTROLLER_BUTTON_X);
    button(ImGuiKey_GamepadFaceUp, SDL_CONTROLLER_BUTTON_Y);
    button(ImGuiKey_GamepadDpadLeft, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    button(ImGuiKey_GamepadDpadRight, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    button(ImGuiKey_GamepadDpadUp, SDL_CONTROLLER_BUTTON_DPAD_UP);
    button(ImGuiKey_GamepadDpadDown, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    button(ImGuiKey_GamepadL1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    button(ImGuiKey_GamepadR1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    button(ImGuiKey_GamepadL3, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    button(ImGuiKey_GamepadR3, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
         SDL_CONTROLLER_AXIS_LEFTX);
    axis(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
         SDL_CONTROLLER_AXIS_LEFTY);
    axis(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight,
         SDL_CONTROLLER_AXIS_RIGHTX);
    axis(ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown,
         SDL_CONTROLLER_AXIS_RIGHTY);

    const float left_trigger = connected
        ? std::max(static_cast<float>(SDL_GameControllerGetAxis(
              controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)) / 32767.0F, 0.0F)
        : 0.0F;
    const float right_trigger = connected
        ? std::max(static_cast<float>(SDL_GameControllerGetAxis(
              controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) / 32767.0F, 0.0F)
        : 0.0F;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, left_trigger > 0.10F, left_trigger);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, right_trigger > 0.10F, right_trigger);
}

#endif

void dkr::runtime::platform::queue_audio(std::int16_t* samples,
                                         std::size_t sample_count) {
    if (!dkr::runtime::netplay::external_side_effects_allowed()) return;
    dkr::runtime::telemetry::record_audio_buffer(sample_count);
    const auto index = ++g_audio_buffers;
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0 && sample_count != 0) {
        const std::size_t maximum_samples =
            static_cast<std::size_t>(std::max(g_audio_frequency, 48000U)) * 2U;
        if (sample_count > maximum_samples || (sample_count & 1U) != 0) {
            std::fprintf(stderr,
                         "[boot][audio] rejected invalid buffer samples=%zu maximum=%zu\n",
                         sample_count, maximum_samples);
            return;
        }
        const std::size_t queued_frames_before =
            SDL_GetQueuedAudioSize(g_audio_device) /
            (2U * sizeof(std::int16_t));
        const std::size_t submitted_frames = sample_count / 2U;
        if (g_audio_nominal_block_frames == 0U) {
            g_audio_nominal_block_frames = submitted_frames;
        }
        // Rollback can occasionally replay several simulation frames before
        // publishing one audible frame. Keep one additional host-only block
        // while online so that replay work never becomes an SDL underrun. The
        // emulated AI length below explicitly excludes this cushion, so game
        // cadence and pitch remain unchanged.
        const bool online_audio =
            g_online_input_routing.load(std::memory_order_acquire);
        const std::uint32_t minimum_cushion = online_audio ? 3U : 2U;
        g_audio_cushion_blocks =
            (std::max)(g_audio_cushion_blocks, minimum_cushion);
        const std::size_t cushion_block_frames = std::max(
            g_audio_nominal_block_frames,
            static_cast<std::size_t>(g_audio_callback_frames));
        g_audio_prime_target_frames = cushion_block_frames *
                                      static_cast<std::size_t>(g_audio_cushion_blocks);
        if (index > 1U) {
            if (queued_frames_before == 0U) {
                if (g_audio_playback_started) {
                    // The queued-audio API fills a drained device with silence.
                    // Pause immediately and rebuild a bounded cushion so a
                    // quantised cutscene refill cadence cannot produce a train
                    // of short silence gaps. This is deliberately host-only:
                    // audio_frames_remaining() continues to expose the original
                    // emulated AI DMA state to DKR.
                    SDL_PauseAudioDevice(g_audio_device, 1);
                    g_audio_playback_started = false;
                    const std::uint32_t maximum_cushion =
                        online_audio ? 4U : 3U;
                    g_audio_cushion_blocks = std::min(
                        g_audio_cushion_blocks + 1U, maximum_cushion);
                    g_audio_prime_target_frames = cushion_block_frames *
                                                  static_cast<std::size_t>(
                                                      g_audio_cushion_blocks);
                }
            }
        }
        g_audio_swap_buffer.resize(sample_count);
        g_audio_equalizer.configure(
            g_audio_frequency,
            dkr::runtime::enhancements::modern_presentation_enabled()
                ? g_bass_gain.load(std::memory_order_relaxed) : 0.0F,
            dkr::runtime::enhancements::modern_presentation_enabled()
                ? g_mid_gain.load(std::memory_order_relaxed) : 0.0F,
            dkr::runtime::enhancements::modern_presentation_enabled()
                ? g_treble_gain.load(std::memory_order_relaxed) : 0.0F);
        const std::uint64_t tone_generation =
            g_ui_tone_generation.load(std::memory_order_acquire);
        if (tone_generation != g_ui_tone_seen_generation) {
            g_ui_tone_seen_generation = tone_generation;
            g_ui_tone_active_frequency_hz = std::clamp(
                g_ui_tone_frequency_hz.load(std::memory_order_relaxed),
                80.0F, 4000.0F);
            const std::uint32_t duration_ms = std::clamp(
                g_ui_tone_duration_ms.load(std::memory_order_relaxed),
                20U, 500U);
            g_ui_tone_total_frames =
                (static_cast<std::size_t>(g_audio_frequency) * duration_ms) /
                1000U;
            g_ui_tone_frames_remaining = g_ui_tone_total_frames;
            g_ui_tone_phase = 0.0;
        }
        for (std::size_t i = 0; i < sample_count; i += 2) {
            // RDRAM's 32-bit word swap leaves each native stereo pair in R,L
            // order. Restore conventional L,R order before sending it to SDL.
            const float volume = g_master_volume.load(std::memory_order_relaxed);
            const auto filtered = g_audio_equalizer.process(
                static_cast<float>(samples[i + 1]),
                static_cast<float>(samples[i]));
            float ui_tone = 0.0F;
            if (g_ui_tone_frames_remaining != 0U &&
                g_ui_tone_total_frames != 0U && g_audio_frequency != 0U) {
                const float progress = 1.0F -
                    static_cast<float>(g_ui_tone_frames_remaining) /
                    static_cast<float>(g_ui_tone_total_frames);
                const float attack = std::clamp(progress / 0.08F, 0.0F, 1.0F);
                const float release = std::clamp(
                    static_cast<float>(g_ui_tone_frames_remaining) /
                        (static_cast<float>(g_ui_tone_total_frames) * 0.32F),
                    0.0F, 1.0F);
                // A softly clipped two-harmonic pulse gives the cue a compact
                // cartridge-era character without sampling or replacing any
                // copyrighted game audio.
                const double fundamental = std::sin(g_ui_tone_phase);
                const double harmonic = std::sin(g_ui_tone_phase * 2.0) * 0.22;
                ui_tone = static_cast<float>(fundamental + harmonic) *
                          5200.0F * attack * release;
                g_ui_tone_phase +=
                    (2.0 * 3.14159265358979323846 *
                     static_cast<double>(g_ui_tone_active_frequency_hz)) /
                    static_cast<double>(g_audio_frequency);
                if (g_ui_tone_phase >= 2.0 * 3.14159265358979323846) {
                    g_ui_tone_phase = std::fmod(
                        g_ui_tone_phase, 2.0 * 3.14159265358979323846);
                }
                --g_ui_tone_frames_remaining;
            }
            const long left = std::lround(filtered.first * volume + ui_tone);
            const long right = std::lround(filtered.second * volume + ui_tone);
            g_audio_swap_buffer[i] = static_cast<std::int16_t>(
                std::clamp(left, -32768L, 32767L));
            g_audio_swap_buffer[i + 1] = static_cast<std::int16_t>(
                std::clamp(right, -32768L, 32767L));
        }
        const auto byte_count = static_cast<Uint32>(sample_count * sizeof(std::int16_t));
        if (SDL_QueueAudio(g_audio_device, g_audio_swap_buffer.data(), byte_count) != 0) {
            std::fprintf(stderr, "[boot][audio] queue failed: %s\n", SDL_GetError());
        } else {
            const std::size_t queued_frames = SDL_GetQueuedAudioSize(g_audio_device) /
                                              (2U * sizeof(std::int16_t));
            if (!g_audio_playback_started && g_audio_prime_target_frames != 0U &&
                queued_frames >= g_audio_prime_target_frames) {
                SDL_PauseAudioDevice(g_audio_device, 0);
                g_audio_playback_started = true;
            }
        }
    }
#else
    (void)samples;
#endif
}

void dkr::runtime::platform::request_ui_tone(float frequency_hz,
                                             std::uint32_t duration_ms) {
    const float clamped_frequency = std::clamp(frequency_hz, 80.0F, 4000.0F);
    const std::uint32_t clamped_duration = std::clamp(duration_ms, 20U, 500U);
    g_ui_tone_frequency_hz.store(clamped_frequency, std::memory_order_relaxed);
    g_ui_tone_duration_ms.store(clamped_duration, std::memory_order_relaxed);
    const std::uint64_t generation =
        g_ui_tone_generation.fetch_add(1U, std::memory_order_release) + 1U;
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device == 0 &&
        QueueStandaloneUiToneLocked(clamped_frequency, clamped_duration)) {
        // The launcher path has already consumed this request. Prevent the
        // first DKR audio buffer from replaying the final countdown note.
        g_ui_tone_seen_generation = generation;
    }
#endif
}

float dkr::runtime::platform::master_volume() {
    return g_master_volume.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_master_volume(float volume) {
    g_master_volume.store(std::clamp(volume, 0.0F, 1.0F), std::memory_order_release);
}

float dkr::runtime::platform::bass_gain() {
    return g_bass_gain.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_bass_gain(float decibels) {
    g_bass_gain.store(dkr::runtime::audio::clamp_eq_gain(decibels),
                      std::memory_order_release);
}

float dkr::runtime::platform::mid_gain() {
    return g_mid_gain.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_mid_gain(float decibels) {
    g_mid_gain.store(dkr::runtime::audio::clamp_eq_gain(decibels),
                     std::memory_order_release);
}

float dkr::runtime::platform::treble_gain() {
    return g_treble_gain.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_treble_gain(float decibels) {
    g_treble_gain.store(dkr::runtime::audio::clamp_eq_gain(decibels),
                        std::memory_order_release);
}

std::size_t dkr::runtime::platform::audio_frames_remaining() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0) {
        // SDL's queue contains one deliberately protected host-side cushion
        // block in addition to the block that represents the N64 AI's active
        // DMA. DKR's audiomgr uses osAiGetLength as feedback when selecting its
        // next 720..848-frame synthesis quantum, so exposing the cushion makes
        // every decision look artificially full and locks production to the
        // 720-frame minimum. Exclude exactly that protected block. While the
        // device is paused for initial/rebuffer priming, report an idle AI so
        // the original manager can promptly generate the blocks needed to
        // start playback.
        if (!g_audio_playback_started) {
            return 0U;
        }
        const std::size_t queued_frames = SDL_GetQueuedAudioSize(g_audio_device) /
                                          (2U * sizeof(std::int16_t));
        // DKR's retail audiomgr targets a small active-DMA residual when it
        // chooses the next synthesis quantum. At 22,050 Hz its aligned authored
        // frame is 736 samples; the initial no-DMA quantum is 848, leaving the
        // intended feedback residual at 112. Centre the host queue controller
        // on that residual rather than exposing an entire buffered block. This
        // lets DKR alternate its original 720/736/752-frame corrections around
        // the queue target instead of slowly draining the cushion.
        const std::size_t authored_frame_frames =
            (((static_cast<std::size_t>(g_audio_frequency) + 29U) / 30U) + 15U) &
            ~std::size_t{15U};
        const std::size_t target_active_residual =
            g_audio_nominal_block_frames > authored_frame_frames
                ? g_audio_nominal_block_frames - authored_frame_frames
                : 0U;
        const std::size_t protected_host_frames =
            g_audio_prime_target_frames > target_active_residual
                ? g_audio_prime_target_frames - target_active_residual
                : 0U;
        const std::size_t feedback_frames =
            queued_frames > protected_host_frames
                ? queued_frames - protected_host_frames
                : 0U;
        // osAiGetLength reports the active N64 DMA, not the sum of both AI
        // slots. DKR uses that distinction when sizing its next audio frame.
        const std::size_t one_dma_frames = std::max(g_audio_frequency / 30U, 1U);
        return std::min(feedback_frames, one_dma_frames);
    }
#endif
    return 0;
}

void dkr::runtime::platform::set_audio_frequency(std::uint32_t frequency) {
    if (frequency == 0) {
        return;
    }
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0 && g_audio_frequency == frequency) {
        return;
    }
    if (g_audio_device != 0) {
        SDL_ClearQueuedAudio(g_audio_device);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    if (g_ui_audio_device != 0) {
        SDL_ClearQueuedAudio(g_ui_audio_device);
        SDL_CloseAudioDevice(g_ui_audio_device);
        g_ui_audio_device = 0;
    }

    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = static_cast<int>(frequency);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    // DKR normally submits 720 stereo frames per authored 30 Hz mix. A
    // 1024-frame host callback cannot be satisfied by one such buffer and
    // repeatedly inserts silence at cutscene transitions. Keep the callback
    // below the retail mix quantum; this changes host delivery granularity,
    // not DKR's emulated AI timing or sample production.
    desired.samples = 512;
    g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (g_audio_device == 0) {
        std::fprintf(stderr, "[boot][audio] open failed at %u Hz: %s\n",
                     frequency, SDL_GetError());
        g_audio_frequency = 0;
        return;
    }
    g_audio_frequency = static_cast<std::uint32_t>(obtained.freq);
    g_audio_callback_frames = obtained.samples;
    g_audio_playback_started = false;
    g_audio_nominal_block_frames = 0U;
    g_audio_prime_target_frames = 0U;
    g_audio_cushion_blocks = 2U;
    g_audio_equalizer.reset();
    std::fprintf(stderr, "[boot][audio] device opened requested=%u actual=%d format=%04X channels=%u\n",
                 frequency, obtained.freq, obtained.format, obtained.channels);
#else
    std::fprintf(stderr, "[boot][audio] frequency=%u (diagnostic backend)\n", frequency);
#endif
}

#if defined(__ANDROID__)
#include "../android/mobile_ui.hpp"
#endif

void dkr::runtime::platform::poll_input() {
#if DKR_RUNTIME_HAS_RT64
    if (g_input_backend_switch_in_progress.load(std::memory_order_acquire)) {
        ClearPublishedControllerInput(false);
        if (g_online_input_routing.load(std::memory_order_acquire)) {
            const std::size_t profile = std::min(
                g_online_input_profile.load(std::memory_order_acquire),
                kControllerCount - 1U);
            dkr::runtime::netplay::online_input_broker().capture_local(
                profile, 0U, 0.0F, 0.0F, true);
        }
        return;
    }
    std::scoped_lock lock(g_platform_mutex);
    const bool sdl3_native = active_input_backend() == InputBackend::SDL3Native;
    if (sdl3_native) {
        RefreshSdl3Controllers();
    } else {
        SDL_GameControllerUpdate();
        RefreshControllers(false);
    }
    const bool blocked = dkr::runtime::ui::overlay_visible();
    const bool window_focused = g_window != nullptr &&
        (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0U;
    const bool online_routing =
        g_online_input_routing.load(std::memory_order_acquire);
    const int configured_keyboard_player =
        dkr::runtime::input::keyboard_player();
    const std::size_t online_profile = std::min(
        g_online_input_profile.load(std::memory_order_acquire),
        kControllerCount - 1U);
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        const bool owns_keyboard =
            dkr::runtime::netplay::keyboard_owns_input_port(
                online_routing, player, configured_keyboard_player);
        const bool allow_background_controller =
            dkr::runtime::input::background_input_enabled(player);
        const bool gameplay_blocked = blocked ||
            (!window_focused && !allow_background_controller);
        dkr::runtime::input::State state{};
        if (sdl3_native) {
            const auto* controller = Sdl3DeviceForInstance(
                g_sdl3_player_instances[player]);
            const auto* gyro = Sdl3DeviceForInstance(
                g_sdl3_gyro_instances[player]);
            state = dkr::runtime::input::poll_snapshot(
                player,
                controller != nullptr ? &controller->snapshot : nullptr,
                gyro != nullptr ? &gyro->snapshot : nullptr,
                owns_keyboard && window_focused, gameplay_blocked,
                !online_routing,
                player == (online_routing ? online_profile : 0U));
        } else {
            state = dkr::runtime::input::poll(
                player, g_controllers[player], g_gyro_controllers[player],
                owns_keyboard && window_focused, gameplay_blocked,
                !online_routing,
                player == (online_routing ? online_profile : 0U));
        }
#if defined(__ANDROID__)
        if (player == (online_routing ? online_profile : 0U)) {
            dkr::runtime::mobile::merge_input(state,
                gameplay_blocked || !window_focused || dkr::runtime::mobile::editing());
        }
#endif
        g_physical_buttons[player].store(state.buttons, std::memory_order_release);
        g_physical_stick_x[player].store(state.stick_x, std::memory_order_release);
        g_physical_stick_y[player].store(state.stick_y, std::memory_order_release);
        if (online_routing && player == online_profile) {
            dkr::runtime::netplay::online_input_broker().capture_local(
                player, state.buttons, state.stick_x, state.stick_y,
                gameplay_blocked);
        }
        // Online physical input is private until the input broker commits a
        // complete synchronized frame. Crucially, a physical poll never
        // clears or mutates the previously committed virtual N64 ports.
        const bool publish_physical =
            dkr::runtime::netplay::publish_physical_input_to_virtual_port(
                online_routing);
        if (publish_physical) {
            g_buttons[player].store(state.buttons, std::memory_order_release);
            g_stick_x[player].store(state.stick_x, std::memory_order_release);
            g_stick_y[player].store(state.stick_y, std::memory_order_release);
        }
    }
#else
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        g_buttons[player].store(0, std::memory_order_release);
        g_stick_x[player].store(0.0F, std::memory_order_release);
        g_stick_y[player].store(0.0F, std::memory_order_release);
        g_physical_buttons[player].store(0, std::memory_order_release);
        g_physical_stick_x[player].store(0.0F, std::memory_order_release);
        g_physical_stick_y[player].store(0.0F, std::memory_order_release);
    }
#endif
}

void dkr::runtime::platform::set_online_input_routing(
    bool enabled, std::uint8_t occupied_mask, std::uint8_t local_slot) {
    g_online_occupied_mask.store(enabled ? occupied_mask : 0U,
                                 std::memory_order_release);
    g_online_local_slot.store(
        enabled ? local_slot : dkr::runtime::netplay::kNoOnlinePlayerSlot,
        std::memory_order_release);
    g_online_input_routing.store(enabled, std::memory_order_release);
    dkr::runtime::netplay::online_input_broker().configure(
        enabled, occupied_mask, local_slot);
}

void dkr::runtime::platform::set_online_input_profile(std::size_t profile) {
    profile = std::min(profile, kControllerCount - 1U);
    g_online_input_profile.store(profile, std::memory_order_release);
    dkr::runtime::netplay::online_input_broker().set_local_profile(profile);
}

std::size_t dkr::runtime::platform::online_input_profile() {
    return std::min(g_online_input_profile.load(std::memory_order_acquire),
                    kControllerCount - 1U);
}

bool dkr::runtime::platform::get_input(int controller, std::uint16_t* buttons,
                                       float* x, float* y) {
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return false;
    }
    const auto index = static_cast<std::size_t>(controller);
    if (g_online_input_routing.load(std::memory_order_acquire)) {
        const auto input =
            dkr::runtime::netplay::online_input_broker().input_for_port(index);
        *buttons = input.buttons;
        *x = dkr::runtime::netplay::unpack_input_axis(input.stick_x);
        *y = dkr::runtime::netplay::unpack_input_axis(input.stick_y);
        return true;
    }
    *buttons = g_buttons[index].load(std::memory_order_acquire);
    *x = g_stick_x[index].load(std::memory_order_acquire);
    *y = g_stick_y[index].load(std::memory_order_acquire);
    return true;
}

bool dkr::runtime::platform::get_physical_input(
    int controller, std::uint16_t* buttons, float* x, float* y) {
    if (controller < 0 || controller >= static_cast<int>(kControllerCount) ||
        buttons == nullptr || x == nullptr || y == nullptr) {
        return false;
    }
    const auto index = static_cast<std::size_t>(controller);
    *buttons = g_physical_buttons[index].load(std::memory_order_acquire);
    *x = g_physical_stick_x[index].load(std::memory_order_acquire);
    *y = g_physical_stick_y[index].load(std::memory_order_acquire);
    return true;
}

bool dkr::runtime::platform::get_local_online_input(
    std::uint16_t* buttons, float* x, float* y, bool* blocked) {
    const auto sample =
        dkr::runtime::netplay::online_input_broker().local_sample();
    if (!sample.valid) return false;
    *buttons = sample.input.buttons;
    *x = dkr::runtime::netplay::unpack_input_axis(sample.input.stick_x);
    *y = dkr::runtime::netplay::unpack_input_axis(sample.input.stick_y);
    if (blocked != nullptr) *blocked = sample.blocked;
    return true;
}

void dkr::runtime::platform::set_rumble(int controller, bool enabled) {
    if (!dkr::runtime::netplay::external_side_effects_allowed()) return;
#if DKR_RUNTIME_HAS_RT64
    if (g_input_backend_switch_in_progress.load(std::memory_order_acquire)) {
        return;
    }
    controller = dkr::runtime::netplay::physical_rumble_port(
        g_online_input_routing.load(std::memory_order_acquire),
        g_online_local_slot.load(std::memory_order_acquire),
        g_online_input_profile.load(std::memory_order_acquire), controller);
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return;
    }
    if (active_input_backend() == InputBackend::SDL3Native) {
        int instance = -1;
        {
            std::scoped_lock lock(g_platform_mutex);
            instance = g_sdl3_player_instances[static_cast<std::size_t>(controller)];
        }
        if (instance >= 0) {
            const bool active = enabled &&
                g_rumble_enabled.load(std::memory_order_acquire);
            const auto strength = active
                ? static_cast<std::uint16_t>(std::lround(
                      std::clamp(g_rumble_strength.load(
                                     std::memory_order_relaxed),
                                 0.0F, 1.0F) * 65535.0F))
                : 0U;
            g_sdl3_input_client.rumble(
                instance, strength, strength,
                active ? 0xFFFFFFFFU : 0U);
        }
        return;
    }
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameController* game_controller = g_controllers[static_cast<std::size_t>(controller)];
    if (game_controller != nullptr) {
        const bool active = enabled && g_rumble_enabled.load(std::memory_order_acquire);
        const Uint16 strength = active
            ? static_cast<Uint16>(std::lround(
                  std::clamp(g_rumble_strength.load(std::memory_order_relaxed),
                             0.0F, 1.0F) * 65535.0F))
            : 0U;
        SDL_GameControllerRumble(game_controller, strength, strength,
                                 active ? SDL_HAPTIC_INFINITY : 0U);
    }
#else
    (void)controller;
    (void)enabled;
#endif
}

float dkr::runtime::platform::rumble_strength() {
    return g_rumble_strength.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_rumble_strength(float strength) {
    g_rumble_strength.store(std::clamp(strength, 0.0F, 1.0F),
                            std::memory_order_release);
}

bool dkr::runtime::platform::rumble_enabled() {
    return g_rumble_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_rumble_enabled(bool enabled) {
    g_rumble_enabled.store(enabled, std::memory_order_release);
#if DKR_RUNTIME_HAS_RT64
    if (!enabled) {
        if (active_input_backend() == InputBackend::SDL3Native) {
            std::vector<int> instances;
            {
                std::scoped_lock lock(g_platform_mutex);
                for (const auto& device : g_sdl3_input_state.devices) {
                    instances.push_back(device.instance);
                }
            }
            for (const int instance : instances) {
                g_sdl3_input_client.rumble(instance, 0U, 0U, 0U);
            }
            return;
        }
        std::scoped_lock lock(g_platform_mutex);
        for (const ControllerRecord& record : g_controller_devices) {
            if (record.controller != nullptr) {
                SDL_GameControllerRumble(record.controller, 0U, 0U, 0U);
            }
        }
    }
#endif
}

bool dkr::runtime::platform::gyro_available(std::size_t player) {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (active_input_backend() == InputBackend::SDL3Native) {
        const auto* device = Sdl3DeviceForInstance(
            g_sdl3_gyro_instances[std::min(player, kControllerCount - 1U)]);
        return device != nullptr && device->gyro &&
               device->snapshot.gyro.available;
    }
    SDL_GameController* controller = g_gyro_controllers[
        std::min(player, kControllerCount - 1U)];
    return controller != nullptr &&
           SDL_GameControllerGetAttached(controller) == SDL_TRUE &&
           SDL_GameControllerHasSensor(
               controller, SDL_SENSOR_GYRO) == SDL_TRUE;
#else
    return false;
#endif
}

ultramodern::input::connected_device_info_t
dkr::runtime::platform::get_connected_device_info(int controller) {
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    }
#if DKR_RUNTIME_HAS_RT64
    const bool expose_rumble =
        dkr::runtime::pak::policy::expose_rumble_pak(
            g_rumble_enabled.load(std::memory_order_acquire), true);
    const bool online_routing =
        g_online_input_routing.load(std::memory_order_acquire);
    if (dkr::runtime::netplay::online_port_occupied(
            online_routing,
            g_online_occupied_mask.load(std::memory_order_acquire),
            static_cast<std::size_t>(controller))) {
        // All peers must expose the same virtual N64 ports. Local physical
        // layout is deliberately irrelevant to game-side roster discovery.
        return {ultramodern::input::Device::Controller,
                expose_rumble ? ultramodern::input::Pak::RumblePak
                              : ultramodern::input::Pak::None};
    }
    std::scoped_lock lock(g_platform_mutex);
    const bool has_gamepad = active_input_backend() == InputBackend::SDL3Native
        ? Sdl3DeviceForInstance(g_sdl3_player_instances[
              static_cast<std::size_t>(controller)]) != nullptr
        : g_controllers[static_cast<std::size_t>(controller)] != nullptr;
    const bool owns_keyboard =
        dkr::runtime::netplay::keyboard_owns_input_port(
            online_routing, static_cast<std::size_t>(controller),
            dkr::runtime::input::keyboard_player());
    return {has_gamepad || owns_keyboard
                ? ultramodern::input::Device::Controller
                : ultramodern::input::Device::None,
            dkr::runtime::pak::policy::expose_rumble_pak(
                expose_rumble, has_gamepad)
                ? ultramodern::input::Pak::RumblePak
                : ultramodern::input::Pak::None};
#else
    return {ultramodern::input::Device::Controller, ultramodern::input::Pak::None};
#endif
}

std::vector<dkr::runtime::platform::ControllerSummary>
dkr::runtime::platform::connected_controllers() {
    std::vector<ControllerSummary> summaries;
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (active_input_backend() == InputBackend::SDL3Native) {
        summaries.reserve(g_sdl3_input_state.devices.size());
        for (const auto& device : g_sdl3_input_state.devices) {
            int assigned_player = -1;
            for (std::size_t player = 0U; player < kControllerCount; ++player) {
                if (g_sdl3_player_instances[player] == device.instance) {
                    assigned_player = static_cast<int>(player);
                    break;
                }
            }
            summaries.push_back({device.instance, device.name, assigned_player,
                                 device.rumble, device.gyro, device.mapped,
                                 device.mapping_source});
        }
        return summaries;
    }
    summaries.reserve(g_controller_devices.size());
    for (const ControllerRecord& record : g_controller_devices) {
        int assigned_player = -1;
        for (std::size_t player = 0; player < kControllerCount; ++player) {
            if (g_controllers[player] == record.controller) {
                assigned_player = static_cast<int>(player);
                break;
            }
        }
        summaries.push_back({static_cast<int>(record.instance), record.name,
                             assigned_player, record.rumble, record.gyro,
                             record.mapped, record.mapping_source});
    }
#endif
    return summaries;
}

dkr::runtime::platform::PlayerControllerStatus
dkr::runtime::platform::player_controller_status(std::size_t player) {
    PlayerControllerStatus status{};
#if DKR_RUNTIME_HAS_RT64
    if (player >= kControllerCount) {
        return status;
    }
    std::scoped_lock lock(g_platform_mutex);
    status.assigned = !g_controller_assignments[player].empty();
    if (active_input_backend() == InputBackend::SDL3Native) {
        const auto* device = Sdl3DeviceForInstance(
            g_sdl3_player_instances[player]);
        if (device != nullptr) {
            status = {true, true, device->name, device->rumble, device->gyro,
                      device->mapping_source};
        }
        return status;
    }
    SDL_GameController* handle = g_controllers[player];
    const auto record = std::find_if(
        g_controller_devices.begin(), g_controller_devices.end(),
        [handle](const ControllerRecord& candidate) {
            return candidate.controller == handle;
        });
    if (record != g_controller_devices.end()) {
        status = {true, true, record->name, record->rumble, record->gyro,
                  record->mapping_source};
    }
#else
    (void)player;
#endif
    return status;
}

dkr::runtime::platform::PlayerInputPreview
dkr::runtime::platform::player_input_preview(std::size_t player) {
    if (player >= kControllerCount) {
        return {};
    }
    PlayerInputPreview preview{
        g_buttons[player].load(std::memory_order_acquire),
        g_stick_x[player].load(std::memory_order_acquire),
        g_stick_y[player].load(std::memory_order_acquire)};
#if DKR_RUNTIME_HAS_RT64
    // The gameplay input path is deliberately neutral while the overlay owns
    // input, and it may not run at all on the launcher screen. Read the
    // selected player's assigned SDL controller directly so this diagnostic
    // remains live without feeding any input back into the game.
    std::scoped_lock lock(g_platform_mutex);
    if (active_input_backend() == InputBackend::SDL3Native) {
        RefreshSdl3Controllers();
        const auto* device = Sdl3DeviceForInstance(
            g_sdl3_player_instances[player]);
        if (device != nullptr) {
            using dkr::runtime::controllers::StandardAxis;
            preview.stick_x = NormalisePreviewAxis(device->snapshot.axes[
                static_cast<std::size_t>(StandardAxis::LeftX)]);
            preview.stick_y = -NormalisePreviewAxis(device->snapshot.axes[
                static_cast<std::size_t>(StandardAxis::LeftY)]);
        }
        return preview;
    }
    SDL_GameControllerUpdate();
    SDL_GameController* controller = g_controllers[player];
    if (controller != nullptr &&
        SDL_GameControllerGetAttached(controller) == SDL_TRUE) {
        preview.stick_x = NormalisePreviewAxis(SDL_GameControllerGetAxis(
            controller, SDL_CONTROLLER_AXIS_LEFTX));
        preview.stick_y = -NormalisePreviewAxis(SDL_GameControllerGetAxis(
            controller, SDL_CONTROLLER_AXIS_LEFTY));
    }
#endif
    return preview;
}

dkr::runtime::controllers::AssignmentMode
dkr::runtime::platform::controller_assignment_mode() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    return g_controller_assignment_mode;
#else
    return controllers::AssignmentMode::Automatic;
#endif
}

void dkr::runtime::platform::set_controller_assignment_mode(
    controllers::AssignmentMode mode) {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    const auto next = mode == controllers::AssignmentMode::Manual
        ? controllers::AssignmentMode::Manual
        : controllers::AssignmentMode::Automatic;
    if (next == controllers::AssignmentMode::Automatic &&
        g_controller_assignment_mode != next) {
        g_controller_assignments = {};
    }
    g_controller_assignment_mode = next;
    if (active_input_backend() == InputBackend::SDL3Native) {
        RefreshSdl3Controllers();
    } else {
        RefreshControllers();
    }
#else
    (void)mode;
#endif
}

bool dkr::runtime::platform::assign_controller(std::size_t player,
                                               int instance) {
#if DKR_RUNTIME_HAS_RT64
    if (player >= kControllerCount) {
        return false;
    }
    std::scoped_lock lock(g_platform_mutex);
    if (active_input_backend() == InputBackend::SDL3Native) {
        RefreshSdl3Controllers();
        const auto* device = Sdl3DeviceForInstance(instance);
        if (device == nullptr || !device->mapped ||
            !controllers::claim(g_controller_assignments, player,
                canonical_controller_key(device->persistent_key))) {
            return false;
        }
        g_controller_assignment_mode = controllers::AssignmentMode::Manual;
        ReconcileSdl3Controllers();
        return true;
    }
    RefreshControllers();
    const auto record = std::find_if(
        g_controller_devices.begin(), g_controller_devices.end(),
        [instance](const ControllerRecord& candidate) {
            return candidate.instance == instance;
        });
    if (record == g_controller_devices.end() || !record->mapped ||
        record->controller == nullptr ||
        !controllers::claim(g_controller_assignments, player,
                            record->persistent_key)) {
        return false;
    }
    g_controller_assignment_mode = controllers::AssignmentMode::Manual;
    ReconcileControllers();
    return true;
#else
    (void)player;
    (void)instance;
    return false;
#endif
}

bool dkr::runtime::platform::clear_controller_assignment(std::size_t player) {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (!controllers::clear(g_controller_assignments, player)) {
        return false;
    }
    g_controller_assignment_mode = controllers::AssignmentMode::Manual;
    if (active_input_backend() == InputBackend::SDL3Native) {
        ReconcileSdl3Controllers();
    } else {
        ReconcileControllers();
    }
    return true;
#else
    (void)player;
    return false;
#endif
}

int dkr::runtime::platform::controller_instance_for_player(std::size_t player) {
#if DKR_RUNTIME_HAS_RT64
    if (player >= kControllerCount) {
        return -1;
    }
    std::scoped_lock lock(g_platform_mutex);
    if (active_input_backend() == InputBackend::SDL3Native) {
        return g_sdl3_player_instances[player];
    }
    SDL_GameController* handle = g_controllers[player];
    return handle != nullptr
        ? static_cast<int>(SDL_JoystickInstanceID(
              SDL_GameControllerGetJoystick(handle)))
        : -1;
#else
    (void)player;
    return -1;
#endif
}

int dkr::runtime::platform::controller_player_for_instance(int instance) {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (active_input_backend() == InputBackend::SDL3Native) {
        for (std::size_t player = 0U; player < kControllerCount; ++player) {
            if (g_sdl3_player_instances[player] == instance) {
                return static_cast<int>(player);
            }
        }
        return -1;
    }
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        SDL_GameController* handle = g_controllers[player];
        if (handle != nullptr && SDL_JoystickInstanceID(
                SDL_GameControllerGetJoystick(handle)) == instance) {
            return static_cast<int>(player);
        }
    }
#else
    (void)instance;
#endif
    return -1;
}

bool dkr::runtime::platform::identify_controller(std::size_t player) {
#if DKR_RUNTIME_HAS_RT64
    if (player >= kControllerCount) {
        return false;
    }
    if (active_input_backend() == InputBackend::SDL3Native) {
        int instance = -1;
        {
            std::scoped_lock lock(g_platform_mutex);
            instance = g_sdl3_player_instances[player];
        }
        return instance >= 0 &&
            g_sdl3_input_client.rumble(instance, 32767U, 32767U, 450U);
    }
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameController* handle = g_controllers[player];
    if (handle == nullptr) {
        return false;
    }
    return SDL_GameControllerRumble(handle, 32767U, 32767U, 450U) == 0;
#else
    (void)player;
    return false;
#endif
}

bool dkr::runtime::platform::begin_controller_mapping(int instance,
                                                      std::size_t player) {
#if DKR_RUNTIME_HAS_RT64
    if (player >= kControllerCount) {
        return false;
    }
    if (active_input_backend() == InputBackend::SDL3Native) {
        (void)instance;
        return false;
    }
    std::scoped_lock lock(g_platform_mutex);
    RefreshControllers();
    const auto record = std::find_if(
        g_controller_devices.begin(), g_controller_devices.end(),
        [instance](const ControllerRecord& candidate) {
            return candidate.instance == instance;
        });
    if (record == g_controller_devices.end() || record->joystick == nullptr) {
        return false;
    }
    g_mapping_session = {};
    g_mapping_session.state = ControllerMappingSession::State::Capturing;
    g_mapping_session.instance = record->instance;
    g_mapping_session.player = player;
    g_mapping_session.controller_name = record->name;
    g_mapping_session.message =
        "Use the physical controls requested below. Escape cancels.";
    SDL_JoystickUpdate();
    const int axis_count = std::max(SDL_JoystickNumAxes(record->joystick), 0);
    g_mapping_session.axis_neutral.resize(static_cast<std::size_t>(axis_count));
    for (int axis = 0; axis < axis_count; ++axis) {
        g_mapping_session.axis_neutral[static_cast<std::size_t>(axis)] =
            SDL_JoystickGetAxis(record->joystick, axis);
    }
    return true;
#else
    (void)instance;
    (void)player;
    return false;
#endif
}

bool dkr::runtime::platform::handle_controller_mapping_event(
    const void* raw_event) {
#if DKR_RUNTIME_HAS_RT64
    const auto* event = static_cast<const SDL_Event*>(raw_event);
    if (event == nullptr) {
        return false;
    }
    std::scoped_lock lock(g_platform_mutex);
    if (g_mapping_session.state !=
        ControllerMappingSession::State::Capturing) {
        return false;
    }

    using controllers::PhysicalInput;
    using controllers::PhysicalInputKind;
    const auto axis_delta = [&](int axis, Sint16 value) {
        const Sint16 neutral = axis >= 0 &&
                static_cast<std::size_t>(axis) <
                    g_mapping_session.axis_neutral.size()
            ? g_mapping_session.axis_neutral[static_cast<std::size_t>(axis)]
            : 0;
        return static_cast<int>(value) - static_cast<int>(neutral);
    };
    const auto event_instance = [&]() -> SDL_JoystickID {
        switch (event->type) {
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            return event->jbutton.which;
        case SDL_JOYAXISMOTION:
            return event->jaxis.which;
        case SDL_JOYHATMOTION:
            return event->jhat.which;
        default:
            return -1;
        }
    }();
    if (event_instance != g_mapping_session.instance) {
        return false;
    }

    if (g_mapping_session.waiting_for_release) {
        const PhysicalInput& held = g_mapping_session.release_input;
        bool released = false;
        if (held.kind == PhysicalInputKind::Button &&
            event->type == SDL_JOYBUTTONUP &&
            static_cast<int>(event->jbutton.button) == held.index) {
            released = true;
        } else if (held.kind == PhysicalInputKind::Axis &&
                   event->type == SDL_JOYAXISMOTION &&
                   static_cast<int>(event->jaxis.axis) == held.index &&
                   std::abs(axis_delta(held.index, event->jaxis.value)) < 8000) {
            released = true;
        } else if (held.kind == PhysicalInputKind::Hat &&
                   event->type == SDL_JOYHATMOTION &&
                   static_cast<int>(event->jhat.hat) == held.index &&
                   event->jhat.value == SDL_HAT_CENTERED) {
            released = true;
        }
        if (released) {
            g_mapping_session.waiting_for_release = false;
            g_mapping_session.release_input = {};
            g_mapping_session.message =
                "Ready for the next control.";
        }
        return true;
    }

    PhysicalInput input{};
    if (event->type == SDL_JOYBUTTONDOWN) {
        input = {PhysicalInputKind::Button,
                 static_cast<int>(event->jbutton.button), 1, 0};
    } else if (event->type == SDL_JOYAXISMOTION &&
               std::abs(axis_delta(static_cast<int>(event->jaxis.axis),
                                   event->jaxis.value)) >= 20000) {
        const int delta = axis_delta(static_cast<int>(event->jaxis.axis),
                                     event->jaxis.value);
        input = {PhysicalInputKind::Axis,
                  static_cast<int>(event->jaxis.axis),
                  delta > 0 ? 1 : -1, 0};
    } else if (event->type == SDL_JOYHATMOTION &&
               event->jhat.value != SDL_HAT_CENTERED) {
        const std::uint8_t value = event->jhat.value;
        const bool cardinal = value == SDL_HAT_UP || value == SDL_HAT_DOWN ||
                              value == SDL_HAT_LEFT || value == SDL_HAT_RIGHT;
        if (!cardinal) {
            g_mapping_session.message =
                "Press one D-pad direction at a time.";
            return true;
        }
        input = {PhysicalInputKind::Hat,
                 static_cast<int>(event->jhat.hat), 1, value};
    } else {
        return true;
    }

    if (!controllers::mapping_input_available(g_mapping_session.definition,
                                               g_mapping_session.step, input)) {
        g_mapping_session.message =
            "That physical input is already assigned. Use a different control.";
        return true;
    }
    g_mapping_session.definition.inputs[g_mapping_session.step] = input;
    ++g_mapping_session.step;
    if (g_mapping_session.step >= controllers::kMappingControlCount) {
        FinishControllerMapping();
        return true;
    }
    g_mapping_session.release_input = input;
    g_mapping_session.waiting_for_release = true;
    g_mapping_session.message = input.kind == PhysicalInputKind::Axis
        ? "Return the stick or trigger to centre."
        : "Release the control to continue.";
    return true;
#else
    (void)raw_event;
    return false;
#endif
}

void dkr::runtime::platform::cancel_controller_mapping() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    g_mapping_session = {};
#endif
}

void dkr::runtime::platform::dismiss_controller_mapping() {
    cancel_controller_mapping();
}

dkr::runtime::platform::ControllerMappingProgress
dkr::runtime::platform::controller_mapping_progress() {
    ControllerMappingProgress progress{};
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    progress.visible = g_mapping_session.state !=
        ControllerMappingSession::State::Idle;
    progress.capturing = g_mapping_session.state ==
        ControllerMappingSession::State::Capturing;
    progress.complete = g_mapping_session.state ==
        ControllerMappingSession::State::Complete;
    progress.success = g_mapping_session.success;
    progress.controller_name = g_mapping_session.controller_name;
    progress.step = g_mapping_session.step;
    progress.total = controllers::kMappingControlCount;
    progress.message = g_mapping_session.message;
    if (progress.capturing && progress.step < progress.total) {
        progress.prompt = controllers::mapping_prompt(
            static_cast<controllers::MappingControl>(progress.step));
    }
#endif
    return progress;
}

bool dkr::runtime::platform::import_controller_mappings(
    const std::filesystem::path& source, std::string& status) {
#if DKR_RUNTIME_HAS_RT64
    if (active_input_backend() == InputBackend::SDL3Native) {
        (void)source;
        status = "Controller map import uses SDL2 compatibility mode. "
                 "Select it and restart DKR-R before importing.";
        return false;
    }
    std::scoped_lock lock(g_platform_mutex);
    const std::vector<std::string> lines = ReadMappingLines(source);
    std::vector<std::string> valid;
    valid.reserve(lines.size());
    for (const std::string& line : lines) {
        if (!MappingLineKey(line).empty() &&
            SDL_GameControllerAddMapping(line.c_str()) >= 0) {
            valid.push_back(line);
        }
    }
    if (valid.empty()) {
        status = "No valid SDL controller mappings were found in that file.";
        return false;
    }
    if (!MergeUserMappings(valid, status)) {
        return false;
    }
    for (ControllerRecord& record : g_controller_devices) {
        CloseControllerRecord(record);
    }
    g_controller_devices.clear();
    g_controllers.fill(nullptr);
    g_gyro_controllers.fill(nullptr);
    RefreshControllers();
    status = "Imported " + std::to_string(valid.size()) +
             " controller mapping" + (valid.size() == 1U ? "." : "s.");
    return true;
#else
    (void)source;
    status = "Controller mappings are unavailable in this build.";
    return false;
#endif
}

bool dkr::runtime::platform::export_controller_mappings(
    const std::filesystem::path& destination, std::string& status) {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    std::error_code error;
    if (!std::filesystem::is_regular_file(g_user_mapping_path, error)) {
        status = "No custom controller mappings have been created yet.";
        return false;
    }
    std::filesystem::copy_file(
        g_user_mapping_path, destination,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        status = "Could not export the controller mapping file.";
        return false;
    }
    status = "Controller mappings exported.";
    return true;
#else
    (void)destination;
    status = "Controller mappings are unavailable in this build.";
    return false;
#endif
}

dkr::runtime::controllers::DesiredAssignments
dkr::runtime::platform::controller_assignment_keys() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    return g_controller_assignments;
#else
    return {};
#endif
}

void dkr::runtime::platform::restore_controller_assignments(
    controllers::AssignmentMode mode,
    const controllers::DesiredAssignments& assignments) {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    g_controller_assignment_mode = mode == controllers::AssignmentMode::Manual
        ? controllers::AssignmentMode::Manual
        : controllers::AssignmentMode::Automatic;
    g_controller_assignments = assignments;
    for (std::string& assignment : g_controller_assignments) {
        assignment = canonical_controller_key(assignment);
    }
    if (active_input_backend() == InputBackend::SDL3Native) {
        ReconcileSdl3Controllers();
    } else {
        ReconcileControllers();
    }
#else
    (void)mode;
    (void)assignments;
#endif
}
