# DKR-R documentation

Start here for the current v1.0.12 instructions. The older plans and playtest logs
are still useful, but they aren't the current feature list.

## Playing and settings

| Guide | What it covers |
|---|---|
| [Getting started](../README.md) | Downloads, ROM import and the main features. |
| [Launcher and overlay](LAUNCHER.md) | Where each tab's settings live and when changes apply. |
| [ROM setup](ROM_SETUP.md) | Supported original revisions, hashes and byte orders. |
| [Graphics and camera](GRAPHICS.md) | Presentation, scenery, HUD, camera and optional water/reflections. |
| [HUD layouts](HUD-WORKSHOP.md) | Grouped 4:3/Fit to window; disabled editor and separate 3/4-player layouts. |
| [Sound](SOUND.md) | Volume, EQ, multiplayer music and rollback mixing restrictions. |
| [Controls](CONTROLLERS.md) | Profiles, N64 setup, SDL2/SDL3, gyro and Android touch groups. |
| [Saves and portable mode](SAVES.md) | Offline/online/modded storage, recovery and profile switching. |
| [Application updates](UPDATES.md) | GitHub checks, signed packages, installation, backups and publishing. |
| [Texture packs](TEXTURE_PACKS.md) | Imports, priorities, CRT filters, track-owned HD packs and texture export/capture. |
| [Custom tracks and characters](LEGACY-MODS-BETA.md) | Legacy imports, roster browsing, AI, adapters and mod management. |
| [Magic Codes](MAGIC_CODES.md) | Launch selection, online rules and one-shot actions. |
| [DKR-R Online](ONLINE_MULTIPLAYER.md) | Public/private hosting, Rollback, Scoreboard, session mods, saves and connection limits. |
| [Public lobbies](PUBLIC_LOBBIES.md) | Shared Discord sign-in, Public/Friends, account count and directory privacy. |
| [Profiles, pictures and sign-in](ONLINE_PROFILES.md) | Shared names, friend cards, Steam Deck persistence and the matching API deployment. |
| [Spectators](SPECTATORS.md) | Watching, cameras, host-approved swaps and late-join gates. |
| [Vehicles](VEHICLES.md) | Custom .dkrkart cars, native selection and managed Store installations. |
| [Achievements](ACHIEVEMENTS.md) | 94 local goals, recognition, notifications and progress protection. |
| [Library administration](MOD_LIBRARY_ADMIN.md) | FTP uploads, batch entries, image controls, usage filters and deployment. |
| [Troubleshooting](TROUBLESHOOTING.md) | Failures, support exports and the correct runtime logs. |
| [Platform support](PLATFORMS.md) | v1.0.12 packages, Linux requirements and excluded platforms. |

## Track Lab and development

- [Modders Guide to Custom Magic Code Creation](../modding/MODDERS-GUIDE.txt):
  complete API 1/API 2 authoring book, settings, native hooks, trophies and online rules.
  The packaged SDK also includes a navigable HTML edition with the full headers.
- [Custom Magic Codes SDK](../modding/SDK.txt) and
  [player guide](../modding/USER-GUIDE.txt).
- [Track Lab addon](../tools/blender/README.md) and
  [addon changelog](../tools/blender/CHANGELOG.md).
- [Custom track format](CUSTOM_TRACKS.md), [level model format](LEVEL_MODEL_FORMAT.md)
  and [object map format](LEVEL_OBJECT_MAP_FORMAT.md).
- [Building](BUILDING.md), [macOS kit](../packaging/MACOS-BUILD-README.md)
  and [development checklist](DEVELOPMENT.md).
- [Architecture](ARCHITECTURE.md), [native runtime](../runtime-recomp/README.md),
  [F3DDKR bridge](F3DDKR.md) and [renderer ownership](RENDER_SNAPSHOT_ARCHITECTURE.md).
- [Legacy test suite](../runtime-recomp/tests/legacy_mods/README.md).
- [Regression and performance harness](REGRESSION.md): repeatable runtime checks,
  private CPU replay routes, checkpoint benchmarks and baseline reports.
- [Reviewed legacy behaviour evidence](LEGACY-BEHAVIOUR-20261010.txt).
  Exact corpus coverage is not blanket permission to execute unknown mod code.
- [Asset policy](ASSET_POLICY.md), [controller database provenance](../assets/controllers/README.md)
  and [CRT asset provenance](../assets/filters/README.md).

