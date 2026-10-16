// Included by runtime_ui.cpp inside its UI namespace, before
// runtime_graphics_ui.inl.
//
// Camera tuning: the Bancada ModernCam inside the game. A floating window over
// the race (F7, or Graphics > Tune the Modern camera) that edits the Modern
// camera live and saves it to modern-camera.ini. It is mouse-only on purpose:
// the controller and keyboard keep driving while it is open.

namespace camtune = dkr::runtime::modern_camera;

struct CameraTuningGroup {
    camtune::TuningGroup group;
    const char* title;
    const char* help;
    bool open;
};

constexpr std::array<CameraTuningGroup, 12> kCameraTuningGroups{{
    {camtune::TuningGroup::Framing, "Framing", "Distance, height and zoom with speed.", true},
    {camtune::TuningGroup::Direction, "Direction",
     "How closely the camera follows the racer through turns and drifts.", true},
    {camtune::TuningGroup::YawSpring, "Yaw spring",
     "How smoothly the camera turns. Damping below 1 adds overshoot.", true},
    {camtune::TuningGroup::Follow, "Follow", "How elastic the position is.", false},
    {camtune::TuningGroup::Terrain, "Terrain", "Pitching with the track's slope.", false},
    {camtune::TuningGroup::LookAhead, "Look ahead", "Aiming where the racer is going.", false},
    {camtune::TuningGroup::Fov, "Field of view", "Sense of speed, added to the level's own FOV.", false},
    {camtune::TuningGroup::Roll, "Roll", "A slight tilt as the racer turns.", false},
    {camtune::TuningGroup::Orbit, "Manual orbit",
     "Turn the camera with the right stick or mouse. Hold the right mouse button to keep the view; Escape frees the pointer.", false},
    {camtune::TuningGroup::Collision, "Walls",
     "When a wall comes between the camera and the racer. The camera rises over the racer rather than into it.", false},
    {camtune::TuningGroup::Shake, "Shake",
     "Shake from boosts and landings. Zero turns shake off. Hits and explosions never shake this camera.", true},
    {camtune::TuningGroup::Hits, "Hits",
     "Lower values smooth the camera after collisions. 100% follows the impact immediately.",
     true},
}};

struct CameraTuningPanel {
    std::atomic<bool> visible{false};
    // What modern-camera.ini holds (or the defaults while there is none).
    camtune::Tuning saved = camtune::default_tuning();
    std::string status;
    std::array<float, 240> lag{};
    std::size_t lag_next = 0U;
};
CameraTuningPanel g_camera_tuning;

std::filesystem::path CameraTuningPath() {
    return g_config_directory / "modern-camera.ini";
}

bool SaveCameraTuning(const camtune::Tuning& tuning);

void LoadCameraTuning() {
    camtune::Tuning tuning = camtune::default_tuning();
    bool migrated = false;
    {
        // Closed before the save below: Windows will not replace a file
        // that is still open.
        std::ifstream input(CameraTuningPath(), std::ios::binary);
        if (input) {
            std::ostringstream text;
            text << input.rdbuf();
            tuning = camtune::parse_tuning(text.str(), &migrated);
        }
    }
    camtune::set_tuning(tuning);
    g_camera_tuning.saved = tuning;
    // An older file is brought up to date once, so the panel and the file
    // agree on what is saved.
    if (migrated && SaveCameraTuning(tuning)) {
        g_camera_tuning.status = "Camera settings updated. Your custom values are kept.";
    }
}

bool SaveCameraTuning(const camtune::Tuning& tuning) {
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    const std::filesystem::path path = CameraTuningPath();
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << camtune::serialize_tuning(tuning);
        if (!output) return false;
    }
    if (!ReplaceSettingsFile(temporary, path)) return false;
    g_camera_tuning.saved = tuning;
    return true;
}

void ToggleCameraTuning() {
    g_camera_tuning.visible.store(!g_camera_tuning.visible.load(std::memory_order_acquire),
                                  std::memory_order_release);
}

void OpenCameraTuning() {
    g_camera_tuning.visible.store(true, std::memory_order_release);
}

