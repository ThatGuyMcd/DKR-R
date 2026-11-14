// Included after the retained control and catalogue helpers. Device preferences
// remain in the existing profile; this owner never changes a mod or its config.
struct LibraryAnalogueUi {
    int player = 0, last_frame = -1;
    ImGuiContext* context = nullptr;
    dkr::library_ui::CaptureTarget capture;
    bool session_requested = false;
} g_library_analogue;

std::size_t LibraryCapturePlayer() {
    return g_library_analogue.capture.resolve(g_selected_player);
}

std::shared_ptr<const dkr::mods::code::Package> LibrarySessionAnalogue() {
    using namespace dkr::runtime::netplay;
    std::shared_ptr<const dkr::mods::code::FrozenSet> codes;
    if (session().active()) {
        if (!g_online_mod_sync)
            return {};
        const auto proof = g_online_mod_sync->snapshot();
        const auto& manifest = session().view().room.manifest;
        if (!proof.profile || !proof.runtime || manifest.mod_manifest_hash.empty() ||
            proof.profile->digest != manifest.mod_manifest_hash)
            return {};
        const auto resources = std::static_pointer_cast<const dkr::mods::online::RuntimeResources>(proof.runtime);
        if (resources->fingerprint() != manifest.owned_resources_hash)
            return {};
        codes = resources->code();
    } else
        codes = dkr::runtime::code_mods::frozen();
    if (codes)
        for (const auto& code : codes->ordered)
            if (code && code->id == dkr::library_ui::AnalogueId && code->modules.contains("analogue-driving"))
                return code;
    return {};
}

void LibraryCancelAnalogueCapture() {
    if (!g_library_analogue.capture.player)
        return;
    g_capture_action = -1;
    g_capture_device = CaptureDevice::None;
    g_capture_popup_pending = false;
    g_capture_finished = false;
    g_capture_secondary_controller = false;
    g_library_analogue.capture.clear();
}

void LibraryFinishUiFrame() {
    if (g_library_analogue.capture.player && (g_library_analogue.context != ImGui::GetCurrentContext() ||
                                              g_library_analogue.last_frame != ImGui::GetFrameCount()))
        LibraryCancelAnalogueCapture();
}

void LibraryBeginAnalogueCapture(dkr::runtime::input::Action action, CaptureDevice device) {
    if (g_capture_action != -1 || !g_library_analogue.capture.begin(unsigned(g_library_analogue.player)))
        return;
    BeginControllerCapture(int(action), device, false);
}

