// Shared launcher/overlay theme and existing controller navigation/OSK.
namespace camplus = dkr::runtime::camera;
bool CameraTuningVisible() {
    return camplus::editor_open.load();
}
void LoadCameraTuning() {} // Old Modern-camera INI no longer drives this editor.
void OpenCameraTuning() {
    camplus::editor_open.store(true);
}
void ToggleCameraTuning() {
    camplus::editor_open.store(!CameraTuningVisible());
}
bool g_camera_plus_mouse = false;
unsigned CameraMouseProfile() {
    const auto a = camplus::activation();
    return a && a->online ? unsigned(dkr::runtime::platform::online_input_profile()) : 0U;
}
void UpdateCameraMouseOrbit() {
    if (g_camera_plus_mouse &&
        (CameraTuningVisible() || g_overlay_visible.load() || !camplus::owns_controls(CameraMouseProfile()))) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        g_camera_plus_mouse = false;
    }
}
bool HandleCameraMouseOrbit(const SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT &&
        camplus::owns_controls(CameraMouseProfile()) && !g_overlay_visible.load() && !CameraTuningVisible()) {
        g_camera_plus_mouse = SDL_SetRelativeMouseMode(SDL_TRUE) == 0;
        return g_camera_plus_mouse;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT && g_camera_plus_mouse) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        g_camera_plus_mouse = false;
        return true;
    }
    if (event.type == SDL_MOUSEMOTION && g_camera_plus_mouse) {
        camplus::actions().mouse(CameraMouseProfile(), -float(event.motion.xrel), float(event.motion.yrel));
        return true;
    }
    return false;
}
void DrawCameraTuningPanel() {
    const auto active = camplus::activation();
    static std::weak_ptr<const camplus::Activation> previous;
    static unsigned profile = 0, group = 0;
    static int preset = 0;
    static std::optional<camplus::Json> saving;
    static std::uint64_t save_generation = 0;
    static std::string status;
    if (previous.lock() != active) {
        previous = active;
        group = 0;
        profile = active && active->online ? unsigned(dkr::runtime::platform::online_input_profile()) : 0U;
        preset = 0;
        saving.reset();
        status.clear();
    }
    bool open = true;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(1120.F, display.x - 32.F), std::min(760.F, display.y - 32.F)},
                             ImGuiCond_Appearing);
    if (!ImGui::Begin("MODERN CAMERA++", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open)
            camplus::editor_open.store(false);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        open = false;
    if (!active || !active->editor) {
        ImGui::TextWrapped("Import and enable ModernCamera++ in Mods/Hacks, then start a new game.");
        const auto problem = camplus::diagnostic();
        if (!problem.empty())
            ImGui::TextWrapped("%s", problem.c_str());
    } else {
        auto& editor = *active->editor;
        const auto library = g_code_library.view();
        if (saving && !library->busy && library->generation > save_generation) {
            if (library->error.empty()) {
                bool matched = false;
                for (const auto& item : library->items)
                    if (item.digest == active->frozen.ordered[0]->digest && item.data == *saving)
                        matched = true;
                if (matched) {
                    editor.saved(*saving);
                    status = "Saved.";
                } else
                    status = "Settings were not saved. Please try again.";
            } else
                status = library->error;
            saving.reset();
        }
        const float width = ImGui::GetContentRegionAvail().x;
        std::vector<SettingsChoice> players;
        for (unsigned i = 0; i < 4; ++i)
            players.emplace_back("Local player " + std::to_string(i + 1));
        int selected = int(profile);
        if (SettingsDropdown("##camera-owner", "Profile", &selected, players, width))
            profile = unsigned(selected);
        std::vector<SettingsChoice> presets;
        for (const auto& value : editor.presets())
            presets.emplace_back(value.name);
        if (!presets.empty() && SettingsDropdown("##camera-preset", "Preset", &preset, presets, width))
            editor.preset(profile, unsigned(preset));
        std::vector<SettingsChoice> sections;
        for (const auto& value : editor.groups())
            sections.emplace_back(value.name);
        int section = int(group);
        if (SettingsDropdown("##camera-section", "Section", &section, sections, width))
            group = unsigned(section);
        const float footer = 180.F;
        ImGui::BeginChild("camera-fields", {0, std::max(48.F, ImGui::GetContentRegionAvail().y - footer)}, false);
        if (group < editor.groups().size())
            for (const auto& field : editor.groups()[group].fields) {
                ImGui::PushID(field.id.c_str());
                const auto value = editor.draft(profile).at(field.id);
                const float inner = ImGui::GetContentRegionAvail().x;
                if (field.type == "Enum") {
                    int choice = value.get<int>();
                    std::vector<SettingsChoice> labels;
                    for (const auto& text : field.choices)
                        labels.emplace_back(text);
                    // A playtest must not expose a nonfunctional headset switch.
                    if (field.id == "mode" && labels.size() == 8) {
                        labels[6].disabled = true;
                        labels[6].detail = "Inspection output is not enabled in this playtest.";
                        labels[7].disabled = true;
                        labels[7].detail = "Headset scene output is not connected yet.";
                    }
                    if (field.id == "first_visibility" && labels.size() == 3) {
                        labels[2].disabled = true;
                        labels[2].detail = "Choose Show vehicle or Hide vehicle in this build.";
                    }
                    if (SettingsDropdown("##enum", field.name, &choice, labels, inner))
                        editor.edit(profile, field.id, choice);
                } else if (field.type == "Number") {
                    const double step = std::max(field.step, (field.maximum - field.minimum) / 10000.);
                    const int count = std::max(1, int(std::ceil((field.maximum - field.minimum) / step)));
                    int position = std::clamp(int(std::lround((value.get<double>() - field.minimum) / step)), 0, count);
                    if (SettingsRange("##number", field.name, &position, 0, count, inner, [&](int point) {
                            char number[40];
                            std::snprintf(number, sizeof(number), "%.2f",
                                          std::min(field.maximum, field.minimum + point * step));
                            return SettingsRangeValue{number, ""};
                        }))
                        editor.edit(profile, field.id, std::min(field.maximum, field.minimum + position * step));
                } else {
                    SettingsLabel(field.name, inner);
                    const auto id = field.id;
                    const auto owner = profile;
                    if (SettingsButton(value.get_ref<const std::string&>().empty()
                                           ? "Enter text"
                                           : value.get_ref<const std::string&>().c_str(),
                                       inner)) {
                        RequestCustomTextKeyboard(field.name.c_str(), value.get<std::string>(), 4097,
                                                  [weak = std::weak_ptr<const camplus::Activation>(active), id,
                                                   owner](const std::string& text) {
                                                      if (auto a = weak.lock())
                                                          a->editor->edit(owner, id, text);
                                                  });
                    }
                }
                PaddockGap(12.F);
                ImGui::PopID();
            }
        ImGui::EndChild();
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float button = std::max(1.F, (width - gap * 3) / 4);
        if (SettingsButton("Apply", button, !editor.pending())) {
            editor.apply();
            status = "Applied.";
        }
        ImGui::SameLine();
        if (SettingsButton("Revert", button))
            editor.revert();
        ImGui::SameLine();
        if (SettingsButton("Reset", button))
            editor.reset(profile);
        ImGui::SameLine();
        if (SettingsButton("Save", button, bool(saving) || library->busy)) {
            editor.apply();
            auto document = editor.save_document();
            if (g_code_library.data(active->frozen.ordered[0]->digest, document)) {
                saving = std::move(document);
                save_generation = library->generation;
                status = "Saving...";
            }
        }
        if (!editor.error().empty())
            ImGui::TextWrapped("%s", editor.error().c_str());
        else if (!status.empty())
            ImGui::TextUnformatted(status.c_str());
        else if (editor.pending() || editor.unsaved())
            ImGui::TextUnformatted("Unsaved changes");
    }
    if (SettingsButton("Close", 120.F))
        open = false;
    ImGui::End();
    if (!open)
        camplus::editor_open.store(false);
}
