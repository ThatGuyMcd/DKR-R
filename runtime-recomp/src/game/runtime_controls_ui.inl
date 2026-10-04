// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_graphics_ui.inl. Presentation only: bindings, devices, stick
// response, motion and shortcuts go through the same input and platform calls
// as before.
//
// CONTROLS in the look of tools/launcher-html (pages/controls.js,
// styles/controls.css): the local player, then Device / Button mapping /
// Stick response / Motion / Shortcuts. Rarely used settings sit behind
// disclosures, and restoring bindings or stick response can be undone.

constexpr unsigned kCtlLine = 0x365361;     // --controls-line
constexpr unsigned kCtlSurface = 0x102C3C;  // --controls-surface

struct ControlsSnapshot {
    std::array<std::array<std::array<int, 3>, static_cast<std::size_t>(dkr::runtime::input::Action::Count)>,
               dkr::runtime::input::kPlayerCount> bindings{};
    float deadzone = 0.0F;
    float anti_deadzone = 0.0F;
    float sensitivity = 0.0F;
    float curve = 0.0F;
    float threshold = 0.0F;
    bool stick_x_inverted = false;
    bool stick_y_inverted = false;
    std::array<std::array<bool, 2>, 3> vehicles{};
};

struct ControlsPageState {
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    bool shared_open = false;
    bool advanced_open = false;
    bool mapping_open = false;
    bool response_open = false;
    // The last restore, so it can be undone until the next controls change
    // or until the page is left.
    std::optional<ControlsSnapshot> undo;
    std::string undo_message;
};
ControlsPageState g_controls_page;

ControlsSnapshot TakeControlsSnapshot() {
    namespace input = dkr::runtime::input;
    ControlsSnapshot snapshot;
    for (std::size_t player = 0; player < input::kPlayerCount; ++player) {
        for (std::size_t index = 0; index < snapshot.bindings[player].size(); ++index) {
            const auto action = static_cast<input::Action>(index);
            snapshot.bindings[player][index] = {input::keyboard_binding(player, action),
                                                input::controller_binding(player, action),
                                                input::secondary_controller_binding(player, action)};
        }
    }
    snapshot.deadzone = input::stick_deadzone();
    snapshot.anti_deadzone = input::stick_anti_deadzone();
    snapshot.sensitivity = input::stick_sensitivity();
    snapshot.curve = input::stick_curve();
    snapshot.threshold = input::trigger_threshold();
    snapshot.stick_x_inverted = input::stick_x_inverted();
    snapshot.stick_y_inverted = input::stick_y_inverted();
    for (std::size_t vehicle = 0; vehicle < snapshot.vehicles.size(); ++vehicle) {
        const auto type = static_cast<input::VehicleClass>(vehicle);
        snapshot.vehicles[vehicle] = {input::vehicle_stick_x_inverted(type),
                                      input::vehicle_stick_y_inverted(type)};
    }
    return snapshot;
}

void RestoreControlsSnapshot(const ControlsSnapshot& snapshot) {
    namespace input = dkr::runtime::input;
    for (std::size_t player = 0; player < input::kPlayerCount; ++player) {
        for (std::size_t index = 0; index < snapshot.bindings[player].size(); ++index) {
            const auto action = static_cast<input::Action>(index);
            input::set_keyboard_binding(player, action, snapshot.bindings[player][index][0]);
            input::set_controller_binding(player, action, snapshot.bindings[player][index][1]);
            input::set_secondary_controller_binding(player, action, snapshot.bindings[player][index][2]);
        }
    }
    input::set_stick_deadzone(snapshot.deadzone);
    input::set_stick_anti_deadzone(snapshot.anti_deadzone);
    input::set_stick_sensitivity(snapshot.sensitivity);
    input::set_stick_curve(snapshot.curve);
    input::set_trigger_threshold(snapshot.threshold);
    input::set_stick_x_inverted(snapshot.stick_x_inverted);
    input::set_stick_y_inverted(snapshot.stick_y_inverted);
    for (std::size_t vehicle = 0; vehicle < snapshot.vehicles.size(); ++vehicle) {
        const auto type = static_cast<input::VehicleClass>(vehicle);
        input::set_vehicle_stick_x_inverted(type, snapshot.vehicles[vehicle][0]);
        input::set_vehicle_stick_y_inverted(type, snapshot.vehicles[vehicle][1]);
    }
}

// SDL's binding names, in the words a player knows (controls.js friendly()).
std::string FriendlyBinding(const std::string& value) {
    static const std::map<std::string, std::string> kNames{
        {"Unbound", "Not assigned"}, {"a", "A"}, {"b", "B"}, {"x", "X"}, {"y", "Y"},
        {"start", "Menu / Start"}, {"back", "View / Select"}, {"guide", "Guide"},
        {"leftshoulder", "Left shoulder"}, {"rightshoulder", "Right shoulder"},
        {"leftstick", "Left stick click"}, {"rightstick", "Right stick click"},
        {"dpup", "D-pad \xE2\x86\x91"}, {"dpdown", "D-pad \xE2\x86\x93"},
        {"dpleft", "D-pad \xE2\x86\x90"}, {"dpright", "D-pad \xE2\x86\x92"},
        {"lefttrigger +", "Left trigger"}, {"righttrigger +", "Right trigger"},
        {"leftx -", "Left stick \xE2\x86\x90"}, {"leftx +", "Left stick \xE2\x86\x92"},
        {"lefty -", "Left stick \xE2\x86\x91"}, {"lefty +", "Left stick \xE2\x86\x93"},
        {"rightx -", "Right stick \xE2\x86\x90"}, {"rightx +", "Right stick \xE2\x86\x92"},
        {"righty -", "Right stick \xE2\x86\x91"}, {"righty +", "Right stick \xE2\x86\x93"},
    };
    if (const auto found = kNames.find(value); found != kNames.end()) return found->second;
    // A two-input shortcut: name each half.
    if (const std::size_t plus = value.find(" + "); plus != std::string::npos) {
        return FriendlyBinding(value.substr(0, plus)) + " + " + FriendlyBinding(value.substr(plus + 3U));
    }
    return value;
}

// .controls-scope: who a section applies to.
void ControlsScope(bool shared, float width) {
    const std::string text = shared ? std::string("Applies to all players")
                                    : "Player " + std::to_string(g_selected_player + 1U) + " settings";
    PaddockText(PaddockReading(13.0F, true, 1.45F), PaddockRgb(kSetWarm), text, width);
    PaddockGap(12.0F);
}

// .controls-separator: a 600 18 px heading, 16 px above its content.
void ControlsHeading(std::string_view text, float width, float top = 0.0F) {
    PaddockGap(top);
    PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText), text, width);
    PaddockGap(16.0F);
}

