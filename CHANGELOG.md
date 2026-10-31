# DKR-R changelog

## v1.0.12 Release Build — 26 October 2026

I've focused on modded lobby joining and made the Online menus easier to use.

- More capacity for session mods and the initial checkpoint, fewer chunk flushes,
  a bounded prepared-content cache and clearer join timings.
- Fixed newer character/vehicle menu callbacks being refused online. Reliable
  racing input has its own lane, with the existing repair and ownership checks.
- Closing a lobby immediately retires its directory entry while keeping Online
  connected. Terminal connection failures also retire the hosted entry.
- Character portraits, compact friend cards, Add a friend above Search and
  friend requests from eligible lobby player cards.
- A shared profile header and display name for Online, friends and the Mod Store.
  Custom PNG/JPG pictures, optional Discord name/image import and picture sharing.
- Tidier lobby summary/visibility controls and account settings, with readable
  name fields and no duplicate Discord username or review/request buttons.
- Linux/Steam Deck remembered sign-in when a desktop keyring is unavailable.
- Live offline FOV adjustment, including paused races, with the corrected guest
  register handling. Existing online restrictions stay.
- Windows updater log handoff and better session diagnostics.
- Fixed installed Store mods returning to Needs Attention after restarting.
  Existing completed entries are recovered without downloading them again.
- Integrated the patch guardrail setup and follow-ups from PRs #76–#78. The full
  source consolidation remains separate.
- Updated current guides and prepared Windows x64, both Linux AppImages and the
  matching Steam Deck download with signed update metadata. No Android/Apple build.

The earlier memory, HUD, water, custom-car and spectator fixes stay. Saves, mods
and settings can stay too. Everyone in a lobby needs the complete v1.0.12 package.

## 1.0.11 — 25 October 2026

This update is mainly about the memory issues and missing HUD elements
people have reported. I've kept the launcher design and the v1.0.10
Online fixes in place.

- Added expansion RAM headroom to the normal game heap, keeping its upper guard.
- Added safer allocation failure handling and useful HUD memory diagnostics.
- Protected graphics data before it waits in the game scheduler.
- Kept queued water animations consistent, with bounded cache recovery.
- Added the larger textures and explicit driver seating in updated .dkrkart cars.
- Updated the guides, Windows/Linux builds and signed auto update files.

Windows x64, Linux x86_64/ARM64 and Steam Deck are included. Android,
macOS and iOS are skipped. Saves and installed mods can stay.


## 1.0.10 — 25 October 2026

This update is mainly about Online stability. I've kept the current launcher
design and brought the accepted playtest.4 fixes into the release.

- Fixed modded lobby preparation getting stuck and improved verified cache reuse.
- Added clearer join progress, Retry Join and Copy Join Diagnostics.
- Fixed races failing to start when spectators were in the lobby.
- Fixed spectators getting a black screen and no audio during custom races.
- Fixed matching downloaded mods conflicting with existing offline copies.
- Tidied up Open Lobbies, keeping more space for the lobby list.
- Kept the earlier Track Select custom-music fix and existing online safeguards.
- Updated the guides, Windows/Linux packages and signed updater metadata.

Windows x64, Linux x86_64/ARM64 and Steam Deck are included. Android is no
longer supported; no new macOS/iOS downloads are included. Use matching
complete v1.0.10 packages for Online. Saves and installed mods can stay.


## 1.0.9 — 24 October 2026

- Add worldwide Public/Friends lobby browsing, distinct signed-in account counts,
  shared Discord sign-in and Private/Public hosting to the existing launcher.
- Keep private rooms out of public lists; preserve host approval, friend/code
  admission, role/capacity checks and verified mod/save preparation.
- Add the private website/API directory for discovery and admission. Existing
  peer transport/Rollback runs gameplay; FTP mod uploading is unchanged.
- Retain up to four racers/eight spectators, spectator cameras and confirmed
  host-approved swaps. Running-spectator directory entry is separately gated.
- Offer Rollback as the public host mode, removing the old backend selector.
- Add the shared connection Scoreboard with separate racer/spectator grades.
- Retain countdown/start, checkpoint-clock, party departure and lobby-return
  repairs; preserve approved preparation across benign account/roster refreshes.
- Add custom .dkrkart cars, native Track Select picking, CPU choices, reviewed
  seating/wheel/LOD handling and Store Vehicles installation/update/repair.
  Hover/plane exports remain reserved; custom cars do not change handling.
