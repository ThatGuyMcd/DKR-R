# DKR-R 1.0.5 Beta 12

This optimisation beta adds reusable graphics-task snapshots, bounded shader
worker pools, lower-wakeup presentation pacing, an exception-safe social worker,
and optional Battery/Balanced/Quality graphics presets. Existing saved settings
are preserved. Android adds capability-checked presentation setup, first-GPU-error
reporting and cached asset extraction. Android first-use defaults render at 480p
with standard framebuffer precision; existing users can apply Balanced explicitly.

Windows x64, Linux x64, Linux ARM64 and Android ARM64 packages are separate builds.
An Android APK building successfully does not qualify every handheld GPU. Physical
Android/ARM device performance, lifecycle and graphics checks remain necessary.
See `docs/BETA12-OPTIMISATION.md` for the exact scope and qualification limits.

This build retains main revision 8a8e927, including the merged Track Lab and
Blender track-editor changes, for playtesting. The Beta 10 fixes described below
are retained. New editor and gameplay behavior still requires visual acceptance.

This playtest build repairs failed model-load cache accounting in US v1.0,
safely rejects null model instances, and gives offline custom-mod sessions
the native expansion-memory heap extent within the existing 8 MiB renderer
boundary. Stock and online sessions keep their original heap extent.
HUD asset-load failures now produce a bounded diagnostic instead of being silent.
Custom-character selection facing, portraits, voices, HUD placement and Blender
Track Lab additions are retained. Visual HUD acceptance remains a playtest check.
Track Lab is available in **Mods/Hacks**, directly below **Magic Codes**, with
its Blender working-folder, import and testing controls intact.
Custom legacy mods remain offline-only, with at most two active custom characters.
Characters can only use replacement portraits and sounds actually supplied by
their mod; unchanged donor assets remain unchanged. Saved ghost identities still
use the original game's character IDs.

DKR-R is a native static recompilation of Diddy Kong Racing. This package does
not include the game ROM or extracted game assets. Supply your own legally
obtained Diddy Kong Racing US v1.0 or US Rev A/v1.1 Game Pak image when
prompted. DKR-R identifies `.z64`, `.v64`, and `.n64` byte orders automatically
and selects the matching revision. Windows embeds Rev A as an in-process DLL in
the single `DKR-R.exe`; Linux contains both revisions in the single AppImage.
No manual engine selection is required, and Windows never launches a second
background game process.

Start `DKR-R.exe` on Windows or the DKR-R AppImage on Linux. Select Accurate for
the original 4:3/30 FPS presentation, or Modern for optional widescreen,
high-refresh interpolation and quality-of-life controls.

Open settings with Escape, F1 or controller Back/View. Alt+Enter and F11 toggle
fullscreen. Local settings, saves, Controller Paks, imported filters and texture
packs are stored outside this package.

Modern mode includes HUD controls for original 4:3 or fit-to-window placement.
HUD artwork retains its original proportions; these presentation settings do
not alter gameplay or online determinism.

The Controls page supports four explicitly assigned local controllers. Known
devices use the bundled cross-platform mapping database. Select any controller
marked **Setup required** to run the N64 mapping wizard; custom mappings are
saved locally and can be imported or exported from the same page.
Automatic input mode uses the private native SDL3 controller and gyro helper on
Steam Deck and the established SDL2 controller path elsewhere. The helper is
headless: the launcher and game remain one SDL2 window. SDL2 compatibility can
be selected explicitly for raw controller remapping. Backend changes apply live
at a neutral input boundary, and SDL2 is restored automatically if the helper
is unavailable.
Enabling **Virtual Controller Paks** creates one independent Pak for every
connected local racer while preserving rumble on supported controllers.

If this package was not downloaded from ThatGuyMcd's official GitHub release,
it may have been modified. Verify the published SHA-256 before running it.

## Online multiplayer

Open **ONLINE MP** to create an encrypted two-to-four-player room or join one
with a five-character Quick Join code. The host
approves every joining device, is always Player 1, and starts only after every
racer is ready. Quick Join needs no account, adapter selection, invitation
file, IP address or port forwarding.
Each machine chooses its own local controller or keyboard profile before
readying; DKR-R routes that source to the host-assigned online player number
automatically.
When `JOINTVENTURE` is enabled, the original shared Adventure hub remains a
single-view, single-racer scene. DKR-R routes control to whichever online racer
the game currently assigns as the native lead; races, bosses and minigames
continue to expose both racers normally.
Hosts can also select **INVITE FRIENDS** in an open lobby. Online friends
receive an in-game notification and a persistent invitation under **OPEN
LOBBIES**. Friend invitations expire after five minutes and use a single-use
authenticated admission; sharing the normal five-character code still uses
the standard host-approval flow.
Quick Join uses a public rendezvous for WebRTC signaling only; it does not carry
gameplay, inputs or save data. DKR-R has no account service, public lobby
directory, gameplay server or relay. Some restrictive networks may be unable
to establish the required direct peer route.

After every racer is ready, Player 1 starts a synchronized five-second
countdown. All peers show the same centred countdown and tones before the
cold-boot synchronization barrier releases gameplay.

## Support information

The Play page can export a privacy-safe settings and system summary. Optional
diagnostic logging and crash dumps can be enabled from the same card, and the
corresponding local folders can be opened directly. Reports exclude ROM/save
contents, personal paths, account names, hardware identifiers and online
invitation data.

The DKR-R application icon was created by POOTERMAN. The DKR-R HDR Texture Pack
Project is led by `sr.gu` and re-imagines DKR's artwork in faithful high
definition.