void ControlsNote(std::string_view text, float width, unsigned colour = kSetMuted) {
    PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(colour), text, width);
    PaddockGap(12.0F);
}

// .controls-page .disclosure: a hairline, a quiet label and a + / - marker.
bool ControlsDisclosure(const char* id, std::string_view label, bool& open, float width) {
    PaddockGap(24.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kCtlLine));
    ImGui::SetCursorScreenPos({at.x, at.y + 1.0F});
    const PaddockPress press = PaddockBeginPress(id, {width, 48.0F});
    if (press.pressed) open = !open;
    const PaddockType type = PaddockReading(14.0F, true, 1.35F);
    const ImU32 ink = PaddockApply(PaddockMix(PaddockRgb(kSetText), PaddockRgb(0xFFFFFF), press.hover));
    const float top = std::round(press.min.y + (48.0F - type.line) * 0.5F);
    PaddockDrawRun(draw, type, {press.min.x, top}, ink, label.data(), label.data() + label.size());
    const char* marker = open ? "\xE2\x80\x93" : "+";
    const float marker_width = PaddockMeasure(type, marker);
    PaddockDrawRun(draw, type, {press.max.x - marker_width, top}, ink, marker, marker + std::strlen(marker));
    PaddockEndPress(press, 6.0F, 1.0F, false);
    if (press.focused) {
        draw->AddRect({press.min.x - 4.5F, press.min.y - 1.5F}, {press.max.x + 4.5F, press.max.y + 1.5F},
                      PaddockCol(0xFFD078), 8.0F, 0, 3.0F);
    }
    if (open) PaddockGap(8.0F);
    return open;
}

// Two equal cells side by side (.controls-row), 12 px apart.
template <typename Left, typename Right>
void ControlsRow(float width, Left&& left, Right&& right) {
    const float half = std::floor((width - 12.0F) * 0.5F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    left(half);
    ImGui::EndGroup();
    const float left_bottom = ImGui::GetItemRectMax().y;
    ImGui::SetCursorScreenPos({at.x + half + 12.0F, at.y});
    ImGui::BeginGroup();
    right(width - half - 12.0F);
    ImGui::EndGroup();
    const float bottom = std::max(left_bottom, ImGui::GetItemRectMax().y);
    ImGui::SetCursorScreenPos({at.x, bottom});
    ImGui::Dummy({width, 0.0F});
}

// A stick axis in the input test (.controls-axis): label, meter, value.
void ControlsAxis(std::string_view label, float value, bool live, float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockType type = PaddockReading(14.0F, false, 1.45F);
    const float mid = at.y + 16.0F;
    PaddockDrawRun(draw, type, {at.x, std::round(mid - type.line * 0.5F)}, PaddockCol(kSetText),
                   label.data(), label.data() + label.size());
    const float meter_x = at.x + 100.0F + 12.0F;
    const float meter_end = at.x + width - 52.0F - 12.0F;
    if (live) {
        const ImVec2 a{meter_x, mid - 8.0F};
        const ImVec2 b{meter_end, mid + 8.0F};
        draw->AddRectFilled(a, b, PaddockCol(0x081B28));
        const float t = std::clamp((value + 1.0F) * 0.5F, 0.0F, 1.0F);
        draw->AddRectFilled({a.x + 1.0F, a.y + 1.0F}, {a.x + 1.0F + (b.x - a.x - 2.0F) * t, b.y - 1.0F},
                            PaddockCol(0x77BFDB));
        draw->AddRect(a, b, PaddockCol(0x476373));
    }
    char text[16]{};
    if (live) std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(value));
    const std::string_view shown = live ? std::string_view(text) : std::string_view("\xE2\x80\x94");
    const float shown_width = PaddockMeasure(type, shown);
    PaddockDrawRun(draw, type, {at.x + width - shown_width, std::round(mid - type.line * 0.5F)},
                   PaddockCol(kSetText), shown.data(), shown.data() + shown.size());
    ImGui::Dummy({width, 32.0F});
}

void BeginControllerCapture(int action, CaptureDevice device, bool secondary = false) {
    g_capture_action = action;
    g_capture_device = device;
    g_capture_secondary_controller = secondary;
    g_capture_popup_pending = true;
    g_capture_finished = false;
}

// ------------------------------------------------------------ device

