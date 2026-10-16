# DKR-R v1.0.7 beta 2

This beta adds Custom Magic Codes, Custom Trophy Races and AI & Boss Difficulty.
Import the supplied `.nrm` through Mods / Hacks, then review, approve and enable
it in Custom Magic Codes. The trophy editor has Cups, full-width Lineup and Share
screens, controller navigation, on-screen text entry and `.dkrcup` sharing.
Custom cups sit below the six-column Custom Tracks rows; stock trophies remain.

Cup Options and Choose a Track no longer display internal text-field labels
that pushed their keyboard buttons beyond the row. Launcher and overlay share
the same fix. The SDK includes the complete Modders Guide as text and HTML.

The technical version is `1.0.7+beta.2` to retain the supplied mods' 1.0.7
minimum-host compatibility. Use matching beta 2 builds for online sessions.
The builds were compiled and statically inspected; gameplay/controller/device
acceptance remains with the maintainer. See RELEASE-NOTES.txt for this beta.

## Carried forward from 1.0.7

This update adds earlier online compatibility checks, built-in update checks,
tidier launcher/overlay wording and lighter surrounding lava glow. Custom
tracks, characters, Track Lab, texture priorities and enhanced water carry on
from the previous release.

Everyone in an online lobby needs 1.0.7. Startup now reports the mismatched
component instead of a generic contract error. See RELEASE-NOTES.txt for
changes and the source release-validation record for actual test coverage.

Packages cover Windows x64, Linux x86_64/ARM64 AppImages, Steam Deck and
Android ARM64. Steam Deck uses the Linux x86_64 payload. ARM, Android and
SteamOS still need physical-device qualification; Apple binaries are not supplied.
Experimental Rollback and hardware ray tracing remain experimental.

In Play, Skip intro logos offers Start menu or Character select. Start menu
keeps its original first sequence. The setting does not change online startup.
Lava has a subtle cached glow on nearby static walls and ground, with its own
on/off checkbox, intensity, pulse and range settings. It needs no per-frame
lighting rays or additional lighting pass. This replaces the costly ray-traced
lava-lighting option without changing water's hardware reflections. Lava pulses
between yellow-white and orange-red heat, not black. Rough hot-rock islands,
shaded ridges and surface brightness remain independent of surrounding glow.

Android build 1070007 keeps the previous application ID and signing identity
to preserve upgrades. Its release Java wrapper is not debuggable; it retains
the existing development signing key, not a newly provisioned production key.

Updates are in **About DKR-R → Updates**, with a check button in Play. Startup checks
run in the background. Downloading and installing require your approval and do
not replace saves, mods, textures or portable data. Published v1.0.6 needs one
manual upgrade to get the updater. Future automatic installations require
signed update metadata on the GitHub release. Android's updater bootstrap uses
the original updater version code 1070001; beta 2 increments it to 1070007 and
requires the same compatible APK signing identity.
On Windows, launch `DKR-R.exe` from the main folder. The runtime and helpers
stay inside `libexec/dkr-r`; keep the whole folder together. Portable mode's
`portable.txt` still goes beside the public `DKR-R.exe`.

## What's changed since 1.0.5

There is more control over the water now! Original, Performance, Balanced,
High and Custom presets keep your resolution, MSAA and FPS target unchanged.
Advanced settings separately control reflection/wake/spray quality and the
water's strength, roughness, ripples, foam, clarity, refraction, flowing detail
and tint. Waterfall/river flow and interpolation, reflection handling and
Track Select water stability have had corrections too.

Custom rows have six alternating backgrounds and a heading on every row.
Future Fun Land restores its own art. Widescreen scrolling keeps images,
arrows and text with their frames, including the brief-stretch correction.
Shared `.dkrmap` texture/sprite tables have stronger length/offset checks for
mixed track and character libraries. Long in-game captions are abbreviated
without renaming installed mods.

Offline custom tracks can choose the vehicles their headers allow. Track Lab
can generate looping preview flights, while preserving authored flights, and
warns about starts that force a vehicle. The offline Modern camera has revised
corner, drift and wall-hit behaviour and migrates existing tuning preferences.

Contributor updates add graphics-command bounds/budgets, safer setup and
packaging, platform-source improvements, and repeatable regression/CI tools.
Golden Balloon and Wave Race now use the compact technology credit rows.
See the bundled release notes and the source changelog for the full breakdown.

## Start playing

Keep the complete package together, including libexec workers and notices.
Start DKR-R.exe on Windows or the AppImage on Linux, then add your own clean
Diddy Kong Racing US v1.0 or US Rev A/v1.1 ROM in Play.
.z64, .v64 and .n64 byte orders are detected from their headers.
Both revisions run in the same application.

Accurate keeps the original 4:3/30 FPS presentation. Modern enables widescreen,
interpolated presentation and optional enhancements.
Escape, F1 or controller Back/View opens settings; Alt+Enter/F11 toggles fullscreen.

## Mods, textures and online

Mods / Hacks imports legacy xdelta/ZIP patches and DKR-R track packages.
Custom tracks keep the originals and use wooden-frame Track Select previews.
Each custom row has its own header and cycles through six custom backgrounds.
Four custom actors share the original Player Select stage; L/R browses the
remaining enabled roster. Reviewed behaviour adapters are not unrestricted
ROM-code execution.

Textures imports RT64/Rice packs and manages CRT filters. Multiple packs can
be active; lower priority numbers win texture clashes.

DKR-R Online uses Quick Join codes or friend invitations. The host chooses
the session mods; clients consent, reuse matching local data, receive only
missing payloads and can keep verified downloads inactive for offline use.
No ROM is transferred. Everyone needs a compatible complete build and the
same original revision.

Auto selects Experimental Rollback. Host Prediction and Strict Input Sync
remain available. Experimental play follows the normal menus, but has stated
local scenery/audio restrictions and ongoing real-world qualification.
The optional Modern camera/F7 tuning panel is for offline local races.

## Saves and support

Back up saves before updating. Offline, modded and online save routes are
separate. Save Manager includes Offline, Online, Modded and Recovery views.
Portable mode in Play takes effect next application launch; it does not move
existing data. A different profile can look empty without deleting the old one.

About DKR-R → Support summary exports support information and opens diagnostic
folders. Android previews also offer startup-log export. Runtime logs retain
history beyond runtime.log/runtime-previous.log; review them before sharing.

No ROMs, personal saves or sample mods are supplied. Verify the official
release SHA-256 before running a downloaded package.

## Guides and credits

Current guides: https://github.com/ThatGuyMcd/DKR-R/tree/main/docs
Release history: https://github.com/ThatGuyMcd/DKR-R/blob/main/CHANGELOG.md
Source and updates: https://github.com/ThatGuyMcd/DKR-R

PR #49 contributes macOS build/platform support and ultrawide sky fixes.
PR #50 contributes Modern camera and Track Lab improvements.
PRs #51–#59 include runtime safety, Track Lab, source platform support,
regression/build tooling and concurrent Friend-test isolation improvements.
POOTERMAN created the application icon; sr.gu leads the community HDR pack.
Golden Balloon informed HUD grouping; Wave Race 64 Recomp informed enhanced water.
See the bundled notices and THIRD_PARTY.md in the source for exact licensing.
