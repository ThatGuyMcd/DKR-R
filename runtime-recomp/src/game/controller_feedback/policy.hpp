#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace dkr::feedback {
enum Capability : std::uint32_t {
    Rumble = 1,
    Led = 2,
    PlayerLed = 4,
    Adaptive = 8,
    Touch = 16,
    Motion = 32,
    Deck = 64,
    Waveform = 128
};
enum class Kind : std::uint8_t { Generic, DualSense, SteamDeck, SteamVirtual };
enum class Transport : std::uint8_t { Unknown, Usb, Bluetooth, Virtual };
constexpr bool racing_owner(bool online, unsigned slot, unsigned profile, unsigned player) {
    return player < 4 && (!online || (slot < 4 && profile == player));
}
enum class Cue : std::uint8_t { Impact, Boost, Landing, Skid, Engine, Menu, Pickup, Countdown, Finish, Count };
struct Settings {
    int mode = 1, strength = 100, balance = 0, impacts = 100, boost = 90, environment = 35, engine = 0, menus = 0;
    int resistance = 0, lights = 0, brightness = 35, deck_pattern = 0, waveform = 0, touch_action = 0;
    bool operator==(const Settings&) const = default;
};
inline Settings bounded(Settings s) {
    s.mode = std::clamp(s.mode, 0, 2);
    s.strength = std::clamp(s.strength, 0, 100);
    s.balance = std::clamp(s.balance, -100, 100);
    for (int* p : {&s.impacts, &s.boost, &s.environment, &s.engine, &s.menus, &s.brightness})
        *p = std::clamp(*p, 0, 100);
    s.resistance = std::clamp(s.resistance, 0, 3);
    s.lights = std::clamp(s.lights, 0, 2);
    s.deck_pattern = std::clamp(s.deck_pattern, 0, 1);
    s.waveform = std::clamp(s.waveform, 0, 1);
    s.touch_action = std::clamp(s.touch_action, 0, 3);
    return s;
}
struct Output {
    std::uint16_t left = 0, right = 0;
    std::uint8_t trigger_mask = 0, resistance = 0, led_mode = 0, brightness = 0, player = 0, pattern = 0, waveform = 0;
    std::uint8_t red = 0, green = 0, blue = 0;
    std::uint32_t audio_endpoint = 0;
    bool operator==(const Output&) const = default;
};
inline bool valid(Output o) {
    return o.trigger_mask <= 3 && o.resistance <= 3 && o.led_mode <= 2 && o.brightness <= 100 && o.player < 4 &&
           o.pattern <= 1 && o.waveform <= 1;
}
// SDL's effect payload starts after the transport header. SDL owns USB/BT
// framing and CRC. Only typed, bounded trigger/LED values cross our IPC.
inline std::array<std::uint8_t, 47> dualsense_effect(Output o, bool improved = true, bool restore_lights = false) {
    std::array<std::uint8_t, 47> b{};
    if (!valid(o))
        return b;
    b[0] = 0x0C; // right and left trigger fields, including an explicit off
    const auto trigger = [&](unsigned offset, unsigned mask) {
        if (!(o.trigger_mask & mask) || !o.resistance)
            return;
        b[offset] = 0x21; // ten-zone resistance, all enabled from zone 2
        b[offset + 1] = 0xFC;
        b[offset + 2] = 0x03;
        std::uint32_t strengths = 0;
        const unsigned force = unsigned(o.resistance); // progressive, at most 3 / 8
        for (unsigned i = 2; i < 10; ++i)
            strengths |= (std::max(1U, (force * (i - 1) + 7) / 8) - 1) << (3 * i);
        for (unsigned i = 0; i < 4; ++i)
            b[offset + 3 + i] = std::uint8_t(strengths >> (8 * i));
    };
    trigger(10, 2);
    trigger(21, 1);
    if (o.led_mode) {
        b[1] = 0x14;
        b[43] = std::uint8_t(1U << o.player);
        b[44] = std::uint8_t(unsigned(o.red) * o.brightness / 100);
        b[45] = std::uint8_t(unsigned(o.green) * o.brightness / 100);
        b[46] = std::uint8_t(unsigned(o.blue) * o.brightness / 100);
    } else if (restore_lights)
        b[1] = 0x18; // release only lights we previously owned
    if ((o.left || o.right) && !o.waveform) {
        b[0] |= 2;
        if (improved)
            b[38] |= 4;
        else
            b[0] |= 1;
        b[2] = std::uint8_t(o.right >> (improved ? 8 : 9));
        b[3] = std::uint8_t(o.left >> (improved ? 8 : 9));
    }
    return b;
}
struct Sample {
    std::uint32_t scene = 0, epoch = 0, frame = 0, generation = 0, flags = 0, vehicle = 0;
    float speed = 0, steer = 0;
    bool driving = false, grounded = false, boosting = false, impact = false, skidding = false, water = false,
         finished = false;
    unsigned surface = 255, bananas = 0, ammunition = 0;
    int countdown = 0;
};
struct Pulse {
    Cue cue = Cue::Impact;
    double start = 0, duration = 0;
    float strength = 0, direction = 0;
};
class Mixer {
    std::array<Pulse, 12> pulses_{};
    unsigned next_ = 0;
    Sample previous_{};
    bool have_ = false;

