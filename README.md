# DKR-R — Diddy Kong Racing Recompiled

<img width="1672" height="941" alt="Diddy Kong Racing Recompiled" src="https://github.com/user-attachments/assets/c913bee2-029c-4e29-9921-0f6abe7a3bf1" />

<p align="center">
  <a href="https://discord.com/invite/AMWfXdBjNP">
    <img src="https://img.shields.io/badge/JOIN%20OUR%20DISCORD-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join our Discord">
  </a>
</p>

DKR-R 1.0.7 brings clearer online compatibility checks, built-in update checks
and a much lighter lava glow. The launcher and overlay have had a tidy-up too.

The current development package is **v1.0.7 beta 2**. It adds Custom Magic Codes,
Custom Trophy Races and AI & Boss Difficulty, with a controller-first cup editor
and cup sharing. Start with the [player guide](modding/USER-GUIDE.txt), or the
[Modders Guide to Custom Magic Code Creation](modding/MODDERS-GUIDE.txt) to make
your own. The SDK includes a navigable offline HTML edition too.

DKR-R is a native static recompilation of Diddy Kong Racing. Bring your own
legally obtained US v1.0 or US Rev A/v1.1 ROM; no game ROM or extracted
Nintendo/Rare assets are included.

## What's new in 1.0.7

- Fix a cross-platform compatibility trap where Windows and Linux line endings
  could give identical replay code different online identities.
- Online checks the replay engine before joining and compares the actual
  prepared mod resources before admitting a client. Startup errors name the
  mismatched part instead of reporting one catch-all error. Everyone in a
  lobby needs 1.0.7; don't mix these packages with 1.0.6.
- The launcher checks GitHub for updates in the background. There is also a
  **Check for updates** button in Play and update controls in About DKR-R.
  Downloads are verified and installation needs your approval. Updates need
  signed release metadata; an ordinary GitHub ZIP alone is not an update offer.
- On Windows, **DKR-R.exe** is the only application to launch. The runtime and
  helpers stay in `libexec/dkr-r`; keep the complete package together.
- Nearby lava glow uses cached scenery colours, not a full-screen ray-traced
  lighting pass. The pulse stays hot, blending yellow-white with orange-red
  instead of black. Glow has its own on/off and intensity controls; hardware
  reflections are separate and can still be demanding.
- Launcher and overlay wording is shorter and more consistent. Unnecessary
  connection/reflection detail sections have been removed. Streamers can
  hide the lobby code without changing the code shared by Copy.
- PR #60 adds repeatable nightly/stress checks, compact dependency caches,
  a reviewed libjuice callback fix and safer Quick Join channel opening.

See the [changelog](CHANGELOG.md), [online guide](docs/ONLINE_MULTIPLAYER.md)
and [release validation](RELEASE-VALIDATION.md) for the details and test coverage.

## Carried forward from 1.0.6

- Skip intro logos in Play, then choose Start menu or Character select. The
  Start menu keeps its opening sequence and music. Online startup stays unchanged.
- Enhanced water has had another stability pass, including effect popping,
  reflection handling and the Whale Bay Track Select crash. Waterfalls now have
  continuous flowing detail with corrected scrolling/interpolation, transparency
  and reflection masks. The river near Snowflake Mountain follows its authored flow.
- There is much more control over the water! Original, Performance, Balanced,
  High and Custom presets are joined by separate reflection, wake and spray
  quality controls. Fine-tune reflection strength/range, roughness, ripples,
  foam, clarity, refraction, waterfall detail and water tint. Presets leave
  your game resolution, MSAA, FPS target and reflection method alone.
- Hot Top Volcano has enhanced lava with moving molten cracks and rough
  hot-rock detail. Version 1.0.7 replaces its costly surrounding lighting with
  the lighter glow described above; surface detail and reflections stay separate.
- Every custom Track Select row now has a CUSTOM TRACKS header and cycles
  through six backgrounds. Future Fun Land restores its own artwork when you
  scroll back. Images, names and arrows stay with their wooden frames on wide
  screens, including the fix for brief horizontal stretching during transitions.
- Custom `.dkrmap` artwork shares the correct texture/sprite namespace with
  character mods. Asset-table length, offset and allocation checks address
  Track Select failures when combining tracks and large character libraries.
  Very long in-game captions are shortened without renaming your installed mods.
- Offline custom tracks can use the vehicles allowed by their headers.
  Track Lab can generate looping Track Select preview flights along the race
  line, while preserving flights placed by the author. Export validation warns
  about race starts that force a different vehicle. Online vehicle selection
  keeps its existing restrictions.
