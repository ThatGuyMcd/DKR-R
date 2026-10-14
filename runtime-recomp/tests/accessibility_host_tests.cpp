#include "runtime_accessibility.hpp"
#include "accessibility_observation.hpp"
#include "accessibility_feedback.hpp"
#include "revision_addresses.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

using namespace dkr::runtime::accessibility;
namespace {
constexpr Bounds bounds{10, 20, 80, 40};
void launcher(HostTree& tree, bool enabled = true, const char* label = "PLAY") {
    tree.begin_frame("launcher/0", "Launcher");
    tree.add_button("play", label, bounds, enabled);
    tree.end_frame();
}
Action action(const HostTree& tree, const char* id = "play", ActionKind kind = ActionKind::Press) {
    const auto snapshot = tree.snapshot();
    return {snapshot.generation, snapshot.scope, id, kind};
}
}
int main() {
    HostTree tree;
    launcher(tree);
    assert(tree.enqueue(action(tree)));
    tree.begin_frame("launcher/0", "Launcher");
    assert(tree.add_button("play", "PLAY", bounds));
    assert(!tree.add_button("play", "PLAY", bounds));
    tree.end_frame();
    tree.begin_frame("launcher/0", "Launcher");
    assert(!tree.add_button("play", "PLAY", bounds));
    tree.end_frame();

    // Disabled or relabelled targets must not run a stale admitted request.
    assert(tree.enqueue(action(tree)));
    tree.begin_frame("launcher/0", "Launcher");
    assert(!tree.add_button("play", "PLAY", bounds, false));
    tree.end_frame();
    assert(!tree.enqueue(action(tree)));
    launcher(tree);
    assert(tree.enqueue(action(tree)));
    tree.begin_frame("launcher/0", "Launcher");
    assert(!tree.add_button("play", "Different action", bounds));
    tree.end_frame();
    launcher(tree);

    // Current page/modal chosen before registration blocks background presses.
    assert(tree.enqueue(action(tree)));
    tree.begin_frame("modal/quit", "Quit?", true);
    assert(!tree.add_button("cancel", "Cancel", bounds));
    tree.add_button("exit", "Exit", bounds);
    tree.end_frame();
    assert(tree.snapshot().modal && tree.snapshot().nodes.size() == 2);
    assert(tree.snapshot().nodes.front().focused);
    assert(!tree.enqueue({tree.snapshot().generation, "launcher/0", "play"}));
    // Real next-frame modal pattern: same modal at begin, modal helper called
    // again, then the queued cancel reaches its current wrapper once.
    assert(tree.enqueue(action(tree, "cancel")));
    tree.begin_frame("modal/quit", "Quit?", true);
    tree.select_scope("modal/quit", "Quit?", true);
    assert(tree.add_button("cancel", "Cancel", bounds));
    tree.add_button("exit", "Exit", bounds);
    tree.end_frame();

    launcher(tree);
    assert(tree.enqueue(action(tree, "play", ActionKind::Focus)));
    launcher(tree);
    assert(tree.snapshot().nodes[0].focused);
    tree.begin_frame("launcher/0", "Launcher");
    tree.add_button("play", "PLAY", bounds);
    tree.select_scope("modal/quit", "Quit?", true);
    tree.add_button("cancel", "Cancel", bounds);
    tree.end_frame();
    assert(tree.snapshot().nodes.size() == 1);
    launcher(tree);
    assert(tree.snapshot().nodes[0].focused); // return focus to the host control

    // Native focus must persist when ImGui's prior keyboard focus is unchanged.
    tree.begin_frame("launcher/0", "Launcher");
    tree.add_button("play", "PLAY", bounds, true, false, true);
    tree.add_button("other", "Other", bounds);
    tree.end_frame();
    assert(tree.enqueue(action(tree, "other", ActionKind::Focus)));
    tree.begin_frame("launcher/0", "Launcher");
    tree.add_button("play", "PLAY", bounds, true, false, true);
    tree.add_button("other", "Other", bounds);
    tree.end_frame();
    tree.begin_frame("launcher/0", "Launcher");
    tree.add_button("play", "PLAY", bounds, true, false, true);
    tree.add_button("other", "Other", bounds);
    tree.end_frame();
    assert(tree.snapshot().nodes[1].focused && !tree.snapshot().nodes[0].focused);

    const auto stale = action(tree);
    tree.clear();
    launcher(tree);
    assert(!tree.enqueue(stale));
    assert(!tree.enqueue(action(tree, "unknown")));
    assert(!tree.enqueue({tree.snapshot().generation, tree.snapshot().scope,
        "play", static_cast<ActionKind>(99)}));
    for (int i = 0; i < 32; ++i) assert(tree.enqueue(action(tree)));
    assert(!tree.enqueue(action(tree))); // bounded native mailbox
    tree.clear();

    launcher(tree);
    const auto generation = tree.snapshot().generation;
    launcher(tree);
    assert(tree.snapshot().generation == generation);
    assert(spoken_label("BROWSE FOR ROM...##rom") == "BROWSE FOR ROM...");
    tree.begin_frame("launcher/0", "Launcher");
    tree.add_button("play", "PLAY", bounds);
    tree.add_text("status", "Ready to race");
    tree.end_frame();
    assert(!tree.enqueue(action(tree, "status", ActionKind::Focus)));
    HostTree deferred;
    launcher(deferred);
    assert(!deferred.has_pending_focus()); // semantic default never moves input
    assert(!deferred.has_accessibility_focus("play"));
    assert(deferred.enqueue(action(deferred, "play", ActionKind::Focus)));
    deferred.begin_frame("launcher/0", "Launcher");
    deferred.add_button("play", "PLAY", bounds);
    assert(deferred.take_focus_request("play"));
    assert(!deferred.take_focus_request("play"));
    deferred.end_frame();
    deferred.clear(); launcher(deferred);
    assert(deferred.enqueue(action(deferred, "play", ActionKind::Focus)));
    deferred.begin_frame("launcher/0", "Launcher");
    deferred.add_button("play", "Different target", bounds);
    assert(!deferred.take_focus_request("play"));
    deferred.end_frame();
    deferred.begin_frame("launcher/1", "Another page");
    deferred.add_button("play", "Different target", bounds);
    assert(!deferred.take_focus_request("play"));
    deferred.end_frame();
    HostTree controls;
    Node slider;
    slider.id = "volume"; slider.label = "Master volume"; slider.role = Role::Slider;
    slider.number = 5; slider.minimum = 0; slider.maximum = 10; slider.help = "Game audio volume";
    Node choice;
    choice.id = "mode"; choice.label = "Mode"; choice.role = Role::Choice;
    choice.maximum = 2; choice.choices = {"Original", "Unavailable", "Modern"};
    choice.choice_enabled = {true, false, true};
    auto draw_controls = [&] {
        controls.begin_frame("settings", "Settings");
        auto result = controls.add_control(slider);
        controls.add_control(choice);
        controls.end_frame();
        return result;
    };
    draw_controls();
    auto set = action(controls, "volume", ActionKind::SetValue);
    set.value = 8;
    assert(controls.enqueue(set));
    auto adjusted = draw_controls();
    assert(adjusted && adjusted->kind == ActionKind::SetValue && adjusted->value == 8);
    assert(!draw_controls()); // native set-value is delivered exactly once
    assert(!controls.enqueue(action(controls, "volume", ActionKind::Press)));
    set = action(controls, "volume", ActionKind::SetValue);
    set.value = 11; assert(!controls.enqueue(set));
    set.value = std::numeric_limits<double>::quiet_NaN(); assert(!controls.enqueue(set));
    set = action(controls, "mode", ActionKind::SetValue);
    set.value = 1; assert(!controls.enqueue(set)); // disabled option
    set.value = 0.5; assert(!controls.enqueue(set)); // fractional option
    set.value = 2; assert(controls.enqueue(set));
    // An enabled choice becoming disabled before drawing rejects the old request.
    choice.choice_enabled[2] = false;
    draw_controls();
    assert(controls.enqueue(action(controls, "volume", ActionKind::Increment)));
    slider.enabled = false;
    assert(!draw_controls());
    assert(!controls.enqueue(action(controls, "volume", ActionKind::Decrement)));
    assert(controls.snapshot().nodes.front().help == "Game audio volume");

    // Native text edits affect a draft only. The existing accept/cancel
    // branches control persistence, and changing target or capacity invalidates
    // a queued request before it reaches the editor.
    HostTree text_tree;
    std::string draft = "Original", committed = draft;
    Node editor;
    editor.id = "name"; editor.label = "Preset name"; editor.role = Role::TextField;
    editor.text_capacity = 24; editor.bounds = bounds;
    const auto draw_text = [&](const char* scope = "modal/text") {
        text_tree.begin_frame(scope, "Enter text", true);
        editor.value = draft;
        const auto edit = text_tree.add_control(editor);
        if (edit) draft = edit->text;
        if (text_tree.add_button("cancel", "Cancel", bounds)) draft = committed;
        if (text_tree.add_button("accept", "Save preset", bounds, !draft.empty())) committed = draft;
        text_tree.end_frame();
        return edit.has_value();
    };
    draw_text();
    auto text_edit = action(text_tree, "name", ActionKind::SetText);
    text_edit.text = "Taj \xF0\x9F\x8F\x81";
    assert(text_tree.enqueue(text_edit));
    assert(draw_text() && draft == text_edit.text && committed == "Original");
    assert(!draw_text()); // publish changed draft, no repeated replacement
    assert(text_tree.enqueue(action(text_tree, "cancel")));
    draw_text(); assert(draft == "Original" && committed == "Original");
    draw_text();
    text_edit = action(text_tree, "name", ActionKind::SetText); text_edit.text = "New preset";
    assert(text_tree.enqueue(text_edit)); draw_text(); draw_text();
    assert(text_tree.enqueue(action(text_tree, "accept")));
    draw_text(); assert(committed == "New preset");
    text_edit = action(text_tree, "name", ActionKind::SetText); text_edit.text = "";
    assert(text_tree.enqueue(text_edit)); draw_text(); draw_text();
    assert(!text_tree.enqueue(action(text_tree, "accept")));
    assert(!text_tree.enqueue(action(text_tree, "name", ActionKind::Press)));
    assert(!text_tree.enqueue(action(text_tree, "name", ActionKind::SetValue)));
    assert(!text_tree.enqueue(action(text_tree, "cancel", ActionKind::SetText)));
    for (const auto& invalid : {std::string(25, 'x'), std::string("a\nb"),
                               std::string("a\0b", 3), std::string("\xC0\xAF"),
                               std::string("\xED\xA0\x80"), std::string("\xF4\x90\x80\x80"),
                               std::string("\xE2\x80\xA8"), std::string("\xF0\x9F")}) {
        text_edit = action(text_tree, "name", ActionKind::SetText); text_edit.text = invalid;
        assert(!text_tree.enqueue(text_edit));
    }
    assert(valid_text_value("", 24));
    assert(!valid_text_value("", 0));
    assert(!valid_text_value("", kMaxTextBytes + 1));
    text_edit = action(text_tree, "name", ActionKind::SetText); text_edit.text = "Changed";
    assert(text_tree.enqueue(text_edit)); editor.text_capacity = 4;
    assert(!draw_text() && draft.empty());
    editor.text_capacity = 24; draw_text();
    text_edit = action(text_tree, "name", ActionKind::SetText); text_edit.text = "Changed";
    assert(text_tree.enqueue(text_edit)); draft = "Keyboard edit";
    assert(!draw_text() && draft == "Keyboard edit");
    text_edit = action(text_tree, "name", ActionKind::SetText); text_edit.text = "Changed";
    assert(text_tree.enqueue(text_edit)); editor.enabled = false;
    assert(!draw_text() && draft == "Keyboard edit");
    editor.enabled = true; draw_text();
    text_edit = action(text_tree, "name", ActionKind::SetText); text_edit.text = "Changed";
    assert(text_tree.enqueue(text_edit));
    assert(!draw_text("modal/other-target") && draft == "Keyboard edit");
    assert(!text_tree.enqueue(text_edit));

    // Percentages are the native value, rather than the painted slider's
    // twenty positions. Apply real queued requests using the UI's shared rule.
    HostTree percent_controls;
    Node percent_slider;
    percent_slider.id = "percent-volume"; percent_slider.label = "Master volume";
    percent_slider.role = Role::Slider;
    percent_slider.number = 100; percent_slider.minimum = 0; percent_slider.maximum = 100;
    percent_slider.step = 5; percent_slider.value = "100 percent";
    const auto draw_percent = [&] {
        percent_controls.begin_frame("percent-settings", "Sound");
        const auto request = percent_controls.add_control(percent_slider);
        if (request) percent_slider.number = range_value_for_action(
            static_cast<int>(percent_slider.number), 0, 100, 5, *request);
        percent_slider.value = std::to_string(static_cast<int>(percent_slider.number)) + " percent";
        percent_controls.end_frame();
    };
    draw_percent();
    assert(percent_controls.snapshot().nodes.front().number == 100);
    assert(percent_controls.snapshot().nodes.front().step == 5);
    auto percent_request = action(percent_controls, "percent-volume", ActionKind::SetValue);
    percent_request.value = 50;
    assert(percent_controls.enqueue(percent_request)); draw_percent();
    assert(percent_slider.number == 50 && percent_slider.value == "50 percent");
    draw_percent(); // publish the changed value before issuing the next request
    assert(percent_controls.enqueue(action(percent_controls, "percent-volume", ActionKind::Increment)));
    draw_percent(); assert(percent_slider.number == 55);
    draw_percent();
    assert(percent_controls.enqueue(action(percent_controls, "percent-volume", ActionKind::Decrement)));
    draw_percent(); assert(percent_slider.number == 50);
    draw_percent();
    percent_request = action(percent_controls, "percent-volume", ActionKind::SetValue);
    percent_request.value = 52.5;
    assert(percent_controls.enqueue(percent_request)); draw_percent();
    assert(percent_slider.number == 55); // nearest five-percent grid point
    // An imported off-grid level remains the rendered/native percentage.
    // Adjustment requests change it by exactly five points, not by snapping.
    percent_slider.number = 73; percent_slider.value = "73 percent";
    draw_percent();
    assert(percent_controls.snapshot().nodes.front().number == 73);
    assert(percent_controls.enqueue(action(percent_controls, "percent-volume", ActionKind::Increment)));
    draw_percent(); assert(percent_slider.number == 78);
    percent_slider.number = 73; percent_slider.value = "73 percent";
    draw_percent();
    assert(percent_controls.enqueue(action(percent_controls, "percent-volume", ActionKind::Decrement)));
    draw_percent(); assert(percent_slider.number == 68);
    percent_slider.number = 98; percent_slider.value = "98 percent";
    draw_percent();
    assert(percent_controls.enqueue(action(percent_controls, "percent-volume", ActionKind::Increment)));
    draw_percent(); assert(percent_slider.number == 100);
    percent_slider.number = 2; percent_slider.value = "2 percent";
    draw_percent();
    assert(percent_controls.enqueue(action(percent_controls, "percent-volume", ActionKind::Decrement)));
    draw_percent(); assert(percent_slider.number == 0);

    Action malformed; malformed.kind = ActionKind::SetValue;
    malformed.value = 1000; assert(range_value_for_action(50, 0, 100, 5, malformed) == 100);
    malformed.value = -1000; assert(range_value_for_action(50, 0, 100, 5, malformed) == 0);
    malformed.value = std::numeric_limits<double>::quiet_NaN();
    assert(range_value_for_action(50, 0, 100, 5, malformed) == 50);
    malformed.value = std::numeric_limits<double>::infinity();
    assert(range_value_for_action(50, 0, 100, 5, malformed) == 50);
    malformed.value = -0.5;
    assert(range_value_for_action(0, -12, 12, 1, malformed) == -1); // existing unit-step rounding
    malformed.kind = ActionKind::Increment;
    assert(range_value_for_action(100, 0, 100, 5, malformed) == 100);
    malformed.kind = ActionKind::Decrement;
    assert(range_value_for_action(0, 0, 100, 5, malformed) == 0);

    // In-race AX traversal exposes the current HUD summary, not a repeated
    // prototype coverage paragraph. Qualified prompts and held items stay useful.
    Observation race_status;
    race_status.kind = StateKind::Race;
    race_status.session = 7;
    race_status.scene = 3;
    race_status.race_start_prompt = RaceStartPrompt::Go;
    race_status.racer = RacerStatus{2, 2, 3, 7, HeldItem{ItemCategory::Boost, 3, 1}};
    const auto race_snapshot = native_snapshot({}, race_status, std::nullopt, Clock::now(), 7,
                                               NativeFocusPolicy::GameSummary);
    assert(race_snapshot.nodes.size() == 1);
    assert(race_snapshot.nodes[0].id == "game-status" && race_snapshot.nodes[0].role == Role::StaticText);
    assert(race_snapshot.nodes[0].focused);
    assert(race_snapshot.nodes[0].label ==
        "Race. GO. Player 1 position 2, lap 2 of 3, bananas 7. Boost, level 3, quantity 1.");
    race_status.race_start_prompt = RaceStartPrompt::None;
    race_status.racer->held_item = HeldItem{};
    const auto changed_race = native_snapshot({}, race_status);
    assert(changed_race.nodes.size() == 1 && changed_race.nodes[0].id == race_snapshot.nodes[0].id);
    assert(changed_race.nodes[0].label ==
        "Race. Player 1 position 2, lap 2 of 3, bananas 7. No held item.");

    // Native game information is a coherent, bounded read-only view of the
    // current observation, independent of a previously retained F8 summary.
    Observation menu;
    menu.kind = StateKind::OptionsMenu;
    menu.session = 7;
    menu.scene = 1;
    menu.menu_name = "Options";
    menu.menu_detail = "Sound and display settings";
    menu.selection_label = "Audio Options";
    menu.menu_entries = {{0, "Audio Options", true, true}, {1, "Unavailable option", false, false}};
    const auto menu_snapshot = native_snapshot({}, menu);
    assert(menu_snapshot.scope == "game-status" && menu_snapshot.nodes.size() == 5);
    assert(menu_snapshot.nodes[0].label == describe_observation(menu));
    assert(menu_snapshot.nodes[1].label == "Options");
    assert(menu_snapshot.nodes[2].label == menu.menu_detail);
    assert(menu_snapshot.nodes[3].label == "Audio Options. Selected.");
    assert(menu_snapshot.nodes[3].selected && menu_snapshot.nodes[3].enabled);
    assert(menu_snapshot.nodes[4].label == "Unavailable option. Unavailable.");
    assert(!menu_snapshot.nodes[4].enabled && !menu_snapshot.nodes[4].selected);
    const auto reset_between_reads = native_snapshot({}, menu, std::nullopt, Clock::now(), menu.session + 1);
    assert(reset_between_reads.nodes.size() == 1);
    assert(reset_between_reads.nodes[0].label == "Game status unavailable.");
    HostTree information;
    information.begin_frame(menu_snapshot.scope, menu_snapshot.label);
    for (const auto& node : menu_snapshot.nodes) {
        assert(node.role == Role::StaticText && !node.focused);
        assert(node.bounds.x == 0 && node.bounds.y == 0 && node.bounds.width == 0 && node.bounds.height == 0);
        assert(node.choices.empty() && node.value.empty());
        assert(!information.add_control(node));
    }
    information.end_frame();
    for (const auto& node : information.snapshot().nodes)
        for (const auto kind : {ActionKind::Press, ActionKind::Focus, ActionKind::SetValue,
                               ActionKind::Increment, ActionKind::Decrement})
            assert(!information.enqueue({information.snapshot().generation, menu_snapshot.scope, node.id, kind}));
    // Moving selection updates both the summary and rows while retaining IDs.
    menu.selection_label = "Unavailable option";
    menu.menu_entries[0].selected = false;
    menu.menu_entries[1].selected = true;
    const auto moved = native_snapshot({}, menu);
    assert(moved.nodes[0].label == describe_observation(menu));
    assert(moved.nodes[3].id == menu_snapshot.nodes[3].id && !moved.nodes[3].selected);
    assert(moved.nodes[4].label == "Unavailable option. Selected. Unavailable.");
    menu.menu_name = "Audio Options";
    menu.menu_detail.clear();
    menu.menu_entries = {{0, "Stereo", true, true}};
    const auto changed_menu = native_snapshot({}, menu);
    assert(changed_menu.nodes.size() == 3 && changed_menu.nodes.back().label == "Stereo. Selected.");
    assert(changed_menu.nodes.back().id != moved.nodes[3].id);
    ++menu.session;
    assert(native_snapshot({}, menu).nodes.back().id != changed_menu.nodes.back().id);
    // Missing samples may retain an unexpired one-shot summary from the same
    // session, but never its rows. Unsupported samples replace that summary.
    const auto manual_time = Clock::now();
    const ManualStatus manual{"F8 retained summary", manual_time + std::chrono::milliseconds{1500}, 7};
    set_game_status("Stale ordinary menu summary");
    const auto missing = native_snapshot({}, std::nullopt);
    assert(missing.nodes.size() == 1 && missing.nodes[0].label == "Game status unavailable.");
    const auto retained = native_snapshot({}, std::nullopt, manual, manual_time, 7);
    assert(retained.nodes.size() == 1 && retained.nodes[0].label == "F8 retained summary");
    const auto expired_manual = native_snapshot({}, std::nullopt, manual, manual.until, 7);
    assert(expired_manual.nodes.size() == 1 && expired_manual.nodes[0].label == "Game status unavailable.");
    menu.kind = StateKind::Unavailable;
    const auto unsupported = native_snapshot({}, menu, manual, manual_time, 7);
    assert(unsupported.nodes.size() == 1 && unsupported.nodes[0].label == "Game status unavailable.");
    menu.kind = StateKind::OptionsMenu;
    // Summary focus is opt-in and never turns readonly information into an
    // actionable control. Reset/expired status must focus its new safe value.
    for (const auto focused : {
        native_snapshot({}, menu, std::nullopt, manual_time, menu.session, NativeFocusPolicy::GameSummary),
        native_snapshot({}, menu, std::nullopt, manual_time, menu.session + 1, NativeFocusPolicy::GameSummary),
        native_snapshot({}, std::nullopt, manual, manual_time, 7, NativeFocusPolicy::GameSummary),
        native_snapshot({}, std::nullopt, manual, manual.until, 7, NativeFocusPolicy::GameSummary)}) {
        assert(focused.nodes[0].id == "game-status" && focused.nodes[0].focused);
        assert(focused.nodes[0].role == Role::StaticText);
        assert(std::count_if(focused.nodes.begin(), focused.nodes.end(), [](const Node& node) { return node.focused; }) == 1);
        HostTree readonly; readonly.begin_frame(focused.scope, focused.label);
        for (const auto& node : focused.nodes) readonly.add_control(node);
        readonly.end_frame();
        for (const auto action : {ActionKind::Press, ActionKind::Focus, ActionKind::SetValue, ActionKind::Increment, ActionKind::Decrement})
            assert(!readonly.enqueue({readonly.snapshot().generation, focused.scope, "game-status", action}));
    }
    const auto reset_focus = native_snapshot({}, menu, manual, manual_time, menu.session + 1, NativeFocusPolicy::GameSummary);
    assert(reset_focus.nodes[0].label == "Game status unavailable.");
    for (const bool modal : {false, true}) {
        Snapshot overlay{12, "overlay/settings", "Settings", modal, {{"setting", "Host setting"}}};
        const auto visible = native_snapshot(overlay, menu);
        assert(visible.scope == overlay.scope && visible.modal == modal && visible.generation == 12);
        assert(visible.nodes.size() == 1 && visible.nodes[0].id == "setting");
        const auto focused_overlay = native_snapshot(overlay, menu, std::nullopt, manual_time, 7, NativeFocusPolicy::GameSummary);
        assert(focused_overlay.scope == overlay.scope && focused_overlay.generation == overlay.generation);
        assert(focused_overlay.nodes.size() == 1 && !focused_overlay.nodes[0].focused);
    }
    menu.menu_entries.clear();
    for (int i = 0; i < 256; ++i) menu.menu_entries.push_back({i, "Row " + std::to_string(i), true, false});
    assert(native_snapshot({}, menu).nodes.size() == 128);
    menu.menu_entries = {{0, "First", true, false}, {0, "Duplicate", true, false}, {1, "", true, false}};
    assert(native_snapshot({}, menu).nodes.size() == 3);

    // Receipt expiry and session boundaries actually remove mailbox rows.
    set_observation_demand(true);
    const auto session = begin_session();
    const auto receipt = Clock::now();
    std::vector<std::uint8_t> memory(0x800000);
    const auto write = [&](std::uint32_t address, std::uint32_t value, unsigned size = 4) {
        for (unsigned i = 0; i < size; ++i)
            memory[((address & 0x7FFFFFU) + i) ^ 3U] = static_cast<std::uint8_t>(value >> ((size - i - 1U) * 8U));
    };
    const auto& addresses = dkr::runtime::revision_addresses::kUsV77;
    write(addresses.GameMode, 1);
    write(addresses.CurrentMenuId, 12);
    write(0x800DF460U, 2);
    for (unsigned i = 0; i < 6; ++i) {
        write(0x800DFA10U + i * 4U, 0x80001000U + i * 128U);
        const std::string label = "Option " + std::to_string(i);
        for (unsigned j = 0; j < label.size(); ++j) write(0x80001000U + i * 128U + j, label[j], 1);
    }
    observe_submission(memory, dkr::runtime::rom::Revision::UsV77, 0, session, receipt);
    const auto fresh = read_observation(receipt);
    assert(fresh && fresh->menu_entries.size() == 6);
    assert(native_snapshot({}, fresh).nodes.size() > 1);
    assert(native_snapshot({}, read_observation(receipt + std::chrono::milliseconds{1500})).nodes.size() > 1);
    const auto expired = native_snapshot({}, read_observation(receipt + std::chrono::milliseconds{1501}));
    assert(expired.nodes.size() == 1 && expired.nodes[0].label == "Game status unavailable.");
    observe_submission({}, dkr::runtime::rom::Revision::Unsupported, 0, session, receipt);
    assert(native_snapshot({}, read_observation(receipt)).nodes.size() == 1);
    reset_session();
    assert(!read_observation(receipt));
    const auto reset = native_snapshot({}, read_observation(receipt));
    assert(reset.nodes.size() == 1 && reset.nodes[0].label == "Game status unavailable.");
    const ManualStatus session_manual{"Session F8", receipt + std::chrono::milliseconds{1500}, session};
    const auto manual_after_reset = native_snapshot({}, read_observation(receipt), session_manual, receipt, current_session());
    assert(manual_after_reset.nodes.size() == 1 && manual_after_reset.nodes[0].label == "Game status unavailable.");
    const auto new_session = begin_session();
    const ManualStatus active_manual{"Same session F8", receipt + std::chrono::milliseconds{1500}, new_session};
    set_observation_demand(false);
    assert(native_snapshot({}, read_observation(receipt), active_manual, receipt, current_session()).nodes[0].label == "Same session F8");
    begin_session();
    assert(native_snapshot({}, read_observation(receipt), active_manual, receipt, current_session()).nodes[0].label == "Game status unavailable.");
    const auto ending_session = current_session();
    const ManualStatus ending_manual{"Ending session F8", receipt + std::chrono::milliseconds{1500}, ending_session};
    end_session(ending_session);
    assert(native_snapshot({}, read_observation(receipt), ending_manual, receipt, current_session()).nodes[0].label == "Game status unavailable.");

    set_game_status("Race paused. Position 2 of 8.");
    assert(game_status() == "Race paused. Position 2 of 8.");
    // A background/missing native window must not start observation or hold a
    // manual read request for replay when the user returns later.
    set_feedback_preferences({true, true, std::chrono::milliseconds{1500}});
    set_observation_demand(true);
    request_game_status_read();
    clear();
    pump_native(nullptr);
    assert(!observation_demanded());
    set_feedback_preferences({});
    std::cout << "PASS: host accessibility scopes, focus, stale actions and mailbox\n";
}