## Releases and credits

- [v1.0.12 release notes](PUBLIC-1.0.12-NOTES.txt), [v1.0.11 notes](PUBLIC-1.0.11-NOTES.txt), [v1.0.10 notes](PUBLIC-1.0.10-NOTES.txt), [v1.0.9 notes](PUBLIC-1.0.9-NOTES.txt), [v1.0.8 notes](PUBLIC-1.0.8-NOTES.txt) and
  [documentation audit](DOCUMENTATION-1.0.12.md).
- [Changelog](../CHANGELOG.md), [beta 5 notes](BETA5-1.0.7.txt), [beta 4 notes](BETA4-1.0.7.txt), [beta 3 notes](BETA3-1.0.7.txt), [beta 2 notes](BETA2-1.0.7.txt) and
  [1.0.7 public release notes](PUBLIC-1.0.7-NOTES.txt).
- [Release validation](../RELEASE-VALIDATION.md) separates recorded automated
  checks from user-led gameplay/hardware acceptance.
- [Packaged readme template](../packaging/RELEASE-README.md).
- [Third-party credits](../THIRD_PARTY.md), [project licence](../LICENSE.md)
  and [runtime licensing notice](../runtime-recomp/COPYING-NOTICE.md).

v1.0.12 includes Windows x64, Linux x86_64/ARM64 AppImages and Steam Deck.
Android releases are no longer supported. macOS and iOS are skipped in this
release; their older downloads keep their own version numbers. See the platform guide and validation
record for exact coverage. Hardware ray tracing and unqualified online graphics
experiments must not be inferred from a successful desktop compile.

## Current code-mod API

The implemented SDK 2.0 is additive to API 1. Use the
[Modding Bible](../modding/MODDERS-GUIDE.txt) and exact SDK headers.
[Modding architecture](MODDING_ARCHITECTURE.md) is the historical design,
not a statement that the current SDK is only a proposal.

Game packages contain no offline Player Guide. These guides remain in source;
the separate SDK includes the Bible in TXT, HTML and PDF.

## Historical records

These files keep the original findings, version numbers, commands and test
counts. They are not instructions for installing the current release, and
their limits/results do not automatically apply to later builds.
Paths stay intact so contributor discussions and source references still work.
Recorded local paths and line numbers are snapshots; they may not resolve on
another machine or against today's source. Use the current guides for setup.

### Earlier Android and ARM work

- [Android compatibility candidate — 27 September 2026](ANDROID-COMPATIBILITY-IMPLEMENTATION-20260927.md)
- [Android compatibility and performance audit — 26 September 2026](ANDROID-COMPATIBILITY-PERFORMANCE-AUDIT-20260926.md)
- [Android frame-pacing pass — build 1050025](ANDROID-FRAMEPACING-20260927.md)
- [Android mobile launcher and touch controls — preview.5](ANDROID-MOBILE-UI.md)
- [OPPO/Mali Android startup freeze — 25 September 2026](ANDROID-OPPO-STARTUP-20260925.md)
- [Android performance follow-up — 26 September 2026](ANDROID-PERFORMANCE-FOLLOWUP-20260926.md)
- [Android quality-preserving scheduling pass — 1050026](ANDROID-REALWORK-PRIORITY-20260927.md)
- [Android recording-enabled startup crash — build 1050024](ANDROID-RECORDING-STARTUP-20260927.md)
- [Android quality-preserving renderer pass — build 1050027](ANDROID-RENDER-PASS-20260927.md)
- [Android stability and follow-up optimisation — 25 September 2026](ANDROID-STABILITY-20260925.md)
- [Android startup regression correction — build 1050023](ANDROID-STARTUP-CORRECTION-20260927.md)
- [ARM platform preview — 1.0.5 beta 11](ARM-PLATFORM-PREVIEW.md)
- [ARM preview validation — 2026-09-21](ARM-PLATFORM-VALIDATION.md)
- [DKR-R Beta 15 - Android presentation/performance candidate](BETA15-ANDROID-RENDER-FLOW.txt)

### Beta and playtest release records