void DrawControlsDevice(ControlsPageState& state, float width, const std::function<void()>& commit) {
    namespace platform = dkr::runtime::platform;
    const auto status = platform::player_controller_status(g_selected_player);
    const auto devices = platform::connected_controllers();
    const int current_instance = platform::controller_instance_for_player(g_selected_player);
    const bool sdl3_native = platform::active_input_backend() == platform::InputBackend::SDL3Native;

    ControlsScope(false, width);
    ControlsHeading("Device", width);
    // The controller drop-down: Not assigned, then every connected device.
    std::vector<SettingsChoice> choices{{"Not assigned", {}, false}};
    std::vector<int> instances{-1};
    int selected = 0;
    for (std::size_t index = 0; index < devices.size(); ++index) {
        const auto& device = devices[index];
        std::string label = device.name;
        const auto same_name = [&](const auto& other) { return other.name == device.name; };
        if (std::count_if(devices.begin(), devices.end(), same_name) > 1) {
            label += " #" + std::to_string(std::count_if(devices.begin(), devices.begin() + index, same_name) + 1);
        }
        std::string detail;
        if (device.assigned_player >= 0 && device.assigned_player != static_cast<int>(g_selected_player)) {
            detail = "Player " + std::to_string(device.assigned_player + 1);
        }
        if (!device.mapped) detail += std::string(detail.empty() ? "" : " \xC2\xB7 ") + "Setup required";
        choices.push_back({label, detail, false});
        instances.push_back(device.instance);
        if (device.instance == current_instance) selected = static_cast<int>(choices.size()) - 1;
    }
    if (status.assigned && !status.connected) {
        choices.push_back({"Assigned controller \xC2\xB7 disconnected", {}, true});
        instances.push_back(current_instance);
        selected = static_cast<int>(choices.size()) - 1;
    }
    if (SettingsDropdown("controller", "Controller", &selected, choices, width)) {
        const int instance = instances[static_cast<std::size_t>(selected)];
        if (instance < 0) {
            platform::clear_controller_assignment(g_selected_player);
            commit();
        } else {
            const auto device = std::find_if(devices.begin(), devices.end(),
                                             [&](const auto& item) { return item.instance == instance; });
            if (device != devices.end() && device->mapped) {
                platform::assign_controller(g_selected_player, instance);
                commit();
            } else if (platform::begin_controller_mapping(instance, g_selected_player)) {
                g_controller_mapping_popup_pending = true;
                g_controller_mapping_completion_saved = false;
            }
        }
    }
    PaddockGap(16.0F);
    // The connection line: a dot, then what the player has.
    {
        std::string line = status.connected ? "Connected"
                         : status.assigned ? "Disconnected" : "No controller assigned";
        if (status.connected && status.rumble) line += " \xC2\xB7 Rumble";
        if (status.connected && status.gyro) line += " \xC2\xB7 Motion";
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const PaddockType type = PaddockReading(16.0F, true, 1.45F);
        ImGui::GetWindowDrawList()->AddCircleFilled({at.x + 4.0F, at.y + type.line * 0.5F}, 4.0F,
                                                    PaddockCol(status.connected ? 0x69DDBCU : 0xB9CDD7U), 12);
        ImGui::SetCursorScreenPos({at.x + 16.0F, at.y});
        PaddockText(type, PaddockRgb(kSetText), line, width - 16.0F);
        ImGui::SetCursorScreenPos({at.x, ImGui::GetItemRectMax().y});
        PaddockGap(12.0F);
    }
    if (!status.connected) {
        ControlsNote(status.assigned ? "Reconnect the assigned controller, or choose another device."
                                     : "Connect a controller, then press one of its buttons. Keyboard mapping is also available.",
                     width);
    } else if (!status.mapping_source.empty()) {
        ControlsNote("Input map: " + status.mapping_source, width);
    }
    ControlsRow(width, [&](float cell) {
        if (SettingsButton("Press a button to assign", cell)) {
            BeginControllerCapture(kAssignControllerCaptureAction, CaptureDevice::Controller);
        }
    }, [&](float cell) {
        if (SettingsButton("Identify with vibration", cell, !status.connected || !status.rumble,
                           SettingsButtonTone::Plain, false,
                           status.connected ? "This controller does not report vibration support."
                                            : "Connect and assign a controller first.")) {
            platform::identify_controller(g_selected_player);
        }
    });
    PaddockGap(20.0F);

    // The input test (.controls-preview).
    {
        PaddockBox box(width, {17.0F, 17.0F});
        const float inner = box.Inner();
        PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText), "Input test", inner);
        PaddockGap(6.0F);
        ControlsNote(status.connected ? "Move the left stick or press a button to check this controller."
                                      : "Waiting for an assigned controller. Live values will appear here.",
                     inner);
        const auto preview = platform::player_input_preview(g_selected_player);
        ControlsAxis("Horizontal", preview.stick_x, status.connected, inner);
        ControlsAxis("Vertical", preview.stick_y, status.connected, inner);
        PaddockGap(10.0F);
        PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSetText),
                    std::string("Buttons: ") + (!status.connected ? "Unavailable"
                                                : preview.buttons != 0U ? "Pressed" : "None pressed"),
                    inner);
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(8.0F), PaddockRgb(kCtlSurface), PaddockRgb(kCtlLine), 1.0F});
        });
    }
    PaddockGap(20.0F);
    bool background = dkr::runtime::input::background_input_enabled(g_selected_player);
    if (SettingsCheck("##background", "Allow controller input when the game is unfocused", &background, width)) {
        dkr::runtime::input::set_background_input_enabled(g_selected_player, background);
        commit();
    }
    PaddockGap(8.0F);
    ControlsNote("For this player only. Keyboard input requires the game to be focused.", width);

    if (ControlsDisclosure("##shared", "Shared settings", state.shared_open, width)) {
        ControlsScope(true, width);
        int keyboard_player = dkr::runtime::input::keyboard_player();
        if (SettingsDropdown("keyboard-player", "Keyboard player", &keyboard_player,
                             {"Player 1", "Player 2", "Player 3", "Player 4"}, width)) {
            dkr::runtime::input::set_keyboard_player(keyboard_player);
            commit();
        }
        PaddockGap(16.0F);
        int assignment = static_cast<int>(platform::controller_assignment_mode());
        if (SettingsDropdown("assignment", "Controller assignment", &assignment,
                             {"Automatic \xC2\xB7 first available player", "Manual"}, width)) {
            platform::set_controller_assignment_mode(assignment == 1
                ? dkr::runtime::controllers::AssignmentMode::Manual
                : dkr::runtime::controllers::AssignmentMode::Automatic);
            commit();
        }
        PaddockGap(16.0F);
        ControlsHeading("Controller Paks", width);
        bool memory_pak = dkr::runtime::pak::enabled();
        if (SettingsCheck("##memory-pak", "Enable Memory Pak", &memory_pak, width)) {
            dkr::runtime::pak::set_enabled(memory_pak);
            commit();
        }
        PaddockGap(8.0F);
        bool rumble_pak = platform::rumble_enabled();
        if (SettingsCheck("##rumble-pak", "Enable Rumble Pak", &rumble_pak, width)) {
            platform::set_rumble_enabled(rumble_pak);
            commit();
        }
        PaddockGap(8.0F);
        if (rumble_pak && dkr::runtime::enhancements::modern_options_visible(
                              dkr::runtime::enhancements::presentation_profile())) {
            int strength = static_cast<int>(std::lround(platform::rumble_strength() * 100.0F));
            if (SettingsRange("##rumble-strength", "Rumble strength", &strength, 0, 100, width,
                              [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; })) {
                platform::set_rumble_strength(static_cast<float>(strength) / 100.0F);
                commit();
            }
            PaddockGap(16.0F);
        }
        ControlsNote("Memory Pak stores game data. Rumble Pak enables vibration on supported controllers. "
                     "Both can stay enabled: the virtual Memory Pak takes priority whenever the game asks "
                     "for storage.",
                     width);
    }

    if (ControlsDisclosure("##advanced", "Advanced compatibility", state.advanced_open, width)) {
        ControlsScope(true, width);
        int backend = static_cast<int>(platform::requested_input_backend());
        if (SettingsDropdown("backend", "Preferred input backend", &backend,
                             {"Automatic (SDL3 on Steam Deck)", "SDL2 compatibility", "SDL3 native"}, width)) {
            platform::set_requested_input_backend(
                backend == static_cast<int>(platform::InputBackend::SDL2Compatibility)
                    ? platform::InputBackend::SDL2Compatibility
                : backend == static_cast<int>(platform::InputBackend::SDL3Native)
                    ? platform::InputBackend::SDL3Native
                    : platform::InputBackend::Automatic);
            commit();
        }
        PaddockGap(8.0F);
        ControlsNote(std::string("Active: ") + platform::input_backend_name(platform::active_input_backend()) +
                         ". " + platform::input_backend_detail(),
                     width, kSetText);
        ControlsNote(platform::input_backend_switch_pending()
                         ? "Switching controller backend safely..."
                         : "Backend changes apply live; the launcher, game window, audio and renderer remain running.",
                     width, platform::input_backend_switch_pending() ? kSetWarm : kSetMuted);
        ControlsNote(sdl3_native ? "SDL3 uses its native controller database. Switch to SDL2 compatibility and "
                                   "restart to create or import raw mappings."
                                 : "Controller map tools create and share raw SDL2 mappings.",
                     width);
        ControlsRow(width, [&](float cell) {
            if (SettingsButton("Remap this controller", cell, sdl3_native || !status.connected,
                               SettingsButtonTone::Plain, false,
                               sdl3_native ? "Available with SDL2 compatibility." : "Connect and assign a controller first.") &&
                platform::begin_controller_mapping(current_instance, g_selected_player)) {
                g_controller_mapping_popup_pending = true;
                g_controller_mapping_completion_saved = false;
            }
        }, [&](float cell) {
            if (SettingsButton("Import controller maps", cell, sdl3_native, SettingsButtonTone::Plain, false,
                               "Available with SDL2 compatibility.")) {
                ImportControllerMappingsWithDialog();
            }
        });
        PaddockGap(12.0F);
        if (SettingsButton("Export custom maps")) ExportControllerMappingsWithDialog();
        if (!g_controller_mapping_status.empty()) {
            PaddockGap(12.0F);
            ControlsNote(g_controller_mapping_status, width, kSetText);
        }
    }
}

