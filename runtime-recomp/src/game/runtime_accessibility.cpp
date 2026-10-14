#include "runtime_accessibility.hpp"
#include "accessibility_observation.hpp"
#include "accessibility_feedback.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <utility>

namespace dkr::runtime::accessibility {
namespace {
constexpr std::size_t kMaxActions = 32;
constexpr std::size_t kMaxNodes = 128;
std::mutex status_mutex;
std::string status_text = "No game status available yet.";
std::atomic<bool> read_requested{false};
DisplayPreferences preferences;
bool pending_read = false;
Clock::time_point read_deadline{};
SessionToken read_session = 0;
std::uint64_t read_sequence = 0;
Clock::time_point manual_status_until{};
std::string manual_status_text;
SessionToken manual_status_session = 0;

bool same_target(const Node& a, const Node& b) {
    return a.id == b.id && a.label == b.label && a.role == b.role &&
        a.enabled == b.enabled && a.selected == b.selected && a.value == b.value &&
        a.number == b.number && a.minimum == b.minimum && a.maximum == b.maximum &&
        a.step == b.step && a.text_capacity == b.text_capacity && a.choices == b.choices && a.choice_enabled == b.choice_enabled;
}
bool can_focus(const Node& node) { return node.role != Role::StaticText; }
bool can_press(const Node& node) { return can_focus(node) && node.role != Role::Slider && node.role != Role::TextField; }
bool can_adjust(const Node& node) { return node.role == Role::Slider || node.role == Role::Choice; }
bool valid_action(const Action& action, const Node& node) {
    if (!node.enabled) return false;
    switch (action.kind) {
    case ActionKind::SetText:
        return node.role == Role::TextField && valid_text_value(action.text, node.text_capacity);
    case ActionKind::Focus: return can_focus(node);
    case ActionKind::Press: return can_press(node);
    case ActionKind::Increment: case ActionKind::Decrement: return can_adjust(node);
    case ActionKind::SetValue:
        if (!can_adjust(node) || !std::isfinite(action.value) ||
            action.value < node.minimum || action.value > node.maximum) return false;
        if (node.role == Role::Choice) {
            const auto index = static_cast<std::size_t>(action.value);
            return action.value == double(index) && index < node.choices.size() &&
                (node.choice_enabled.empty() || (index < node.choice_enabled.size() && node.choice_enabled[index]));
        }
        return true;
    }
    return false;
}
bool valid_bounds(Bounds b) {
    return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.width) &&
        std::isfinite(b.height) && b.width >= 0 && b.height >= 0;
}
}

bool valid_text_value(std::string_view text, std::size_t capacity) {
    if (capacity == 0 || capacity > kMaxTextBytes || text.size() > capacity) return false;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x20 || first == 0x7F) return false;
        if (first < 0x80) continue;
        unsigned count = 0;
        std::uint32_t codepoint = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { count = 1; codepoint = first & 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 2; codepoint = first & 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 3; codepoint = first & 7; minimum = 0x10000; }
        else return false;
        if (text.size() - i < count) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (codepoint < minimum || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint == 0x2028 || codepoint == 0x2029) return false;
    }
    return true;
}

std::string spoken_label(std::string label) {
    if (const auto suffix = label.find("##"); suffix != std::string::npos)
        label.resize(suffix);
    return label;
}

