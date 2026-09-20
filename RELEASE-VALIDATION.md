# Release validation

Current development target: **v1.0.5 Beta 11** (main revision `8a8e927`). See
[Legacy mod compatibility coverage](docs/LEGACY-MOD-COMPATIBILITY-BETA7.md) and
[Offline mod usage and limits](docs/LEGACY-MODS-BETA.md).
Beta 11 build/package results are recorded with its artifacts; the historical
results below do not qualify that build.

See [Beta 10 regression investigation and qualification](docs/BETA10-REGRESSION-VALIDATION.md)
for the confirmed crash, exact patch scope and remaining visual checks.

Earlier development packages: see
[Optional mipmaps and loading-feedback validation](docs/GENERATED-MIPMAP-VALIDATION.md) and
[Online stability changes and qualification limits](docs/ONLINE-STABILITY.md).
The historical 1.0.4 results below do not qualify the current beta.

## v1.0.5 Beta 3 finish presentation rebuild — 8 September 2026

See [Finish presentation changes and qualification](docs/FINISH-PRESENTATION-VALIDATION.md).
This rebuild delays the post-race aspect switch until the first contracted
wooden-frame task has been submitted, and replaces task-wide finish-camera
interpolation disabling with per-camera shot continuity. Its exact final test
results and package hashes are in
`dist/playtest-v1.0.5-beta.3-finish-presentation-20260908/BUILD-MANIFEST.json`.
The preceding Beta 3 output is preserved. Native visual acceptance is pending;
online protocol 47 and all prior protected systems remain unchanged.

## v1.0.5 Beta 3 boss introductions and client input — 8 September 2026

- Windows Release: 68/68 project suites passed (178.23 seconds).
- Linux Release under Ubuntu 24.04/WSL: 68/68 passed (87.95 seconds).
- An expanded 120-case deterministic transport-clock matrix passed separately
  on both platforms after the full-suite run. It covers 2/3/4 peers, UDP and
  Quick Join commit-history policies, Rollback and Lockstep, 0/20/60/100/150/200 ms
  base RTTs, asymmetric jitter/loss, manual 1/2 and automatic settings, send
  blockage and a client scheduling pause. This tests production protocol/input
  logic, not actual WAN/SDK bandwidth or renderer timing.
- Six held button edges per client were delivered without duplication in each
  case; maximum observed scripted sample-to-consumption delay stayed below
  900 ms including the injected pause. Low manual delay still reduces throughput
  on high RTT paths; use the explicit automatic-delay option for those routes.
- Boss classification reads the active ROM's own redirect table through the
  existing generated payload entrypoint. Normal-race topology, controller slot
  ownership and the baseline/epoch/Arm/Go sequence are preserved.
- Gameplay protocol is 47; all racers require this build. Friend/profile/save
  protocols and dependency patches are unchanged. All 44 patch hashes validate.
- Rollback: `dist/playtest-v1.0.5-beta.2-friend-delivery-20260907`, copied and
  hash-checked with a source snapshot in `build/beta3-baseline-20260908`.
- No game/launcher window was opened for visual testing. Real boss fly-ins,
  both Adventure lead players, normal races after bosses, native Steam Deck,
  rotating physical hosts and distant Internet sessions remain user acceptance
  checks. Beta 3 is not promoted to known-good before that feedback.
- Package checks and artifact hashes are recorded in the Beta 3 output folder's
  build manifest. No GitHub publication is authorized by this packaging pass.

## v1.0.5 Beta 1 split-screen presentation — 7 September 2026

- Windows and Linux Release builds: 64/64 project suites passed on each platform.
- Three-player native Windows validation confirmed corrected world/HUD proportions,
  outward item and position-counter placement, and restored animated-number transparency.
- Scope, evidence, rollback location and remaining platform/player-count qualification
  are recorded in [Split-screen validation](docs/SPLIT-SCREEN-VALIDATION.md).
- No online simulation, controller routing, shadow, dependency-source or launcher
  implementation changes are included in this presentation pass.

## v1.0.5 Beta 1 boundary recovery / four-player — 7 September 2026

- Gameplay protocol 46 requires this rebuild on every connected machine.
- Normal races/minigames allow 2–4 players; JOINTVENTURE Adventure stays 2-player.
- Existing rollback packages and a pre-edit source snapshot are retained in
  `build/rollback-pre-online-gaps-four-player-20260907`; the prior candidate is
  not promoted to known-good solely on automated test results.
- Final-source suite, packaging and native results are recorded in
  `dist/online-boundary-recovery-four-player-v1.0.5-beta.1-20260907/VALIDATION.md`.
