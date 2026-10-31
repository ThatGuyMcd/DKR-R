# DKR-R - Diddy Kong Racing Recompiled

<img width="1672" height="941" alt="Diddy Kong Racing Recompiled" src="https://github.com/user-attachments/assets/c913bee2-029c-4e29-9921-0f6abe7a3bf1" />

<p align="center">
  <a href="https://discord.com/invite/AMWfXdBjNP">
    <img src="https://img.shields.io/badge/JOIN%20OUR%20DISCORD-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join our Discord">
  </a>
</p>

<p align="center">
  <a href="https://positron-portal.com/dkr-r/">
    <img src="https://img.shields.io/badge/OPEN%20MOD%20STORE-2EA44F?style=for-the-badge" alt="Open Mod Store">
  </a>
</p>

## DKR-R v1.0.12 Release Build

I've worked on making modded lobbies quicker to join and tidied up the Online
menus. Friends now have compact cards, profile pictures can be changed, and
Online and the Mod Store share one display name. Steam Deck can remember your
Discord sign-in without a desktop keyring. The FOV slider also works live now.
All the existing memory, HUD and spectator fixes are kept.

Your saves, mods and settings can stay. Please use the complete v1.0.12
package on every machine in an Online lobby.

DKR-R is a native static recompilation of Diddy Kong Racing. Bring your own
legally obtained **US v1.0 or US Rev A/v1.1 ROM**. No ROM or extracted game
assets are supplied.

## Get started

1. Download your platform's complete package from [GitHub Releases](https://github.com/ThatGuyMcd/DKR-R/releases).
2. Windows: extract the whole ZIP and start **DKR-R.exe**. Keep `assets` and
   `libexec` together; the private helpers are part of the installation.
3. Linux / Steam Deck: make the **AppImage** executable and launch it.
4. Add your original ROM in **Play**, choose your controls and start.

v1.0.12 is available for Windows x64, Linux x86_64, Linux ARM64 and Steam
Deck. Both Linux architectures have AppImages. Linux needs glibc 2.38,
GLIBCXX_3.4.32 and a compatible Vulkan driver.

Android releases are no longer supported. I'm leaving Android out until
performance is good enough. There are no new macOS or iOS downloads in
this release. See [platform support](docs/PLATFORMS.md) for the details.

Accurate keeps the original presentation. Modern offers widescreen,
interpolated presentation rates and optional graphics settings. **Escape**,
**F1** or controller **Back/View** opens the overlay.

## Online, public lobbies and spectators

- Reuse your Mod Store Discord sign-in, or choose **Sign in with Discord** in
  DKR-R Online. Offline play and local achievements need no account.
- **Public** shows worldwide advertised lobbies; **Friends** keeps the existing
  friend route. The online total counts connected signed-in accounts, with
  duplicate launcher instances counted once.
- Hosts choose **Private** or **Public**. Private is the default; private rooms
  stay out of the public list and use friends or lobby codes.
- Host approval, available places, compatible builds, ROMs, saves and verified
  session mods still apply. A listing does not grant a controller port.
- Up to four racers and eight spectators can share a spectator-capable lobby.
  Watch all splits, follow a racer or use the T.T.-style director. The host can
  approve racer/spectator swaps without restarting the application.
- Run **Check Rollback connection** while waiting to share the same Scoreboard
  with everyone. Grades estimate connection timing; they are not an FPS guarantee.

The website API handles discovery, presence and admission. Gameplay continues
over the existing peer connection and Rollback path. Restrictive networks can
still prevent direct connections. Joining a running race through the directory
is separately gated by the service's spectator setting; it never admits a late
racer. There is no host migration or bundled gameplay relay.

Use matching complete v1.0.12 packages on every peer. Start with the
[Online guide](docs/ONLINE_MULTIPLAYER.md), [public lobby guide](docs/PUBLIC_LOBBIES.md)
and [spectator guide](docs/SPECTATORS.md).

## Mods, cars and authoring

**Mods / Hacks → Mod Store** browses tracks, characters, Vehicles, Custom Magic
Codes and texture packs, with screenshots, account features and managed
Install / Update / Repair actions. Changed executable code needs your approval.

Import supported **`.dkrkart` custom cars** directly or from ZIPs. Enable them
under Vehicles, then choose a car after character selection in Track Select.
Vanilla stays available. Cars replace visuals, with shared online choices;
handling, collision and Adventure are unchanged. Custom hovercraft and plane
exports are reserved formats, not supported vehicle types in this release.
See [Vehicles](docs/VEHICLES.md) and [mods and installation](docs/LEGACY-MODS-BETA.md).

**Track Lab** provides Blender track authoring and `.dkrmap` imports, custom
music, minimaps and matching HD packs. **Custom Magic Codes / SDK 2.0** add
supported hooks, game services, tools and live development features. API 1
remains supported; existing packages do not need a forced conversion.

Custom Trophy Races, the Dev Menu and ModernCamera++ remain optional code mods.
The Dev Menu uses separate development progress. SDK tools do not grant
unrestricted access to native engine memory or other players' local settings.
Start with the [Modding Bible](modding/MODDERS-GUIDE.txt) and
[Track Lab guide](tools/blender/README.md).

**Textures → Texture Dumping** can export installed content, capture textures
encountered during play, browse a dump and create a starter replacement pack.
Working edits survive re-export. Keep dumps of original artwork private;
distribute only artwork you have permission to share. See [texture packs and authoring](docs/TEXTURE_PACKS.md).

## Achievements, saves and updates

**Achievements** tracks 94 personal goals locally, with filters, pinned goals,
notifications and import/export. Existing provable save milestones can be
recognised explicitly. It has no RetroAchievements login or account sync.
Watching and development sessions do not award gameplay achievements.
See [Achievements](docs/ACHIEVEMENTS.md).

Ordinary mods share your personal Adventure. Host online progress, client
session copies and development saves remain separate. Save Manager preserves
older mod-set saves under **Backups & Recovery**. Updating does not require a
save reset or a mod reinstall. See [Saves](docs/SAVES.md).

The launcher can check GitHub and offer compatible signed updates. Downloads
and installation need your choice. A release needs both `DKR-R-update.json`
and `DKR-R-update.sig`, signed with the existing trusted release key, before
automatic installation is available. See [Updates](docs/UPDATES.md).

## Guides and contributing

- [Documentation index](docs/README.md), [v1.0.12 release notes](docs/PUBLIC-1.0.12-NOTES.txt)
  and [changelog](CHANGELOG.md).
- [Troubleshooting](docs/TROUBLESHOOTING.md) and [release validation](RELEASE-VALIDATION.md).
- [Building](docs/BUILDING.md) and [development](docs/DEVELOPMENT.md).
- [Licence](LICENSE.md), [third-party credits](THIRD_PARTY.md) and [asset policy](docs/ASSET_POLICY.md).

Changes go through the Patch Pipeline. Do not edit dependency checkouts,
`RecompiledFuncs` or `RecompiledPatches`. Builds and static package checks are
recorded separately from physical-device, controller and multiplayer acceptance.

## Contributors and credits

POOTERMAN created the application icon. `sr.gu` leads the DKR-R HDR Texture
Pack Project. Golden Balloon informed the HUD work; Wave Race 64 Recomp
provided the enhanced-water reference. Platform ports, Track Lab, runtime,
Quick Join and build/CI improvements retain their contributor attribution in
the source, changelog and third-party notices. Thanks to the mod creators and
players providing reports. See [third-party credits](THIRD_PARTY.md) and the
[runtime notice](runtime-recomp/COPYING-NOTICE.md).
