# DualSense and Steam Deck feedback plan

This records the approved haptics plan following the Mods/Hacks crash repair.
The implementation and its remaining physical-device qualification are tracked
in [Controller feedback](../CONTROLLER_FEEDBACK.md). The native Deck patterns
and USB waveform routes are opt-in prototypes; hardware support is not inferred
from passing automated tests.

The primary test setup is an LCD Steam Deck, launched through Steam/Big Picture
and from Desktop Mode, and a DualSense tested over both USB and Bluetooth.
The design also keeps ordinary Xbox-style controllers working.

**What caused the launcher crash**

The supplied runtime log records an access violation in
`ImGui::FindRenderedTextEnd`, called from `DrawModsHacks` at the section-tab
width calculation. Moving Mod Library left four labels in a drawing loop that
still indexed five. The fifth read supplied an invalid string pointer. This
was introduced in the previous UI change; the startup and Mod Library overlay
checks did not open Mods/Hacks and missed it.

The repair shares the four labels through `library_ui/policy.hpp` and uses the
array's size for the drawing loop. Separate explicitly armed, isolated-profile
checks visit the collapsed state and all four sections in the real native
launcher and in-game overlay. Normal startup does not arm these checks.

**What DKR-R currently does**

These findings come from the first-party source and the actual selected
Patch Pipeline outputs, rather than just the base source capsule:

| Existing code | Finding | Implication |
|---|---|---|
| `runtime_platform.cpp`, `set_rumble` | Original Rumble Pak on/off becomes the same low- and high-frequency amplitude. At 100%, each is 65535. | The normal amplitude already reaches the API maximum. Raising a slider above 100% is not a diagnosis or a guaranteed improvement. There is no distinction between road texture, engine, boost and impacts here. |
| `runtime_platform.cpp`, `identify_controller` | Identification sends 32767 to both channels for 450 ms. | This is roughly a half-scale test, not a test of the 100% game-rumble setting. |
| `runtime_platform.cpp`, `ShouldTrySdl3Input` | Automatic selection already recognises Deck hosts. | Preserve that detection and the existing SDL2 compatibility fallback. Do not assume every Deck session is actually using the intended route. |
| `src/input/sdl3_input_host.cpp` | The native helper enables HIDAPI and the Deck driver and already has a Deck gyro fallback. | Extend this owner instead of adding a second controller manager or disrupting gyro support. |
| `sdl3_input_protocol.hpp` | Protocol version 1 carries devices, standard buttons/axes, gyro and a basic Rumble command. | The IPC needs typed capabilities, effect commands and results for advanced features. A successful socket send currently does not prove the hardware accepted an effect. |
| SDL2 output and SDL3 helper output | Normal rumble paths do not surface the SDL output-call failure to the player. | A missing or rejected effect can appear to work while feeling weak or absent. |
| Current input-host dependency | The checked SDL3 source is 3.4.14 at commit `147a8ee32dbf9ac02f3794964490687b6bbda1bc`. The supplied Windows runtime log reports SDL2 2.26.3. | The helper and game runtime are separate SDL versions. The startup line saying the SDL3 backend is "not reported" is not an adequate capability report. |
| `online_input_policy.hpp` and `external_side_effects_allowed` | Online rumble is routed from the local racer slot to the chosen physical input profile; replay side effects are guarded. | Keep both protections. Feedback must not play on another local controller, or repeat whenever rollback replays a collision. |
| `modding/include/dkr/presentation.h` | A view snapshot already describes speed, vehicle, velocity, steer and boost. | These are useful candidates for feedback, but we still need to publish them through an explicitly owned, thread-safe presentation snapshot. |
| `modding/include/dkr/game.h` and `runtime_telemetry.hpp` | Racer queries expose some race state; performance telemetry is timing information. Neither is a complete surface/impact haptics event stream. | Do not claim that every effect can be obtained from an existing ready-made telemetry feed. Surface and collision cues need a separate audit and possibly a small observer. |

**Why the Steam Deck feels different**

