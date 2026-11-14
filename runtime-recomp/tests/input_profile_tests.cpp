#include "runtime_input.hpp"

#include <cassert>

int main() {
    using namespace dkr::runtime::input;
    constexpr int kPlayerOneSource = 7;
    constexpr int kPlayerTwoSource = 11;
    constexpr int kSecondarySource = 13;

    reset_defaults();
    set_controller_binding(0U, Action::A, kPlayerOneSource);
    set_controller_binding(1U, Action::A, kPlayerTwoSource);
    assert(controller_binding(0U, Action::A) == kPlayerOneSource);
    assert(controller_binding(1U, Action::A) == kPlayerTwoSource);
    assert(controller_binding(2U, Action::A) != kPlayerOneSource);

    copy_bindings(0U, 2U);
    assert(controller_binding(2U, Action::A) == kPlayerOneSource);
    set_controller_binding(2U, Action::B, kPlayerTwoSource);
    assert(controller_binding(0U, Action::B) != kPlayerTwoSource);

    // Each N64 action may have two controller sources. A physical source must
    // remain unique inside one player profile so one button cannot trigger two
    // unrelated N64 actions accidentally.
    set_secondary_controller_binding(0U, Action::A, kSecondarySource);
    assert(secondary_controller_binding(0U, Action::A) == kSecondarySource);
    set_secondary_controller_binding(0U, Action::B, kPlayerOneSource);
    assert(controller_binding(0U, Action::A) == kUnbound);
    assert(secondary_controller_binding(0U, Action::B) == kPlayerOneSource);
    set_controller_binding(0U, Action::Z, kSecondarySource);
    assert(secondary_controller_binding(0U, Action::A) == kUnbound);
    assert(controller_binding(0U, Action::Z) == kSecondarySource);

    // Global shortcuts are independent actions and support two-button chords.
    const ShortcutBinding overlay_shortcut{21, 22};
    const ShortcutBinding texture_shortcut{23, kUnbound};
    const ShortcutBinding fullscreen_shortcut{24, 25};
    const ShortcutBinding recenter_shortcut{26, 27};
    set_shortcut_controller_binding(ShortcutAction::ToggleOverlay, overlay_shortcut);
    set_shortcut_controller_binding(ShortcutAction::ToggleTexturePack, texture_shortcut);
    set_shortcut_controller_binding(ShortcutAction::ToggleFullscreen, fullscreen_shortcut);
    set_shortcut_controller_binding(ShortcutAction::RecenterGyro, recenter_shortcut);
    assert(shortcut_controller_binding(ShortcutAction::ToggleOverlay).primary == overlay_shortcut.primary);
    assert(shortcut_controller_binding(ShortcutAction::ToggleOverlay).secondary == overlay_shortcut.secondary);
    assert(shortcut_controller_binding(ShortcutAction::ToggleTexturePack).primary == texture_shortcut.primary);
    assert(shortcut_controller_binding(ShortcutAction::ToggleFullscreen).secondary == fullscreen_shortcut.secondary);
    assert(shortcut_controller_binding(ShortcutAction::RecenterGyro).primary == recenter_shortcut.primary);
    assert(shortcut_controller_binding(ShortcutAction::RecenterGyro).secondary == recenter_shortcut.secondary);

    // Vehicle inversion is deliberately independent. Inverting planes must
    // not alter car or hovercraft steering.
    set_vehicle_stick_x_inverted(VehicleClass::Car, false);
    set_vehicle_stick_x_inverted(VehicleClass::Hovercraft, false);
    set_vehicle_stick_x_inverted(VehicleClass::Plane, true);
    set_vehicle_stick_y_inverted(VehicleClass::Car, false);
    set_vehicle_stick_y_inverted(VehicleClass::Hovercraft, true);
    set_vehicle_stick_y_inverted(VehicleClass::Plane, true);
    assert(!vehicle_stick_x_inverted(VehicleClass::Car));
    assert(!vehicle_stick_x_inverted(VehicleClass::Hovercraft));
    assert(vehicle_stick_x_inverted(VehicleClass::Plane));
    assert(!vehicle_stick_y_inverted(VehicleClass::Car));
    assert(vehicle_stick_y_inverted(VehicleClass::Hovercraft));
    assert(vehicle_stick_y_inverted(VehicleClass::Plane));

    set_keyboard_player(3);
    assert(keyboard_player() == 3);
    set_keyboard_player(99);
    assert(keyboard_player() == 3);
    set_keyboard_player(-8);
    assert(keyboard_player() == 0);

    // Background controller input is an explicit, independent policy for
    // each selected N64 port. Enabling one controller must not silently make
    // the remaining local players active while DKR-R is unfocused.
    for (std::size_t player = 0U; player < kPlayerCount; ++player) {
        set_background_input_enabled(player, false);
        assert(!background_input_enabled(player));
    }
    set_background_input_enabled(2U, true);
    assert(!background_input_enabled(0U));
    assert(background_input_enabled(2U));
    assert(!background_input_enabled(3U));
    return 0;
}
