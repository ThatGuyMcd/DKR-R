void DrawAnalogueDrivingControls(float width) {
    namespace driving = dkr::runtime::driving;
    namespace input = dkr::runtime::input;
    const unsigned player = unsigned(g_selected_player);
    auto p = driving::profile(player);
    (void)dkr::runtime::platform::player_input_preview(player);
    PaddockGap(18.F);
    ControlsHeading("Analogue driving", width);
    ControlsNote("Requires the Analogue Acceleration & Braking gameplay mod. Each player has separate settings.",
                 width);
    if (SettingsCheck("analogue-driving-enabled", "Use analogue accelerator and brake", &p.enabled, width)) {
        driving::configure(player, p);
        SaveSettings();
    }
    PaddockGap(8.F);
    if (SettingsButton("Apply RT / LT driving layout", width)) {
        using A = input::Action;
        const int rt = input::encode_controller_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, true);
        const int lt = input::encode_controller_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, true);
        input::set_controller_binding(player, A::AnalogueAccelerator, rt);
        input::set_controller_binding(player, A::AnalogueBrake, lt);
        input::set_controller_binding(player, A::Z, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        if (input::controller_binding(player, A::L) == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            input::set_controller_binding(player, A::L, input::kUnbound);
        p.enabled = true;
        driving::configure(player, p);
        SaveSettings();
    }
    ControlsNote(
        "RT accelerates, LT brakes, LB uses items. The preset replaces the conflicting item/L bindings. Keyboard and face-button fallbacks stay available.",
        width);
    for (unsigned channel = 0; channel < 2; ++channel) {
        ImGui::PushID(int(channel));
        auto& c = channel ? p.brake : p.accelerator;
        const auto action = channel ? input::Action::AnalogueBrake : input::Action::AnalogueAccelerator;
        PaddockGap(12.F);
        ControlsHeading(channel ? "Brake" : "Accelerator", width);
        const auto binding = input::controller_binding_name(input::controller_binding(player, action));
        if (SettingsButton((binding + "##analogue-binding").c_str(), width))
            BeginControllerCapture(int(action), CaptureDevice::Controller, false);
        PaddockGap(6.F);
        if (input::keyboard_player() == int(player) && SettingsButton("Set keyboard fallback", width))
            BeginControllerCapture(int(action), CaptureDevice::Keyboard, false);
        const auto preview = driving::unpack(driving::preview[player].load());
        const float amount = driving::ratio(channel ? preview.brake : preview.accelerator);
        const std::string percent = std::to_string(int(std::lround(amount * 100))) + "%";
        ImGui::ProgressBar(amount, {width, 20.F}, percent.c_str());
        PaddockGap(6.F);
        bool changed = false;
        const auto percentage = [](int value) { return SettingsRangeValue{std::to_string(value), "%"}; };
        changed |= SettingsRange("release", "Release deadzone", &c.release, 0, 20, width, percentage);
        changed |= SettingsRange("full", "Full travel", &c.full, 50, 100, width, percentage);
        changed |= SettingsRange("curve", "Response curve", &c.curve, 50, 250, width, [](int v) {
            char t[16];
            std::snprintf(t, sizeof(t), "%.2f", double(v) / 100);
            return SettingsRangeValue{t, ""};
        });
        changed |= SettingsCheck("invert", "Invert axis", &c.inverted, width);
        if (changed) {
            driving::configure(player, p);
            SaveSettings();
        }
        ImGui::PopID();
    }
}
