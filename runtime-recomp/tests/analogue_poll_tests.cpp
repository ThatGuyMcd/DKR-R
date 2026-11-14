#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "runtime_input.hpp"
#include "runtime_enhancements.hpp"
#include "analog_driving/settings.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
// This fixture selects Modern input shaping without starting a renderer.
bool dkr::runtime::enhancements::modern_presentation_enabled() {
    return true;
}
int main() {
    namespace in = dkr::runtime::input;
    namespace drive = dkr::runtime::driving;
    using A = in::Action;
    assert(SDL_Init(0) == 0);
    in::reset_defaults();
    const int rt = in::encode_controller_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, true);
    // The default Modern-camera accelerator also uses RT. That shared
    // source must not turn a partly pressed analogue trigger into full A.
    assert(in::controller_binding(0, A::AnalogueAccelerator) == rt);
    drive::Profile p;
    p.enabled = true;
    drive::configure(0, p);
    dkr::runtime::controllers::ControllerSnapshot pad{};
    pad.connected = true;
    for (unsigned n : {0U, 64U, 128U, 192U, 255U}) {
        const float raw = .03F + .97F * float(n) / 255;
        pad.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = std::int16_t(std::lround(raw * 32767));
        pad.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = std::int16_t(std::lround((.03F + .97F * .5F) * 32767));
        const auto s = in::poll_snapshot(0, &pad, nullptr, false, false, false, false);
        if (std::abs(int(s.accelerator) - int(n)) > 1 || std::abs(int(s.brake) - 128) > 1 || s.driving_flags != 1) {
            std::cerr << "Travel " << n << ": accelerator=" << unsigned(s.accelerator) << " brake=" << unsigned(s.brake)
                      << " flags=" << unsigned(s.driving_flags) << '\n';
            return 1;
        }
        assert(!(s.buttons & 0x2000)); // LT's old item binding cannot also fire.
        assert(bool(s.buttons & 0x8000) == (n != 0));
    }
    pad.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 16384;
    pad.buttons[SDL_CONTROLLER_BUTTON_A] = 1;
    assert(in::poll_snapshot(0, &pad, nullptr, false, false, false, false).accelerator == 255);
    auto blocked = in::poll_snapshot(0, &pad, nullptr, false, true, false, false);
    assert(!blocked.buttons && !blocked.accelerator && !blocked.brake && !blocked.driving_flags);
    assert(drive::unpack(drive::preview[0].load()).accelerator == 255); // setup remains live
    pad.connected = false;
    auto disconnected = in::poll_snapshot(0, &pad, nullptr, false, false, false, false);
    assert(!disconnected.accelerator && !disconnected.brake && !(disconnected.buttons & 0xC000));
    pad.connected = true;
    pad.buttons = {};
    p.accelerator.inverted = true;
    drive::configure(0, p);
    in::set_controller_binding(0, A::AnalogueAccelerator, SDL_CONTROLLER_BUTTON_A);
    pad.buttons[SDL_CONTROLLER_BUTTON_A] = 1;
    assert(in::poll_snapshot(0, &pad, nullptr, false, false, false, false).accelerator == 255);
    pad.buttons = {};
    pad.axes = {};
    assert(!in::poll_snapshot(0, &pad, nullptr, false, false, false, false).accelerator);
    SDL_Quit();
    std::cout << "Actual SDL snapshot polling preserves partial, combined, digital, blocked and disconnected input.\n";
}
