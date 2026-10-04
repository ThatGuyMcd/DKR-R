# Android compatibility candidate — 27 September 2026

> Superseded for Android: device testing found a deterministic startup crash in
> build 1050022's colour-readback self-test. Do not distribute that APK. See
> [the 1050023 correction](ANDROID-STARTUP-CORRECTION-20260927.md). The historical
> verification below did not exercise this Android-only call on a phone.

## Delivery status and limits

This implements the source-confirmed compatibility corrections and low-risk
performance controls from the approved Android audit. It is a **device-test
candidate**, not proof of perfect performance or support for every Android GPU.
The version name stays `1.0.5-beta.12`; Android versionCode is **1050022** (previous
water-performance APK: 1050021). The preview application ID and signing identity
are unchanged. Install as an update; do not uninstall or delete settings/saves.

No Android device was attached during this pass. Physical startup, image accuracy,
surface recovery, thermal behaviour and FPS improvements remain unqualified.
The measurement-dependent portions of the plan are explicitly listed below;
they have not been replaced with speculative renderer changes.

## Recovery and patch ownership

Before editing, the existing three delivery artifacts and 139 dirty/untracked
source files were copied into:

`G:/DiddykongWorkFolder/android-compat-20260927/baseline/`

The previous APK SHA-256 is
`9aa8befc0438e25633a0f8650e3cdb044a7401944f58930a72fa57189a38f9a0`.
The repository HEAD was `ab141cb0c031a8f2986d1fa15dadc98b076e8bcc`, but this was a
dirty checkout: restoring HEAD alone is NOT equivalent to restoring the baseline.

All dependency modifications use checked copies selected by CMake. RT64, SDL,
N64ModernRuntime and other dependency checkouts, RecompiledFuncs and
RecompiledPatches were not edited. Existing water/UV, HUD, custom-mod/Track Lab,
online/save-isolation and host-task-lifetime fixes remain in the composition.
Docker was left untouched as requested; no stale Linux ARM artifact is relabelled.

## Implemented corrections

### Vulkan buffer visibility and failed-resource containment

- `vulkan-mapped-memory.patch` implements map/readRange invalidation and
  unmap/writtenRange flushing using allocation-aware VMA helpers. Empty ranges
  are no-ops, null ranges cover the buffer, malformed ranges fail explicitly.
  VMA handles coherent-memory no-ops and non-coherent atom alignment. Existing
  GPU fence ordering is retained; mapping is not treated as GPU completion.
- `vulkan-resource-failures.patch` prevents failed buffer/image allocation,
  resource-view, framebuffer or framebuffer-render-pass creation from returning
  an apparently usable object. It logs the original operation and allocation
  dimensions/flags where applicable.
- Renderer workers have no universal exception-safe unwind boundary. Therefore
  a terminal resource fault preserves the faulting worker/resources, records the
  first error and posts Android UI directly before parking that worker. This is
  **terminal containment, not in-process renderer recovery**. The native dialog
  allows Export logs and an explicitly confirmed Close app without waiting for
  the renderer. No successful fence/task completion is fabricated. A process
  restart is necessary after this fault class; unsaved progress may be lost.
- Existing safe display-list abort handling remains separate. A native startup
  wait dialog also covers the case where setup has not presented any frame after
  15 seconds, before a guest task exists. That is a warning, not cancellation.

### Actual capabilities and startup self-checks

- `vulkan-capability-preflight.patch` queries exact descriptor-layout support,
  variable-descriptor bounds, full-pipeline/per-stage normal and update-after-bind
  descriptor budgets, image formats/usages, extents, mip levels and sample counts.
  It does not shrink shader tables or silently clamp texture indices.
- Instance API selection respects the loader; VMA's core API version does not
  exceed the selected physical device. The APK's Vulkan 1.2 manifest requirement
  is unchanged. A lower-feature renderer fallback is not included.
- `renderer-transfer-qualification.patch` runs two different 16 KiB GPU buffer
  copy/readback patterns, followed by a 16x16 RGBA8 colour-attachment clear and
  image readback. Real fences precede CPU comparisons. This probes uploads,
  cache visibility, an actual colour attachment and copyback. It does **not**
  certify every raster shader, swapchain scanout or GPU driver. These checks
  compiled here; they have not yet run on a physical Android device.

### Android surface replacement

- Android callbacks publish a ref-counted native-window generation before SDL
  wakes its native thread, and unpublish before destruction. Render-thread leases
  retain the old native window until its GPU work can be retired safely.
- `vulkan-surface-recovery.patch` distinguishes surface loss from device failure,
  drains actual queue work before destroying/rebinding a lost/replaced surface,
  and recreates presentation resources against the new generation.
- `present-surface-ownership.patch` retires framebuffer references before their
  attached swapchain views are destroyed. Acquisition uses a retryable 100 ms
  timeout, not a timeout that authorizes reuse of GPU-owned resources.
- API-30+ frame-rate requests are dynamically resolved; API 28/29 remain valid.
  They are hints to Android, not a second FPS limiter or a forced display mode.
  Pre-rotation and replacement of the existing pacing authority were not added.