- Add 94 local achievements, explicit existing-progress recognition, pinned
  goals and safe progress import/export, without account synchronisation.
- Add installed-content texture export, live capture, dump browsing and starter
  replacement packs, preserving edited Working files on re-export.
- Retain SDK 2/API 1 compatibility, shared personal saves, isolated online/dev
  progress, Camera++, previous presentation fixes and the Windows memory fix.
- Retain merged upstream track-ID safety/failure-safe imports, saved graphics
  preset display and reviewed TLS integration.
- Preserve website batch entry, image clear/reset, five automatic upload retries,
  FTP file usage filters and the Vehicles label with stored category ID `legacy`.
- Rebuild Windows x64, both Linux AppImages, Steam Deck and Android ARM64. APK
  versionCode is 1090001 with the original signing identity. Linux ABI requirements
  are GLIBC 2.38 and GLIBCXX_3.4.32. New Apple packages were not built in this pass.
- Refresh current guides and release/signing instructions; preserve dated records
  and separate static build verification from physical-device acceptance.

## 1.0.8 — 20 October 2026

- Fix Windows startup committing the 4 GiB guard/address reservation.
- Commit only the existing 512 MiB usable RDRAM; leave 3.5 GiB guarded.
- Log the failed allocation phase, Windows error, requested sizes and memory
  availability instead of the old generic allocation message.
- Preserve the accepted 1.0.7 water/source composition, saves, SDK and mods.
- Rebuild Windows, Linux x86_64/ARM64, Steam Deck and Android ARM64.
- Keep the existing Android signing identity and use version code 1080001.

## 1.0.7 — 20 October 2026

Back up your data and keep the complete package together when updating.
Everyone in an online lobby needs a matching compatible release.

A. CUSTOM MAGIC CODES AND SDK
  1. Import executable Custom Magic Codes through Mods / Hacks as .nrm packages.
  2. Explicit code permission, enabled state and per-package generated settings.
  3. Supported native hooks/calls, typed game services and execution budgets.
  4. Deterministic host-owned settings and code-mod admission online.
  5. API 2 is additive; existing API 1 mods are not forcibly converted.
  6. SDK 2 module/capability queries, stable handles and stale-scene rejection.
  7. Entity/racer inspection, typed/batched edits and supported overrides.
  8. Scene/lap/checkpoint/item/result events and explicit queue overflow.
  9. Static ray/sphere collision queries and route/checkpoint inspection/editing.
 10. Supported scene/weather/audio/race operations with lifecycle/authority gates.
 11. Published controller-friendly tools UI, ordered actions and live settings.
 12. Opted-in cross-mod setting changes, not unrestricted control over other mods.
 13. Read-only symbol/memory inspection within the admitted contract.
 14. Debug lines/boxes/labels, bounded documents/workspaces and source maps.
 15. Scaffolding, package inspection and relocation-safe authoring tools.
 16. Corrected local/split relocation handling and Dev Menu startup/save seeding.
 17. Separate SDK ZIP and one current Modding Bible in TXT, HTML and PDF.
 18. Clear limits: not arbitrary host code, private engine writes or unrestricted
     ROM execution. Unsupported operations remain unavailable, not guessed.

B. EXAMPLE MODS AND CUSTOM TROPHY RACES
 19. Custom cups below Custom Tracks, without replacing stock trophies.
 20. Six-column custom-track rows, custom cup rows and named validation errors.
 21. Choice of tracks, per-round laps/vehicle and ordered/shuffled lineups.
 22. Cups / full-width Lineup / Share screens, single-line round controls.
 23. Controller navigation, on-screen text entry, bounded nested modals/buttons.
 24. Automatic track-catalogue refresh when opening the mod's details.
 25. Data-only cup import/export/sharing; receiving a cup does not invent or
     automatically install missing track assets or grant code permission.
 26. AI & Boss Difficulty sample, plus the separately scoped live SDK 2 example.
 27. Dev Menu: inspect/watch entities and resources, edit supported racer values,
     teleport, tools pause/step, scene controls, route/geometry inspection,
     debug overlays, supported documents/workspaces and opted-in live settings.
 28. Development saves remain isolated. Normal online lobbies do not silently
     become developer sessions; host authority and participant consent apply.
 29. Package conflicts/permissions, lifecycle limits and unsupported tools are
     explained in the Bible rather than promising that literally anything works.

