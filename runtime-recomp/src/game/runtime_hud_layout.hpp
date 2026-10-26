#pragma once

#include "hud_layout_policy.hpp"
#include "hud_group_layout.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct recomp_context;

namespace dkr::runtime::hud {

void configure(const std::filesystem::path& config_directory);

LayoutMode mode();
bool apply_basic_mode(LayoutMode value);
bool self_test_basic_settings(const std::filesystem::path& empty_directory);
void set_mode(LayoutMode value);
float global_scale();
void set_global_scale(float value);

void set_viewport_extent(int width, int height);
void begin_authored_frame(std::uint8_t* rdram);
groups::Layout layout();
bool apply_layout(const groups::Layout& value);
void preview_layout(const std::optional<groups::Layout>& value);
std::vector<std::string> preset_names();
bool save_preset(const std::string& name, const groups::Layout& value);
std::optional<groups::Layout> load_preset(const std::string& name);
bool delete_preset(const std::string& name);
void begin_timer(std::uint8_t* rdram, recomp_context* context);
void end_timer(std::uint8_t* rdram, recomp_context* context);

void begin_element(std::uint8_t* rdram, recomp_context* context);
void end_element(std::uint8_t* rdram, recomp_context* context);
void begin_minimap(std::uint8_t* rdram, recomp_context* context);
void end_minimap(std::uint8_t* rdram, recomp_context* context);
void begin_player_pass(std::uint8_t* rdram, recomp_context* context);
void end_player_pass(std::uint8_t* rdram, recomp_context* context);
void begin_general_pass(std::uint8_t* rdram, recomp_context* context);
void end_general_pass(std::uint8_t* rdram, recomp_context* context);
void begin_dialogue_pass(std::uint8_t* rdram, recomp_context* context);
void end_dialogue_pass(std::uint8_t* rdram, recomp_context* context);

} // namespace dkr::runtime::hud

extern "C" void dkr_hud_element_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_element_end(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_minimap_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_minimap_end(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_player_pass_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_player_pass_end(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_general_pass_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_general_pass_end(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_dialogue_pass_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_dialogue_pass_end(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_refresh_combined_accessories(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_timer_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_timer_end(std::uint8_t* rdram, recomp_context* context);