- The Modern camera has revised corner, drift and wall-hit behaviour, less
  spring lag and no corner roll by default. Existing tuning files migrate
  while preserving changed values. The camera remains an offline feature.
- Malformed graphics commands now have stronger bounds and execution-budget
  checks. Water rendering also has additional allocation, viewport and MSAA
  safety checks. Golden Balloon and Wave Race credits use the same tidy link
  rows as the other technology credits.
- Contributor work adds safer dependency setup, build locking and packaging,
  improved macOS support, and an iOS sideload source port. Apple binaries are
  not part of this delivery. Windows, both Linux architectures, Steam Deck and
  Android use the same release source.
- Developers now have a regression/performance harness with retained reports,
  private retail replay checks, repeatable build presets, faster incremental
  builds and CI caching, workflow linting and mobile compilation checks.

See the [1.0.6 changelog](CHANGELOG.md#106--12-october-2026) for the full breakdown,
[Graphics](docs/GRAPHICS.md) for the water/lighting controls, and
[Track Lab](tools/blender/README.md) for authoring changes.

The features carried forward from 1.0.5 include:

- Custom tracks sit below the original worlds in Track Select, with the wooden
  frames and previews intact. Your original tracks stay where they belong.
- Custom characters join the original animated Player Select stage. Four are
  shown at a time; highlight a custom slot and use L/R to browse the rest.
  Eligible races can also use custom characters for AI racers.
- Import legacy `.xdelta` patches and ZIPs, or make your own `.dkrmap` tracks
  with the Blender Track Lab addon. Reviewed behaviour adapters support more
  than asset replacements, without running arbitrary patched ROM code.
- Online play supports 2–4 racers, with two-player Adventure. Auto selects
  Experimental Rollback; Host Prediction and Strict Input Sync remain available.
- The host chooses the online mod set. Clients can consent to receive missing
  mods, reuse matching installed content and keep verified downloads for
  offline play.
- Multiple texture packs can be enabled together. Lower priority number means
  higher priority when textures clash.
- The launcher and in-game overlay share the updated styling. There's a
  dedicated Textures page, tidier mod cards, duplicate review and bulk source
  preparation.
- Modern presentation includes grouped 4:3/Fit to window HUD layouts,
  interpolation, camera clipping improvements, scenery controls and optional
  enhanced water with screen-space or experimental hardware reflections.
- An optional Modern camera and F7 tuning panel have been added for local races.
- Save ownership is separated between offline, modded and online play. Support
  tools now retain log history as well as the latest two runtime logs.
- Performance, audio, model lifetime, water rendering and online preparation
  have had further fixes. See the [changelog](CHANGELOG.md) for the breakdown.

Experimental Rollback and hardware ray tracing are still experimental.
A public version number does not mean every network, mod or GPU has been
qualified. The [platform guide](docs/PLATFORMS.md) explains the current coverage.

## Getting started

1. Download the complete [release package](https://github.com/ThatGuyMcd/DKR-R/releases)
   for your platform and check its published SHA-256.
2. Extract the Windows ZIP and start `DKR-R.exe`, or make the Linux AppImage
   executable and start it.
3. In **Play**, use **Add a ROM…** and select your original ROM. The Game ROM
   dropdown lets you switch between installed revisions.
4. Choose **Accurate** for the original 4:3/30 FPS presentation, or **Modern**
   for widescreen, higher presentation rates and the additional settings.
5. Optionally tick **Skip intro logos** and choose **Start menu** or
   **Character select**, then select **PLAY OFFLINE**.

Skip intro is remembered between launches and is off by default. It does not
change online startup. Start menu retains the normal title sequence and music;
Character select follows the normal title exit into Player Select. Track Lab's
direct-track launch still takes priority when it is enabled.

`.z64`, `.v64` and `.n64` byte orders are detected from the file itself.
Both supported revisions run inside the same application; there's no separate
revision launcher. See [ROM setup](docs/ROM_SETUP.md) for hashes and import help.

Open the overlay with **Escape**, **F1** or controller **Back/View**.
**Alt+Enter** and **F11** toggle fullscreen. The overlay keeps the game behind
it while using the launcher's menu styling.

Higher presentation rates interpolate the game image. They do not speed up
race timers, physics or the original audio clock.

## Online play and mods

Open **DKR-R Online**, choose **Host / Quick Join**, then create a lobby or
enter a friend's five-character code. Friend invites use the same lobby system.
Everyone needs a compatible complete build and the same original ROM revision.
Quick Join needs no account or manual port forwarding, but restrictive networks
can still prevent a direct connection.

The host is the Mod Master! Enable the tracks and characters before creating
the lobby. Clients review the requirements before downloading anything.
Only missing content is transferred, and no ROM is shared. Online activation
does not replace the client's offline mod choices or personal saves.

For setup, backends, save handling and troubleshooting, see
[online multiplayer](docs/ONLINE_MULTIPLAYER.md),
[legacy mods](docs/LEGACY-MODS-BETA.md) and [save protection](docs/SAVES.md).

## Texture packs and Track Lab

For those of you looking for the DKR-R Community Texture Pack, it can be found
in our [Discord server](https://discord.com/invite/AMWfXdBjNP). The pack is a
separate download, not part of the application.

Use **Textures** to import packs, set their priorities and manage CRT filters.
[Texture-pack instructions](docs/TEXTURE_PACKS.md) cover RT64/Rice formats and
Track Lab's matching HD packs.

Want to make your own tracks? Start with the
[Track Lab addon guide](tools/blender/README.md). The launcher integration is
under **Mods / Hacks → Track Lab**.

## Saves and support

Back up your saves before updating. **Save Manager** separates Offline, Online,
Modded and Recovery views. Portable mode is available in Play; switching it
takes effect on the next application launch and does not move existing saves.

For reports, open **About DKR-R → Support summary**. Export a support summary,
open the logs/crash-dump folders, or use the Android startup-log export when
available. Runtime logs can contain private paths and connection information;
review them before sharing. See [troubleshooting](docs/TROUBLESHOOTING.md).

## Platforms and building

The current Public Build packages are:

- `DKR-R-1.0.7-Windows-x64.zip`
- `DKR-R-1.0.7-Linux-x86_64.AppImage`
- `DKR-R-1.0.7-Linux-aarch64.AppImage`
- `DKR-R-1.0.7-SteamDeck-x86_64.AppImage` (same Linux payload)
- `DKR-R-1.0.7-Android-arm64.apk` (Android 9+, ARM64)
- `track_lab.zip` for Blender.

ARM, Android and Steam Deck still need physical-device checks. Release numbering
is not a promise that every device is qualified. No macOS/iOS binary is included
in this build pass; their source support is preserved.
See [platform support](docs/PLATFORMS.md) for the platform-specific requirements.

Contributing without a ROM? Start with the [developer guide](docs/DEVELOPMENT.md).
`python3 scripts/check.py` runs the fast source/tooling checks without a C++
build or dependency downloads; host presets cover the C++ tests.

For a source build, follow [Building DKR-R](docs/BUILDING.md). Both ROM
revisions and the appropriate Patch Pipeline outputs are required for a
complete release. Never hand-edit submodules or generated recompilation files.

## Documentation

Background GitHub release checks are available under **About DKR-R → Updates**,
with a manual check in Play, verified downloads and user-approved installation.
Your saves, mods and portable data stay in place. Published v1.0.6 needs one
manual upgrade to get this feature. See [Updates](docs/UPDATES.md) for supported
install routes, recovery and release-signing requirements.

The [documentation index](docs/README.md) separates player guides, modding,
development, proposals and historical investigations.
[Release validation](RELEASE-VALIDATION.md) records what was checked for each
delivery.

## Credits and licensing

DKR-R was created by ThatGuyMcd with help from the project's contributors.
Thanks to the contributors behind the platform support, Modern camera,
Track Lab, graphics safety and development tools. The 1.0.6 integration includes
the merged work from PRs #51–#59. Version 1.0.7 also includes PR #60 alongside
the local updater, launcher and lava changes.

The application icon was created by
[POOTERMAN](https://www.deviantart.com/pooterman). `sr.gu` leads the DKR-R HDR
Texture Pack Project. Golden Balloon informed the grouped HUD work, and
Wave Race 64 Recomp provided the enhanced-water renderer reference.

DKR-R uses N64Recomp, N64ModernRuntime, RT64, SDL2, a private SDL3 input helper
on supported desktop builds, and Dear ImGui. See [THIRD_PARTY.md](THIRD_PARTY.md)
and [LICENSE.md](LICENSE.md) for credits, provenance and licensing.

Diddy Kong Racing and its related names and assets belong to their respective
owners. DKR-R does not include or grant rights to the game ROM or its assets.