C. MOD STORE AND INSTALLATION MANAGEMENT
 30. Built-in catalogue with both grid/browse and detailed browse-and-inspect views.
 31. Item images/details, categories, searching/filtering and download progress.
 32. Discord account flow, reviews and requests, with role-gated catalogue tools.
     These features use the deployed website backend; its role checks still apply.
 33. One Install action performs automatic verification; no redundant verify click.
 34. Correct direct-package retrieval instead of installing an HTML error page.
 35. Friendly installed mod/texture names instead of the temporary name "package".
 36. Persistent installation receipts for tracks, characters, code and textures.
 37. Installed/Update/Repair state reflects owned content, not download history.
 38. Distinguish exact duplicate evidence from a possible same-name match.
 39. Replace/reinstall, separate copies where supported, editions where identity
     cannot safely be duplicated, and explicit cancel/remove choices.
 40. Update selected/all, skip/pin options, preserved compatible settings and
     reapproval when executable code changes; no silent mod-code activation.
 41. Recoverable background installation with durable transaction journals,
     per-library protection, retained previous editions and startup recovery.
 42. Faster protection of retired whole legacy groups without weakening backups.
 43. Correct canonical managed paths, Unicode and extended Windows ZIP paths.
 44. Complete track extraction including declared music and HD assets.
 45. Nested single-course ZIP discovery fixes the Mario Circuit edge case;
     ambiguous multi-course archives are refused rather than selected randomly.
 46. Exact directory ownership/duplicate-ID handling and stable Installed status.
 47. Viewport-bounded dialogs, reachable cancel, wider keyboard buttons and
     tidier installed/update/account/review/request/moderation layouts.

D. SAVES AND SAVE MANAGER
 48. One personal offline Adventure with ordinary track/character/gameplay mods;
     changing a mod list no longer switches personal save ownership.
 49. Preserve old mod-set saves and migration copies; explicit recovery selection,
     not an automatic merge or deletion of competing progress.
 50. Fixed stock EEPROM layout when added .dkrmap courses extend asset tables.
     Initials/balloon data must not be reinterpreted through a larger course count.
 51. Added-course records use a versioned companion keyed by course/vehicle,
     not extra fields jammed into the 512-byte stock EEPROM.
 52. Save Manager's current personal/online/recovery/Pak views and real targets.
 53. Max Out Entire Save, selected-Adventure completion and shared unlock actions
     commit after confirmation, preserve initials/records and verify the write.
 54. Manual edits use Apply; stale drafts cannot overwrite newer disk progress.
 55. Background save jobs, launch/write fencing, checksum-aware imports and
     recoverable multi-file transfers with old bundle compatibility.
 56. One persistent host online Adventure, Continue default, explicit copy/fresh
     choices and separate client session copies. No personal client overwrite.
 57. Confirmed-only rollback record publication, not speculative persistence.
 58. Development sandboxes seed missing progress from the saved personal file;
     existing sandboxes require explicit refresh. Explain why Dev Menu can show
     different progress, without restoring the old mod-list save-swapping story.
 59. Portable mode remains a different data location, not automatic migration.

E. ONLINE, CONTENT AND PRESENTATION
 60. OS-line-ending-independent replay identity and earlier linked-engine checks.
 61. Prepared playable-resource proofs and component-specific startup failures.
 62. .dkrmap audio/deep-path preparation and native-track admission corrections.
 63. Custom Magic Code backend/launch-state, argument-memory and state-handoff fixes.
 64. Big Donut and Moon Station object-map loading fixes without opening real-ROM DMA.
 65. On/off state for modern .dkrmap cards, not only legacy imports.
 66. My racer fullscreen and local view choices for eligible experimental races.
 67. Native divider removal, full-height sky, offline-style HUD/minimap positioning.
 68. Human display names plus race positions, label sizing/fading and the black-box fix.
 69. PR #60 Quick Join callback/transport and reviewed libjuice callback corrections.
 70. Custom portrait extraction/differences and in-world portraits for Fire Mountain
     and Smokey Castle, including the subsequent crash correction.

Independent online maximum-LOD/scenery parity is not qualified for this
release. The accepted rendering paths and fullscreen HUD/nameplates stay.

