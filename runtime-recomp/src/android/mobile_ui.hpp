#pragma once
#include "../game/runtime_input.hpp"
#include <filesystem>
union SDL_Event;
namespace dkr::runtime::mobile {
void configure(const std::filesystem::path& directory);
void install_context();
bool event(const SDL_Event& event, bool gameplay);
void merge_input(input::State& state, bool blocked);
void clear();
void draw_controls(bool overlay);
void settings();
void editor();
bool editing();
bool controller_keyboard();
void constrain_modal();
void menu(int& page, bool live, bool& restart, bool& quit);
void begin_content();
void end_content();
} // namespace dkr::runtime::mobile