bool CameraTuningVisible() {
    return g_camera_tuning.visible.load(std::memory_order_acquire);
}

// ------------------------------------------------------------ mouse orbit

// The mouse orbits the Modern camera of the keyboard player's port whenever
// that port races with it: the pointer is captured (relative mode) for as
// long as nothing else needs it, and let go for the menu, another window or
// this panel. With the panel open the pointer stays free for it, and holding
// the right button over the race orbits instead. Either way, holding the
// right button keeps the camera where it was put.
enum class CameraMouseMode : std::uint8_t { Off, Always, Held };

struct CameraMouseOrbit {
    CameraMouseMode mode = CameraMouseMode::Off;
    std::size_t port = 0U;
    std::uint64_t racing_at = 0U; // SDL ticks when the port last raced with the Modern camera
};
CameraMouseOrbit g_camera_mouse;

// Loop and door cameras hand the race back to retail for a moment; keep the
// pointer captured through them instead of flashing it.
constexpr std::uint64_t kCameraMouseGraceMs = 1000U;

bool ImGuiWantsMouse() {
    std::scoped_lock guard(g_inspector_guard);
    if (g_inspector == nullptr) return false;
    std::scoped_lock frame_lock(g_inspector->frameMutex);
    return ImGui::GetIO().WantCaptureMouse;
}

void StopCameraMouseOrbit() {
    if (g_camera_mouse.mode == CameraMouseMode::Off) return;
    g_camera_mouse.mode = CameraMouseMode::Off;
    camtune::set_mouse_orbit_held(g_camera_mouse.port, false);
    SDL_SetRelativeMouseMode(SDL_FALSE);
}

bool StartCameraMouseOrbit(CameraMouseMode mode, std::size_t port) {
    if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0) return false;
    g_camera_mouse.mode = mode;
    g_camera_mouse.port = port;
    return true;
}

// Something else needs the pointer: the menu, a key-binding capture, or
// another window.
bool CameraMousePointerNeeded() {
    return g_overlay_visible.load(std::memory_order_acquire) || dkr::runtime::ui::input_capture_active() ||
        SDL_GetKeyboardFocus() == nullptr;
}

// Once per event pump: capture the pointer when a race starts, let it go when
// something else needs it.
void UpdateCameraMouseOrbit() {
#if !defined(__ANDROID__)
    const auto port = static_cast<std::size_t>(dkr::runtime::input::keyboard_player());
    const std::uint64_t now = SDL_GetTicks64();
    const bool available = camtune::mouse_orbit_available(port);
    if (available) g_camera_mouse.racing_at = now;
    const bool racing = g_camera_mouse.racing_at != 0U && now - g_camera_mouse.racing_at < kCameraMouseGraceMs;
    const bool pointer_needed = CameraMousePointerNeeded();
    switch (g_camera_mouse.mode) {
    case CameraMouseMode::Always:
        if (pointer_needed || CameraTuningVisible() || !racing || port != g_camera_mouse.port) {
            StopCameraMouseOrbit();
        }
        break;
    case CameraMouseMode::Held:
        if (pointer_needed || !racing) StopCameraMouseOrbit();
        break;
    case CameraMouseMode::Off:
        if (available && !pointer_needed && !CameraTuningVisible()) {
            StartCameraMouseOrbit(CameraMouseMode::Always, port);
        }
        break;
    }
#endif
}