F. CAMERA, WATER, CREDITS AND PERFORMANCE
 71. Optional ModernCamera++ importable package, presets, local profiles, input
     bindings and live F7 tuning, with native scripted-scene fallbacks.
 72. Camera++ private renderer ownership and corrected vehicle-shadow interpolation.
 73. Replaces the older camera route with an optional importable package. Camera++ is not automatically enabled or a required gameplay mod.
 74. PC OpenXR work is incomplete. VR, standalone Quest and headset compatibility are not release features.
 75. Lighter cached nearby lava glow, independently switchable from water effects.
 76. Hot yellow-white/orange-red pulses, not black; subtle capped surrounding glow.
 77. Original-water river/waterfall interpolation when enhanced optics are off.
 78. Correct source-frame flow/wrap history across fullscreen/windowed presentation.
 79. Owner-accepted transparent HD water family loading and coherent promotion.
 80. Border-free spinning-logo picture, readable rounded translucent copyright
     plaque, original font and RECOMPILED BY THATGUYMCD! line, higher-FOV sky coverage.
 81. Full-width Start menu character-name travel with original proportions.
 82. Full-frame credits, moving original-font text, lower sky and edge-covering fades.
 83. Credits crash correction: native vertical readback bounds, not negative overscan.
 84. Native GPU allocation validation/guarded mip fallback and replacement-cache headroom.
 85. First surgical performance pass: reused flow storage, larger-table texture
     indexing, CPU mip scratch, exact checkpoint reserve and monotonic presentation.
 86. Optional diagnostic launch/logging. No measured FPS gain is claimed without
     comparable owner captures; deeper GPU/scheduler work is not completed.
 87. Withdrawn atmosphere preview: no new volumetric/dynamic lighting/shadow system
     is advertised. Stock effects and the existing water/lava path remain.

G. LAUNCHER, APPLICATION UPDATES AND CONTRIBUTOR WORK
 88. Shorter consistent launcher/overlay wording and tidier mod cards/dialogs.
 89. Removed unnecessary Save Location/connection/reflection disclosures and
     reorganised support summary/credits with compact support actions.
 90. Hide lobby code for streaming; Copy still provides the invitation code.
 91. Startup and Play-tab application update checks, consent, signed verified
     platform packages, recovery and preserved user data.
 92. One public Windows DKR-R.exe; private runtime/helpers remain in libexec/dkr-r.
 93. PR #60 nightly/sanitizer/stress/cache tooling is retained, along with earlier
     merged CI, platform, Track Lab and test-isolation contributions. PR #49-#59
     work already shipped in 1.0.6 is carried forward, not counted as new here.


## Earlier development records

This is the player-facing release history. Current instructions live in the
[documentation index](docs/README.md); dated investigations retain the findings
and limitations of the build they describe.

## 1.0.7 beta 5 — 17 October 2026

- Add renderer-owned maximum racer detail for Auto / Experimental Rollback,
  independent on every device. Keep separate animated vertices and materials,
  bounded optional work and an original-mesh fallback; do not redraw the world.
- Extend supported live scenery models and billboards using local distance
  and coverage settings, alongside the retained retail scenery cache. Share
  the controls between launcher and overlay. Terrain and procedural wave-grid
  allocation remain shared; this is not an unrestricted world redraw.
- Correct Modern four-player object ranges and retain eligible noninteractive
  local multiplayer scenery when requested, without changing time trials.
- Carry forward beta 4's online mod, Track Select and startup corrections.
  Windows x64 and Linux x86_64 AppImage only. Every peer needs beta 5.
  Gameplay acceptance remains with the maintainer.

## 1.0.7 beta 4 — 16 October 2026

- Fix the Custom Magic Code argument bridge for online Track Select previews.
  Preserve the loader's separate stack without opening private engine memory
  to mods. Code errors now identify the package, callback, PC and memory read.
- 17 October follow-up: fix the online startup crash with Custom Magic Codes
  by capturing their state before the original game memory is released. Fix
  modern tracks rejected at valid odd geometry offsets; retain all source and
  destination bounds and real-ROM DMA rules. Both peers need this correction.
- 17 October correction: fix downloaded .dkrmap music validation in deep Windows
  cache paths and prepare native tracks without the legacy ROM importer.
  Preserve verification, installed-file reuse and recoverable cache refresh.
- Fix Custom Magic Code admission reading a joining client's default backend
  before the host's room rules arrive. Keep host and final launch restrictions.