### Low-risk performance and memory changes

- Two explicit Low-load presets: native/1x, standard framebuffer precision,
  no MSAA, no supersampling, at 30 or 60 FPS presentation. Existing presets,
  manual settings, HUD aspect and saved preferences are not reset on upgrade.
  Lower internal resolution is a quality/performance option, not an equal-quality
  engine-speedup claim. Simulation and network/input cadence are unchanged.
- Android memory-pressure signals reduce retention of **unused replacement
  textures** to 128 or 64 MiB through the existing locked/deferred eviction path.
  UI-hidden alone does not lower the budget. Live/referenced GPU textures are
  never freed by the Java callback. This is not a cap on total process memory.
- Touch hit-target vectors retain capacity between frames instead of allocating
  and freeing a fresh vector on every touch-only UI frame. Rendering/input
  behaviour, multitouch and the Menu button are retained.

### Useful reports without screenshots or adb

About → Support now exposes Android build identity and **Record performance
report**. Start, close the overlay, play the slow area, then export logs. Capture
ends automatically after 60 seconds; it can also be stopped/exported early.

The fixed-capacity recorder records successful swapchain-return intervals,
p50/p95/p99/max and long intervals, CPU-side wall-time regions, readback byte
counts and pipeline-creation regions. A once-per-second Android sample records
process CPU time, native heap/PSS and thermal status where available. Scene/task
and asynchronous rolling renderer/GPU history are enabled in runtime.log for
the capture. No per-frame disk output is added to the recorder.

These are not physical scanout intervals. Nested/concurrent wall regions must
not be added together as CPU utilisation. Unavailable measurements are labelled;
older rolling GPU history can briefly include the preceding scene. Exact native
binary identity/device details remain in the existing diagnostic ZIP. The new
performance file does not contain ROMs, saves, friend codes or private profiles;
existing runtime logs should still be reviewed before sharing.

### Android app icon

The application and round-icon manifest entries now use an adaptive icon with
the existing `DKR-R-Short-Logo.png` wordmark on a dark blue background. Gradle
copies the original logo verbatim into generated resources; its 666:375 aspect
is preserved and it is centred inside the adaptive-mask safe area. No new logo
art, monochrome recolouring or dependency asset edits were introduced.

## Verification performed

- Android arm64 Release native compilation and Java/resource APK assembly.
- Windows and Linux x64: **85/85 project tests passed** on each platform.
  Includes new map-range/overflow, memory-pressure, surface-generation/refcount
  concurrency, bounded recorder, preset preservation and terminal fault tests.
- Terminal fault test is a separate short-lived synthetic process: it checks
  first-error retention, independent notification and prevention of invalid use
  or unsafe unwind. It is not a real Android driver OOM test.
- Visible isolated Windows Vulkan/Modern 60 FPS smoke: 1,172 completed guest
  graphics tasks in 40 seconds, clean shutdown. This is desktop regression
  evidence, not an Android performance benchmark.
- After the final startup-warning addition, both desktop builds were repeated;
  all six directly affected tests passed on each. The final Windows smoke
  completed 1,175 graphics tasks and shut down cleanly.
- Android package identity, adaptive-icon resource, unchanged signing certificate,
  arm64 architecture and ZIP alignment verified during packaging. Final artifact
  hashes and any subsequent targeted checks are in the delivery manifest.
- Packaging retains the ROM/save exclusion checks and refuses private task/water
  qualification builds. Desktop packaging exercises save/Pak and input helpers.

## Remaining plan gates — not claimed complete

1. **Physical-device qualification:** startup and repeated launch/exit on Honor,
   one affected Adreno handheld and one Mali device; stock textures first, then
   packs/mods. Exercise file picker, background/resume, lock/unlock, folding,
   orientation and system overlays. Confirm new native dialogs and export on
   failing devices. Validate with Vulkan layers on a suitable test device.
2. **Measured optimisation:** compare the same scene/settings at 1x/2x, 30/60,
   cold/warm and stock/pack, including water-heavy maps. Obtain 60-second reports
   and sustained thermal runs. Internal-target pixel instrumentation and detailed
   per-thread CPU attribution are not yet complete; current logs include output
   size/resolution scale and aggregate CPU samples.
3. **Framebuffer/readback reductions:** retain all compatibility work until guest
   consumers, custom mods and image comparisons prove a pass/copy redundant.
   No global readback disable, fence removal or guest ordering change was made.
4. **Larger touch UI, pipeline-cache and pacing changes:** persistent Vulkan caches,
   immutable touch-only draw publication, Swappy integration, ADPF performance
   hints, adaptive resolution and pre-rotation remain measurement/lifecycle gated.
   Adding these untested together would contradict the surgical/no-regression
   requirement. Refresh-rate hints and thermal reporting are present; they are
   not equivalent to full Swappy/ADPF integration.
5. **Unsupported GPUs:** missing indispensable features require a separately
   qualified lower-feature backend. A budget preset cannot supply those features.

The next useful input is the new build's performance/startup ZIP from affected
hardware. This candidate creates that evidence without requiring screen capture.
