// Included after platform input owners are declared. No renderer/guest I/O.
namespace {
void FeedbackStart() {
    static const bool once = [] {
        dkr::feedback::service().sink([](const dkr::feedback::Device& d, dkr::feedback::Output o) {
#if DKR_RUNTIME_HAS_RT64
            if (g_input_backend_switch_in_progress.load())
                return false;
            if (d.native)
                return g_sdl3_input_client.feedback(d.instance, o, d.key, d.generation);
            // Own a separate SDL reference on the output worker. Never borrow a
            // controller pointer that the input owner can concurrently close.
            SDL_GameController* pad = nullptr;
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                if (SDL_JoystickGetDeviceInstanceID(i) == d.instance) {
                    pad = SDL_GameControllerOpen(i);
                    break;
                }
            if (!pad)
                return false;
            const bool ok = SDL_GameControllerRumble(pad, o.left, o.right, (o.left || o.right) ? 250 : 0) == 0;
            SDL_GameControllerClose(pad);
            return ok;
#else
            (void)d;
            (void)o;
            return false;
#endif
        });
        return true;
    }();
    (void)once;
}
#if DKR_RUNTIME_HAS_RT64
void FeedbackPublishInput(unsigned player, bool native, bool allowed) {
    namespace f = dkr::feedback;
    f::Device device{};
    device.native = native;
    device.allowed = allowed && g_rumble_enabled.load() &&
                     f::racing_owner(g_online_input_routing.load(), g_online_local_slot.load(),
                                     unsigned(g_online_input_profile.load()), player);
    device.gain = g_rumble_enabled.load() ? g_rumble_strength.load() : 0.F;
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    if (player == 0) {
        const auto v = dkr::runtime::netplay::experimental::runtime_view();
        f::service().confirm(std::uint32_t(v.epoch), v.confirmed);
    }
#endif
    if (native) {
        const auto* source = Sdl3DeviceForInstance(g_sdl3_player_instances[player]);
        if (source) {
            const auto& p = source->feedback;
            device.instance = source->instance;
            device.name = source->name;
            device.key = source->persistent_key;
            device.generation = g_sdl3_input_state.generation;
            device.capabilities = p.feedback_capabilities;
            device.kind = p.kind;
            device.transport = p.transport;
            device.battery = p.battery;
            device.vendor = p.vendor;
            device.product = p.product;
            device.firmware = p.firmware;
            device.acknowledged = p.feedback_ack;
            device.failures = p.feedback_failures;
            device.result = p.feedback_result;
            device.detail = std::string(p.feedback_detail.begin(),
                                        std::find(p.feedback_detail.begin(), p.feedback_detail.end(), '\0'));
            device.route = g_sdl3_input_client.detail();
            for (unsigned i = 0; i < std::min(p.audio_count, 4U); ++i)
                device.audio.emplace_back(p.audio_ids[i], std::string(p.audio_names[i].data()));
        }
    } else if (auto* pad = g_controllers[player]) {
        device.instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
        const auto* name = SDL_GameControllerName(pad);
        device.name = name ? name : "Controller";
        device.capabilities = SDL_GameControllerHasRumble(pad) ? f::Rumble : 0;
        device.route = "SDL2 compatibility: ordinary rumble; advanced features require SDL3.";
    }
    // The digital pedal keybinds do not imply analogue trigger ownership.
    const auto accelerator =
        dkr::runtime::input::controller_binding(player, dkr::runtime::input::Action::AnalogueAccelerator);
    const auto brake = dkr::runtime::input::controller_binding(player, dkr::runtime::input::Action::AnalogueBrake);
    const auto pedals = dkr::runtime::driving::unpack(dkr::runtime::driving::physical[player].load());
    device.pedals = (pedals.flags & dkr::runtime::driving::Enabled) != 0;
    for (const int binding :
         {accelerator, brake,
          dkr::runtime::input::secondary_controller_binding(player, dkr::runtime::input::Action::AnalogueAccelerator),
          dkr::runtime::input::secondary_controller_binding(player, dkr::runtime::input::Action::AnalogueBrake)}) {
        if (binding == 1009)
            device.pedal_mask |= 1;
        if (binding == 1011)
            device.pedal_mask |= 2;
    }
    f::service().input(player, std::move(device));
}
void FeedbackTouch(unsigned player, dkr::runtime::input::State& state, bool allowed) {
    static std::array<bool, 4> previous{};
    const auto* device = Sdl3DeviceForInstance(g_sdl3_player_instances[player]);
    const auto setting = dkr::feedback::service().status(player).settings.touch_action;
    const bool one = device && device->feedback.touch_down[0], two = one && device->feedback.touch_down[1];
    if (allowed && one && setting == 1)
        state.camera_actions |= 1U;
    if (allowed && two && !previous[player] && setting == 2)
        dkr::feedback::overlay_requested = true;
    previous[player] = two;
}
#endif
} // namespace
