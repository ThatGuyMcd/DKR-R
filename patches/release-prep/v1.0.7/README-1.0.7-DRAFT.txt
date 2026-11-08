REVIEW DRAFT - NOT YET APPLIED TO THE PUBLIC README
Release links, platform requirements and acceptance must be finalised before
publication. This draft does not announce a release or claim untested results.

# DKR-R - Diddy Kong Racing Recompiled

Keep the existing project artwork and Discord buttons here.

## DKR-R v1.0.7

Right, there is a LOT in this update! We have a proper Mod Store built into the
launcher, a much more powerful modding SDK, custom trophy cups, a Dev Menu,
ModernCamera++, and a much simpler save system. There have also been plenty
of fixes to Online, custom tracks, portraits, water, the intro and the credits.

DKR-R is a native static recompilation of Diddy Kong Racing. You need your own
legally obtained US v1.0 or US Rev A/v1.1 ROM. No ROM or extracted game assets
are included.

### Getting started

1. Download the package for your system from the Releases page.
2. On Windows, extract the WHOLE folder and open DKR-R.exe. Keep assets and
   libexec with it. The files inside libexec are helpers, not extra games to launch.
3. On Linux or Steam Deck, make the AppImage executable and launch it.
4. On Android, install the compatible APK over your existing copy. Back up
   your saves first; you should not need to uninstall the app to update it.
5. Add your supported original ROM in Play, choose your settings and start!

Accurate keeps the original presentation. Modern gives you widescreen,
interpolated frame rates and the optional graphics settings. Escape, F1 or
controller Back/View opens the overlay.

Use the final release download links here. Link docs/PLATFORMS.md for the
actual supported OS/GPU requirements, not guessed minimums.

## What's changed since v1.0.6?

### A Mod Store, right in the launcher

You no longer need to download everything separately and then work out where
to import it. Open Mods / Hacks > Mod Store to browse the library, view the
screenshots and details, and download and install a mod from the same place.

- Choose between the grid view and the more detailed browse-and-inspect view.
- Browse tracks, characters, Custom Magic Codes and texture packs.
- Sign in with Discord for the account features, reviews and mod requests.
- Moderators and admins get the library tools their account is allowed to use.
- One Install button handles the download and verification. You do not have
  to click through a separate verify step first.
- Installed mods and textures use their proper names, not "package".
- The Store remembers what it installed and can show updates or a repair action.
- Duplicate matches give you a choice. Reinstalling or updating should not
  quietly create another copy every time.
- Updates replace the selected managed installation and keep compatible
  settings. Changed executable code still needs your approval.

There are fixes for the failed character/code downloads, texture Installed
status, long Windows paths, track ZIP extraction and Mario Circuit's extra
wrapper folders. The Store also has tidier forms, readable dialogs and better
controller navigation.

### Custom Magic Codes and the Modding SDK 2.0

Custom Magic Codes let modders do much more than swap a model or a texture.
They can add settings, change supported game behaviour, use hooks and build
their own in-game tools. Import a .nrm through Mods / Hacks, review it, then
approve and enable it. Only enable code from creators you trust.

SDK 2.0 expands that with racer and object inspection, supported live edits,
events, collision queries, checkpoint tools, debug drawing, live settings and
controller-friendly tools panels. Existing API 1 mods are still supported.
This is not a switch that makes arbitrary ROM hacks or unsafe native code work.

The separate SDK download includes the source examples and the completely
revised Modding Bible in TXT, HTML and PDF. The guide covers package creation,
settings, hooks, game services, development tools and online rules.

### Build and share your own trophy cups

The Custom Trophy Races mod lets you choose the tracks, laps and supported
vehicle for each round, then race them in lineup order or shuffle the order.
Your cups appear below Custom Tracks in Track Select. The original cups stay.

The editor has dedicated Cups, full-width Lineup and Share screens, compact
round controls and controller navigation. Text fields have on-screen keyboard
entry, and the track list refreshes when you open the details.

You can import and export cup definitions to share them with other players.
They will still need the tracks your cup uses; sharing a cup does not include
someone else's track assets or automatically approve a code mod.

Custom tracks now use six columns per row, and modern .dkrmap tracks have
working On/Off controls in the mod library as well.

### AI difficulty and a proper Dev Menu

The AI & Boss Difficulty example lets you make things easier or harder to suit
how you want to play. The SDK also includes a separate live-settings example
for modders. Do not enable conflicting difficulty mods together.

The Dev Menu is for people developing tracks and code mods. It provides
inspection, watches, supported racer/object editing, teleport and scene tools,
pause/step controls, route and checkpoint tools, debug overlays and shareable
development documents. The Modding Bible explains what each tool can change.

Developer tools use separate development progress. They do not write over your
normal Adventure. Online development also needs the right lobby permissions
and host authority; ordinary lobbies do not become development sessions just
because one person has the mod installed.

### One personal save

Changing a character, track or ordinary gameplay mod should not send you to a
different Adventure file. Normal offline play now uses one personal save, with
the same three slots, whether those mods are on or off.

Your older mod-set saves are kept. If one has the progress you want, choose it
in Save Manager > Backups & Recovery and check the preview before restoring
it. Nothing tries to add together balloon totals from different playthroughs.

Save Manager has had a proper repair too:

- Max Out and unlock actions save after you confirm them. Changing an editor
  draft is no longer mistaken for having saved it.
- Manual edits use Apply Changes, and a stale draft cannot overwrite a newer save.
- Save writes are checked, with backups and clearer recovery choices.
- Controller Pak backup/import/export and save bundles are included.
- Added .dkrmap records live separately from the original 512-byte EEPROM.
  Installing a course must not shift initials or invent impossible balloon totals.
- The host keeps separate online progress, and clients keep their personal saves.
- Dev Menu progress is separate. A new development copy starts from saved
  personal progress; an existing one is refreshed only when you choose to do it.

Portable mode is still a different data location. Turning it on does not
silently move the files from your normal profile. See the save guide before
restoring or moving progress.

### DKR-R Online

Windows and Linux no longer disagree about identical replay code just because
of text line endings. Online startup checks the linked engine and prepared
playable mod resources earlier, and failures identify the mismatched part.
There are also fixes for native-track music paths, code-mod startup/Track Select,
Big Donut and Moon Station, and Quick Join callback handling.

In the Online tab, Your online view lets each player choose My racer fullscreen
for supported experimental races. The divider, sky coverage and HUD have been
corrected, and other human racers can show their display name and race position
instead of the broken black labels. Menus and shared scenes keep their normal
layout. You can also hide your lobby code while streaming.

Everyone in a lobby needs a compatible complete release and the same supported
ROM revision. Auto selects Experimental Rollback; the other backends remain
available. See the online guide for exact limits and supported platform pairs.
Do not promise independent online high-detail models/ranges until the release
audit and owner acceptance settle the rolled-back graphics paths.

### ModernCamera++ and custom portraits

ModernCamera++ is an optional code mod with camera presets, local profiles,
look/rear-view controls and live F7 tuning. It is not automatically enabled.
Its renderer keeps camera work separate from the shared game state, and the
vehicle-shadow interpolation correction is included.

VR is not part of this release. The OpenXR work is not a finished or
headset-tested feature, so we are not presenting it as one.

Custom character portrait support has also had fixes, including the character
artwork used inside Fire Mountain and Smokey Castle. This is separate from
the minigame HUD, which was already working.

### Water, lava and the presentation fixes

The enhanced water and its quality controls carry forward from v1.0.6. This
update fixes more of the surrounding issues rather than pretending the entire
water feature is new again.

- Original rivers and waterfalls interpolate properly with enhanced effects off.
- HD liquid animation frames load as a family, avoiding the initial repeated
  changes between original and HD artwork.
- Lava pulses between hot yellow-white and orange-red, not all the way to black.
- Nearby lava glow is much lighter and has its own On/Off, intensity and range
  controls. You do not need the costly old ray-traced lighting pass for it.
- The spinning-logo intro fills the selected viewport, with a rounded
  translucent background behind the copyright and the new recompilation credit.
- Start-menu moving names and credits use the wider viewport without stretching
  the original font.
- Credits fill the frame, including the lower sky and black fades. The native
  readback-bounds and failed GPU-allocation crash corrections are retained.

The broken atmosphere experiment has been removed. There is no new dynamic
lighting/volumetric fog/shadow system to turn on in this release.

There has also been work to reduce repeated allocations and texture lookup
work, reuse CPU texture buffers and keep presentation pacing on a monotonic
clock. We are not quoting an FPS increase that has not been measured. Your
resolution, effects, texture packs and system still affect performance.

### A tidier launcher and overlay

The launcher and overlay use shorter, more useful descriptions. Mod cards,
buttons, keyboards and nested dialogs have had layout fixes. Unnecessary
Save Location, Connection Details and Reflection Details sections have been
removed, and the support summary sits above the credits with compact actions.

Application update checks are built in, with a small Check for updates button
in Play. Downloading/installing needs your approval and verified release data.
For the existing 1.0.7+beta builds, use the documented manual stable-upgrade
route: their version reader treats +beta metadata as the same base version.

## Before updating

Back up your user data, keep the full application package together and use
matching builds for Online. Do not delete your profile to fix a missing mod
or a save that appears different. The current save and troubleshooting guides
explain how to find the right copy and export useful logs.

## Guides and modding

- Getting started and platform requirements: docs/README.md and docs/PLATFORMS.md
- Saves and recovery: docs/SAVES.md
- Online: docs/ONLINE_MULTIPLAYER.md
- Mods, the Store and code packages: the current mod and Magic Codes guides
- Graphics, textures and Camera++: the current graphics/texture guides
- SDK 2.0 and Modding Bible: the separate SDK release download
- Full changes: CHANGELOG.md

Resolve these to final valid Markdown links when applying this draft.

## Contributors and credits

Keep the existing contributor credits, licence and asset notices. PR #60's
nightly/stress checks, compact dependency caches and Quick Join fixes are
included. Earlier merged platform, Track Lab, runtime and build-tool work is
still credited; changes already in v1.0.6 are not counted as new work here.

POOTERMAN created the application icon. Keep the HDR texture-pack project,
Golden Balloon HUD reference, Wave Race enhanced-water reference and every
required third-party notice accurately attributed.

Thanks to everyone making tracks, characters, code mods and texture packs,
and everyone sending useful reports. There is a huge amount in this one!