void HostTree::begin_frame(std::string scope, std::string label, bool modal) {
    std::scoped_lock lock(mutex_);
    frame_ = {published_.generation, std::move(scope), std::move(label), modal, {}};
    pending_.swap(queued_);
    queued_.clear();
    std::optional<Action> latest_focus;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->kind == ActionKind::Focus) {
            latest_focus = *it;
            it = pending_.erase(it);
        } else ++it;
    }
    if (latest_focus) pending_.push_back(std::move(*latest_focus));
    building_ = true;
    pressed_ = false;
    frame_keyboard_focus_.clear();
    if (frame_.scope != published_.scope) {
        focus_request_.clear();
        focus_request_target_.reset();
        focus_request_scope_.clear();
        pending_.clear();
        if (modal && !published_.modal) {
            saved_focus_ = focused_id_;
            saved_accessibility_focus_ = accessibility_focus_;
            saved_focus_scope_ = published_.scope;
            const auto saved = std::find_if(published_.nodes.begin(), published_.nodes.end(),
                [&](const Node& node) { return node.id == focused_id_; });
            saved_focus_target_ = saved != published_.nodes.end() ? std::optional<Node>(*saved) : std::nullopt;
        }
        focused_id_ = !modal && published_.modal && frame_.scope == saved_focus_scope_
            ? saved_focus_ : std::string{};
        accessibility_focus_ = !focused_id_.empty() && saved_accessibility_focus_;
        if (accessibility_focus_) {
            focus_request_ = focused_id_;
            focus_request_scope_ = frame_.scope;
            focus_request_target_ = saved_focus_target_;
        }
    }
}

void HostTree::select_scope(std::string scope, std::string label, bool modal) {
    std::scoped_lock lock(mutex_);
    if (!building_) return;
    if (frame_.scope != scope) {
        pending_.clear();
        if (modal && !frame_.modal) {
            saved_focus_ = focused_id_;
            saved_accessibility_focus_ = accessibility_focus_;
            saved_focus_scope_ = frame_.scope;
            const auto saved = std::find_if(frame_.nodes.begin(), frame_.nodes.end(),
                [&](const Node& node) { return node.id == focused_id_; });
            saved_focus_target_ = saved != frame_.nodes.end() ? std::optional<Node>(*saved) : std::nullopt;
        }
        frame_.nodes.clear();
        focused_id_.clear();
        accessibility_focus_ = false;
        focus_request_.clear();
        focus_request_target_.reset();
        focus_request_scope_.clear();
    }
    frame_.scope = std::move(scope);
    frame_.label = std::move(label);
    frame_.modal = modal;
}

std::optional<Action> HostTree::register_node(Node node) {
    if (!building_ || node.id.empty() || !valid_bounds(node.bounds) ||
        frame_.nodes.size() >= kMaxNodes) return std::nullopt;
    node.label = spoken_label(std::move(node.label));
    if (std::any_of(frame_.nodes.begin(), frame_.nodes.end(),
        [&](const Node& existing) { return existing.id == node.id; })) return std::nullopt;
    std::optional<Action> result;
    if (node.focused && node.enabled) {
        frame_keyboard_focus_ = node.id;
        if (keyboard_focus_ != node.id) {
            if (focused_id_ != node.id) accessibility_focus_ = false;
            focused_id_ = node.id;
        }
    }
    const auto previous = std::find_if(published_.nodes.begin(), published_.nodes.end(),
        [&](const Node& old) { return same_target(old, node); });
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->id != node.id) { ++it; continue; }
        const auto action = *it;
        it = pending_.erase(it);
        if (action.generation != published_.generation ||
            action.scope != frame_.scope || previous == published_.nodes.end() ||
            !valid_action(action, node)) continue;
        if (action.kind == ActionKind::Focus) {
            focused_id_ = node.id;
            accessibility_focus_ = true;
            focus_request_ = node.id;
            focus_request_scope_ = frame_.scope;
            focus_request_target_ = node;
        }
        else if (!pressed_) {
            result = action;
            pressed_ = true;
        }
    }
    node.focused = node.id == focused_id_;
    frame_.nodes.push_back(std::move(node));
    return result;
}

