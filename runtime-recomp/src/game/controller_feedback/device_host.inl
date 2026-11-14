// All SDL3 feedback I/O stays on the private helper's owner thread.
using dkr::feedback::Output;
using dkr::feedback::Kind;
using dkr::feedback::Transport;
Kind FeedbackKind(const HostDevice& d) {
    if (d.vendor == 0x054C && (d.product == 0x0CE6 || d.product == 0x0DF2))
        return Kind::DualSense;
    if (d.vendor == kValveVendorId && d.product == kSteamDeckProductId)
        return Kind::SteamDeck;
    if (d.vendor == kValveVendorId && d.product == kSteamVirtualGamepadProductId)
        return Kind::SteamVirtual;
    return Kind::Generic;
}
Transport FeedbackTransport(SDL_Gamepad* g) {
    const auto c = SDL_GetGamepadConnectionState(g);
    return c == SDL_JOYSTICK_CONNECTION_WIRED      ? Transport::Usb
           : c == SDL_JOYSTICK_CONNECTION_WIRELESS ? Transport::Bluetooth
                                                   : Transport::Unknown;
}
void FeedbackCloseAudio(HostDevice& d) {
    if (d.feedback_audio) {
        SDL_DestroyAudioStream(d.feedback_audio);
        d.feedback_audio = nullptr;
    }
}
bool FeedbackDeckPattern(HostDevice& d, Output o) {
    if (FeedbackKind(d) != Kind::SteamDeck)
        return false;
    if (SDL_GetTicks() < d.pattern_retry) {
        SDL_SetError("Native Deck pattern output is cooling down after a rejected command.");
        return false;
    }
    if (!d.feedback_hid) {
        auto* list = SDL_hid_enumerate(kValveVendorId, kSteamDeckProductId);
        const char* path = nullptr;
        unsigned matches = 0;
        for (auto* c = list; c; c = c->next)
            if (c->interface_number == kSteamDeckControllerInterface) {
                path = c->path;
                ++matches;
            }
        if (matches == 1)
            d.feedback_hid = SDL_hid_open_path(path);
        SDL_hid_free_enumeration(list);
        if (!d.feedback_hid) {
            d.pattern_retry = SDL_GetTicks() + 2000;
            return false;
        }
    }
    bool ok = true;
    for (unsigned side = 1; side <= 2; ++side) {
        const auto strength = side == 1 ? o.left : o.right;
        const auto b = dkr::feedback::deck_effect(side, strength);
        ok = (SDL_hid_send_feature_report(d.feedback_hid, b.data(), b.size()) == int(b.size())) && ok;
    }
    if (!ok)
        d.pattern_retry = SDL_GetTicks() + 2000;
    return ok;
}
void FeedbackAudioCandidates(DevicePacket& p) {
    if (p.kind != Kind::DualSense || p.transport != Transport::Usb)
        return;
    static DevicePacket cached{};
    static Uint64 next = 0;
    const auto now = SDL_GetTicks();
    if (now < next) {
        p.audio_count = cached.audio_count;
        p.audio_ids = cached.audio_ids;
        p.audio_names = cached.audio_names;
        return;
    }
    next = now + 1000;
    int count = 0;
    auto* ids = SDL_GetAudioPlaybackDevices(&count);
    for (int i = 0; ids && i < count && p.audio_count < 4; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        const auto text = Lowercase(name ? name : "");
        SDL_AudioSpec spec{};
        if ((text.find("wireless controller") == text.npos && text.find("dualsense") == text.npos) ||
            !SDL_GetAudioDeviceFormat(ids[i], &spec, nullptr) || spec.channels != 4)
            continue;
        const auto n = p.audio_count++;
        p.audio_ids[n] = ids[i];
        std::memcpy(p.audio_names[n].data(), name, std::min(std::strlen(name), p.audio_names[n].size() - 1));
    }
    SDL_free(ids);
    cached = p;
}
bool FeedbackOpenAudio(HostDevice& d, Output o) {
    if (SDL_GetTicks() < d.audio_retry && d.failed_audio_endpoint == o.audio_endpoint) {
        SDL_SetError("Controller actuator audio is cooling down after a failed open.");
        return false;
    }
    DevicePacket p{};
    p.kind = FeedbackKind(d);
    p.transport = FeedbackTransport(d.gamepad);
    FeedbackAudioCandidates(p);
    bool found = false;
    for (unsigned i = 0; i < p.audio_count; ++i)
        found |= p.audio_ids[i] == o.audio_endpoint;
    if (!found) {
        SDL_SetError("Select and test a four-channel DualSense USB audio endpoint.");
        return false;
    }
    if (d.feedback_audio && d.feedback_output.audio_endpoint == o.audio_endpoint)
        return true;
    FeedbackCloseAudio(d);
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 4, 48000};
    d.feedback_audio = SDL_OpenAudioDeviceStream(o.audio_endpoint, &spec, nullptr, nullptr);
    if (!d.feedback_audio) {
        d.audio_retry = SDL_GetTicks() + 5000;
        d.failed_audio_endpoint = o.audio_endpoint;
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(d.feedback_audio)) {
        FeedbackCloseAudio(d);
        return false;
    }
    return true;
}
void FeedbackReset(HostDevice& d) {
    FeedbackCloseAudio(d);
    bool retry = false;
    if (d.gamepad && d.feedback_owned) {
        if (d.advanced_owned) {
            const auto b = dkr::feedback::dualsense_effect({}, true, d.lights_owned);
            retry = !SDL_SendGamepadEffect(d.gamepad, b.data(), int(b.size()));
            if (!retry)
                d.lights_owned = false;
        }
        SDL_RumbleGamepad(d.gamepad, 0, 0, 0);
        if (d.feedback_hid)
            FeedbackDeckPattern(d, {});
    }
    d.feedback_output = {};
    d.feedback_owned = d.advanced_owned = retry;
    d.feedback_until = retry ? SDL_GetTicks() + 100 : 0;
    if (retry) {
        ++d.feedback_failures;
        d.feedback_result = -1;
        d.feedback_detail = "Controller reset was rejected; retrying while connected.";
    }
}
void FeedbackClose(HostDevice& d) {
    FeedbackReset(d);
    if (d.feedback_hid) {
        SDL_hid_close(d.feedback_hid);
        d.feedback_hid = nullptr;
    }
}
bool FeedbackApply(HostDevice& d, Output o, std::uint64_t sequence, unsigned lease) {
    if (!dkr::feedback::valid(o) || lease < 20 || lease > 350)
        return false;
    bool ok = true;
    std::string detail;
    const bool advanced =
        FeedbackKind(d) == Kind::DualSense && (o.trigger_mask || o.led_mode || o.waveform || d.advanced_owned);
    if (o.pattern && !FeedbackDeckPattern(d, o)) {
        ok = false;
        detail = "Native Deck pattern unavailable; using ordinary rumble.";
        o.pattern = 0;
    }
    if (o.waveform) {
        if (FeedbackKind(d) != Kind::DualSense || !FeedbackOpenAudio(d, o)) {
            ok = false;
            detail = SDL_GetError();
            o.waveform = 0;
            FeedbackCloseAudio(d);
        }
    } else
        FeedbackCloseAudio(d);
    if (advanced) {
        const auto bytes = dkr::feedback::dualsense_effect(o, SDL_GetGamepadFirmwareVersion(d.gamepad) >= 0x0224,
                                                           d.lights_owned && !o.led_mode);
        const bool result = SDL_SendGamepadEffect(d.gamepad, bytes.data(), int(bytes.size()));
        if (!result) {
            detail = SDL_GetError();
            o.trigger_mask = o.waveform = 0;
            FeedbackCloseAudio(d);
        }
        if (result)
            d.lights_owned = o.led_mode != 0;
        ok = result && ok;
        d.advanced_owned = d.advanced_owned || result;
    } else if (!o.pattern)
        ok = SDL_RumbleGamepad(d.gamepad, o.left, o.right, (o.left || o.right) ? lease : 0) && ok;
    if (!ok && !o.pattern && !o.waveform)
        SDL_RumbleGamepad(d.gamepad, o.left, o.right, (o.left || o.right) ? lease : 0);
    d.feedback_owned = true;
    d.feedback_output = o;
    d.feedback_until = SDL_GetTicks() + lease;
    d.feedback_ack = sequence;
    d.feedback_result = ok ? 1 : -1;
    if (!ok)
        ++d.feedback_failures;
    d.feedback_detail = ok ? "Hardware API accepted the command." : detail.empty() ? SDL_GetError() : detail;
    return ok;
}
void FeedbackPump(HostDevice& d) {
    if (d.feedback_owned && SDL_GetTicks() >= d.feedback_until) {
        FeedbackReset(d);
        return;
    }
    if (!d.feedback_audio)
        return;
    // At most 40 ms queued; normal audio/speaker channels remain silent here.
    if (SDL_GetAudioStreamQueued(d.feedback_audio) > 48000 * 4 * int(sizeof(float)) / 50)
        return;
    const auto data = dkr::feedback::waveform_block(d.feedback_output, d.feedback_phase);
    if (!SDL_PutAudioStreamData(d.feedback_audio, data.data(), int(sizeof(data)))) {
        ++d.feedback_failures;
        d.feedback_result = -1;
        d.feedback_detail = SDL_GetError();
        FeedbackReset(d);
    }
}
void FeedbackFill(DevicePacket& p, const HostDevice& d) {
    using namespace dkr::feedback;
    p.kind = FeedbackKind(d);
    p.transport = p.kind == Kind::SteamVirtual ? Transport::Virtual : FeedbackTransport(d.gamepad);
    p.vendor = d.vendor;
    p.product = d.product;
    p.firmware = SDL_GetGamepadFirmwareVersion(d.gamepad);
    p.feedback_capabilities = d.rumble ? Rumble : 0;
    const auto properties = SDL_GetGamepadProperties(d.gamepad);
    if (SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false))
        p.feedback_capabilities |= Led;
    if (SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN, false))
        p.feedback_capabilities |= PlayerLed;
    if (p.kind == Kind::DualSense && (p.feedback_capabilities & Led))
        p.feedback_capabilities |= Adaptive;
    if (p.kind == Kind::SteamDeck)
        p.feedback_capabilities |= Deck;
    if (d.gyro)
        p.feedback_capabilities |= Motion;
    if (SDL_GetNumGamepadTouchpads(d.gamepad) > 0)
        p.feedback_capabilities |= Touch;
    p.feedback_ack = d.feedback_ack;
    p.feedback_failures = d.feedback_failures;
    p.feedback_result = d.feedback_result;
    std::memcpy(p.feedback_detail.data(), d.feedback_detail.data(),
                std::min(d.feedback_detail.size(), p.feedback_detail.size() - 1));
    int percentage = -1;
    SDL_GetGamepadPowerInfo(d.gamepad, &percentage);
    p.battery = std::int16_t(std::clamp(percentage, -1, 100));
    for (int touch = 0; touch < 2; ++touch) {
        const int pad = p.kind == Kind::DualSense ? 0 : touch, finger = p.kind == Kind::DualSense ? touch : 0;
        bool down = false;
        float x = 0, y = 0, pressure = 0;
        if (SDL_GetGamepadTouchpadFinger(d.gamepad, pad, finger, &down, &x, &y, &pressure)) {
            p.touch_down[touch] = down ? 1 : 0;
            p.touch[touch * 3] = x;
            p.touch[touch * 3 + 1] = y;
            p.touch[touch * 3 + 2] = pressure;
        }
    }
    FeedbackAudioCandidates(p);
    if (p.audio_count)
        p.feedback_capabilities |= Waveform;
}

