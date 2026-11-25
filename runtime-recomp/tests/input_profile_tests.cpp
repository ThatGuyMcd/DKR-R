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

    // A saved table is installed verbatim: the defaults share a source between
    // actions that are never live together, and a reload must keep both.
    restore_controller_binding(0U, Action::CUp, kPlayerTwoSource);
    restore_controller_binding(0U, Action::OrbitUp, kPlayerTwoSource);
    assert(controller_binding(0U, Action::CUp) == kPlayerTwoSource);
    assert(controller_binding(0U, Action::OrbitUp) == kPlayerTwoSource);
    restore_secondary_controller_binding(0U, Action::Z, kPlayerTwoSource);
    assert(controller_binding(0U, Action::CUp) == kPlayerTwoSource);
    assert(secondary_controller_binding(0U, Action::Z) == kPlayerTwoSource);
    assert(controller_binding(1U, Action::CUp) != kPlayerTwoSource);
    // Picking a source in the UI still moves it off the action that held it.
    set_controller_binding(0U, Action::CDown, kPlayerTwoSource);
    assert(controller_binding(0U, Action::CUp) == kUnbound);
    assert(controller_binding(0U, Action::OrbitUp) == kUnbound);
    assert(secondary_controller_binding(0U, Action::Z) == kUnbound);

    // The v1.0.14 and v1.0.15 reload left the earlier action of each shared
    // default unbound. The repair puts it back, one player at a time, and
    // only while the damage is exactly what that reload left.
    reset_defaults();
    const int z_default = controller_binding(0U, Action::Z);
    const int c_left_default = controller_binding(0U, Action::CLeft);
    const auto unbind = [](std::size_t player) {
        for (const Action action :
             {Action::Z, Action::Accelerate, Action::CUp, Action::CDown, Action::CLeft, Action::CRight}) {
            restore_controller_binding(player, action, kUnbound);
        }
    };
    unbind(0U);
    unbind(1U);
    assert(repair_reload_unbound_defaults(0U, false) == 6U);
    assert(controller_binding(0U, Action::Z) == z_default);
    assert(controller_binding(0U, Action::CLeft) == c_left_default);
    assert(controller_binding(1U, Action::Z) == kUnbound);
    assert(repair_reload_unbound_defaults(0U, false) == 0U);
    // An analogue driving layout owns the triggers: Z and Accelerate stay.
    assert(repair_reload_unbound_defaults(1U, true) == 4U);
    assert(controller_binding(1U, Action::Z) == kUnbound);
    assert(controller_binding(1U, Action::Accelerate) == kUnbound);
    assert(controller_binding(1U, Action::CLeft) == c_left_default);
    // A second source on the action, or a partner moved off the shared
    // source, is something the player did. Those two are left alone.
    unbind(2U);
    restore_secondary_controller_binding(2U, Action::Z, kSecondarySource);
    restore_controller_binding(2U, Action::OrbitUp, kPlayerOneSource);
    assert(repair_reload_unbound_defaults(2U, false) == 4U);
    assert(controller_binding(2U, Action::Z) == kUnbound);
    assert(controller_binding(2U, Action::CUp) == kUnbound);
    assert(controller_binding(2U, Action::Accelerate) == controller_binding(0U, Action::Accelerate));
    reset_defaults();

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