- Fix Big Donut / Moon Station's valid non-8-byte-aligned compressed object-map
  loads in offline and owned online asset bridges, without loosening real-ROM
  DMA, bounds or completion-queue checks. No track re-import is required.
- Offer a cup-preserving update for the two exact withdrawn Trophy Races sample
  packages, improve named cup/track errors and retain failure-stage diagnostics.
  Validate shared-recording metadata per course; hash PCM without copying a
  whole extra song. Use the same corrected package on every online device.
- Add missing On/Off switches for normal .dkrmap imports, not just tracks kept
  from Online. Save local choices across refresh/restart without editing packs.
  Share the controls between launcher and overlay, including track Details.
- Withdraw the Online graphics-parity redraw that caused race-rendering
  corruption. Restore the corrected beta 3 rendering path and decoder bounds.
- Keep the working fullscreen, sky, divider, offline-style HUD and player-name
  corrections. The new Online racer LOD and extended mesh/object range work
  is deferred; offline settings and the existing scenery cache are unchanged.
- Windows x64 and Linux x86_64 AppImage correction only. Compilation and
  static inspection are separate from maintainer gameplay acceptance.

## 1.0.7 beta 3 — 16 October 2026

- Correction rebuild: suppress native divider/coverage fills in local
  fullscreen, repair two-player sky coverage, restore offline-style HUD
  positioning/sizing and keep the minimap/racer markers together. Correct
  reversed nameplate combiner words responsible for black label rectangles.
  Use the corrected package on every online device, not a mixture of beta 3 builds.
- Add **Your online view** to DKR-R Online in the launcher and overlay. Choose
  Original split-screen or My racer fullscreen independently on each device.
  This is built in; no Custom Magic Code package is required.
- Expand the admitted local owner's race camera using a full viewport, signed
  clipping and separate HUD placement, without changing player counts, input
  routing, cameras, AI, audio or authoritative race state. Unknown/shared scenes,
  menus, results and two-player Adventure hubs retain the original layout.
- Add depth-tested names and live race positions above other human racers,
  with position-only/off choices and 75–150% label sizing. AI markers stay native.
  Text tiles are cached, distance-faded and isolated from the game's textures.
- Support these presentation options through Auto / Experimental Rollback on
  both US ROM revisions. Local settings never enter host rules, mod settings or
  checkpoints. Matching beta 3 replay engines are required across the lobby.
- Android versionCode is 1070009. Gameplay, controller and online acceptance
  remain with the maintainer; compilation/package inspection do not replace it.

## 1.0.7 beta 2 — 16 October 2026

- Add Custom Magic Codes API 1: portable guest C packages, explicit code
  approval, generated settings, eligible native hooks/calls, typed game services
  and frozen host-owned online configuration through Experimental Rollback.
- Include separately importable Custom Trophy Races 1.0.2 and AI & Boss
  Difficulty 1.0.0. Custom cups use their own Track Select rows after six-column
  custom tracks; the original trophies stay available.
- Give the trophy editor Cups, full-width Lineup and Share screens, controller
  shortcuts, on-screen text entry, automatic track refresh and data-only cup
  import/export. Custom Magic Code cards share the track/character layout.
- Fix right-edge overflow in nested cup dialogs: hide the shared text field's
  internal ImGui label so it cannot push the keyboard beyond its row. Bound
  dropdowns and their choice rows to the current content area.
- Add the full Modders Guide to Custom Magic Code Creation, all public API
  declarations, source examples and a navigable offline HTML edition in the SDK.
- Package label v1.0.7 beta 2 uses 1.0.7+beta.2 internally to retain compatibility
  with the working code packages' minimum 1.0.7 requirement. Android versionCode
  is 1070007. Build/package inspection is not gameplay/device acceptance.

- Withdraw the atmosphere preview: remove its dynamic lighting, shadow maps,
  enhanced/volumetric fog, light shafts and settings from the launcher, overlay
  and Track Lab. Keep the original lighting, fog and shadows, and the existing
  enhanced water/lava effects.

## 1.0.7 initial release candidate — 15 October 2026 (historical)

### Online

- Make replay-engine identities independent of Windows/Linux text line endings.
  Identical generated code no longer looks like a different engine just because
  it was generated on another operating system. Real content changes still fail
  the compatibility check.
- Compare the linked replay-engine identity during lobby admission, including
  packages with the same public version label but different replay code.
