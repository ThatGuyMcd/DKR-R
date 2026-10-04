# Android mobile launcher and touch controls — preview.5

## Scope

This is an Android-only launcher/overlay presentation and input adapter. It
does not change the game's HUD projection, split-screen layout, simulation,
network protocol or renderer dependency sources. Desktop page implementations
and physical-controller mappings remain in place.

The project version remains `1.0.5-beta.11`; the Android preview is `.5`
(version code `1050015`). Install over the previous preview rather than
uninstalling it if you want to retain its private app data.

## Launcher and overlay

- Android density establishes logical UI coordinates, and Android's system
  font scale is respected up to 150%. Render data is converted back to the
  existing renderer's physical-coordinate contract after ImGui rendering.
- A Menu drawer replaces the desktop sidebar. Page content occupies the
  remaining width, with safe-area margins and a scrollable content region.
- Finger pans scroll the window under the initial contact, including nested
  scrolling regions. A short bounded fling follows release. Synthetic SDL
  touch-mouse events are discarded to avoid duplicate activation.
- A tap activates an existing control; horizontal motion supports sliders.
  Direct dragging is reserved for layout-editor control groups so that the
  editor's options sheet can also scroll.
- Online navigation and Controls navigation use compact selectors on Android.
- Modal bounds respect available safe-area dimensions. Existing text fields
  request Android's keyboard; existing controller keyboard UI is retained.
- Insets include system bars, cutouts and the software keyboard where the
  Android API reports them. Activity metrics are cached on the Android UI
  thread rather than querying Views from the rendering thread.

## N64-style controls

The controls provide a grey analog stick, blue A, green B, yellow C buttons,
D-pad, L/R/Z, red Start and a Menu button. Touch input enters the existing
physical-input capture path before the online input broker; it does not bypass
network synchronization.

Open **Controls → Touch Screen → Edit Touch Layout** to customize:

- Drag groups or use directional nudge buttons.
- Change group size (100–150%), opacity (25–100%) and visibility.
- Choose Classic or left-handed placement, Undo, Cancel or Save.
- Use Test controls for isolated visual feedback; this does not inject input
  into the game. Tap Menu to return to the editor.
- Wide and near-square displays retain separate layouts.

Global controls offer Automatic/Always on/Off visibility, floating analog
origin, deadzone and sensitivity. Automatic mode hides game controls after
physical-controller activity. Menu remains available even when controls are
hidden. C-button and D-pad groups use a compact arrangement on short displays.

The versioned `android-touch-layout-v1.txt` lives inside the existing config
directory. Values are bounded on load. Saving uses a temporary file and an
atomic same-directory replacement. Cancel restores both layouts as they were
when the editor opened. Overlapping groups generate a warning; the editor
does not silently rearrange user placements.

Finger IDs retain independent ownership. Buttons combine across fingers;
only the owning finger controls the stick. Focus loss, backgrounding, resizing,
overlay entry and queue overflow release touch state. The layout editor blocks
gameplay input while it is open.

## Validation and limitations

- Windows full DKR suite: **80/80 passed**.
- Linux ARM64 under QEMU: **79/80 passed**. The existing DirectSession timing
  assertion at `direct_session_tests.cpp:1302` (`realtime_attempts == 1U`)
  failed in that run. It was not weakened or skipped in the full suite.
- Final targeted policy/UI/graphics tests: **3/3 passed** on Windows and
  Linux ARM64 after the editor-scroll correction.
- Android native compilation succeeded. Packaging outcomes are recorded in
  `ARM-PLATFORM-VALIDATION.md`.
- Headless UI checks cover logical/physical coordinates, finger scrolling,
  cancellation, synthetic mouse suppression and one-time event normalization.
  Policy checks cover multi-touch ownership, axis direction, clamping, deadzone,
  safe bounds, presets and contact limits.
- No Android device was attached during implementation. Real-device layout,
  text sharpness, IME behavior, multi-touch feel and lifecycle behavior remain
  user acceptance checks. This is not a claim of verified phone performance.
- Controls are edited as groups, not arbitrary individual-button remapping.
  Existing complex page contents are reused; unusually narrow windows may
  still reveal page-specific layout issues requiring follow-up.

## Device acceptance checklist

1. Install preview.5 over preview.4. Check Play, Graphics, Controls, Mods and
   Online for readable text, reachable controls and finger scrolling.
2. Enter text, open/close the software keyboard and verify that modal buttons
   remain reachable. Test both the folded and unfolded phone.
3. Start a race; steer while holding A and using Z/R. Lift each finger
   independently, then background/resume and ensure nothing remains held.
4. Open Menu, change a layout, Cancel, edit again and Save. Relaunch to verify
   persistence. Test the left-handed preset and controller Auto visibility.
5. Check 2/3/4-player HUD positioning and physical-controller behavior remain
   unchanged. Exercise online touch input only after ordinary play is sound.

## Rollback

The preview.4 packages and source snapshots were preserved before this work in
`G:/DiddykongWorkFolder/arm-port-20260921/mobile-ui-baseline-20260922`.
Original packages also remain in `packages/android-preview4-final`. No ROMs,
saves, private profiles or mod banks are included in the new distributions.