Valve documents the LCD Deck's haptic trackpads and its conversion of game
rumble into haptics. Feeling feedback around the trackpads is therefore
expected. It is not evidence, by itself, that DKR-R has overlooked another pair
of conventional grip rumble motors. We cannot promise Xbox-style whole-device
shake by changing a setting. See [Valve's LCD Deck specifications](https://www.steamdeck.com/en/tech/deck)
and [its rumble API notes](https://partner.steamgames.com/doc/api/ISteamInput#TriggerVibration).
The LCD [iFixit teardown](https://www.ifixit.com/News/57101/steam-deck-teardown)
also shows the small voice-coil haptic hardware at each trackpad.

There can still be a software problem. Candidates to measure are the active
physical/virtual device, Steam's own intensity settings, output-call failures,
the SDL backend selected, repeated start/stop timing and the waveform produced
by rumble emulation. These are hypotheses until we have a short output test
from the LCD Deck. The Windows crash log contains GameCube controllers and
cannot diagnose the Deck's physical output.

The goal is clear, distinguishable, correctly routed feedback at the strongest
comfortable level the hardware and the user's system settings allow. Changing
envelopes and contrast can improve perceived strength even when amplitude is
already at its maximum. It is not a way to create extra hardware power.

**What each device can contribute**

The hardware inventory is broader than the first implementation. "Later" below
means a separate validation gate, not a feature to advertise as already supported.

| Device feature | Proposed use in DKR-R | Support and priority |
|---|---|---|
| DualSense left/right haptic actuators | Distinct impacts, boost pulses, landing thumps and lighter environmental texture; directional balance when event direction is known. | First deliver timed/enveloped rumble. Full waveform haptics is a later USB audio investigation. |
| DualSense adaptive L2/R2 | Light accelerator resistance, progressive brake feel and short vehicle-specific trigger effects. | High priority after output routing. Only arm pedal effects when the player's binding and active analogue mod actually make that trigger a pedal. |
| DualSense gyro and accelerometer | Optional steering or camera motion, touch/button activation and recenter. | Reuse the current timestamped gyro path; expose only supported input modes. |
| DualSense multi-touch pad and click | Overlay navigation, an optional shortcut/radial selector, gyro activation or a mapped Look back action. | Preserve ordinary bindings. Touch gestures require new local input data; a touchpad click is not full touch support. |
| DualSense RGB light bar | Player colour, ready/lobby status, boost colour or item colour. | Optional, with independent brightness/off. Never the sole source of race information. |
| DualSense player indicators | Identify local player assignment. | Use backend support; do not confuse these with online racer positions. |
| DualSense mute button/indicator | Optional mapped action and an indicator that reflects real microphone state if audio support is later added. | Do not light the mute indicator as a decorative game effect or imply that a live microphone is muted. |
| DualSense battery/charging information | Small controller-status readout and optional low-battery notice. | Query if exposed. No repeated racing interruptions. |
| DualSense speaker, microphone and headset jack | Optional short local item/countdown cues; choose controller audio explicitly. | Later, platform-dependent work. This is not a proposal to add voice chat or record microphone audio automatically. |
| DualSense Create/PS/system buttons | Use extra remappable buttons only where the OS/backend exposes them. | Keep system-reserved shortcuts intact. |
| DualSense Edge variants | Recognise exposed rear/Fn inputs and respect trigger-stop/profile differences. | Conditional follow-up; standard DualSense support must not depend on an Edge. Do not rewrite controller firmware profiles. |
| Deck left/right haptics | Distinct impact, boost, terrain and menu patterns; independent left/right tests and balance. | Core Deck work. Work with haptic output rather than assuming conventional rumble motors. |
| Deck two trackpads, touch, click and pressure-related input | Fine menu navigation, optional radial shortcuts, gyro activation and small UI detents. | Capability/route-dependent. Avoid doubling Steam's existing trackpad click feedback. |
| Deck capacitive stick touch | Activate gyro while a thumb rests on a stick; release to reposition the handheld. | Optional. Preserve the existing gyro settings and allow button activation instead. |
| Deck six-axis IMU | Optional motion steering/camera and recenter. | Reuse the native helper and existing gyro fallback. |
| Deck four rear buttons | Map Look back, items, overlay/chat or other existing actions without releasing a stick. | Bind only inputs actually exposed by the selected route; honour Steam layouts. |
| Deck analogue triggers | Keep the new accelerator/brake bindings and calibration. | Already supported by the optional analogue mod. The Deck does not have DualSense-style motorised trigger resistance. |
| Deck touchscreen | Easier launcher/overlay touch navigation and on-screen keyboard access. | Separate input polish. It should not change the racing HUD or bind system gestures. |
| Deck battery/power state | Optional quieter feedback profile and device status. | Secondary. Respect the user's feedback preference rather than silently weakening it. |

Sony documents the DualSense actuators, triggers and other controls on its
[controller page](https://www.playstation.com/en-gb/accessories/dualsense-wireless-controller/).
Its [PC support page](https://www.playstation.com/en-gb/support/hardware/pair-dualsense-controller-bluetooth/)
distinguishes USB haptic/audio availability from Bluetooth support. Edge-only
features are described [separately](https://www.playstation.com/en-gb/accessories/dualsense-edge-wireless-controller/).
Those hardware descriptions are not a guarantee that a generic PC input API
exposes every feature.

For granular control, the common effect model should support independent
left/right amplitude, duration, attack/hold/decay, pulse spacing, burst count,
directional balance and smooth cancellation. Effects can layer and duck each
other rather than simply adding until everything clips at maximum strength.
The ordinary SDL rumble channels describe two amplitude controls; their names
do not give us an arbitrary frequency-in-Hz or waveform-streaming interface.
Distinct textures on that path come from carefully timed envelopes and pulses.

Where a richer backend is proven, add authored ticks, clicks, tones, noise-like
textures and sweeps with explicit frequency/shape controls. DualSense USB
waveform output and native Deck pattern commands need separate qualification;
the common API must not pretend every controller can render the same signal.
Adaptive triggers add another dimension: effect start position, a graded force
curve, a soft detent or short vibration, with independent left/right limits.
The Deck can offer a haptic cue at a virtual detent but cannot physically resist
the trigger like a DualSense. None of these is steering-wheel force feedback.

**The connection and API limits we need to respect**

1. Direct native SDL3 is the main portable path. The existing helper is already
   shipped on Windows and Linux. Keep the renderer's SDL2 integration intact;
   this is not an SDL migration of the whole application.
2. A virtual Xbox-style device may carry working buttons and rumble while hiding
   the physical DualSense's advanced capabilities. Correlate devices and choose
   one output owner; do not send duplicate effects to physical and virtual pads.
3. DualSense adaptive triggers are not the same thing as Xbox impulse-trigger
   rumble. In the checked [SDL PS5 driver](https://github.com/libsdl-org/SDL/blob/147a8ee32dbf9ac02f3794964490687b6bbda1bc/src/joystick/hidapi/SDL_hidapi_ps5.c),
   the generic trigger-rumble function is unsupported. The device-specific effect
   path carries trigger parameters. Use a bounded, typed DualSense effect builder.
4. The checked [SDL Deck driver](https://github.com/libsdl-org/SDL/blob/147a8ee32dbf9ac02f3794964490687b6bbda1bc/src/joystick/hidapi/SDL_hidapi_steamdeck.c)
   supports ordinary rumble, but its custom SendEffect, trigger-rumble and RGB
   LED paths are unsupported. Calling the generic SendEffect API does not make
   arbitrary Deck haptic waveforms available.
5. Bluetooth PlayStation effects depend on enhanced reports. Handle that
   deliberately and explain availability in the device panel. SDL notes that
   [enhanced reports](https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_ENHANCED_REPORTS)
   can affect other DirectInput applications and cannot simply be switched back
   without cycling a PlayStation controller. Avoid silently changing unrelated
   controller software or system settings.
6. Steam Input offers rumble, haptic pulse/pattern APIs and
   [DualSense trigger effects](https://partner.steamgames.com/doc/api/ISteamInput#SetDualSenseTriggerEffect).
   Its simple haptic API combines gain with user settings and clamps positive
   gain, so it is not an unlimited strength override. Native Steamworks access
   would need an appropriate application context and a distribution decision.
   Do not rely on another application's App ID or make the AppImage require
   Steam merely to obtain feedback.
7. For richer native Deck patterns outside Steam, evaluate a small first-party
   adapter against published protocol definitions, with bounded commands and
   real hardware tests. The available command definitions are a research lead,
   not proof that every pattern works correctly on LCD and OLED firmware.
   Preserve Steam ownership, preferences and the existing gyro bridge. If this
   cannot coexist reliably, keep native SDL rumble envelopes as the fallback.

**What the game should feel like**

These are proposed authored effects, not hardware support claims:

| Situation | Proposed response |
|---|---|
| Car accelerating | Optional quiet engine texture that rises with load/speed; small R2 resistance where supported. Silence at rest. |
| Hovercraft | Softer, slower pulses for lift and water motion; a different texture from the car. |
| Plane | Light propeller-like texture and brief turbulence/landing accents. Keep it subtle during long flights. |
| Sand/grass/rough ground | A low-priority texture distinct from smooth road. Only select a named surface when genuine scene data identifies it. |
| Water and wave contact | Soft swells and a short splash/landing accent. No extra simulation or water-physics work. |
| Drift/skid | A controlled pulse that indicates traction/load without a permanent full-strength buzz. |
| Boost pad or boost activation | A clear rising pulse followed by a short tail; stronger than background engine feedback. |
| Landing after a jump | Brief impact scaled to available, validated landing information. |
| Wall/racer collision | Short directional thump if impact direction is known; otherwise a centred hit. |
| Firing or being hit by an item | Different short signatures, with hits overriding background texture. No information about unseen opponents. |
| Banana/item collection | Optional light tick, distinguishable from a collision. |
| Countdown | Small separated pulses and a different final start pulse. |
| Finish/results | An optional brief completion pattern, followed by silence. |
| Menus | Optional quiet focus/confirm detents; avoid duplicate Steam UI haptics. No racing trigger resistance in menus. |

Not every item belongs in the first build. Begin with impacts, boost, landings
and tested original-rumble conversion. Add continuous texture only once it
does not drown out those cues. Named surface effects need verified surface
data; a texture name or a performance counter is not an adequate substitute.

**Implementation order and acceptance gates**

1. **Make the existing output observable before tuning it.** Add a compact
   diagnostics view for selected physical profile, SDL/backend version, native
   or Steam-virtual route, connection type and detected capabilities. Show
   requested level, last accepted output and rate-limited failure information.
   Add separate left/right 25%, 50% and 100% finite tests with a Stop button.
   Rename or explain the half-scale identification pulse. Gate: the LCD Deck
   and DualSense each produce a repeatable test on the intended device, or the
   panel gives a specific unavailable/failed reason.

2. **Repair routing and output lifecycle.** Use one feedback owner per physical
   device. Keep the current online racer-to-input-profile mapping. Compare
   native Deck output, Steam's virtual route and Desktop mode with Steam closed.
   Keep the working route rather than globally forcing users to disable Steam
   Input. Check call results and helper acknowledgement, reconnect, assignment
   changes and backend switching. Replace indefinite output ownership with
   finite effects refreshed under a short lease. Gate: output stops on pause,
   focus loss where appropriate, menu return, leaving a lobby, disconnect and
   normal shutdown, and old effects do not resume after reconnect.

3. **Add a small local feedback service.** Put new first-party code under
   `runtime-recomp/src/game/controller_feedback/`, using the existing approved
   integration path. Separate event collection, effect mixing and device output.
   Use bounded queues, latest-value coalescing and a rate-limited worker; no
   USB/HID calls or blocking IPC on the game simulation/render thread. A proposed
   starting point is 30–60 Hz for changing effect envelopes, with immediate
   queued impact events and a short expiry/watchdog. Tune measured values rather
   than treating these rates as guarantees. Gate: a missing/slow device cannot
   stall a frame or accumulate an unbounded command queue.

4. **Extend the existing helper deliberately.** Add capability flags and typed
   feedback commands/results to its authenticated local IPC; negotiate a new
   helper protocol version. Preserve saved button/axis numbering and existing
   device identity. Keep raw report construction inside the device owner;
   arbitrary bytes must not become a generic network/IPC feature. A missing or
   old helper gets a clear fallback instead of crashing. This local protocol
   change is separate from the online game protocol.

5. **Tune LCD Deck feedback first.** Measure the default rumble path at full
   requested output, then compare finite impact/boost envelopes, left/right
   balance, repeated pulses and comfort levels. Use contrast and event shaping,
   not a cosmetic 200% slider. Respect Steam/firmware preferences. Richer haptic
   patterns may use an optional tested native adapter or a valid Steam Input
   integration; ordinary feedback must work without that optional route.
   Gate: an A/B playtest on the LCD Deck in Big Picture and Desktop confirms
   distinguishable feedback, correct controls and no loss of gyro support.

6. **Add DualSense effects for USB and Bluetooth.** Start with ordinary haptic
   envelopes, light bar and player identification. Add conservative L2/R2
   profiles, activated only for matching bindings and session configuration.
   Compose trigger, rumble and LED state in one owner so independent writes do
   not reset each other. Keep a complete reset state for menus/reconnects. Give
   resistance its own Off/Light/Balanced/Firm control and default it gently.
   Gate: both transport tests work, and unsupported/virtual routes fall back
   honestly without losing standard input.

7. **Prototype full USB waveform haptics separately.** Investigate a controller
   audio endpoint for authored haptic signals, with explicit endpoint/format
   validation and a separate bounded audio buffer. Match it to the correct pad;
   do not move the player's normal game audio or send the whole soundtrack to
   the actuators. The checked SDL PS5 path switches between rumble emulation
   and audio haptics, so those modes need deliberate coordination. Sony's PC
   documentation requires USB for haptic feedback. Qualify Windows USB first;
   treat Linux and other paths as experimental until physically demonstrated.
   Failure must leave normal game sound and rumble available.

8. **Add the useful extra inputs.** Expose rear buttons, touch activation,
   gestures and optional gyro behaviour through the current Controls system.
   All mappings remain local and convertible to the already recorded gameplay
   inputs. Keep the analogue mod's own pedal setup in its Details. Do not take
   over Deck Steam/Quick Access or PlayStation system shortcuts. Gate: no lost
   bindings, double actions or change in normal controller behaviour when the
   new features are off.

9. **Integrate confirmed game events carefully.** Publish a small immutable
   local-racer feedback snapshot at an existing owned boundary. Audit available
   fields before adding surface/impact observers. If new guest hooks are needed,
   use the repository's checked hook policy and obtain the required approval
   while the consolidated hook integration is missing; do not invent addresses
   or edit generated guest code. Deduplicate one-shot effects by scene/epoch,
   frame, local racer and event identity. Use local input presentation for
   immediate pedal feel, and release gameplay impacts through accepted/confirmed
   events. Never issue hardware output while replaying old ticks.

10. **Finish with paired platform builds and real-device qualification.** Ship
    matching Windows and Linux AppImage test packages only after the feature's
    gates are complete. Verify online compatibility identities; an output-only
    service should not casually change cross-play. If any simulation change is
    required, keep the deliberate identity and both-owner review process.

**Settings and visual scope**

Keep the existing launcher design. Add a compact, collapsible **Controller
feedback** card in Controls, for the currently selected local player/device:

- Mode: Off, Classic Rumble Pak, Enhanced feedback.
- Master strength and left/right balance; finite test buttons and Stop.
- Independent impacts/boost/environment/engine/menu levels. Engine texture can
  be turned off without losing meaningful hit/boost cues.
- A small capability/route status and Copy feedback diagnostics action.
- DualSense controls only where appropriate: trigger resistance, trigger
  vibration, light bar, player indicators and optional USB waveform mode.
- Deck controls only where available: feedback profile and optional richer
  haptic mode, with its route/availability explained.
- Optional gyro/touch/rear-button mappings in their existing control sections.

Keep reading fonts for labels and values, normal card spacing and controller
navigation. Unsupported controls explain the missing capability; they must
not look enabled while doing nothing. Save preferences per local profile and
device class with migration defaults, keeping today's assignments intact.

**Verification before calling the haptics work finished**

- Unit tests: envelope limits, priority/ducking, amplitude curves, saturation,
  expiry, off/reset states, packet validation and one output owner per device.
- Fake-device tests: failed effects, unplug/reconnect, helper death, stale
  versions, full queues and output acknowledgement. A socket send is not an
  acknowledgement of a successful hardware operation.
- Replay tests: repeated confirmed frames and replayed impacts do not produce
  duplicate effects; session transitions retire old events; spectators do not
  vibrate a racer's physical profile by accident.
- Input regression: keyboard/Xbox/other controllers, four local profiles,
  saved assignments, analogue pedal bindings and the existing Deck gyro path.
- Real LCD Deck: Steam/Big Picture, Desktop with Steam running, Desktop with
  Steam closed, resume from suspend, dock/undock and an external pad attached.
- Real DualSense: Windows USB and Bluetooth first; native Linux/Deck USB and
  Bluetooth separately. Include firmware/version notes and virtual-wrapper
  cases. Edge is a distinct follow-up test, not assumed equivalent.
- Playtest stock and custom tracks, all vehicle types, battles, online hosts,
  clients and spectators. Compare identical scenes with feedback Off/Classic/
  Enhanced. Record frame times and helper traffic as well as subjective feel.
- Proposed performance budget: bounded memory, no guest-state copying per
  effect, no physical I/O under the simulation/render locks, and a small
  measured CPU cost. Set an explicit threshold from the LCD Deck baseline;
  do not promise zero stutter or improved network latency from haptics work.
- Required repository guard/tooling/pre-commit and host C++ checks, production
  source-chain audits, archive readback, runtime startup and actual Controls
  navigation on both platforms. No submodule or generated guest-code edits.

The first deliverable should be reliable device diagnostics and correct,
repeatable existing rumble. The next should be distinctive Deck effects and
DualSense triggers/lights. Full USB waveform haptics, controller audio and
experimental native Deck patterns stay behind their own hardware gates.