- Bind the prepared mod-resource fingerprint into the lobby and launch proof.
  A downloaded source manifest alone no longer counts as a playable resource
  match. Installed verified sources and immutable preparation caches are reused.
- Startup exchanges explicit engine, ROM, resource, schema, checkpoint-size,
  launch, save and epoch fields. A rejection names the component and records
  both identities in the logs. Bounds, authentication, checkpoint integrity,
  save isolation and the normal Loaded/Start gate remain enforced.
- Protocol 50 and mod-transfer protocol 3 require matching 1.0.7 peers.
- PR #60 fixes repeated/missed Quick Join channel-open callbacks and adds
  type-correct libjuice hash callbacks through the reviewed dependency pipeline.
- Add a remembered Hide/Show lobby-code control for streaming. This hides the
  displayed code, not host approval or the value copied for an invited friend.

### Launcher, updates and graphics

- Add background startup update checks and a manual check in Play. About DKR-R
  contains update preferences, download/install controls and recovery status.
  Only signed metadata and verified platform packages may be installed, with
  user approval. Published 1.0.6 needs a manual upgrade to obtain the updater.
- Windows uses one public DKR-R.exe, with runtime/helpers stored privately in
  libexec/dkr-r. Linux retains AppImage updates; Android uses its installer and
  retains the existing application/signing identity.
- Shorten launcher/overlay help, warnings and modals while keeping actionable
  failure reasons. Remove the Connection Details and Reflection Details sections.
- Replace ray-traced surrounding lava lighting with bounded cached scenery
  glow. No per-frame lighting rays or extra GPU lighting pass are needed.
  Old ray-lighting preferences migrate to the lightweight mode. The glow is
  independent of lava surface detail and hardware reflections.
- Lava pulses between yellow-white and orange-red without fading to black.
  Nearby light is subtler, with lower gain, reach and caps even at high settings.
- PR #60 adds nightly sanitizer/stress workflows, compact dependency caches,
  CI summaries and developer documentation.

See [release validation](RELEASE-VALIDATION.md) for checks and outstanding
physical-device/WAN acceptance. Compilation is not a claim of universal
cross-platform determinism or performance.

## 1.0.6 — 12 October 2026

There's more in this update than the new lava! These changes are compared
against the published 1.0.5 source. Existing custom characters, online mod
sharing, texture priorities and save protection are carried forward.

### Water, waterfalls and lava

- Additional enhanced-water stability, scratch-allocation, viewport and MSAA
  protections address effect popping and the Whale Bay Track Select crash.
  Hardware-scene failures retain screen-space reflections where available;
  the UI reports the active method and bounded allocation fallback status.
- New water presets: Original, Performance, Balanced, High and Custom.
  Reflection, wake and spray detail can be set separately. Appearance controls
  cover reflection strength/range, roughness, ripples, foam, wakes, clarity,
  refraction, waterfall detail and RGB tint, with reset controls and optional
  bounded GPU timing logs. These do not change race physics or the selected
  game resolution, MSAA, FPS target or reflection method.
- Waterfalls/fountains use a continuous flowing material, preserve authored
  alpha/fog, and have reflection opacity masks and corrected UV interpolation.
  Authored mapping keeps joined river sheets aligned; Snowflake Mountain's
  river follows the retail scroll direction without reversing vertical falls.

### Play, Track Select and presentation

- Play has a **Skip intro logos** tickbox with Start menu and Character select
  destinations. Start menu keeps its original first sequence, music and reveal
  timing. Both choices are offline-only; online and direct-track startup stay intact.
- Enhanced lava now casts optional ray-tested, pulsating diffuse light onto nearby
  geometry. RTX lava lighting settings control intensity, pulse strength, speed
  and range. A separate tickbox disables only surrounding lighting. Performance,
  Balanced and High lighting quality use quarter-, half- and full-resolution
  lighting buffers; the final game image stays at the selected resolution.
  The surface has rough hot-rock islands, raised-looking ridges and molten cracks
  without adding tessellation or extra ray queries. Unsupported GPUs retain emissive lava.
- Skip intro restores the title music-change/fade boundary before the original
  Start menu initializes. Character select and online startup remain unchanged.
- Every custom track row has a header and cycles through six supplied backgrounds.
  Scrolling back to Future Fun Land restores its own original art.
  The Windows correction routes these draws through the current background
  callback, avoiding the stale menu list that could cover or erase the previews.