struct FeedbackVirtualProbe {
    unsigned effects = 0, rumble = 0;
    std::array<std::uint8_t, 47> last{};
};
bool FeedbackVirtualSelfTest() {
    FeedbackVirtualProbe probe;
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x054C;
    desc.product_id = 0x0CE6;
    desc.naxes = 6;
    desc.nbuttons = 21;
    desc.button_mask = (1U << 21) - 1;
    desc.axis_mask = (1U << 6) - 1;
    desc.name = "DKR-R feedback self-test (virtual)";
    desc.userdata = &probe;
    desc.Rumble = [](void* p, Uint16, Uint16) {
        ++static_cast<FeedbackVirtualProbe*>(p)->rumble;
        return true;
    };
    desc.SetLED = [](void*, Uint8, Uint8, Uint8) { return true; };
    desc.SendEffect = [](void* p, const void* data, int size) {
        auto& v = *static_cast<FeedbackVirtualProbe*>(p);
        if (size != 47)
            return false;
        ++v.effects;
        std::memcpy(v.last.data(), data, 47);
        return true;
    };
    const auto id = SDL_AttachVirtualJoystick(&desc);
    if (!id)
        return false;
    HostDevice d{};
    d.gamepad = SDL_OpenGamepad(id);
    d.vendor = desc.vendor_id;
    d.product = desc.product_id;
    bool ok = d.gamepad != nullptr;
    if (ok) {
        Output output{};
        output.left = 40000;
        output.right = 20000;
        output.trigger_mask = 3;
        output.resistance = 2;
        ok = FeedbackApply(d, output, 5, 100) && d.feedback_ack == 5 && d.feedback_result == 1 && probe.effects > 0 &&
             probe.last[10] == 0x21 && probe.last[21] == 0x21;
        d.feedback_until = 0;
        FeedbackPump(d);
        ok = ok && !d.feedback_owned && probe.last[10] == 0 && probe.last[21] == 0;
        FeedbackClose(d);
        SDL_CloseGamepad(d.gamepad);
    }
    SDL_DetachVirtualJoystick(id);
    std::fprintf(stderr, "[feedback][self-test] typed effects, hardware acknowledgement and lease reset=%s\n",
                 ok ? "passed" : "FAILED");
    return ok;
}