// True when the event belonged to the mouse orbit.
bool HandleCameraMouseOrbit(const SDL_Event& event) {
    switch (event.type) {
    case SDL_MOUSEBUTTONDOWN: {
        if (event.button.which == SDL_TOUCH_MOUSEID) return false;
        if (g_camera_mouse.mode != CameraMouseMode::Off) {
            if (event.button.button == SDL_BUTTON_RIGHT) camtune::set_mouse_orbit_held(g_camera_mouse.port, true);
            return true;
        }
        // The panel is open, so the pointer is free: the right button over
        // the race orbits while it is held.
        if (event.button.button != SDL_BUTTON_RIGHT) return false;
        const auto port = static_cast<std::size_t>(dkr::runtime::input::keyboard_player());
        if (CameraMousePointerNeeded() || !camtune::mouse_orbit_available(port) || ImGuiWantsMouse() ||
            !StartCameraMouseOrbit(CameraMouseMode::Held, port)) {
            return false;
        }
        camtune::set_mouse_orbit_held(port, true);
        return true;
    }
    case SDL_MOUSEBUTTONUP:
        if (g_camera_mouse.mode == CameraMouseMode::Off || event.button.which == SDL_TOUCH_MOUSEID) return false;
        if (event.button.button == SDL_BUTTON_RIGHT) {
            camtune::set_mouse_orbit_held(g_camera_mouse.port, false);
            if (g_camera_mouse.mode == CameraMouseMode::Held) StopCameraMouseOrbit();
        }
        return true;
    case SDL_MOUSEWHEEL:
        return g_camera_mouse.mode != CameraMouseMode::Off && event.wheel.which != SDL_TOUCH_MOUSEID;
    case SDL_MOUSEMOTION:
        if (g_camera_mouse.mode == CameraMouseMode::Off || event.motion.which == SDL_TOUCH_MOUSEID) return false;
        camtune::add_mouse_motion(g_camera_mouse.port, static_cast<float>(event.motion.xrel),
                                  static_cast<float>(event.motion.yrel));
        return true;
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) StopCameraMouseOrbit();
        return false;
    default:
        return false;
    }
}