- Track Select previews, names and arrows now stay attached to their frames
  when scrolling on wider screens. Signed drawing and a separate bounded cell
  table remove the old 4:3 cut-off without stretching frames or loading extra
  cinematic scenes. The Windows follow-up keeps widened drawing clips separate
  from native framebuffer sizing, preventing the brief horizontal stretching
  of images, arrows and text during scrolling. Full-screen confirmation zoom
  remains unchanged.
- Golden Balloon and Wave Race credits now share the compact name-and-link
  layout used by the other technology credits, with their acknowledgements retained.
- Custom `.dkrmap` 3D/2D textures and sprites share boot-owned namespaces with
  character artwork. Asset-table scans use exact available lengths instead of
  truncating large libraries at 4,096 words. Signed ID/offset, monotonic-table
  and allocation checks reject invalid data rather than guessing or overrunning RAM.
- Offline authored tracks can use the vehicles their headers allow without
  writing fake Adventure progress to the save. Online retains its existing gate.
- Track Lab export generates a looping preview flight when a race/boss track
  has none. Generate Preview Flight creates editable nodes and preserves
  manually authored flights. Validation warns about forced-vehicle race starts.
- Modern camera keeps up better in sustained corners and at speed, smooths
  drift transitions, revises wall-hit recovery and no longer rolls in corners
  by default. Version-2 tuning migration preserves changed preferences.

### Stability and platforms

- F3DDKR graphics commands are checked before fetch, including counted lists,
  out-of-range targets, incomplete packets and loops. Shared command budgets
  prevent malformed lists from running indefinitely.
- All changes merged into main through PRs #51–#59 are integrated, including HLE
  bounds protection, setup/test improvements, session fixture safety, Blender work
  and the iOS source port. Apple binaries are not supplied in this build pass.
- Windows, Linux x86_64, Linux ARM64, Steam Deck and Android use version 1.0.6.
  Android build code is 1060002; it retains the existing app ID and signing identity
  for upgrades. ARM, Android and SteamOS still need physical-device qualification.

- macOS source changes retain the original app artwork, Retina icon support,
  memory-aware online cache sizing, prerequisite checks, concurrent-build locks
  and packaging validation. The iOS sideload source port adds Metal/touch,
  document import, lifecycle and device-only IPA checks. No Apple binary is
  supplied in this release pass; its platform limitations remain documented.

### Developer tooling

- Repeatable regression/performance harness with private retail CPU routes,
  ROM-free checkpoint benchmarks, same-machine baselines, deterministic
  fingerprints and HTML/JSON/JUnit reports with retained attempt logs.

- Fast ROM-free contributor checks, Debug host presets, editor compilation
  databases and the same preset commands in CI.
- Worktree-safe source/dependency paths and incremental generation that keeps
  unchanged output timestamps.
- One checked dependency patch implementation across platforms, non-mutating
  validation, explicit repair, and safer nested dependency preparation.
- More discoverable tests, isolated temporary fixtures, accurate Blender skips
  and failure reporting, and retained CI diagnostics.
- Opt-in Linux packaging, automation-friendly Windows launchers, and source/
  Track Lab archives published only after successful creation and validation.
- Release-mode assertions remain enabled in tests. Direct-session fixtures
  avoid live-worker races; friend-delivery fixtures atomically claim their own
  temporary directories for concurrent runs rather than sharing clock names.
- Windows/Linux/macOS CI gains checked compiler/dependency caches and retained
  test summaries. Android/iOS compilation CI checks platform adapters; it does
  not substitute for complete APK/IPA builds or device gameplay. Workflow
  lint/security checks use pinned, checksum-verified tools and read-only tokens.

## 1.0.5 Public Build — 10 October 2026