bool HostTree::add_button(std::string id, std::string label, Bounds bounds,
                          bool enabled, bool selected, bool keyboard_focused) {
    std::scoped_lock lock(mutex_);
    const auto result = register_node({std::move(id), std::move(label), Role::Button,
                          bounds, enabled, selected, keyboard_focused});
    return result && result->kind == ActionKind::Press;
}
std::optional<Action> HostTree::add_control(Node node) {
    std::scoped_lock lock(mutex_);
    return register_node(std::move(node));
}
void HostTree::add_text(std::string id, std::string label, Bounds bounds) {
    std::scoped_lock lock(mutex_);
    register_node({std::move(id), std::move(label), Role::StaticText, bounds});
}
void HostTree::end_frame() {
    std::scoped_lock lock(mutex_);
    if (!building_) return;
    // Keep the action generation stable across purely visual redraws. Every
    // semantic/scope change invalidates requests from the previous tree.
    const bool changed = frame_.scope != published_.scope ||
        frame_.modal != published_.modal || frame_.nodes.size() != published_.nodes.size() ||
        !std::equal(frame_.nodes.begin(), frame_.nodes.end(), published_.nodes.begin(),
            [](const Node& a, const Node& b) { return same_target(a, b); });
    frame_.generation = published_.generation + (changed ? 1 : 0);
    if (!focused_id_.empty() && std::none_of(frame_.nodes.begin(), frame_.nodes.end(),
        [&](const Node& node) { return node.id == focused_id_ && node.enabled; })) focused_id_.clear();
    if (focused_id_.empty()) {
        const auto first = std::find_if(frame_.nodes.begin(), frame_.nodes.end(),
            [](const Node& n) { return can_focus(n) && n.enabled; });
        if (first != frame_.nodes.end()) {
            focused_id_ = first->id;
            // A semantic fallback lets VoiceOver discover a first control.
            // It must not clear an active mouse edit or move keyboard focus.
            accessibility_focus_ = false;
        }
    }
    for (auto& node : frame_.nodes) node.focused = node.id == focused_id_;
    keyboard_focus_ = std::move(frame_keyboard_focus_);
    published_ = std::move(frame_);
    pending_.clear();
    building_ = false;
}
void HostTree::clear() {
    std::scoped_lock lock(mutex_);
    const auto generation = published_.generation + 1;
    published_ = {};
    published_.generation = generation;
    frame_ = {};
    queued_.clear(); pending_.clear();
    focused_id_.clear(); saved_focus_.clear();
    accessibility_focus_ = saved_accessibility_focus_ = false;
    keyboard_focus_.clear(); frame_keyboard_focus_.clear();
    focus_request_.clear();
    focus_request_scope_.clear(); saved_focus_scope_.clear();
    focus_request_target_.reset(); saved_focus_target_.reset();
    building_ = false;
}
Snapshot HostTree::snapshot() const {
    std::scoped_lock lock(mutex_);
    return published_;
}
bool HostTree::enqueue(Action action) {
    std::scoped_lock lock(mutex_);
    if (action.generation != published_.generation || action.scope != published_.scope ||
        queued_.size() >= kMaxActions || action.text.size() > kMaxTextBytes) return false;
    const auto node = std::find_if(published_.nodes.begin(), published_.nodes.end(),
        [&](const Node& n) { return n.id == action.id && valid_action(action, n); });
    if (node == published_.nodes.end()) return false;
    queued_.push_back(std::move(action));
    return true;
}
bool HostTree::has_pending_focus() const {
    std::scoped_lock lock(mutex_);
    return !focus_request_.empty() || std::any_of(pending_.begin(), pending_.end(),
        [](const Action& action) { return action.kind == ActionKind::Focus; });
}
bool HostTree::has_accessibility_focus(const std::string& id) const {
    std::scoped_lock lock(mutex_);
    return accessibility_focus_ && focused_id_ == id;
}
bool HostTree::take_focus_request(const std::string& id) {
    std::scoped_lock lock(mutex_);
    if (focus_request_ != id || focus_request_scope_ != frame_.scope) return false;
    const auto target = std::find_if(frame_.nodes.begin(), frame_.nodes.end(),
        [&](const Node& node) { return node.id == id && node.enabled; });
    if (target == frame_.nodes.end()) return false;
    if (!focus_request_target_ || !same_target(*target, *focus_request_target_)) {
        focus_request_.clear(); focus_request_target_.reset();
        if (focused_id_ == id) focused_id_.clear();
        return false;
    }
    focus_request_.clear();
    focus_request_target_.reset();
    return true;
}
HostTree& host_tree() { static HostTree tree; return tree; }
void begin_frame(std::string scope, std::string label, bool modal) {
    host_tree().begin_frame(std::move(scope), std::move(label), modal);
}
void select_scope(std::string scope, std::string label, bool modal) {
    host_tree().select_scope(std::move(scope), std::move(label), modal);
}
bool add_button(std::string id, std::string label, Bounds bounds, bool enabled, bool selected, bool keyboard_focused) {
    return host_tree().add_button(std::move(id), std::move(label), bounds, enabled, selected, keyboard_focused);
}
void add_text(std::string id, std::string label, Bounds bounds) {
    host_tree().add_text(std::move(id), std::move(label), bounds);
}
std::optional<Action> add_control(Node node) { return host_tree().add_control(std::move(node)); }
void end_frame() { host_tree().end_frame(); }
void clear() { host_tree().clear(); }
void set_game_status(std::string text) {
    std::scoped_lock lock(status_mutex);
    // A one-shot F8 read does not keep decoding enabled. Retain its fresh
    // result for the observer's receipt timeout instead of replacing it with
    // "no observation" on the next idle event-pump tick.
    if (!observation_demanded() && Clock::now() < manual_status_until) return;
    status_text = text.empty() ? "No game status available yet." : std::move(text);
}
std::string game_status() { std::scoped_lock lock(status_mutex); return status_text; }
DisplayPreferences display_preferences() { std::scoped_lock lock(status_mutex); return preferences; }
void set_display_preferences(DisplayPreferences value) { std::scoped_lock lock(status_mutex); preferences = value; }
void request_game_status_read() { read_requested.store(true); }
Snapshot native_snapshot(Snapshot snapshot, const std::optional<Observation>& observation,
                         const std::optional<ManualStatus>& manual, Clock::time_point now,
                         std::uint64_t session, NativeFocusPolicy focus_policy) {
    if (!snapshot.scope.empty()) return snapshot;
    snapshot.scope = "game-status";
    snapshot.label = "DKR-R game status";
    snapshot.nodes.clear();
    // A reset between the mailbox read and session read invalidates even a
    // nonnull sample. Default session zero is reserved for pure fixtures.
    if (observation && session != 0 && observation->session != session) {
        snapshot.nodes.push_back({"game-status", "Game status unavailable."});
        snapshot.nodes.back().focused = focus_policy == NativeFocusPolicy::GameSummary;
        return snapshot;
    }
    snapshot.nodes.push_back({"game-status", observation ?
        (observation->kind == StateKind::Unavailable ? "Game status unavailable." : describe_observation(*observation)) :
        (manual && manual->session != 0 && manual->session == session && now < manual->until ?
            manual->text : "Game status unavailable.")});
    // Native keyboard focus can point at this readonly summary without
    // registering an action or moving focus among changing guest menu rows.
    snapshot.nodes.back().focused = focus_policy == NativeFocusPolicy::GameSummary;
    if (!observation || observation->kind == StateKind::Unavailable || observation->menu_name.empty())
        return snapshot;
    // IDs identify the decoded menu and row, independent of its changing
    // selection. These nonvisual nodes have no guest-to-screen geometry.
    const auto prefix = "game-menu/" + std::to_string(observation->session) + "/" +
        std::to_string(static_cast<unsigned>(observation->kind)) + "/" +
        std::to_string(observation->scene) + "/" + observation->menu_name + "/";
    snapshot.nodes.push_back({prefix + "name", observation->menu_name});
    if (!observation->menu_detail.empty())
        snapshot.nodes.push_back({prefix + "detail", observation->menu_detail});
    for (const auto& entry : observation->menu_entries) {
        if (snapshot.nodes.size() >= kMaxNodes) break;
        const auto id = prefix + "row/" + std::to_string(entry.index);
        if (entry.label.empty() || std::any_of(snapshot.nodes.begin(), snapshot.nodes.end(),
            [&](const Node& node) { return node.id == id; })) continue;
        Node node{id, entry.label + (entry.selected ? ". Selected." : "") +
            (entry.enabled ? "" : (entry.selected ? " Unavailable." : ". Unavailable."))};
        node.enabled = entry.enabled;
        node.selected = entry.selected;
        snapshot.nodes.push_back(std::move(node));
    }
    return snapshot;
}
void pump_native(SDL_Window* window) {
    auto snapshot = host_tree().snapshot();
    const auto now = Clock::now();
    auto observation = read_observation(now);
    // A reset after the mailbox read conservatively invalidates retained F8
    // text; decoded summary and rows still share their one observation.
    const auto session = current_session();
    std::optional<ManualStatus> manual;
    {
        std::scoped_lock lock(status_mutex);
        if (now < manual_status_until)
            manual = ManualStatus{manual_status_text, manual_status_until, manual_status_session};
    }
    // The main thread never opens a HostTree frame: the graphics worker may
    // simultaneously publish an overlay. Summary and rows share this read.
    // Bind the native window before checking its input focus, as before.
    // The first pump attaches the native window with ordinary SDL focus; a
    // following pump can opt into summary focus once that window is bound.
    // Never acquire it for normal players or background/native-dialog input.
    const auto focus_policy = snapshot.scope.empty() && native::screen_reader_active() && native::input_focused(window)
        ? NativeFocusPolicy::GameSummary : NativeFocusPolicy::RegisteredControls;
    native::update(window, native_snapshot(snapshot, observation, manual, now, session, focus_policy));
    const bool focused_game = snapshot.scope.empty() && native::input_focused(window);
    const bool requested = read_requested.exchange(false);
    if (!focused_game) pending_read = false; // never replay a background/dialog request
    else if (requested) {
        pending_read = true;
        read_deadline = now + std::chrono::milliseconds{1500};
        read_session = observation ? observation->session : 0;
        read_sequence = observation ? observation->sequence : 0;
    }
    const auto speech = feedback_preferences();
    set_observation_demand(focused_game && (speech.automatic_speech ||
        native::screen_reader_active() || pending_read));
    // Read after enabling demand: old samples cleared while disabled cannot
    // satisfy an explicit read. A new submission must reach the mailbox.
    observation = read_observation(now);
    bool delivered_manual = false;
    if (pending_read && observation &&
        (observation->session != read_session || observation->sequence != read_sequence)) {
        const auto message = describe_observation(*observation);
        {
            std::scoped_lock lock(status_mutex);
            status_text = message;
            manual_status_text = message;
            manual_status_session = observation->session;
            manual_status_until = now + std::chrono::milliseconds{1500};
        }
        native::announce(message);
        pending_read = false;
        delivered_manual = true;
    } else if (pending_read && now >= read_deadline) {
        set_game_status("No fresh game status available.");
        native::announce(game_status());
        pending_read = false;
        delivered_manual = true;
    }
    static FeedbackScheduler feedback;
    const auto automatic = feedback.update(observation, !focused_game || pending_read,
        speech, now);
    if (automatic && !delivered_manual) native::announce(*automatic);
}
void detach_native() {
    native::detach(); clear(); read_requested.store(false); pending_read = false;
    set_observation_demand(false);
    std::scoped_lock lock(status_mutex);
    manual_status_until = {};
    manual_status_text.clear();
    manual_status_session = 0;
}

#if !defined(__APPLE__) || defined(DKR_IOS) || defined(DKR_ACCESSIBILITY_NO_NATIVE)
namespace native {
void update(SDL_Window*, const Snapshot&) {}
void detach() {}
void announce(const std::string&) {}
bool screen_reader_active() { return false; }
bool input_focused(SDL_Window*) { return false; }
}
#endif
} // namespace dkr::runtime::accessibility