  public:
    void clear() {
        pulses_ = {};
        previous_ = {};
        have_ = false;
        next_ = 0;
    }
    void pulse(Cue cue, double now, float amount = 1, float direction = 0) {
        if (!std::isfinite(amount) || !std::isfinite(direction) || !std::isfinite(now))
            return;
        const double duration = cue == Cue::Finish    ? .45
                                : cue == Cue::Boost   ? .34
                                : cue == Cue::Landing ? .18
                                : cue == Cue::Menu    ? .035
                                : cue == Cue::Pickup  ? .06
                                                      : .13;
        pulses_[next_++ % pulses_.size()] = {cue, now, duration, std::clamp(amount, 0.F, 1.F),
                                             std::clamp(direction, -1.F, 1.F)};
    }
    bool observe(Sample s, double now) {
        if (!s.driving || !std::isfinite(s.speed) || !std::isfinite(s.steer)) {
            clear();
            return false;
        }
        if (!have_ || s.scene != previous_.scene || s.epoch != previous_.epoch ||
            s.generation != previous_.generation) {
            clear();
            previous_ = s;
            have_ = true;
            return false;
        }
        if (s.frame <= previous_.frame)
            return false;
        if (s.boosting && !previous_.boosting)
            pulse(Cue::Boost, now);
        if (s.grounded && !previous_.grounded)
            pulse(Cue::Landing, now, .75F);
        if (s.impact && !previous_.impact)
            pulse(Cue::Impact, now); // no guessed impact direction
        if (s.bananas > previous_.bananas || s.ammunition > previous_.ammunition)
            pulse(Cue::Pickup, now, .2F);
        if (s.ammunition < previous_.ammunition)
            pulse(Cue::Pickup, now, .45F);
        if (s.countdown >= 0 && previous_.countdown > 0 && s.countdown / 30 != previous_.countdown / 30)
            pulse(Cue::Countdown, now, s.countdown == 0 ? .65F : .3F);
        if (s.finished && !previous_.finished)
            pulse(Cue::Finish, now, .7F);
        previous_ = s;
        return true;
    }
    Output mix(Settings settings, double now, bool classic, bool allowed) const {
        Output out{};
        const auto s = bounded(settings);
        if (!allowed || !s.mode)
            return out;
        float l = classic ? 1.F : 0.F, r = l;
        if (s.mode == 2) {
            l = classic ? .7F : 0;
            r = l;
            float background = 0;
            if (have_ && previous_.driving) {
                background = std::min(std::abs(previous_.speed) / 1500.F, .15F) * s.engine * .01F;
                if (previous_.skidding)
                    background += .15F * s.environment * .01F;
                if (previous_.water)
                    background += .09F * s.environment * .01F;
                else if (previous_.grounded && std::abs(previous_.speed) > 20 &&
                         (previous_.surface == 1 || previous_.surface == 2 || previous_.surface == 4 ||
                          previous_.surface == 13))
                    background += .12F * s.environment * .01F;
                if (previous_.finished)
                    background = 0;
                background *= float(.65 + .35 * std::sin(now * (previous_.vehicle == 1   ? 24
                                                                : previous_.vehicle == 2 ? 80
                                                                                         : 55)));
            }
            float peak = std::max(l, r);
            for (const auto& p : pulses_) {
                const double age = now - p.start;
                if (p.duration <= 0 || age < 0 || age >= p.duration)
                    continue;
                const float level = (p.cue == Cue::Boost ? s.boost : p.cue == Cue::Menu ? s.menus : s.impacts) * .01F;
                const float envelope = float(std::min(age / .008, 1.) * std::pow(1. - age / p.duration, 1.5));
                const float v = level * p.strength * envelope;
                l = std::max(l, v * (p.direction > 0 ? 1 - p.direction : 1));
                r = std::max(r, v * (p.direction < 0 ? 1 + p.direction : 1));
                peak = std::max(peak, v);
            }
            const float duck = 1 - std::clamp(peak, 0.F, 1.F);
            l = std::max(l, background * duck);
            r = std::max(r, background * duck);
        }
        const float gain = s.strength * .01F;
        out.left = std::uint16_t(
            std::lround(std::clamp(l * gain * (s.balance > 0 ? 1 - s.balance * .01F : 1), 0.F, 1.F) * 65535));
        out.right = std::uint16_t(
            std::lround(std::clamp(r * gain * (s.balance < 0 ? 1 + s.balance * .01F : 1), 0.F, 1.F) * 65535));
        return out;
    }
};
} // namespace dkr::feedback