// ------------------------------------------------------------ bindings

void DrawControlsBindings(ControlsPageState& state, float width, const std::function<void()>& commit) {
    namespace input = dkr::runtime::input;
    const bool keyboard = input::keyboard_player() == static_cast<int>(g_selected_player);
    ControlsScope(false, width);
    ControlsHeading("Button mapping", width);
    ControlsNote("Select a binding, then press a key or controller button. Escape cancels.", width);
    ControlsNote(keyboard ? std::string("Controller names use the standard gamepad layout.")
                          : "Keyboard input belongs to Player " + std::to_string(input::keyboard_player() + 1) +
                                ". Change it in Device \xE2\x86\x92 Shared settings.",
                 width);

    // The table: N64 control | (Keyboard) | Controller | Alternate.
    const int columns = keyboard ? 4 : 3;
    const bool cards = width <= 580.0F;
    const float column = std::floor(width / static_cast<float>(columns));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockType head = PaddockReading(13.0F, true, 1.45F);
    const PaddockType group_type = PaddockReading(14.0F, true, 1.45F);
    const PaddockType label_type = PaddockReading(15.0F, true, 1.45F);
    const std::array<const char*, 4> kHeads{{"N64 control", "Keyboard", "Controller", "Alternate"}};
    if (!cards) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        int slot = 0;
        for (int index = 0; index < 4; ++index) {
            if (index == 1 && !keyboard) continue;
            PaddockDrawRun(draw, head, {at.x + column * static_cast<float>(slot) + 8.0F, at.y + 10.0F},
                           PaddockCol(kSetMuted), kHeads[index], kHeads[index] + std::strlen(kHeads[index]));
            ++slot;
        }
        const float bottom = at.y + 10.0F + head.line + 10.0F;
        draw->AddRectFilled({at.x, bottom}, {at.x + width, bottom + 1.0F}, PaddockCol(kCtlLine));
        ImGui::Dummy({width, bottom + 1.0F - at.y});
    }
    const std::array<std::pair<const char*, std::array<int, 6>>, 4> kGroups{{
        {"Analogue stick", {0, 1, 2, 3, -1, -1}},
        {"Main buttons", {4, 5, 6, 7, 12, 13}},
        {"D-pad", {8, 9, 10, 11, -1, -1}},
        {"C-buttons", {14, 15, 16, 17, -1, -1}},
    }};
    for (const auto& [group, actions] : kGroups) {
        PaddockGap(20.0F);
        ImGui::Indent(8.0F);
        PaddockText(group_type, PaddockRgb(kSetText), group, width - 16.0F);
        ImGui::Unindent(8.0F);
        PaddockGap(8.0F);
        int row_index = 0;
        for (const int index : actions) {
            if (index < 0) continue;
            const auto action = static_cast<input::Action>(index);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::PushID(index);
            // A row: its label, then one button per column.
            const auto binding_button = [&](int device_column, float cell_width) {
                const bool is_keyboard = device_column == 0;
                const bool secondary = device_column == 2;
                const int source = is_keyboard ? input::keyboard_binding(g_selected_player, action)
                                 : secondary ? input::secondary_controller_binding(g_selected_player, action)
                                             : input::controller_binding(g_selected_player, action);
                const bool unbound = source == input::kUnbound;
                const std::string name = FriendlyBinding(
                    is_keyboard ? input::keyboard_binding_name(source) : input::controller_binding_name(source));
                ImGui::PushID(device_column);
                const std::string label = name + "##binding";
                if (SettingsButton(label.c_str(), cell_width, false,
                                   unbound ? SettingsButtonTone::Unbound
                                   : secondary ? SettingsButtonTone::Outline : SettingsButtonTone::Plain,
                                   true, nullptr, 8.0F)) {
                    BeginControllerCapture(index, is_keyboard ? CaptureDevice::Keyboard : CaptureDevice::Controller,
                                           secondary);
                }
                ImGui::PopID();
            };
            if (!cards) {
                const float row_height = 44.0F + 8.0F;
                if (row_index % 2 == 0) {
                    draw->AddRectFilled(at, {at.x + width, at.y + row_height}, PaddockCol(0xFFFFFF, 3U));
                }
                draw->AddRectFilled({at.x, at.y + row_height - 1.0F}, {at.x + width, at.y + row_height},
                                    PaddockCol(0xFFFFFF, 12U));
                const char* label = input::action_label(action);
                PaddockDrawRun(draw, label_type, {at.x + 8.0F, std::round(at.y + (row_height - label_type.line) * 0.5F)},
                               PaddockCol(kSetText), label, label + std::strlen(label));
                int slot = 1;
                for (int device_column = 0; device_column < 3; ++device_column) {
                    if (device_column == 0 && !keyboard) continue;
                    ImGui::SetCursorScreenPos({at.x + column * static_cast<float>(slot) + 4.0F, at.y + 4.0F});
                    binding_button(device_column, column - 8.0F);
                    ++slot;
                }
                ImGui::SetCursorScreenPos({at.x, at.y + row_height});
                ImGui::Dummy({width, 0.0F});
            } else {
                // Narrow: the label on its own line, then labelled cells.
                PaddockGap(8.0F);
                ImGui::Indent(4.0F);
                PaddockText(PaddockReading(16.0F, true, 1.45F), PaddockRgb(kSetText), input::action_label(action),
                            width - 8.0F);
                ImGui::Unindent(4.0F);
                PaddockGap(6.0F);
                const float cell = std::floor(width / static_cast<float>(columns - 1));
                const ImVec2 row_at = ImGui::GetCursorScreenPos();
                int slot = 0;
                float bottom = row_at.y;
                for (int device_column = 0; device_column < 3; ++device_column) {
                    if (device_column == 0 && !keyboard) continue;
                    ImGui::SetCursorScreenPos({row_at.x + cell * static_cast<float>(slot) + 4.0F, row_at.y});
                    ImGui::BeginGroup();
                    PaddockText(PaddockReading(12.0F, false, 1.45F), PaddockRgb(kSetMuted),
                                kHeads[static_cast<std::size_t>(device_column + 1)], cell - 8.0F);
                    PaddockGap(5.0F);
                    binding_button(device_column, cell - 8.0F);
                    ImGui::EndGroup();
                    bottom = std::max(bottom, ImGui::GetItemRectMax().y);
                    ++slot;
                }
                ImGui::SetCursorScreenPos({row_at.x, bottom + 8.0F});
                draw->AddRectFilled({row_at.x, bottom + 7.0F}, {row_at.x + width, bottom + 8.0F},
                                    PaddockCol(0xFFFFFF, 12U));
                ImGui::Dummy({width, 0.0F});
            }
            ImGui::PopID();
            ++row_index;
        }
    }
    PaddockGap(20.0F);
    const std::string player = std::to_string(g_selected_player + 1U);
    if (SettingsButton("Restore this player\xE2\x80\x99s bindings")) {
        ControlsSnapshot before = TakeControlsSnapshot();
        input::reset_defaults(g_selected_player);
        commit();
        state.undo = before;
        state.undo_message = "Player " + player + " bindings restored.";
    }
    if (ControlsDisclosure("##mapping", "More mapping actions", state.mapping_open, width)) {
        if (g_selected_player != 0U) {
            if (SettingsButton("Copy Player 1 bindings")) {
                ControlsSnapshot before = TakeControlsSnapshot();
                input::copy_bindings(0U, g_selected_player);
                commit();
                state.undo = before;
                state.undo_message = "Copied Player 1 bindings to Player " + player + ".";
            }
            PaddockGap(12.0F);
        }
        if (SettingsButton("Restore all players\xE2\x80\x99 bindings")) {
            ControlsSnapshot before = TakeControlsSnapshot();
            for (std::size_t index = 0; index < input::kPlayerCount; ++index) input::reset_defaults(index);
            commit();
            state.undo = before;
            state.undo_message = "All player bindings restored.";
        }
    }
}