There's been a lot added since the earlier releases! This update brings the
current local work together with contributor PRs
[#49](https://github.com/ThatGuyMcd/DKR-R/pull/49) and
[#50](https://github.com/ThatGuyMcd/DKR-R/pull/50).
These contributions are integrated in this source branch; this entry does not
claim the branch has already been pushed or the PRs closed on GitHub.

### Launcher and controls

- Shared launcher/overlay styling, more consistent spacing, readable text and
  backgrounds, and better use of available width.
- ROM selection restored in Play, portable-mode control and startup window mode
  following the selected graphics setting.
- Textures separated from Mods / Hacks. Mod libraries use searchable/filterable
  cards, with details, hide/restore, removal and duplicate review.
- Bulk re-preparation of retained legacy patch sources in Help & imports.
- SDL2/SDL3 controller ownership, hot-plug and discovery fixes, including work
  to avoid periodic controller-discovery stalls.
- Android's separate port adds a mobile menu, finger scrolling and editable
  N64-style touch-control groups. Hardware performance remains under review.

### Tracks and characters

- Additive legacy tracks with wooden-frame previews beneath the original worlds.
  Fresh Track Select entry starts at Dino Domain.
- Four visible custom actors on the original Player Select stage, with L/R
  browsing that skips characters displayed in other custom slots.
- A much larger enabled character roster, bounded by presentation identities
  and memory budgets rather than the previous two/16-character limits.
- Custom AI selections in eligible races and scoped custom portraits/voices
  wherever the mod actually supplies replacements.
- Reviewed native behaviour adapters for racer parameters, object-header data
  and known executable changes. Unknown code still requires a dedicated adapter.
- Track Lab geometry, textures, minimaps, music, waterfalls and HD pack workflows.
  Contributor texture cleanup uses recoverable multi-file replacement.

### Online play

- Auto defaults to genuine Experimental Rollback; Host Prediction and Strict
  Input Sync remain selectable in the same launcher.
- Experimental play follows the normal game boot/menu flow and supports the
  admitted US v1.0/v1.1 payloads, rather than forcing a single test track.
- Checkpoint, input scheduling, scene transitions, Track Select, finish/boss
  handling, resource lifetime and disconnect-return fixes.
- Host-managed mods with client consent, local content reuse, missing-payload
  transfer, host/client progress and offline retention of verified downloads.
- Separate online EEPROM/Pak ownership and confirmed persistence, protecting
  personal offline progress.
- Some simulation-affecting visual settings and independent sound-category
  mixing remain restricted in Experimental Rollback. It is still experimental.

### Presentation and performance

- Grouped one/two-player HUD placement without horizontally stretching artwork.
  The accepted three/four-player HUD remains separate; the HUD editor is disabled.
- Finish interpolation and the timing of the return to 4:3 during frame zoom-out.
- Camera near-plane/clipping work and ultrawide sky-motion corrections.
- Water draw, matching and scrolling-wrap improvements.
- Optional enhanced water, screen-space reflections and experimental hardware
  ray queries, with aspect-ratio, budget, lifetime and performance fixes.
- Bounded renderer workers, owned rendering snapshots, pacing and allocation
  work; fixes for audio dropouts and unsafe custom-model lifetime.
- Optional offline Modern camera with an F7 tuning panel, contributed in PR #50.
- macOS source/build support and setup/lifecycle improvements from PR #49.
- Runtime log history, more useful diagnostics and package/worker checks.

### Delivery and limitations

This delivery contains Windows x64, Linux x64 AppImage and the Track Lab addon.
It does not replace the separate Android/ARM previews or claim a newly tested
macOS release. Automated checks cover compilation, policies, runtime ownership
and package contents; real gameplay, WAN routes and hardware still need testing.
See [release validation](RELEASE-VALIDATION.md) for the recorded results.

## Earlier releases

The previous README recorded these highlights. They are retained as release
history, not as current instructions or a guarantee that every device was tested.

### 1.0.4

The earlier documented Windows/Linux release included both supported US ROM
revisions, Accurate/Modern presentation, controller profiles, gyro, audio/EQ,
Save Manager, Magic Codes, texture packs, CRT filters and support tools.
Its detailed qualification remains in [release validation](RELEASE-VALIDATION.md).

### 1.0.2

Stability and performance fixes.

### 1.0.1

- Shadow/vehicle-prop, texture-alignment and widescreen black-bar fixes.
- Extended controller compatibility, SDL2/SDL3 and native Steam Deck gyro.
- Launcher/overlay improvements and per-vehicle control inversion.
- Two-player online play and shared two-player Adventure routing.
- Quick Join, authenticated friend invitations and synchronised startup.
- Performance work and an audio-overflow crash correction.

### Original release

Accurate 4:3/30 FPS and optional Modern widescreen/interpolated presentation,
local multiplayer controls, audio settings, saves, Controller Paks and renderer
enhancements formed the starting point. The current guides describe how those
features work now.
