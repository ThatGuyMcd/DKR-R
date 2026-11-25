# DKR-R - Diddy Kong Racing Recompiled

<img width="1672" height="941" alt="Diddy Kong Racing Recompiled" src="https://github.com/user-attachments/assets/c913bee2-029c-4e29-9921-0f6abe7a3bf1" />

[Join our Discord](https://discord.com/invite/AMWfXdBjNP) · [Open the Mod Library](https://positron-portal.com/dkr-r/) · [Downloads](https://github.com/ThatGuyMcd/DKR-R/releases)

## DKR-R v1.0.16

Start a Speedrunning attempt, watch a replay or race a ghost from Play. Browse
public lobbies, suggest a host and hand over the lobby without closing it.
Cloud Saves can sync Vanilla, Modded and Online Adventure independently.
Save Manager keeps slots beside the editor, and Achievements opens the Badge
Gallery.

Use matching builds for online play. Bring your own legally obtained
**US v1.0 or US Rev A/v1.1 ROM**. See the [release notes](docs/releases/1.0.16/RELEASE-NOTES.txt),
[documentation index](docs/DOCUMENTATION-1.0.16.md) and
[validation record](RELEASE-VALIDATION.md).

## Get started

1. Download the complete package for your platform from [GitHub Releases](https://github.com/ThatGuyMcd/DKR-R/releases).
2. Windows: extract the whole ZIP and start **DKR-R.exe**. Keep `assets`,
   `libexec`, helpers and licence notices together.
3. Linux / Steam Deck: make the **AppImage** executable and launch it.
4. Add your original ROM in **Play**, choose your controls and start.

The release targets Windows x64, Linux x86_64, Linux ARM64 and Steam Deck.
Steam Deck uses the same payload as the Linux x86_64 AppImage. Android is not
supported for current releases; macOS and iOS downloads remain on their earlier
versions. See [platform support](docs/PLATFORMS.md) and the
[validation record](RELEASE-VALIDATION.md) for requirements and actual coverage.

Some legacy `.xdelta` mods require a clean **US v1.0 ROM to install**, including
manual imports. Add it once in Play; you can keep v1.1 selected for playing.
There is no bundled revision-conversion data. See [ROM setup](docs/ROM_SETUP.md).

## Play and the overlay

**Escape**, **F1** or controller **Back/View** opens the overlay. It uses the same
pages as the launcher. **Play** contains Achievements, **Speedrunning**,
**Session Mods** and Save Manager. Turn **Session Mods** off to play Vanilla
Adventure or on to play Modded Adventure. Each has its own save slots, balloons
and unlocks. Online and development saves are stored separately. See the
[Speedrunning guide](docs/SPEEDRUNNING.md) and [Saves](docs/SAVES.md).

Start Game stays in a compact bar at the bottom while the page scrolls above it.
It starts selected when a ROM is ready, and you can move away with the controller
normally. The overlay uses the same position for Return to the Race.

**Skip launcher** boots the selected game automatically and can be turned off
again in the overlay. **Language** changes the launcher and overlay immediately;
it does not translate the ROM or community content. The initial eleven language
packs are drafts awaiting native-speaker review. Seasonal colours, decorations
and button patterns follow your computer's region and local date.

The left stick moves between page elements and the right stick scrolls. The
sidebar switch chooses LB/RB or D-pad navigation. See the [launcher guide](docs/LAUNCHER.md),
[languages](docs/LOCALISATION.md) and [seasonal themes](docs/LAUNCHER_THEMES.md).

## Graphics and controls

Accurate keeps the original presentation. Modern offers wider aspect ratios,
explicit render resolutions, interpolation and optional graphics features.
You can render above the display's resolution if your GPU can handle it.
**DKR colours** brightens enhanced water while keeping your reflection method.
Hardware ray tracing remains experimental. CRT controls and standalone Look
behind are in **Graphics > HUD & effects**. Look behind works online too.

The optional **Analogue Acceleration & Braking** Custom Magic Code adds proper
trigger travel for all three standard vehicle types. Its bindings, calibration
and live bars are in the mod's **Details**. It must be installed and enabled.

**Controls > Controller feedback** offers Classic/Enhanced effects, per-player
settings and output diagnostics. DualSense extras and direct Deck patterns have
capability checks and experimental options. Physical feel depends on the device
and input route; the LCD Deck has trackpad haptics rather than grip rumble motors.

See [Graphics](docs/GRAPHICS.md), [analogue driving](docs/ANALOGUE-DRIVING.md)
and [controller feedback](docs/CONTROLLER_FEEDBACK.md).

## Online

Sign in with your Mod Library Discord account, browse Public/Friends lobbies,
or host a Private/Public lobby. Up to four racers and eight spectators can share
a supported lobby; Adventure supports two racers. Host approval, compatible
ROM/build/settings, verified mods and save admission still apply.

Lobby cards show connection timing for hosts and clients. **Check Rollback
connection** opens the shared Scoreboard. Ping and route readings describe the
connection; they do not guarantee a frame rate. Initial startup uses the new
Syncing Game screen until the game is ready.

Hosts can open **Host settings** while the room is open or during a race. Press
**Apply changes** to update prediction/input delay live or close new spectator
joins. Capacity, menu and replay edits made during play wait until the lobby.
Fresh profiles use automatic delay and a 14-frame prediction window; saved
preferences are kept. Larger windows can cover more latency and need more memory
and correction work. Spectators never supply racing inputs; loading and catching
up can take longer than the initial 1.5-second playback-buffer target.

See [Online](docs/ONLINE_MULTIPLAYER.md), [public lobbies](docs/PUBLIC_LOBBIES.md)
and [spectators](docs/SPECTATORS.md). Gameplay uses the admitted peer transport.
The website handles discovery and accounts. In a supported waiting lobby, the
host can offer ownership to another connected player. Everyone stays in the
lobby once the new host accepts and the transfer completes. If the current host
disconnects before a transfer completes, the lobby closes.

## Free mods and authoring

**Mod Library** has its own sidebar page, including in the overlay. Browse free
tracks, characters, custom cars, Custom Magic Codes and textures; open a card
for details, download counts, comments and reviews. Downloads requested in-game
wait for the launcher before installation. **Installed Mods** manages your local
content, including Textures and Track Lab. Changed executable code needs approval.

Custom `.dkrkart` cars change visuals, not handling. They are selected after
character selection in Track Select; custom hovercraft/planes remain reserved
formats. Track Lab imports `.dkrmap` courses and supports Blender authoring,
music, minimaps and matching HD packs. A battle map needs valid mode-specific
objects and connected AI paths; selecting a battle type alone is not enough.

SDK 2.0 remains additive to API 1. See [mods](docs/LEGACY-MODS-BETA.md),
[vehicles](docs/VEHICLES.md), [Track Lab](tools/blender/README.md) and the
[Modding Bible](modding/MODDERS-GUIDE.txt). Distribute only assets you have
permission to share; keep original-game texture dumps private.

## Saves, support and updates

[Saves and Save Manager](docs/SAVES.md) explains personal, online and development
progress. Updating does not require a reset or mod reinstall. Portable mode
selects a different profile location on the next launch; it does not move data.

Automatic crash reporting is optional. The first-launch notice saves your choice,
and **About DKR-R > Support** can change it later. Reports use redacted log
snapshots and retry while the launcher is open after a connection failure.
Raw dumps need separate consent and can contain personal information. Complete
anonymity cannot be guaranteed. See [crash reporting](docs/CRASH_REPORTING.md).

The launcher checks GitHub for compatible signed updates; downloading and
installing remain your choice. See [Updates](docs/UPDATES.md).

Startup cleanup clears verified leftover downloads, installation preparation and
old caches once per launch, continuing at low priority while playing.
Completed Mod Library installs also clear their own verified disposable files
without starting another full cleanup pass.
**About DKR-R > Support > Storage** provides a preview and manual
controls. Saves, ROMs and installed content are kept. See [Storage](docs/STORAGE.md).

## Documentation and contributing

- [All current guides](docs/README.md), [release notes](docs/releases/1.0.16/RELEASE-NOTES.txt)
  and [known limitations/validation](RELEASE-VALIDATION.md).
- [Troubleshooting](docs/TROUBLESHOOTING.md), [building](docs/BUILDING.md),
  [development](docs/DEVELOPMENT.md) and [contributing](CONTRIBUTING.md).
- [Release procedure](docs/RELEASING.md), [third-party credits](THIRD_PARTY.md),
  [licence](LICENSE.md) and [runtime licensing](runtime-recomp/COPYING-NOTICE.md).
