#pragma once
#include <atomic>
namespace dkr::chat {
// Event and render threads communicate through requests, not ImGui state.
inline std::atomic<bool> typing{false}, open_requested{false}, close_requested{false};
inline std::atomic<bool> capture_key{false}, settings_changed{false};
inline std::atomic<bool> suppress_open_text{false};
inline std::atomic<bool> on_screen_keyboard{false};
inline bool captures_game_input() {
    return typing.load() || capture_key.load();
}
inline void request_open() {
    typing = true;
    open_requested = true;
}
inline void request_close() {
    typing = false;
    close_requested = true;
    suppress_open_text = false;
}
enum class InputAction { Pass, Consume, Forward };
enum class InputKind { KeyDown, KeyUp, Text, Other };
inline InputAction input_action(InputKind kind, int key, bool repeat, bool can_open, int shortcut, bool escape) {
    if (typing.load()) {
        if (kind == InputKind::KeyDown && escape && !repeat) {
            request_close();
            return InputAction::Consume;
        }
        if (kind == InputKind::Text && suppress_open_text.exchange(false))
            return InputAction::Consume;
        if (kind == InputKind::KeyUp && key == shortcut)
            suppress_open_text = false;
        if (kind == InputKind::KeyDown || kind == InputKind::KeyUp || kind == InputKind::Text)
            return InputAction::Forward;
    } else if (can_open && kind == InputKind::KeyDown && key == shortcut && !repeat) {
        suppress_open_text = true;
        request_open();
        return InputAction::Consume;
    }
    return InputAction::Pass;
}
} // namespace dkr::chat