// One label + slider row of a two-column table. Gold labels differ from the
// DKR-R tuning; right-click puts a value back.
bool CameraTuningSlider(const camtune::TuningField& field, camtune::Tuning& tuning,
                        const camtune::Tuning& defaults) {
    float& value = camtune::field_value(tuning, field);
    const float base = camtune::field_value(defaults, field);
    const float scale = field.degrees ? static_cast<float>(180.0 / camtune::kPi) : 1.0F;
    const bool differs = value != base;
    ImGui::PushID(field.key);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (differs) ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
    ImGui::TextWrapped("%s", field.label);
    if (differs) ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    const std::string_view unit = field.unit;
    const bool tight = unit.empty() || unit == "\xC2\xB0" || unit == "x";
    char format[32];
    std::snprintf(format, sizeof(format), "%%.%df%s%s", field.decimals, tight ? "" : " ", field.unit);
    float shown = value * scale;
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::SliderFloat("##value", &shown, field.minimum, field.maximum, format,
                                      ImGuiSliderFlags_AlwaysClamp);
    if (changed) value = shown / scale;
    if (ImGui::IsItemHovered()) {
        char shipped[32];
        std::snprintf(shipped, sizeof(shipped), format, base * scale);
        ImGui::SetTooltip("%s\nDKR-R: %s. Right-click to reset, Ctrl+click to type.", field.key, shipped);
    }
    if (differs && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        value = base;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool CameraTuningRows(camtune::TuningGroup group, camtune::Tuning& tuning, const camtune::Tuning& defaults) {
    bool changed = false;
    if (!ImGui::BeginTable("##rows", 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.9F);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.1F);
    for (const camtune::TuningField& field : camtune::kTuningFields) {
        if (field.group == group) changed |= CameraTuningSlider(field, tuning, defaults);
    }
    ImGui::EndTable();
    return changed;
}

void CameraTuningStatus(const camtune::Readout& live) {
    namespace enh = dkr::runtime::enhancements;
    if (!enh::modern_presentation_enabled()) {
        ImGui::TextColored(kWarm, "Requires Modern graphics.");
    } else if (!camtune::requested()) {
        ImGui::TextColored(kWarm, "The Modern camera is off.");
    } else if (live.live) {
        ImGui::TextColored(kAccent, "Player %u camera. Changes apply while driving.",
                           static_cast<unsigned>(live.port + 1U));
    } else {
        ImGui::TextWrapped("Start an offline race to adjust the camera.");
    }
}

void CameraTuningReadout(CameraTuningPanel& panel, const camtune::Readout& live) {
    panel.lag[panel.lag_next] = live.live ? std::clamp(live.lag, -45.0F, 45.0F) : 0.0F;
    panel.lag_next = (panel.lag_next + 1U) % panel.lag.size();
    if (!live.live) return;
    if (ImGui::BeginTable("##readout", 4, ImGuiTableFlags_SizingStretchSame)) {
        const auto cell = [](const char* name, float value, int decimals, const char* unit) {
            // Round first so a value that shows as zero never shows as "-0".
            const float step = decimals > 0 ? 10.0F : 1.0F;
            const float shown = std::round(value * step) / step + 0.0F;
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", name);
            ImGui::Text("%.*f%s", decimals, shown, unit);
        };
        cell("Speed", live.speed * 3.6F, 0, " km/h");
        cell("Camera vs nose", live.lag, 0, "\xC2\xB0");
        cell("Slip", live.slip, 0, "\xC2\xB0");
        cell("Roll", live.roll, 1, "\xC2\xB0");
        cell("FOV", live.fov, 1, "\xC2\xB0");
        cell("Boom", live.arm, 1, " m");
        cell("Clear of walls", live.clearance * 100.0F, 0, "%");
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Shake");
        char shake[16];
        std::snprintf(shake, sizeof(shake), "%.2f", live.trauma * live.trauma);
        ImGui::ProgressBar(live.trauma * live.trauma, {-FLT_MIN, 0.0F}, shake);
        ImGui::EndTable();
    }
    const auto chip = [](const char* name, bool on) {
        ImGui::SameLine();
        if (on) ImGui::TextColored(kWarm, "%s", name);
        else ImGui::TextDisabled("%s", name);
    };
    ImGui::TextDisabled("State:");
    chip("Drift", live.drifting);
    chip("Boost", live.boosting);
    chip("Air", !live.grounded);
    chip("Look back", live.look_back);
    chip("Hit", live.steadying);
    ImGui::SameLine();
    ImGui::TextDisabled("(%u seen)", static_cast<unsigned>(live.hits));
    ImGui::PlotLines("##lag", panel.lag.data(), static_cast<int>(panel.lag.size()),
                     static_cast<int>(panel.lag_next), "Camera vs nose, last 4 s (\xC2\xB1" "45\xC2\xB0)",
                     -45.0F, 45.0F, {-FLT_MIN, 56.0F});
}

void DrawCameraTuningPanel() {
    CameraTuningPanel& panel = g_camera_tuning;
    camtune::Tuning tuning = camtune::tuning();
    const camtune::Tuning defaults = camtune::default_tuning();
    const camtune::Readout live = camtune::readout();
    bool changed = false;

    ImFont* body = PaddockReading(13.0F).font;
    ImFont* strong = PaddockReading(13.0F, true).font;
    if (body != nullptr) ImGui::PushFont(body);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0F, 12.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8.0F, 4.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.0F, 6.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.08F, 0.12F, 0.94F});
    ImGui::PushStyleColor(ImGuiCol_Border, kWarm);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, {0.04F, 0.16F, 0.22F, 1.0F});
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, {0.06F, 0.26F, 0.33F, 1.0F});
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, {0.04F, 0.16F, 0.22F, 0.9F});
    // A quieter grab, so the value it slides under stays readable.
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, {1.0F, 0.67F, 0.08F, 0.45F});
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x - 24.0F, 24.0F}, ImGuiCond_FirstUseEver, {1.0F, 0.0F});
    ImGui::SetNextWindowSize({480.0F, std::max(display.y * 0.8F, 320.0F)}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({360.0F, 240.0F}, {std::max(display.x, 360.0F), std::max(display.y, 240.0F)});
    bool open = true;
    if (ImGui::Begin("Modern camera tuning (F7)###dkr-camera-tuning", &open, ImGuiWindowFlags_NoNav)) {
        CameraTuningStatus(live);
        bool modern_camera = camtune::requested();
        if (ImGui::Checkbox("Modern camera", &modern_camera)) {
            camtune::set_enabled(modern_camera);
            SaveSettings();
        }
        ImGui::SameLine();
        bool look_back = camtune::look_back_requested();
        if (ImGui::Checkbox("Look back", &look_back)) {
            camtune::set_look_back_enabled(look_back);
            SaveSettings();
        }
        CameraTuningReadout(panel, live);

        ImGui::SeparatorText("Presets");
        int matched = -1;
        for (std::size_t index = 0U; index < camtune::kTuningPresets.size(); ++index) {
            const camtune::TuningPreset& preset = camtune::kTuningPresets[index];
            if (matched < 0 && camtune::same_tuning(tuning, preset.make())) matched = static_cast<int>(index);
            if (index > 0U) {
                const float next = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x +
                    ImGui::CalcTextSize(preset.name).x + ImGui::GetStyle().FramePadding.x * 2.0F;
                if (next < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
            }
            const bool selected = matched == static_cast<int>(index);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, kWarm);
                ImGui::PushStyleColor(ImGuiCol_Text, {0.02F, 0.08F, 0.12F, 1.0F});
            }
            if (ImGui::Button(preset.name)) {
                tuning = preset.make();
                changed = true;
            }
            if (selected) ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preset.help);
        }
        ImGui::TextWrapped("%s", matched >= 0
            ? camtune::kTuningPresets[static_cast<std::size_t>(matched)].help
            : "Custom. Gold labels mark changed settings.");
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("Motion sick? Start from Comfort: roll, shake, FOV changes and sway "
                            "are the usual causes.");
        ImGui::PopTextWrapPos();

        const bool unsaved = !camtune::same_tuning(tuning, panel.saved);
        ImGui::BeginDisabled(!unsaved);
        if (ImGui::Button("Save")) {
            panel.status = SaveCameraTuning(tuning)
                ? "Saved to modern-camera.ini."
                : "Could not save modern-camera.ini.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert to saved")) {
            tuning = panel.saved;
            changed = true;
            panel.status.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Copy as C++")) {
            ImGui::SetClipboardText(camtune::tuning_code(tuning).c_str());
            panel.status = "Copied the block for default_tuning() in modern_camera_dkr.hpp.";
        }
        if (unsaved) {
            ImGui::TextColored(kWarm, "Unsaved changes: they last until you quit.");
        } else if (!panel.status.empty()) {
            ImGui::TextDisabled("%s", panel.status.c_str());
        }
        bool step_log = camtune::step_log();
        if (ImGui::Checkbox("Record the camera to runtime.log", &step_log)) camtune::set_step_log(step_log);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Records camera movement in the log until turned off or DKR-R closes.");
        }
        ImGui::Spacing();

        // NoNav here too: arrows and Space may be driving the racer.
        ImGui::BeginChild("##camera-tuning-values", {0.0F, 0.0F}, ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
        if (strong != nullptr) ImGui::PushFont(strong);
        ImGui::SeparatorText("Enhancements");
        if (strong != nullptr) ImGui::PopFont();
        for (const camtune::TuningFeature& feature : camtune::kTuningFeatures) {
            ImGui::PushID(feature.key);
            bool on = tuning.params.*feature.member != 0;
            const bool differs = on != (defaults.params.*feature.member != 0);
            if (differs) ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
            if (ImGui::Checkbox(feature.label, &on)) {
                tuning.params.*feature.member = on ? 1 : 0;
                changed = true;
            }
            if (differs) ImGui::PopStyleColor();
            ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextDisabled("%s", feature.help);
            ImGui::PopTextWrapPos();
            if (on) changed |= CameraTuningRows(feature.group, tuning, defaults);
            ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (strong != nullptr) ImGui::PushFont(strong);
        ImGui::SeparatorText("Parameters");
        if (strong != nullptr) ImGui::PopFont();
        for (const CameraTuningGroup& group : kCameraTuningGroups) {
            ImGui::PushID(group.title);
            if (ImGui::CollapsingHeader(group.title, group.open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextDisabled("%s", group.help);
                ImGui::PopTextWrapPos();
                changed |= CameraTuningRows(group.group, tuning, defaults);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(7);
    if (body != nullptr) ImGui::PopFont();

    if (changed) {
        camtune::set_tuning(tuning);
        panel.status.clear();
    }
    if (!open) panel.visible.store(false, std::memory_order_release);
}