// ------------------------------------------------------------ stick response

void DrawControlsDriving(ControlsPageState& state, float width, const std::function<void()>& commit) {
    namespace input = dkr::runtime::input;
    ControlsScope(true, width);
    ControlsHeading("Stick response", width);
    const auto tenths = [](int value) {
        char text[16]{};
        std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(value) / 10.0);
        return SettingsRangeValue{text, "%"};
    };
    int sensitivity = static_cast<int>(std::lround(input::stick_sensitivity()));
    if (SettingsRange("##stick-sensitivity", "Stick sensitivity", &sensitivity, 50, 150, width,
                      [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; })) {
        input::set_stick_sensitivity(static_cast<float>(sensitivity));
        commit();
    }
    PaddockGap(8.0F);
    ControlsNote("Increase to reach full steering with less stick movement.", width);
    PaddockGap(4.0F);
    int deadzone = static_cast<int>(std::lround(input::stick_deadzone() * 10.0F));
    if (SettingsRange("##stick-deadzone", "Stick deadzone", &deadzone, 0, 350, width, tenths)) {
        input::set_stick_deadzone(static_cast<float>(deadzone) / 10.0F);
        commit();
    }
    PaddockGap(8.0F);
    ControlsNote("Ignores small movements near the center. Increase if the stick drifts.", width);
    ControlsHeading("Axis direction by vehicle", width, 20.0F);

    // Vehicle | Horizontal | Vertical (.controls-vehicle-table).
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const PaddockType head = PaddockReading(13.0F, true, 1.45F);
        const float column = std::floor(width / 3.0F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const std::array<const char*, 3> kHeads{{"Vehicle", "Horizontal", "Vertical"}};
        for (int index = 0; index < 3; ++index) {
            PaddockDrawRun(draw, head, {at.x + column * static_cast<float>(index) + 8.0F, at.y + 8.0F},
                           PaddockCol(kSetMuted), kHeads[index], kHeads[index] + std::strlen(kHeads[index]));
        }
        float y = at.y + 8.0F + head.line + 8.0F;
        draw->AddRectFilled({at.x, y}, {at.x + width, y + 1.0F}, PaddockCol(kCtlLine));
        y += 1.0F;
        constexpr std::array<const char*, 3> kVehicles{{"Car", "Hovercraft", "Plane"}};
        const PaddockType row_type = PaddockReading(16.0F, true, 1.45F);
        for (std::size_t vehicle = 0; vehicle < kVehicles.size(); ++vehicle) {
            const auto type = static_cast<input::VehicleClass>(vehicle);
            const float row_height = 8.0F + 44.0F + 8.0F;
            PaddockDrawRun(draw, row_type, {at.x + 8.0F, std::round(y + (row_height - row_type.line) * 0.5F)},
                           PaddockCol(kSetText), kVehicles[vehicle], kVehicles[vehicle] + std::strlen(kVehicles[vehicle]));
            ImGui::PushID(static_cast<int>(vehicle));
            bool invert_x = input::vehicle_stick_x_inverted(type);
            ImGui::SetCursorScreenPos({at.x + column + 8.0F, y + 8.0F});
            if (SettingsCheck("##x", "Invert", &invert_x, column - 16.0F)) {
                input::set_vehicle_stick_x_inverted(type, invert_x);
                commit();
            }
            bool invert_y = input::vehicle_stick_y_inverted(type);
            ImGui::SetCursorScreenPos({at.x + column * 2.0F + 8.0F, y + 8.0F});
            if (SettingsCheck("##y", "Invert", &invert_y, column - 16.0F)) {
                input::set_vehicle_stick_y_inverted(type, invert_y);
                commit();
            }
            ImGui::PopID();
            y += row_height;
            draw->AddRectFilled({at.x, y - 1.0F}, {at.x + width, y}, PaddockCol(kCtlLine));
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, y - at.y + 20.0F});
    }

    if (ControlsDisclosure("##response", "Advanced response", state.response_open, width)) {
        int anti = static_cast<int>(std::lround(input::stick_anti_deadzone() * 10.0F));
        if (SettingsRange("##anti-deadzone", "Stick anti-deadzone", &anti, 0, 500, width, tenths)) {
            input::set_stick_anti_deadzone(static_cast<float>(anti) / 10.0F);
            commit();
        }
        PaddockGap(8.0F);
        ControlsNote("Adds a minimum output once the stick leaves its deadzone.", width);
        PaddockGap(4.0F);
        int curve = static_cast<int>(std::lround(input::stick_curve() * 100.0F));
        if (SettingsRange("##curve", "Response curve", &curve, 50, 250, width, [](int value) {
                char text[16]{};
                std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(value) / 100.0);
                return SettingsRangeValue{text, ""};
            })) {
            input::set_stick_curve(static_cast<float>(curve) / 100.0F);
            commit();
        }
        PaddockGap(8.0F);
        ControlsNote("Changes how steering builds as the stick moves. 1.00 gives a linear response.", width);
        PaddockGap(4.0F);
        int threshold = static_cast<int>(std::lround(input::trigger_threshold() * 100.0F));
        if (SettingsRange("##threshold", "Trigger threshold", &threshold, 5, 95, width,
                          [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; })) {
            input::set_trigger_threshold(static_cast<float>(threshold) / 100.0F);
            commit();
        }
        PaddockGap(8.0F);
        ControlsNote("How far a trigger must be pressed before it counts as a button press.", width);
    }
    PaddockGap(12.0F);
    ControlsNote("These adjustments shape the final N64 stick sample once per game update. Accurate keeps the "
                 "original response.",
                 width);
    if (SettingsButton("Restore stick response and inversions")) {
        ControlsSnapshot before = TakeControlsSnapshot();
        input::set_stick_deadzone(23.95F);
        input::set_stick_anti_deadzone(0.0F);
        input::set_stick_sensitivity(100.0F);
        input::set_stick_curve(1.0F);
        input::set_stick_x_inverted(false);
        input::set_stick_y_inverted(false);
        input::set_trigger_threshold(0.5F);
        for (std::size_t vehicle = 0; vehicle < 3U; ++vehicle) {
            input::set_vehicle_stick_x_inverted(static_cast<input::VehicleClass>(vehicle), false);
            input::set_vehicle_stick_y_inverted(static_cast<input::VehicleClass>(vehicle), false);
        }
        commit();
        state.undo = before;
        state.undo_message = "Stick response and vehicle inversions restored.";
    }
}

