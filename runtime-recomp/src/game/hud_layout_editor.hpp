#pragma once
#include <string>
namespace dkr::runtime::hud::editor {
bool draw_settings(bool modern, float width, void (*request_preset_name)(), void (*draw_keyboard)(),
                   bool in_game = false);
bool review_active();
void draw_review();
void accept_preset_name(const std::string& name);
void cancel();
bool active();
void request_back();
void interrupt_move();
} // namespace dkr::runtime::hud::editor
