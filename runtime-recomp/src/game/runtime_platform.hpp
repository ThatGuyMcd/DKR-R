#pragma once

#include "ultramodern/input.hpp"
#include "ultramodern/renderer_context.hpp"
#include "controller_assignment_policy.hpp"
#include "input_backend_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dkr::runtime::platform {

bool initialise();
void shutdown();
void configure_input(const std::filesystem::path& config_directory);
InputBackend requested_input_backend();
InputBackend active_input_backend();
void set_requested_input_backend(InputBackend backend);
const char* input_backend_name(InputBackend backend);
std::string input_backend_detail();
bool input_backend_switch_pending();

#if DKR_RUNTIME_HAS_RT64
ultramodern::renderer::WindowHandle create_window();
ultramodern::renderer::WindowHandle prepare_window_for_game();
void* prepare_window_for_launcher();
void pump_window_events(void*);
void* sdl_window();
bool handle_window_shortcut(const void* event, bool renderer_active);
void toggle_fullscreen(bool renderer_active);
void update_fullscreen_cursor(const void* event = nullptr);
void update_ui_gamepad_navigation();
void pump_input_backend_events();
#endif

void queue_audio(std::int16_t* samples, std::size_t sample_count);
// Requests a short, presentation-only synthesized UI cue. Launcher cues use a
// host-only audio path; in-game cues are mixed into the next host audio block.
// Neither path advances or mutates DKR's emulated audio state.
void request_ui_tone(float frequency_hz, std::uint32_t duration_ms);
std::size_t audio_frames_remaining();
void set_audio_frequency(std::uint32_t frequency);
float master_volume();
void set_master_volume(float volume);
float bass_gain();
void set_bass_gain(float decibels);
float mid_gain();
void set_mid_gain(float decibels);
float treble_gain();
void set_treble_gain(float decibels);

void poll_input();
// Online play has one local racer per machine. The selected local control
// profile feeds that racer; the synchronized session then routes the sample to
// its immutable online slot. The occupied mask also virtualizes identical N64
// controller ports on every peer.
void set_online_input_routing(bool enabled, std::uint8_t occupied_mask = 0U,
                              std::uint8_t local_slot = 0xFFU);
void set_online_input_profile(std::size_t profile);
std::size_t online_input_profile();
bool get_input(int controller, std::uint16_t* buttons, float* x, float* y);
// Reads the latest private physical sample for a local control profile. Online
// netcode consumes this lane before publishing a complete synchronized frame
// to DKR's virtual controller ports.
bool get_physical_input(int controller, std::uint16_t* buttons, float* x,
                        float* y);
bool get_local_online_input(std::uint16_t* buttons, float* x, float* y,
                            bool* blocked = nullptr);
void set_rumble(int controller, bool enabled);
bool rumble_enabled();
void set_rumble_enabled(bool enabled);
float rumble_strength();
void set_rumble_strength(float strength);
bool gyro_available(std::size_t player = 0U);
ultramodern::input::connected_device_info_t get_connected_device_info(int controller);

struct ControllerSummary {
    int instance = -1;
    std::string name;
    int assigned_player = -1;
    bool rumble = false;
    bool gyro = false;
    bool mapped = true;
    std::string mapping_source;
};

struct PlayerControllerStatus {
    bool assigned = false;
    bool connected = false;
    std::string name;
    bool rumble = false;
    bool gyro = false;
    std::string mapping_source;
};

struct PlayerInputPreview {
    std::uint16_t buttons = 0;
    float stick_x = 0.0F;
    float stick_y = 0.0F;
};

struct ControllerMappingProgress {
    bool visible = false;
    bool capturing = false;
    bool complete = false;
    bool success = false;
    std::string controller_name;
    std::string prompt;
    std::string message;
    std::size_t step = 0;
    std::size_t total = 0;
};

std::vector<ControllerSummary> connected_controllers();
PlayerControllerStatus player_controller_status(std::size_t player);
PlayerInputPreview player_input_preview(std::size_t player);
controllers::AssignmentMode controller_assignment_mode();
void set_controller_assignment_mode(controllers::AssignmentMode mode);
bool assign_controller(std::size_t player, int instance);
bool clear_controller_assignment(std::size_t player);
int controller_instance_for_player(std::size_t player);
int controller_player_for_instance(int instance);
bool identify_controller(std::size_t player);
bool begin_controller_mapping(int instance, std::size_t player);
bool handle_controller_mapping_event(const void* event);
void cancel_controller_mapping();
void dismiss_controller_mapping();
ControllerMappingProgress controller_mapping_progress();
bool import_controller_mappings(const std::filesystem::path& source,
                                std::string& status);
bool export_controller_mappings(const std::filesystem::path& destination,
                                std::string& status);
controllers::DesiredAssignments controller_assignment_keys();
void restore_controller_assignments(
    controllers::AssignmentMode mode,
    const controllers::DesiredAssignments& assignments);

} // namespace dkr::runtime::platform
