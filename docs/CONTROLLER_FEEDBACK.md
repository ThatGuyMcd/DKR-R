# Controller feedback

Open **Controls > Controller feedback** for the local player you want to set up.
Classic keeps the original Rumble Pak feel. Enhanced adds separate impacts,
boost, landings, item/banana cues, countdowns and a short finish effect. Engine,
skid and surface texture can be adjusted without turning down impacts.

Settings are saved for each local player and controller type. Existing
bindings, the Rumble Pak switch and its master strength are kept. Start with
the finite left/right tests at 25%, then 50% and 100%. Stop feedback silences
racing effects until you change a feedback setting. Test buttons still work.

**DualSense**

Use the SDL3 backend for advanced features. The panel shows the actual helper
version, connection, capabilities and the last accepted or rejected output.
A virtual Xbox-style controller may hide the physical pad's extra features.
Keep the route that works for your controls; do not globally disable Steam Input.

With Enhanced selected, resistance can be Off, Light, Balanced or Firm. It only
arms while racing with the analogue mod active and a trigger bound as an
analogue pedal. Start with Light. Turning it off, leaving the race or losing
the input lease releases the triggers. Player colour and optional race cues
have their own light setting and brightness.

Touch shortcuts can hold Look back, toggle the overlay with a two-finger tap,
or hold the existing gyro setup while touching the pad. Enable/configure gyro
in Motion steering first. For Look back, enable Look behind in Graphics too.
Ordinary touchpad clicks and exposed rear buttons
remain available through the existing bindings. System buttons are left alone.

USB and Bluetooth must be tested separately. Bluetooth enhanced reports can
affect other controller software; reconnecting the pad leaves that mode.
The microphone and mute indicator are not used for game effects.

**LCD Steam Deck**

The LCD Deck's haptics are around the trackpads. Enhanced feedback uses finite
envelopes and distinct pulses to improve clarity. System intensity still
applies, and 100% is already the maximum ordinary rumble request. It does not
turn the Deck into a controller with conventional grip rumble motors.

Try Steam/Big Picture, Desktop with Steam running and Desktop with Steam
closed. Copy feedback diagnostics in each case. The existing Deck gyro bridge
and control layouts are kept. Steam virtual output uses ordinary rumble.

Experimental native patterns are available only on the direct physical Deck
route. They use finite commands without changing firmware or input ownership.
If the HID command is unavailable, ordinary rumble is used and the diagnostic
reports the failure. Leave this off until comparing it on the actual LCD Deck.

**Experimental USB waveform haptics**

This needs a DualSense over USB with an exposed four-channel controller audio
endpoint. Select the matching endpoint explicitly, then opt in and test it.
Authored actuator signals go to channels 3/4; this stream leaves speaker
channels silent and does not redirect the game's audio or record a microphone.
The endpoint and waveform opt-in reset when the launcher closes or the device
is reassigned. Normal rumble remains the fallback if opening the stream fails.
Bluetooth and Linux waveform support must not be assumed from Windows results.

**Online and performance**

Only the local racer's assigned profile receives racing feedback. Confirmed
frame events are deduplicated; replaying a tick cannot play another impact.
Read-only retained snapshots provide the existing racer/scene fields, including
the reviewed wheel-surface data. No guest memory is written for feedback.

Output runs on a bounded worker with finite leases. The private helper reports
hardware API results, resets expired effects and closes actuator audio on
disconnect/shutdown. An acknowledgement means the API accepted a command;
physical feel still needs checking on the controller.

**What still needs playtesting**

- LCD Deck in Big Picture and both Desktop arrangements, including suspend,
  resume, docking and an external controller attached.
- DualSense USB and Bluetooth on Windows, then Linux/Deck separately. Record
  firmware, selected route and diagnostics, and start with gentle resistance.
- All three vehicles, stock/custom tracks, battle modes, local multiplayer,
  online host/client roles and spectators. Check effects stop on transitions.
- Compare Off, Classic and Enhanced in the same scene. Judge impact clarity,
  continuous texture, comfort and frame times separately.

Native Steamworks integration, controller-speaker sound cues, Edge-specific
firmware controls and additional platform-specific sensor modes remain outside
the qualified feature set. The portable implementation does not require a
Steamworks App ID. The optional hardware routes above are test features, not
claims of completed physical-device qualification.
