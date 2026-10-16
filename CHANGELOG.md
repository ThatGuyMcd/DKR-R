# DKR-R changelog

This is the player-facing release history. Current instructions live in the
[documentation index](docs/README.md); dated investigations retain the findings
and limitations of the build they describe.

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

## 1.0.7 — 15 October 2026

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
