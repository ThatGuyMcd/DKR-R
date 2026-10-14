#pragma once

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace dkr::runtime::accessibility {

struct Bounds {
    float x = 0, y = 0, width = 0, height = 0;
};
enum class Role { Button, PushButton = Button, StaticText, Checkbox, Slider, Choice, Disclosure, MenuItem, TextField };
enum class ActionKind { Press, Focus, SetValue, Increment, Decrement, SetText };
struct Node {
    std::string id, label;
    Role role = Role::StaticText;
    Bounds bounds;
    bool enabled = true, selected = false, focused = false;
    std::string value, help;
    double number = 0, minimum = 0, maximum = 0, step = 1;
    std::vector<std::string> choices;
    std::vector<bool> choice_enabled;
    std::size_t text_capacity = 0; // UTF-8 bytes, excluding the terminator
};
struct Snapshot {
    std::uint64_t generation = 0;
    std::string scope, label;
    bool modal = false;
    std::vector<Node> nodes;
};
struct Action {
    std::uint64_t generation = 0;
    std::string scope, id;
    ActionKind kind = ActionKind::Press;
    double value = 0;
    std::string text;
};
inline constexpr std::size_t kMaxTextBytes = 4096;
// Single-line UTF-8 only; rejected edits never partially replace the value.
bool valid_text_value(std::string_view text, std::size_t capacity);
enum class NativeFocusPolicy { RegisteredControls, GameSummary };
struct Observation;
struct ManualStatus {
    std::string text;
    std::chrono::steady_clock::time_point until;
    std::uint64_t session = 0;
};
// Compose read-only game information from one fresh mailbox value. An active
// host scope always wins; retained one-shot status never retains menu rows.
Snapshot native_snapshot(Snapshot host, const std::optional<Observation>& observation,
                         const std::optional<ManualStatus>& manual = std::nullopt,
                         std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
                         std::uint64_t session = 0,
                         NativeFocusPolicy focus_policy = NativeFocusPolicy::RegisteredControls);

// Native set-value requests snap to the declared grid; increment/decrement
// preserve their declared delta even when the current value is off-grid.
// Clamp before rounding/conversion so even malformed requests stay safe.
inline int range_value_for_action(int current, int minimum, int maximum, int step, const Action& action) {
    if (minimum > maximum) return current;
    const double stride = std::max(step, 1);
    double requested = current;
    switch (action.kind) {
    case ActionKind::SetValue: requested = action.value; break;
    case ActionKind::Increment: requested += stride; break;
    case ActionKind::Decrement: requested -= stride; break;
    default: return current;
    }
    if (!std::isfinite(requested)) return current;
    requested = std::clamp(requested, static_cast<double>(minimum), static_cast<double>(maximum));
    if (action.kind == ActionKind::SetValue)
        requested = stride == 1.0 ? std::round(requested)
                                  : minimum + std::round((requested - minimum) / stride) * stride;
    return static_cast<int>(std::clamp(requested, static_cast<double>(minimum), static_cast<double>(maximum)));
}

// Native callbacks only enqueue identifiers. The host checks the node again
// while drawing its current frame, before entering the existing button branch.
class HostTree {
public:
    void begin_frame(std::string scope, std::string label, bool modal = false);
    void select_scope(std::string scope, std::string label, bool modal = false);
    bool add_button(std::string id, std::string label, Bounds bounds,
                    bool enabled = true, bool selected = false, bool keyboard_focused = false);
    void add_text(std::string id, std::string label, Bounds bounds = {});
    std::optional<Action> add_control(Node node);
    void end_frame();
    void clear();
    Snapshot snapshot() const;
    bool enqueue(Action action);
    bool has_pending_focus() const;
    bool has_accessibility_focus(const std::string& id) const;
    bool take_focus_request(const std::string& id);
private:
    mutable std::mutex mutex_;
    Snapshot published_, frame_;
    std::deque<Action> queued_, pending_;
    std::string focused_id_, saved_focus_;
    std::string keyboard_focus_, frame_keyboard_focus_;
    std::string focus_request_;
    std::string focus_request_scope_, saved_focus_scope_;
    std::optional<Node> focus_request_target_, saved_focus_target_;
    bool building_ = false, pressed_ = false;
    bool accessibility_focus_ = false, saved_accessibility_focus_ = false;
    std::optional<Action> register_node(Node node);
};

std::string spoken_label(std::string label);
HostTree& host_tree();
void begin_frame(std::string scope, std::string label, bool modal = false);
void select_scope(std::string scope, std::string label, bool modal = false);
bool add_button(std::string id, std::string label, Bounds bounds,
                bool enabled = true, bool selected = false, bool keyboard_focused = false);
void add_text(std::string id, std::string label, Bounds bounds = {});
std::optional<Action> add_control(Node node);
void end_frame();
void clear();

// Only pump_native/detach_native touch the OS, and only on the main thread.
// Render/observer workers may publish plain C++ state without calling AppKit.
void pump_native(SDL_Window* window);
void detach_native();
void set_game_status(std::string text);
void request_game_status_read();
std::string game_status();
struct DisplayPreferences {
    bool increase_contrast = false, reduce_transparency = false;
    bool reduce_motion = false, differentiate_without_color = false;
};
DisplayPreferences display_preferences();
void set_display_preferences(DisplayPreferences preferences);

namespace native {
void update(SDL_Window* window, const Snapshot& snapshot);
void detach();
void announce(const std::string& text);
bool screen_reader_active();
bool input_focused(SDL_Window* window);
}
} // namespace dkr::runtime::accessibility