void DrawLibraryAnalogueSettings(float width) {
    namespace driving = dkr::runtime::driving;
    namespace input = dkr::runtime::input;
    auto& ui = g_library_analogue;
    if (ui.context != ImGui::GetCurrentContext())
        LibraryCancelAnalogueCapture();
    ui.context = ImGui::GetCurrentContext();
    ui.last_frame = ImGui::GetFrameCount();
    ++g_settings_regular_labels;
    PaddockGap(18.F);
    ControlsHeading("Analogue driving controls", width);
    ImGui::BeginDisabled(ui.capture.player.has_value());
    SettingsDropdown("analogue-profile-player", "Local player", &ui.player,
                     {"Player 1", "Player 2", "Player 3", "Player 4"}, width);
    const unsigned player = unsigned(ui.player);
    auto profile = driving::profile(player);
    (void)dkr::runtime::platform::player_input_preview(player);
    PaddockGap(12.F);
    if (SettingsCheck("analogue-profile-enabled", "Use analogue accelerator and brake for this player",
                      &profile.enabled, width)) {
        driving::configure(player, profile);
        SaveSettings();
    }
    PaddockGap(10.F);
    if (SettingsButton("Apply RT / LT driving layout", width)) {
        input::set_controller_binding(player, input::Action::AnalogueAccelerator,
                                      input::encode_controller_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, true));
        input::set_controller_binding(player, input::Action::AnalogueBrake,
                                      input::encode_controller_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, true));
        input::set_controller_binding(player, input::Action::Z, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        if (input::controller_binding(player, input::Action::L) == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            input::set_controller_binding(player, input::Action::L, input::kUnbound);
        profile.enabled = true;
        driving::configure(player, profile);
        SaveSettings();
    }
    ControlsNote(
        "RT accelerates, LT brakes, LB uses items. The preset replaces conflicting item/L bindings. Keyboard and face-button fallbacks stay available.",
        width);
    if (dkr::runtime::platform::controller_instance_for_player(player) < 0)
        ControlsNote("No controller is assigned to this player. Assign one in Controls > Device.", width);
    for (unsigned channel = 0; channel < 2; ++channel) {
        ImGui::PushID(int(channel));
        auto& calibration = channel ? profile.brake : profile.accelerator;
        const auto action = channel ? input::Action::AnalogueBrake : input::Action::AnalogueAccelerator;
        PaddockGap(14.F);
        ControlsHeading(channel ? "Brake" : "Accelerator", width);
        const auto binding = input::controller_binding_name(input::controller_binding(player, action));
        if (SettingsButton((binding + "##analogue-controller").c_str(), width))
            LibraryBeginAnalogueCapture(action, CaptureDevice::Controller);
        PaddockGap(8.F);
        if (input::keyboard_player() == int(player)) {
            const auto key = input::keyboard_binding_name(input::keyboard_binding(player, action));
            if (SettingsButton(("Keyboard: " + key + "##analogue-keyboard").c_str(), width))
                LibraryBeginAnalogueCapture(action, CaptureDevice::Keyboard);
            PaddockGap(8.F);
        }
        const auto preview = driving::unpack(driving::preview[player].load());
        const float amount = driving::ratio(channel ? preview.brake : preview.accelerator);
        const auto label = std::to_string(int(std::lround(amount * 100))) + "%";
        ImGui::ProgressBar(amount, {width, 20.F}, label.c_str());
        PaddockGap(10.F);
        if (ImGui::TreeNode("Calibration")) {
            const auto percent = [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; };
            const float inner = std::max(100.F, ImGui::GetContentRegionAvail().x);
            bool changed = false;
            changed |= SettingsRange("release", "Release deadzone", &calibration.release, 0, 20, inner, percent);
            changed |= SettingsRange("full", "Full travel", &calibration.full, 50, 100, inner, percent);
            changed |= SettingsRange("curve", "Response curve", &calibration.curve, 50, 250, inner, [](int value) {
                char text[16];
                std::snprintf(text, sizeof(text), "%.2f", double(value) / 100);
                return SettingsRangeValue{text, ""};
            });
            changed |= SettingsCheck("invert", "Invert axis", &calibration.inverted, inner);
            if (changed) {
                driving::configure(player, profile);
                SaveSettings();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    PaddockGap(12.F);
    ControlsNote("Saved automatically on this device. Each local player has separate settings.", width);
    if (ui.capture.player) {
        DrawControlsCaptureModal();
        if (g_capture_action == -1)
            ui.capture.clear();
    }
    --g_settings_regular_labels;
}

void DrawInstalledAnalogueSettings(const dkr::mods::code::LibraryItem& item) {
    const auto active = LibrarySessionAnalogue();
    const bool running = active && active->digest == item.digest;
    if (!dkr::library_ui::configurable(item.id, item.selected, item.approved, item.compatibility.empty(), running))
        return;
    const float width = std::max(180.F, ImGui::GetContentRegionAvail().x);
    ControlsNote(running        ? "Active in this session. These controller settings apply locally."
                 : item.enabled ? "Enabled for the next game. Set up your local controls below."
                                : "This mod is off. Your controls will be ready when you enable it for a game.",
                 width);
    DrawLibraryAnalogueSettings(width);
}

void DrawSessionAnalogueCard(float width) {
    if (!dkr::runtime::netplay::session().active())
        return;
    const auto active = LibrarySessionAnalogue();
    if (!active)
        return;
    const auto library = g_code_library.view();
    for (const auto& item : library->items)
        if (item.digest == active->digest)
            return;
    PaddockBox box(std::min(width, 460.F), {18, 16});
    PaddockHeading(active->name, 22, 0xFFF0C2, box.Inner());
    PaddockGap(10.F);
    ControlsNote("Online session copy. Your offline library stays separate.", box.Inner());
    PaddockGap(10.F);
    if (PaddockButton("Details##session-analogue", PaddockButtonKind::Plain))
        g_library_analogue.session_requested = true;
    box.End(CupCardBackground);
    PaddockGap(18.F);
}

void DrawSessionAnalogueModal() {
    constexpr const char* name = "Analogue Acceleration & Braking###session-analogue-details";
    if (!g_library_analogue.session_requested && !ImGui::IsPopupOpen(name))
        return;
    const auto active = LibrarySessionAnalogue();
    if (g_library_analogue.session_requested) {
        g_library_analogue.session_requested = false;
        if (active)
            ImGui::OpenPopup(name);
    }
    if (!ImGui::IsPopupOpen(name))
        return;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(760.F, display.x - 32.F), std::min(960.F, display.y - 32.F)},
                             ImGuiCond_Appearing);
    if (!BeginPaddockModalWindow(name, ImGuiWindowFlags_NoSavedSettings))
        return;
    ImGui::BeginChild("Session analogue controls", {0, -64.F}, false);
    if (active) {
        PaddockHeading(active->name, 26, 0xFFCD67, ImGui::GetContentRegionAvail().x);
        PaddockModalText(
            "Online session copy. The host controls mod selection; your controller setup stays on this device.");
        DrawLibraryAnalogueSettings(std::max(180.F, ImGui::GetContentRegionAvail().x));
    } else
        PaddockModalText("This online mod is no longer active.");
    ImGui::EndChild();
    PaddockGap(12.F);
    if (PaddockButton("Close", PaddockButtonKind::Plain)) {
        LibraryCancelAnalogueCapture();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