- [v1.0.5 Beta 10 — model-load crash and missing HUD investigation](BETA10-REGRESSION-VALIDATION.md)
- [Beta 12 optimisation scope and qualification](BETA12-OPTIMISATION.md)
- [DKR-R v1.0.5 beta 15 - online stability correction (2026-10-07)](BETA15-ONLINE-STABILITY-20261007.txt)
- [DKR-R v1.0.5 Beta 15 - Playtest 10](BETA15-PLAYTEST10-SAVE-PROTECTION.txt)
- [DKR-R v1.0.5 Beta 15 Playtest 11 - Custom Character Roster](BETA15-PLAYTEST11-CUSTOM-ROSTER.txt)
- [DKR-R v1.0.5 Beta 15 Playtest 12 - Host-managed online custom content](BETA15-PLAYTEST12-ONLINE-MODS.txt)
- [DKR-R v1.0.5 Beta 15 Playtest 14](BETA15-PLAYTEST14-ONLINE-REVISIONS.txt)
- [DKR-R 1.0.5 Beta 15 - Playtest 15 Fix 1](BETA15-PLAYTEST15-FIX1.txt)
- [DKR-R 1.0.5-beta.15-playtest.15-fix.2](BETA15-PLAYTEST15-FIX2.txt)
- [DKR-R 1.0.5 beta 15 - Playtest 15](BETA15-PLAYTEST15-ROLLBACK.txt)
- [DKR-R 1.0.5-beta.15-playtest.16-fix.1 - music event-queue correction](BETA15-PLAYTEST16-AUDIO-FIX.txt)
- [DKR-R 1.0.5 Beta 15 Playtest 16 - Memory Safety Correction 1](BETA15-PLAYTEST16-MEMORY-SAFETY.txt)
- [DKR-R 1.0.5 Beta 15 Playtest 16 Fix 2 - Mod library](BETA15-PLAYTEST16-MOD-LIBRARY.txt)
- [DKR-R 1.0.5 Beta 15 - Playtest 16](BETA15-PLAYTEST16-WATER-PERFORMANCE.txt)
- [DKR-R 1.0.5 Beta 15 Playtest 2 - Implementation / handoff](BETA15-PLAYTEST2-STARTUP.txt)
- [DKR-R 1.0.5 Beta 15 Playtest 3 - restoration candidate](BETA15-PLAYTEST3-PARITY.txt)
- [DKR-R 1.0.5 Beta 15 Playtest 4 - bounded output-efficiency candidate](BETA15-PLAYTEST4-PERFORMANCE.txt)
- [DKR-R v1.0.5 Beta 15 Playtest 5 - boss victory scene correction](BETA15-PLAYTEST5-BOSS-DIAGNOSTICS.txt)
- [DKR-R 1.0.5 Beta 15 - Playtest 6 performance candidate](BETA15-PLAYTEST6-PERFORMANCE.txt)
- [DKR-R 1.0.5 Beta 15 - Playtest 7 CPU follow-up](BETA15-PLAYTEST7-CPU.txt)
- [DKR-R 1.0.5 Beta 15 - Playtest 8: local scenery/performance candidate](BETA15-PLAYTEST8-SCENERY.txt)
- [DKR-R 1.0.5 Beta 15 - Playtest 9](BETA15-PLAYTEST9-CAMERA.txt)
- [DKR-R 1.0.5 Beta 15](BETA15-RELEASE.txt)
- [DKR-R Playtest 12 crash investigation / corrective rebuild - 5 October 2026](PLAYTEST12-CRASH-INVESTIGATION-20261005.txt)
- [DKR-R Playtest 12 expanded-roster rebuild - 5 October 2026](PLAYTEST12-EXPANDED-ROSTER-20261005.txt)
- [Modded multiplayer finish-line investigation and candidate correction](PLAYTEST14-FINISH-OUTPUT-FIX-20261006.txt)
- [DKR-R 1.0.5-beta.15-playtest.14 - shared online mod sources rebuild](PLAYTEST14-MOD-SYNC-PERFORMANCE-20261006.txt)
- [DKR-R 1.0.5-beta.15-playtest.14 - model-reference safety candidate](PLAYTEST14-MODEL-REFERENCE-CANDIDATE-20261006.txt)
- [DKR-R 1.0.5 Beta 15 Playtest 14 - online mod admission rebuild](PLAYTEST14-ONLINE-RETENTION-AND-PROGRESS.txt)
- [Playtest 14: trophy scenery model loading and Windows launcher recovery](PLAYTEST14-SPAWN-ASSET-TYPE-FIX-20261006.txt)
- [DKR-R: PR46 integration rebuild (2026-10-04)](PR46-INTEGRATION-20261004.txt)