// ------------------------------------------------------------ motion

void DrawControlsMotion(float width, bool live, const std::function<void()>& commit) {
    namespace input = dkr::runtime::input;
    const std::size_t player = g_selected_player;
    ControlsScope(false, width);
    ControlsHeading("Motion steering", width);
    bool gyro = input::gyro_enabled(player);
    if (SettingsCheck("##gyro", "Enable gyro steering", &gyro, width)) {
        input::set_gyro_enabled(gyro, player);
        commit();
    }
    PaddockGap(8.0F);
    ControlsNote("Requires a controller with a motion sensor.", width);
    if (!gyro) return;
    int axis = static_cast<int>(input::gyro_axis(player));
    if (SettingsDropdown("motion-style", "Motion style", &axis,
                         {"Roll controller like a wheel", "Turn controller left and right"}, width)) {
        input::set_gyro_axis(axis == 1 ? input::GyroAxis::Yaw : input::GyroAxis::Roll, player);
        commit();
    }
    PaddockGap(16.0F);
    const auto percent = [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; };
    int sensitivity = static_cast<int>(std::lround(input::gyro_sensitivity(player)));
    if (SettingsRange("##gyro-x", "Horizontal sensitivity", &sensitivity, 25, 300, width, percent)) {
        input::set_gyro_sensitivity(static_cast<float>(sensitivity), player);
        commit();
    }
    PaddockGap(16.0F);
    int y_sensitivity = static_cast<int>(std::lround(input::gyro_y_sensitivity(player)));
    if (SettingsRange("##gyro-y", "Vertical sensitivity", &y_sensitivity, 25, 300, width, percent)) {
        input::set_gyro_y_sensitivity(static_cast<float>(y_sensitivity), player);
        commit();
    }
    PaddockGap(16.0F);
    int deadzone = static_cast<int>(std::lround(input::gyro_deadzone(player) * 10.0F));
    if (SettingsRange("##gyro-deadzone", "Motion deadzone", &deadzone, 0, 120, width, [](int value) {
            char text[16]{};
            std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(value) / 10.0);
            return SettingsRangeValue{text, " \xC2\xB0/s"};
        })) {
        input::set_gyro_deadzone(static_cast<float>(deadzone) / 10.0F, player);
        commit();
    }
    PaddockGap(16.0F);
    bool inverted = input::gyro_inverted(player);
    if (SettingsCheck("##gyro-invert-x", "Invert horizontal gyro", &inverted, width)) {
        input::set_gyro_inverted(inverted, player);
        commit();
    }
    PaddockGap(8.0F);
    bool y_inverted = input::gyro_y_inverted(player);
    if (SettingsCheck("##gyro-invert-y", "Invert vertical gyro", &y_inverted, width)) {
        input::set_gyro_y_inverted(y_inverted, player);
        commit();
    }
    PaddockGap(20.0F);
    // Live steering, recenter and calibration (the overlay owns a running game).
    const bool available = dkr::runtime::platform::gyro_available(player);
    {
        PaddockBox box(width, {17.0F, 17.0F});
        const float inner = box.Inner();
        PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText), "Steering test", inner);
        PaddockGap(6.0F);
        ControlsAxis("Horizontal", input::gyro_steering_position(player), live && available, inner);
        ControlsAxis("Vertical", input::gyro_steering_y_position(player), live && available, inner);
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(8.0F), PaddockRgb(kCtlSurface), PaddockRgb(kCtlLine), 1.0F});
        });
    }
    PaddockGap(20.0F);
    const char* reason = !live ? "Available from the in-game overlay." : "No motion sensor was reported.";
    ControlsRow(width, [&](float cell) {
        if (SettingsButton("Recenter steering", cell, !live || !available, SettingsButtonTone::Plain, false,
                           reason)) {
            input::recenter_gyro(player);
        }
    }, [&](float cell) {
        if (SettingsButton("Calibrate controller", cell, !live || !available || input::gyro_calibrating(player),
                           SettingsButtonTone::Plain, false, reason)) {
            input::begin_gyro_calibration(player);
        }
    });
    PaddockGap(12.0F);
    if (input::gyro_calibrating(player)) {
        char text[64]{};
        std::snprintf(text, sizeof(text), "Keep the controller still: %.0f%%",
                      static_cast<double>(input::gyro_calibration_progress(player) * 100.0F));
        ControlsNote(text, width, kSetWarm);
    } else if (!live) {
        ControlsNote("Recenter and calibrate your controller from the in-game overlay.", width);
    } else if (!available) {
        ControlsNote("No SDL gyro sensor was reported by Controller " + std::to_string(player + 1U) + ".", width);
    }
}

// ------------------------------------------------------------ shortcuts

