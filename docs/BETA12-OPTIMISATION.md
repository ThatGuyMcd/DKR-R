# Beta 12 optimisation scope and qualification

## Rollback and boundaries

Before this pass, existing modified/untracked source and the previous four
packages were preserved in
`G:/DiddykongWorkFolder/beta12-optimisation-20260924/baseline`.
The original beta11 generation pipeline, Track Lab, Blender tooling, HUD,
custom mods and online synchronization are retained. No dependency checkout,
RecompiledFuncs or RecompiledPatches was edited. CMake applies checked patches
to isolated build copies and fails configuration if their context changes.

## Implemented

- Immutable 8 MiB graphics-task snapshots reuse up to four completed buffers.
  Every submitted task still receives a full copy. Live snapshots never alias;
  pool exhaustion allocates normally, never blocks emulated task completion.
- Social worker exceptions cannot repeatedly retry an expired deadline. Queued
  jobs still wake immediately, and a failing error reporter cannot kill the worker.
- Renderer worker budgets: Android at most 2 raster/2 uber/1 texture workers;
  desktop at most 4/4/2. At least one worker remains in every pool. Android
  replacement-texture retention is limited to 512 MiB; this is not preallocation.
- Presentation waits sleep through the coarse interval once, retaining the
  original adaptive two-millisecond tail. No guest tick, net timeout, rollback
  window or input-sampling deadline is changed.
- Optional Battery (480p/30), Balanced (480p/60), Quality (window resolution/60)
  presets disclose framebuffer/filtering changes before Apply. HUD and aspect
  settings are preserved. Existing saved configurations are not overwritten.
- Android first-use defaults select 480p and standard framebuffer precision.
- Android swapchains request only color-attachment usage actually required by
  final presentation; check capabilities; respect fixed extents, unbounded
  maximum image counts and all composite-alpha modes. Retired swapchains are
  not reused after failed replacement. Presentation is checked before pipelines
  are built. Identity-transform support is explicitly required rather than
  silently rotating content incorrectly on an unsupported surface.
- Android records the first submission/fence/acquire/surface error, reports it
  through Android's native UI, and requests orderly runtime shutdown. This does
  not promise recovery from a GPU driver that itself hangs indefinitely.
- Android bundled assets are extracted once per installed update, with atomic
  file/marker replacement and missing-file repair. User saves/mods are untouched.
- Linux accelerated launcher prototype: opt in with
  `DKR_LINUX_ACCELERATED_LAUNCHER=1`. It recreates a non-Vulkan launcher window
  only at a renderer-free boundary, falls back to software on renderer creation
  failure, and recreates the Vulkan window before game start. Proven software
  rendering remains default pending Gamescope/Wayland/device qualification.

## Tests and evidence

Final packages and build/test logs are in
`G:/DiddykongWorkFolder/beta12-optimisation-20260924/packages` (logs in its parent).
Source HEAD is `ab141cb0c031a8f2986d1fa15dadc98b076e8bcc`, with the preserved
pre-existing ARM/mobile changes and this pass's working-tree changes. New tests cover snapshot lease isolation, reuse and
pool destruction; bounded exception retries and reporter exceptions; surface
image-count rules; mobile worker budgets; first-error retention; and preset
preservation. Existing full application regression suites remain required.

Final verification on 25 September 2026:

| Target | Automated result | Package |
| --- | --- | --- |
| Windows x64 | 81/81 application tests passed | ZIP; importer, pak, SDL3 and live input-switch package checks passed |
| Linux x64 | 81/81 application tests passed | AppImage; package smoke/data-exclusion checks passed |
| Linux ARM64 | 80/81 application tests passed under QEMU | AppImage; package smoke/data-exclusion checks passed |
| Android ARM64 | Native build + Gradle assembly passed | APK; signature verified, 16 KB ZIP/ELF alignment checked, versionCode 1050016 |

ARM64 `DKRDirectSession` fails at `direct_session_tests.cpp:1302`
(`transport->realtime_attempts == 1U`), including an isolated rerun. The same
QEMU assertion was recorded before this pass. It was not weakened or skipped;
ARM online qualification remains open. Android uses the same preview package
ID and signing certificate as the previous APK to preserve upgrade continuity.

A synthetic Linux x64/WSL microbenchmark (three baseline/candidate pairs,
180 waits at 60 Hz per run) measured 179–197 ms process CPU for the original
pacing loop versus 43–47 ms for the candidate over each three-second interval.
Maximum observed deadline lateness was 168 us versus 52 us. This only measures
the pacing function, not game FPS or whole-process CPU. In six 300-task copy
trials, allocating snapshots took 225–288 ms and pooled snapshots 191–224 ms;
copying all 8 MiB and consumer reads remained present in both paths. These
synthetic results do not predict thermal or mobile-device improvements.

The 240 MiB/s snapshot-copy figure at 30 tasks/s is arithmetic, not a measured
CPU saving: copying is intentionally retained. Reduced allocation counts and
wakeups must not be described as a guaranteed temperature or FPS improvement.

## Qualification still required before enabling experimental work by default

No physical Android or Linux ARM device is connected to this build environment.
Audio-only failure reports do not identify a single confirmed driver fault;
test this package on each previously affected GPU and export startup logs if
it still fails. Check warm/cold launch, suspend/resume, capture, fold/rotation,
touch and controllers, sustained racing and mod/texture-pack workloads.

The broader plan also includes persistent driver-keyed Vulkan pipeline caches,
thermal feedback, surface replacement recovery, and full event-driven wakeup
conversion. These are NOT claimed implemented or qualified by this beta.
They need independent lifecycle/concurrency tests and physical-device evidence;
forcing them into the package without that evidence would violate the surgical
no-regression requirement. Social snapshot dirty-generation tracking likewise
remains unchanged until every mutation/expiry path has been covered.

The Linux accelerated path needs launcher-to-game-to-launcher checks under
X11, Wayland and Gamescope, including fullscreen and focus restoration. Test
20–30 minutes with matched resolution/FPS before comparing CPU/GPU usage,
frame-time percentiles, thermals and battery use between builds.