- Native chained-cutscene, four-device, Steam Deck and adverse-WAN qualification
  must not be inferred from the protocol regression suite. See the package's
  explicit outstanding gates before promoting this beta to a release.

## v1.0.5 Beta 1 online recovery qualification — 6 September 2026

- Cutscene-correction Windows Release build: 62/62 project suites passed (88.87 seconds).
- Cutscene-correction Linux Release build: 62/62 project suites passed (14.20 seconds),
  Ubuntu 24.04 under WSL. This is not Steam Deck gameplay qualification.
- Gameplay protocol 45 requires matching online-recovery-cutscenes builds on both peers.
- The preceding protocol-44 recovery package failed native consecutive-cutscene
  validation and is withheld. The corrected native balloon-plus-key retest is
  pending; protocol/policy tests are not a replacement for that acceptance gate.
- Regression coverage, protected systems and outstanding native/WAN acceptance
  gates are listed in `docs/ONLINE-STABILITY.md`.
- Output is staged separately from the previous cancellation rebuild; its
  packages and a pre-edit source snapshot remain available for rollback.
- Packaging and native test results are recorded in the output folder's
  `VALIDATION.md`; this section does not substitute historical release results
  for current-package tests.

## DKR-R 1.0.4 historical release validation

Validated on 3 September 2026 from `C:\DKRPort`.

## Reproducibility and repository safety

- `VERSION`, Windows resources and Linux AppStream metadata resolve to `1.0.4`.
- N64ModernRuntime, N64Recomp, RT64 and the matching DKR decomp are locked to
  exact commits in `dependencies.lock.json`.
- Release metadata validation verified all 42 dependency-patch SHA-256 values
  declared by `patches/manifest.json` against the pinned dependency revisions.
- The repository asset scan passed across 1,117 files.
- Final documentation contains no development-stage roadmap language or
  research-project references.
- ROMs, saves, Controller Pak files, local configuration, logs and build caches
  are excluded from source and binary releases.

## Windows x64

- Native Visual Studio 2022 Release build completed.
- All 55 DKR-R regression tests passed, including the two-map gameplay handoff,
  single-viewport Adventure boss policy, dual-control Adventure race policy and
  conflicting-scene rejection cases. The regular-race policy also verifies the
  retail six-racer topology and both four-racer challenge variants without
  weakening the boss or shared-hub gates. Accessory-policy coverage verifies
  that an enabled virtual Mem Pak takes storage-probe priority, a disabled Mem
  Pak falls through to the retail accessory probe, and disabling Rumble Pak
  clears the game-visible rumble mask.
- The built and staged executables passed the virtual Controller Pak round-trip
  and backup-recovery self-test. The staged SDL3 input host and two live
  SDL2-to-SDL3-to-SDL2 switching round trips also passed.
- The Launcher and Overlay share the same `Enable Mem Pak` and `Enable Rumble
  Pak` controls under Controls > Device; both settings retain their existing
  persistent `memory_pak` and `rumble` configuration keys.
- The 40-file staged package and ZIP passed prohibited-extension and N64-header
  scans.
- Executable file and product versions report `1.0.4`.

Deliverable:

```text
dist/DKR-R-1.0.4-Windows-x64.zip
SHA-256 93E3569E13DF6C225B7DDE99D486F98C29A4E1DE9A655F8E778E8C4A44722A25
```

## Linux x86_64

- Native Ubuntu 24.04/GCC 13 Release build completed with RT64/Vulkan.
- All 55 DKR-R regression tests passed, including the same Mem Pak priority and
  Rumble Pak enablement policy exercised by the Windows build.
- The native runtime and packaged AppImage passed the virtual Controller Pak
  round-trip and backup-recovery self-test. The private SDL3 input host and two
  live input-backend switching round trips also passed.
- AppImage staging passed a 189-file prohibited-data scan before SquashFS
  creation.

Deliverable:

```text
dist/DKR-R-1.0.4-Linux-x86_64.AppImage
SHA-256 53AB95E4B067109DA477F256BBF7ADEC33BBB57DBF3CD9318E5C15EDC14DDDF4
```

## Source archive

The deterministic source package passed a 353-file archive scan for unsafe
paths, prohibited game-data extensions and N64 ROM headers:

```text
dist/DKR-R-1.0.4-Source.zip
```

## Scope

These checks are non-interactive release gates. The confirmed gameplay,
graphics, interpolation and audio baseline remains the user-visible acceptance
test for DKR-R 1.0.4.