void DrawControlsShortcuts(float width, const std::function<void()>& commit) {
    namespace input = dkr::runtime::input;
    ControlsScope(true, width);
    ControlsHeading("Game shortcuts", width);
    ControlsNote("Available for editing from any player. Select a binding to record a key, button or two-button "
                 "combination. Controller shortcuts listen to Player 1\xE2\x80\x99s controller.",
                 width);
    bool quick_restart = input::quick_restart_enabled();
    if (SettingsCheck("##quick-restart", "Enable quick race restart", &quick_restart, width)) {
        input::set_quick_restart_enabled(quick_restart);
        commit();
    }
    constexpr std::array<std::pair<input::ShortcutAction, const char*>, kShortcutActionCount> kShortcuts{{
        {input::ShortcutAction::QuickRestart, "Quick race restart"},
        {input::ShortcutAction::ToggleOverlay, "Open / close DKR-R menu"},
        {input::ShortcutAction::ToggleTexturePack, "Toggle texture pack"},
        {input::ShortcutAction::ToggleFullscreen, "Windowed / fullscreen"},
        {input::ShortcutAction::RecenterGyro, "Recenter gyro"},
    }};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockGap(8.0F);
    for (const auto& [shortcut, label] : kShortcuts) {
        if (shortcut == input::ShortcutAction::QuickRestart && !quick_restart) continue;
        ImGui::PushID(static_cast<int>(shortcut));
        PaddockGap(20.0F);
        PaddockText(PaddockReading(16.0F, true, 1.4F), PaddockRgb(kSetText), label, width);
        PaddockGap(12.0F);
        ControlsRow(width, [&](float cell) {
            PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSetMuted), "Keyboard", cell);
            PaddockGap(6.0F);
            const auto binding = input::shortcut_keyboard_binding(shortcut);
            const std::string name =
                FriendlyBinding(ShortcutBindingName(CaptureDevice::Keyboard, binding)) + "##keyboard";
            if (SettingsButton(name.c_str(), cell, false,
                               binding.primary == input::kUnbound ? SettingsButtonTone::Unbound
                                                                  : SettingsButtonTone::Plain)) {
                BeginShortcutCapture(CaptureDevice::Keyboard, shortcut);
            }
        }, [&](float cell) {
            PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSetMuted), "Controller", cell);
            PaddockGap(6.0F);
            const auto binding = input::shortcut_controller_binding(shortcut);
            const std::string name =
                FriendlyBinding(ShortcutBindingName(CaptureDevice::Controller, binding)) + "##controller";
            if (SettingsButton(name.c_str(), cell, false,
                               binding.primary == input::kUnbound ? SettingsButtonTone::Unbound
                                                                  : SettingsButtonTone::Plain)) {
                BeginShortcutCapture(CaptureDevice::Controller, shortcut);
            }
        });
        PaddockGap(20.0F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        draw->AddRectFilled({at.x, at.y - 1.0F}, {at.x + width, at.y}, PaddockCol(kCtlLine));
        ImGui::PopID();
    }
}

// ------------------------------------------------------------ capture

// "Choose a new control" (.controls-capture): what is being bound, how to
// bind it, and Remove / Cancel.
void DrawControlsCaptureModal() {
    using dkr::runtime::input::Action;
    constexpr const char* kCapturePopup = "CHOOSE A NEW CONTROL";
    if (g_capture_popup_pending) {
        ImGui::OpenPopup(kCapturePopup);
        g_capture_popup_pending = false;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(560.0F, display.x - 32.0F);
    ImGui::SetNextWindowSize({width, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({display.x * 0.5F, display.y * 0.5F}, ImGuiCond_Appearing, {0.5F, 0.5F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {26.0F, 26.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(kCtlSurface));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(kSetBorder));
    const bool visible = ImGui::BeginPopupModal(kCapturePopup, nullptr,
                                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                    ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_AlwaysAutoResize |
                                                    ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    if (!visible) return;
    g_paddock_modal_frame = ImGui::GetFrameCount();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    const float inner = width - 52.0F;
    PaddockText(PaddockReading(20.0F, true, 1.4F), PaddockRgb(kSetText), "Choose a new control", inner);
    PaddockGap(13.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + inner, at.y + 1.0F}, PaddockCol(kSetWarm));
        PaddockGap(1.0F + 22.0F);
    }
    std::string heading;
    std::string help;
    if (g_capture_action >= 0) {
        heading = "Player " + std::to_string(g_selected_player + 1U) + " \xC2\xB7 " +
                  dkr::runtime::input::action_label(static_cast<Action>(g_capture_action));
        help = g_capture_device == CaptureDevice::Keyboard
            ? "Press a keyboard key. Escape cancels."
            : "Press a gamepad button or move an axis firmly. Escape cancels.";
    } else if (g_capture_action == kShortcutCaptureAction) {
        heading = ShortcutActionLabel(g_capture_shortcut_action);
        help = std::string("Press one ") + (g_capture_device == CaptureDevice::Keyboard ? "key" : "controller button") +
               ", or hold the first and press a second. The chord is saved automatically. Escape cancels.";
    } else if (g_capture_action == kAssignControllerCaptureAction) {
        heading = "Assign Player " + std::to_string(g_selected_player + 1U);
        help = "Press any button on the controller you want this player to use. Escape cancels.";
    }
    PaddockText(PaddockReading(18.0F, true, 1.4F), PaddockRgb(kSetText), heading, inner);
    PaddockGap(13.0F);
    PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetText), help, inner);
    if (g_capture_action == kShortcutCaptureAction && g_shortcut_capture_count == 1) {
        PaddockGap(12.0F);
        const dkr::runtime::input::ShortcutBinding pending{g_shortcut_capture_sources[0],
                                                           dkr::runtime::input::kUnbound};
        PaddockText(PaddockReading(16.0F, true, 1.45F), PaddockRgb(kSetWarm),
                    "Captured: " + FriendlyBinding(ShortcutBindingName(g_capture_device, pending)), inner);
    }
    PaddockGap(24.0F);
    const char* clear_label = g_capture_action == kAssignControllerCaptureAction ? "Clear assignment"
                                                                                 : "Remove binding";
    const float clear_width = SettingsButtonWidth(clear_label);
    const float cancel_width = SettingsButtonWidth("Cancel");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inner - clear_width - 12.0F - cancel_width);
    if (SettingsButton(clear_label, clear_width)) {
        if (g_capture_action >= 0) {
            const auto action = static_cast<Action>(g_capture_action);
            if (g_capture_device == CaptureDevice::Keyboard) {
                dkr::runtime::input::set_keyboard_binding(g_selected_player, action, dkr::runtime::input::kUnbound);
            } else if (g_capture_secondary_controller) {
                dkr::runtime::input::set_secondary_controller_binding(g_selected_player, action,
                                                                      dkr::runtime::input::kUnbound);
            } else {
                dkr::runtime::input::set_controller_binding(g_selected_player, action, dkr::runtime::input::kUnbound);
            }
        } else if (g_capture_action == kShortcutCaptureAction) {
            const dkr::runtime::input::ShortcutBinding unbound{};
            if (g_capture_device == CaptureDevice::Keyboard) {
                dkr::runtime::input::set_shortcut_keyboard_binding(g_capture_shortcut_action, unbound);
            } else {
                dkr::runtime::input::set_shortcut_controller_binding(g_capture_shortcut_action, unbound);
            }
        } else if (g_capture_action == kAssignControllerCaptureAction) {
            dkr::runtime::platform::clear_controller_assignment(g_selected_player);
        }
        SaveSettings();
        g_capture_finished = true;
    }
    ImGui::SameLine(0.0F, 12.0F);
    if (SettingsButton("Cancel", cancel_width)) g_capture_finished = true;
    if (g_capture_finished) {
        ImGui::CloseCurrentPopup();
        g_capture_action = -1;
        g_capture_device = CaptureDevice::None;
        g_capture_secondary_controller = false;
        g_capture_finished = false;
    }
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

// ------------------------------------------------------------ page

void DrawControlsPage(float available_width, bool live) {
    ControlsPageState& state = g_controls_page;
    const int frame = ImGui::GetFrameCount();
    const bool entered = state.context != ImGui::GetCurrentContext() || state.last_frame != frame - 1;
    if (entered) {
        state.context = ImGui::GetCurrentContext();
        state.undo.reset();
    }
    state.last_frame = frame;
    // A shortcut chord waits a moment for its second input.
    if (g_capture_action == kShortcutCaptureAction && g_shortcut_capture_count == 1 &&
        std::chrono::steady_clock::now() >= g_shortcut_capture_deadline) {
        CommitShortcutCapture();
    }
    // Any change after a restore ends its undo.
    const std::function<void()> commit = [&state] {
        SaveSettings();
        state.undo.reset();
    };
    const float width = available_width;
    ++g_settings_regular_labels;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});

    DrawPageHeading("CONTROLS");
    // h1 { min-height: 0; margin-bottom: 10px }: the heading's box is 6 px
    // taller than the study's 58 px line.
    PaddockGap(4.0F);
    PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSetMuted),
                "Configure your devices and button mappings.", width);
    PaddockGap(24.0F);

    // The local player (.controls-player-bar): four painted chips.