### Online and rollback investigations

- [Experimental Rollback local graphics: restored implementation status](EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt)
- [Experimental rollback — development checkpoint, updated 1 October 2026](EXPERIMENTAL-ROLLBACK-IMPLEMENTATION-20260927.md)
- [Experimental rollback performance pass — 2 October 2026](EXPERIMENTAL-ROLLBACK-PERFORMANCE-20261002.md)
- [DKR-R v1.0.5 Beta 15 Playtest 2 - EXPERIMENTAL ROLLBACK](EXPERIMENTAL-ROLLBACK-TESTING.txt)
- [Beta 12 — host task lifetime correction](HOST-TASK-LIFETIME-20260925.md)
- [Online latency candidate — 1.0.5-beta.12-online.1](ONLINE-LATENCY-CANDIDATE-20260927.md)
- [DKR-R v1.0.5 Beta 3: Online input delivery and boss introductions](ONLINE-STABILITY.md)

### Legacy import and character milestones

- [Legacy activation phase — v1.0.5 Beta 5](LEGACY-ACTIVATION-WORK.md)
- [Additive legacy Custom Characters — implementation proposal](LEGACY-CUSTOM-CHARACTERS-PLAN.md)
- [Legacy mod compatibility — v1.0.5 Beta 7](LEGACY-MOD-COMPATIBILITY-BETA7.md)
- [v1.0.5 Beta 5 — legacy importer preview](LEGACY-MOD-IMPORT-PREVIEW.md)
- [Native-stage custom character selection — revised proposal](LEGACY-NATIVE-CHARACTER-STAGE-PLAN.md)
- [Native-stage character selector — private candidate](LEGACY-NATIVE-STAGE-IMPLEMENTATION.md)

### Track Lab implementation plans

- [Track Lab - Blender addon plan](BLENDER_ADDON_PLAN.md)
- [A real minimap for custom tracks](CUSTOM_MINIMAP_PLAN.md)
- [Custom music in the Blender track addon](CUSTOM_MUSIC_PLAN.md)

### Presentation and optimisation investigations

- [DKR-R - NSO controller investigation and launcher options](CONTROLLER-STUTTER-AND-LAUNCHER-OPTIONS-20261004.txt)
- [Beta 12 CPU/power follow-up — 25 September 2026](CPU-POWER-OPTIMISATION.md)
- [DKR-R 1.0.5 beta 15 - enhanced-water preview (2026-10-08)](ENHANCED-WATER-BETA15.txt)
- [v1.0.5 Beta 3 — finish presentation rebuild](FINISH-PRESENTATION-VALIDATION.md)
- [Further optimisation pass — 25 September 2026](FURTHER-OPTIMISATION-20260925.md)
- [Optional mipmaps — v1.0.5 Beta 2](GENERATED-MIPMAP-VALIDATION.md)
- [Beta 5 HUD reference alignment — 2026-09-08](HUD-REFERENCE-ALIGNMENT.md)
- [Standalone launcher performance — 1.0.4](LAUNCHER-PERFORMANCE.md)
- [Live texture LOD bias — v1.0.5-beta.1 candidate](LIVE-TEXTURE-LOD.md)
- [Magic Codes: v1.0.4 repair and validation](MAGIC-CODES.md)
- [Optional generated texture mipmaps — approved plan and implementation record](OPTIONAL-MIPMAP-PLAN.md)
- [v1.0.5 Beta 1 split-screen presentation validation](SPLIT-SCREEN-VALIDATION.md)
- [Track performance pass — v1.0.5 Beta 1](TRACK-PERFORMANCE.md)
- [Water-heavy level performance investigation — 26 September 2026](WATER-LEVEL-PERFORMANCE-INVESTIGATION-20260926.md)
- [Water performance pass — implementation and qualification](WATER-PERFORMANCE-IMPLEMENTATION-20260926.md)

### Other historical research

- [Original legacy test milestones](../runtime-recomp/tests/legacy_mods/HISTORY.md).
- [Contributor report on retail AI](../RELATORIO_IA_BOTS.md), retained in its
  original language and author voice.

## Keeping the docs current

A feature change needs its current guide, changelog and affected package notes
updated together. Check menu names, defaults, revision/platform gates and known
limits against source. Preserve licences and dated evidence rather than changing
old test results to look current. See [Development](DEVELOPMENT.md).