#if defined(__ANDROID__)
    dkr::runtime::mobile::settings();
    PaddockGap(20.0F);
#endif
    {
        const bool narrow = width <= 580.0F;
        const PaddockType label_type = PaddockReading(14.0F, false, 1.45F);
        const float label_width = std::ceil(PaddockMeasure(label_type, "Local player"));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float chip_height = std::ceil(PaddockSign(18.0F, 1.4F).line + 24.0F);
        float chips_x = at.x + label_width + 16.0F;
        float chips_width = std::min(520.0F, width - label_width - 16.0F);
        float chips_y = at.y;
        if (narrow) {
            PaddockText(label_type, PaddockRgb(kSetMuted), "Local player", width);
            PaddockGap(8.0F);
            chips_x = at.x;
            chips_width = width;
            chips_y = ImGui::GetCursorScreenPos().y;
        } else {
            ImGui::GetWindowDrawList()->AddText(label_type.font, label_type.size,
                                                {at.x, std::round(at.y + (chip_height - label_type.line) * 0.5F) +
                                                           PaddockGlyphTop(label_type)},
                                                PaddockCol(kSetMuted), "Local player");
        }
        const float chip_width = std::floor((chips_width - 30.0F) / 4.0F);
        for (std::size_t player = 0; player < dkr::runtime::input::kPlayerCount; ++player) {
            ImGui::SetCursorScreenPos({chips_x + (chip_width + 10.0F) * static_cast<float>(player), chips_y});
            const std::string label = "Player " + std::to_string(player + 1U);
            if (DrawSoundChip(label.c_str(), player == g_selected_player, chip_width)) {
                g_selected_player = player;
            }
        }
        ImGui::SetCursorScreenPos({at.x, chips_y + chip_height});
        ImGui::Dummy({width, 0.0F});
    }
    PaddockGap(20.0F);
    const int picked = SettingsSecondaryNav("controls-sections",
                                            {"Device", "Button mapping", "Stick response", "Motion", "Shortcuts"},
                                            g_controls_section, width);
    if (picked >= 0) g_controls_section = picked;
    PaddockGap(22.0F);

    // The undo bar (.controls-feedback).
    if (state.undo) {
        PaddockBox box(width, {17.0F, 13.0F});
        const float inner = box.Inner();
        const float button_width = SettingsButtonWidth("Undo");
        const ImVec2 at = ImGui::GetCursorScreenPos();
        PaddockText(PaddockReading(16.0F, false, 1.45F), PaddockRgb(kSetText), state.undo_message,
                    inner - button_width - 16.0F);
        const float text_bottom = ImGui::GetItemRectMax().y;
        ImGui::SetCursorScreenPos({at.x + inner - button_width, at.y});
        if (SettingsButton("Undo", button_width)) {
            RestoreControlsSnapshot(*state.undo);
            SaveSettings();
            state.undo.reset();
            OlNotify("Previous controls settings restored.");
        }
        const float bottom = std::max(text_bottom, ImGui::GetItemRectMax().y);
        ImGui::SetCursorScreenPos({at.x, bottom});
        ImGui::Dummy({inner, 0.0F});
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(8.0F), PaddockRgb(0x183F3A), PaddockRgb(0x75BAA5), 1.0F});
        });
        PaddockGap(20.0F);
    }

    const bool modern = dkr::runtime::enhancements::modern_options_visible(
        dkr::runtime::enhancements::presentation_profile());
    if ((g_controls_section == 2 || g_controls_section == 3) && !modern) {
        ControlsScope(g_controls_section == 2, width);
        ControlsHeading(g_controls_section == 2 ? "Stick response" : "Motion steering", width);
        ControlsNote("These adjustments require Modern presentation. Accurate preserves the original control "
                     "response.",
                     width);
        if (SettingsButton("Open graphics settings")) g_page_navigation_request = kPageGraphics;
    } else if (g_controls_section == 0) {
        DrawControlsDevice(state, width, commit);
    } else if (g_controls_section == 1) {
        DrawControlsBindings(state, width, commit);
    } else if (g_controls_section == 2) {
        DrawControlsDriving(state, width, commit);
    } else if (g_controls_section == 3) {
        DrawControlsMotion(width, live, commit);
    } else {
        DrawControlsShortcuts(width, commit);
    }

    PaddockGap(28.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kCtlLine));
        PaddockGap(1.0F + 14.0F);
        PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSetMuted),
                    "Changes are saved automatically.", width);
    }
    ImGui::PopStyleVar();
    --g_settings_regular_labels;
    DrawControlsCaptureModal();
    DrawControllerMappingModal();
}
