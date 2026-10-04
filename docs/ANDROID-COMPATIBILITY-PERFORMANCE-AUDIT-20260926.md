# Android compatibility and performance audit — 26 September 2026

## Status and scope

Investigation and proposed implementation plan only. No application code,
dependency checkout, generated code, settings, saves, or delivery packages were
changed in this pass. This document is the only new deliverable.

The user confirmed that the current reports concern the latest water-performance
APK: versionName `1.0.5-beta.12`, versionCode **1050021**. The retained artifact is
`G:/DiddykongWorkFolder/water-performance-20260926/delivery/DKR-R-1.0.5-beta.12-Android-arm64-preview.apk`.
Its delivery manifest records SHA-256
`9aa8befc0438e25633a0f8650e3cdb044a7401944f58930a72fa57189a38f9a0`.
Several earlier APKs have the same versionName, so future reports must retain
versionCode and installed native-library hash.

The audit inspected project adapters, Android packaging/Activity, checked patches,
the final staged Android renderer sources selected by build.ninja, retained
investigation evidence, and official Android/Khronos/VMA guidance. `adb devices
-l` returned no connected device. There is no new physical Android benchmark or
crash capture in this pass. The original D: ZIP paths are no longer accessible
here; historical device findings below refer to their retained investigation
reports, not a newly parsed archive.

The source findings are real; their contribution to a particular current phone's
failure or frame time is not automatically established. In particular, neither a
Windows benchmark nor successful APK compilation qualifies Android performance.

## Executive conclusion

There are three separate workstreams:

1. **Correctness and compatibility:** memory visibility, allocation failure
   handling, feature/limit qualification, and surface lifecycle.
2. **Throughput:** excessive resolution, extra framebuffer rendering/readback,
   uploads, texture pressure, shader compilation and always-active touch UI.
3. **Delivery and sustainability:** presentation pacing, refresh-rate coordination,
   CPU wakeups and sustained thermal behaviour.

Lowering settings cannot supply a missing GPU feature or repair invalid memory
visibility. Conversely, fixing a crash does not make every frame cheaper. The
recommended work therefore begins with correctness and usable diagnostics, then
optimises measured expensive work in small independently reversible batches.

## Existing fixes that must remain intact

- Android private renderer storage and launcher-to-Vulkan window handoff.
- RGBA8 swapchain/final-presentation agreement and surface capability checks.
- SDL Vulkan/EGL lifecycle separation.
- Descriptor allocation/use guards and the bounded full-pool compatibility retry.
- Core-promoted descriptor/scalar-feature recognition and required feature checks.
- Correct semaphore wait-stage array sizing.
- Native graphics-task lifetime ownership across slow startup work; do not restore
  the original eleven-retrace timeout for still-owned host work.
- Actual successful-presentation diagnostics, native failure/stall dialogs and
  log export independent of ImGui.
- Android Accurate 2x/standard-precision budget and explicit mobile presets.
- Bounded shader/texture workers, reusable graphics snapshots and idle-wait work.
- The latest water batching/history and UV scroll-wrap fixes.

These are present in the checked pipeline/current staging. This is not a proposal
to apply them again. See [task-lifetime qualification](C:/DKRPort/docs/HOST-TASK-LIFETIME-20260925.md)
and [water qualification](C:/DKRPort/docs/WATER-PERFORMANCE-IMPLEMENTATION-20260926.md).

## Findings and evidence

### A. Mapped Vulkan buffers lack non-coherent memory handling

**Source-confirmed portability defect; current device impact unmeasured.**

The buffer allocator uses `VMA_MEMORY_USAGE_AUTO` and host-access flags without
requiring HOST_COHERENT. `VulkanBuffer::map` maps and returns a pointer but ignores
the read range. `unmap` ignores the written range and only unmaps. No flush or
invalidate calls exist in the adapter; registering those Vulkan function pointers
with VMA is not the same as calling them. The final staged Android source retains
this behaviour.

Evidence:

- [Allocation and map/unmap](C:/DKRPort/extern/rt64/src/contrib/plume/plume_vulkan.cpp:846).
- [Uploader writes and supplies a written range](C:/DKRPort/extern/rt64/src/render/rt64_buffer_uploader.cpp:75).
- [Readback supplies a read range, then memcpy](C:/DKRPort/extern/rt64/src/render/rt64_native_target.cpp:360).

If the chosen memory is non-coherent, GPU uploads can see stale CPU writes and
CPU readback can see stale GPU results. This can plausibly produce missing or
corrupt graphics while simulation/audio continue. It does NOT establish that
the current Honor, Thor or every Mali device chose that memory type.

VMA explicitly requires flushing CPU writes and invalidating before CPU reads
for non-coherent allocations; map/unmap do not do it automatically. Its helpers
handle non-coherent atom alignment. [VMA memory mapping documentation](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html).

**Proposed correction:** checked Android pipeline patch implementing the existing
map/readRange and unmap/writtenRange contract, including null/empty ranges and
failure propagation. Use allocation-aware flush/invalidate helpers, retain fence
ordering, and avoid unnecessary operations on coherent allocations. Audit every
mapping caller and persistent mapping before implementation. Test coherent and
non-coherent uploads/readbacks with known byte patterns and validation enabled.
Do not simply force a memory type that some devices do not provide.

### B. Resource-allocation failures are not comprehensively contained

**Source-confirmed error-handling gap; no current out-of-memory diagnosis.**

The earlier descriptor failure path now has guards. Buffer/image constructors
still log an allocation failure and return, while their factories return a
non-null C++ resource object. A mapping failure returns nullptr, and ordinary
upload/readback callers immediately memcpy through the returned pointer.

- [Buffer allocation failure](C:/DKRPort/extern/rt64/src/contrib/plume/plume_vulkan.cpp:893).
- [Image allocation failure](C:/DKRPort/extern/rt64/src/contrib/plume/plume_vulkan.cpp:1013).
- [Factories](C:/DKRPort/extern/rt64/src/contrib/plume/plume_vulkan.cpp:4191).
- [Current failure stages](C:/DKRPort/runtime-recomp/src/game/graphics_health.hpp:6).

This is the same general class of failed-create/continued-use problem as the
historical descriptor crash, not proof that today's crashes have that cause.
Image/view creation can also fail for unsupported formats/usages rather than RAM.

**Proposed correction:** carry typed creation/map/view failures to an established
safe owner boundary; prevent invalid resources from reaching memcpy, descriptors,
command recording or submission. Preserve the first failure, requested sizes,
formats and relevant budget information. Audit worker shutdown and startup
partial construction. Merely returning nullptr from factories without fixing
their callers is insufficient. Do not skip required draw commands indefinitely
or throw through arbitrary driver/worker frames. Test allocation failures at
multiple stages, safe cancellation, reporting and subsequent app restart.

### C. Feature checks do not cover the actual descriptor limits/layouts

**Confirmed qualification gap; compatibility threshold varies by device.**

The texture layout declares **8,192 entries**. Both RGBA texture and TMEM shader
arrays also declare 8,192 entries. Variable allocation sizes can be much smaller;
the declared range is not evidence that 8,192 textures are eagerly resident.

- [Descriptor range](C:/DKRPort/extern/rt64/src/render/rt64_descriptor_sets.h:294).
- [Shader arrays](C:/DKRPort/extern/rt64/src/shaders/FbRendererCommon.hlsli:43).
- [Existing feature checks](C:/DKRPort/patches/android/vulkan-descriptor-safety.patch:124).

The current adapter checks partially-bound, variable-count, runtime-array,
sampled-image update-after-bind, nonuniform indexing and scalar layout features.
It does not query `VkPhysicalDeviceDescriptorIndexingProperties` or
`vkGetDescriptorSetLayoutSupport` for the actual layout. The effective per-stage,
per-set and aggregate update-after-bind budgets must be checked across the full
pipeline, not just against one array length. Layout support and numerical limit
checks complement each other. [Khronos layout support](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDescriptorSetLayoutSupport.html),
[descriptor indexing limits](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDescriptorIndexingProperties.html).

**Proposed correction:** preflight the exact device configuration before gameplay:
loader/device API agreement, enabled features/extensions, descriptor layouts and
limits, required render/storage/depth formats, sample counts and a small real
upload/draw/readback/presentation exercise. Log explicit failure reasons and keep
the launcher usable. Creating a swapchain alone does not prove pixels are correct.

If limits require a smaller texture-table variant, change shader declarations,
layouts, allocation/growth rules, index assignment and in-flight lifetimes
together. Test multiple active packs and mods at capacity. Never clamp indices
or silently drop textures. If a required feature is absent, a lower-feature
binding/shader path is a separate engineering task, not a flag to disable checks.

### D. Android surface lifecycle and pre-rotation remain incomplete areas

**Source-confirmed limitations; not a proven cause of every startup failure.**

The application stores the native window handle during setup. SDL can later
release and replace that handle during Android surface destruction/recreation.
The current project window adapter logs/forwards lifecycle events; it does not
implement a new native-window generation and Vulkan-surface recreation contract.
The swapchain resizes its existing surface. Surface-loss errors enter the fatal
graphics-health path rather than a qualified recoverable lifecycle.

- [Window setup/event handling](C:/DKRPort/runtime-recomp/src/android/android_rt64_window.cpp:36).
- [Existing SDL lifecycle patch](C:/DKRPort/patches/android/sdl2-vulkan-surface-lifecycle.patch).
- [Surface/acquisition handling](C:/DKRPort/patches/android/vulkan-surface-capabilities.patch).

The Vulkan presentation transform remains identity. On phones with a different
natural orientation, Android may need an extra compositor rotation. The old OPPO
report had a portrait-shaped surface despite a landscape Activity, but did not
prove whether the surface was transient, validly rotated or mis-sized. Android
documents pre-rotation as a way to avoid compositor work; its published gains
are not DKR-R measurements. [Android pre-rotation guidance](https://developer.android.com/games/optimize/vulkan-prerotation).

**Proposed correction:** model surface availability/generation separately from
game state; stop submissions to unavailable surfaces, retain correct ownership,
and recreate surface/swapchain only at safe render-thread boundaries. Handle
out-of-date, suboptimal, surface-lost and device-lost distinctly. Device loss
is not something to retry indefinitely. Audit indefinite acquire/fence waits;
a diagnostic timeout must never authorize reuse of GPU-owned resources.

Add pre-rotation only with a consistent final-image, UI, scissor and touch-input
transform. Test landscape/reverse-landscape, phone/tablet natural orientation,
fold/unfold, screenshot/system overlays, file picker and resume. Do not blindly
transpose width/height, rotate the world projection twice, or pause an online
simulation independently of its peers.

### E. Resolution policy can still request disproportionately expensive work

**Confirmed settings behaviour and historical workload evidence.**

Modern defaults and Balanced use 2x/60/standard precision/no MSAA. Battery also
uses 2x, with a 30 FPS presentation target. Quality uses Auto and high precision.
Existing saved preferences survive upgrades. Therefore the newer APK does not
necessarily use its new-install budget. Accurate already receives its separate
mobile budget; that historical defect must not be presented as still unfixed.

- [Mobile presets](C:/DKRPort/runtime-recomp/src/game/mobile_graphics_preset.hpp:6).
- [Auto resolution mapping](C:/DKRPort/runtime-recomp/src/game/rt64_renderer.cpp:207).
- [Earlier OPPO evidence](C:/DKRPort/docs/ANDROID-OPPO-STARTUP-20260925.md).

The old OPPO run logged Auto scale 10 and a 90 FPS target. At a fixed aspect,
10x versus 2x represents 25 times the render-target pixels. This is arithmetic,
not a measured 25-fold slowdown or evidence of the latest tester's settings.

**Proposed correction:** expose the effective internal pixel dimensions and add
an explicit, reversible mobile budget profile. Compare native/1x, 2x and current
settings at equal aspect. Keep UI at readable screen resolution. Offer a bounded
adaptive-resolution mode only after reliable timing exists, with hysteresis and
safe target changes rather than constant reallocations. Preserve advanced manual
choices; warn about expensive Auto/precision/MSAA combinations without silently
resetting saves/settings. Reduce presentation cost, not simulation speed.

### F. Native framebuffer compatibility work can duplicate rendering and stall

**Confirmed execution path; Android cost needs measurement.**

`renderToRAM` defaults true. `State::fullSync` can render a native-resolution
compatibility pass, convert colour/depth, wait for graphics completion and copy
back into guest memory. Enhanced/interpolated presentation rendering is separate.
The disabled branch still has upload/worker waits, so toggling this one setting
would not eliminate every synchronization cost.

- [Default](C:/DKRPort/extern/rt64/src/common/rt64_emulator_configuration.cpp:13).
- [Branch and native pass](C:/DKRPort/extern/rt64/src/hle/rt64_state.cpp:1159).
- [Wait and copyback](C:/DKRPort/extern/rt64/src/hle/rt64_state.cpp:1474).

The final Android staged state source retains these paths. This is a high-value
candidate because lowering the visible resolution does not remove the native
compatibility work. Mobile tiled rendering is particularly sensitive to external
memory traffic and pass boundaries. [Khronos tiled-rendering guidance](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html).

**Proposed correction:** count native passes, rendered/read-back pixels, colour
versus depth consumers, bytes and CPU/GPU waits per scene. Inventory guest reads,
framebuffer-as-texture use, screenshots/transitions/depth effects and custom mods.
Then remove only proven redundant passes/copies, use dirty regions and batch
independent transfers where legal. Unknown consumers retain the existing path.
Do not globally disable readback, remove fences, change guest completion order,
or discard attachments needed by a later pass. Reduce pass/barrier cost only
after resource-dependency and image comparisons establish equivalence.

### G. Always-active Android touch UI adds CPU and locking work

**Confirmed Android/desktop difference; magnitude not yet measured.**

Android intentionally bypasses the closed-overlay fast return so the Menu touch
button remains available. This builds an ImGui frame and applies style each
time. Inspector holds a mutex throughout frame construction; presentation draw
takes that same mutex. A controller does not remove the Menu path.

- [Android visibility override and frame work](C:/DKRPort/runtime-recomp/src/game/runtime_ui.cpp:11783).
- [Inspector lock lifetime](C:/DKRPort/extern/rt64/src/gui/rt64_inspector.cpp:233).

**Proposed correction:** separately measure UI construction, lock wait, uploads
and GPU draw. Introduce a compact touch-only path if material: cache static
geometry/layout/style, rebuild on relevant changes and publish immutable draw
data without holding a lock through heavy work. Retain immediate button/stick
feedback, multitouch, Menu accessibility, notifications, waiting spinners,
controller hotplug and editor behaviour. Do not simply hide touch controls or
throttle input polling to claim a saving.

### H. Compilation and memory retention can worsen spikes

**Source-confirmed mechanisms; current-device contribution unmeasured.**

- Pipeline creation uses `VK_NULL_HANDLE` pipeline caches in the final Android
  Vulkan stage. Driver-internal caching may exist, but there is no application
  persistent Vulkan pipeline cache. Runtime raster specialisation still occurs.
- Shader pools already cap Android at two raster/two uber workers, and texture
  streaming at one; increasing thread count is not a free improvement.
- Replacement cache retention is already capped at **512 MiB** on Android.
  This is not an eager 512 MiB allocation or a total-process memory limit. Active
  textures, native targets, snapshots, uploads and shader allocations are extra.
- Eviction skips referenced textures. No Android app-level memory-pressure
  budgeting path was found in the project Activity/adapters.

Evidence: [pipeline creation](C:/DKRPort/patches/performance/vulkan-measurements.patch),
[worker and cache policy](C:/DKRPort/patches/performance/renderer-workers.patch),
[replacement eviction](C:/DKRPort/extern/rt64/src/render/rt64_texture_cache.cpp:69).

**Proposed correction:** separate cold and warm runs. If compilation spikes are
material, add a bounded persistent Vulkan cache keyed by device/driver/cache UUID
and shader/build identity; use safe locking, atomic writes and corruption fallback.
Prewarm only demonstrated common pipelines without starving startup. A cache
cannot fix sustained low FPS once compilation is over. [Khronos pipeline caches](https://docs.vulkan.org/guide/latest/pipeline_cache.html).

Measure total resident/peak memory and supported Vulkan heap budgets, not only
replacement-cache bytes. Use device-appropriate soft budgets and memory-pressure
signals to schedule safe eviction of unused resources. Bound mip generation,
decode/upload staging and retained target history. Never free live GPU resources;
keep user packs/mipmap options, and offer a reversible lower-memory configuration
when content cannot fit. Diagnose native OOM, Android low-memory kill, driver
failure and user termination separately.

### I. Android pacing and thermal coordination are not yet integrated

**Confirmed missing platform integration; stutter/thermal attribution unmeasured.**

The portable wait sleeps until approximately two milliseconds before the deadline
and retains the precision tail. The platform/event loop wakes roughly every
millisecond. No project integration for Swappy, refresh-rate requests, performance
hint sessions or thermal feedback was found. Android display refresh can differ
from the selected render target.

- [Presentation wait](C:/DKRPort/runtime-recomp/src/game/presentation_wait.hpp:38).
- [Platform loop](C:/DKRPort/runtime-recomp/src/game/game_main.cpp:1049).

**Proposed correction:** measure real presentation intervals/queue latency, CPU
wakeups and runnable-versus-blocked time first. Integrate one presentation pacing
authority, preferably qualified Android Frame Pacing at the Vulkan boundary,
rather than stacking it over the existing deadline sleeps. Coordinate display
rate where supported. Keep simulation, audio and online input deadlines unchanged.
Android's frame-pacing library addresses uneven frame delivery and queue stuffing;
it cannot make an over-budget rendering workload cheap. [Android Frame Pacing](https://developer.android.com/games/sdk/frame-pacing).

Use capability/API-gated thermal reporting and performance hints for actual
critical threads. Prefer sustainable workload adjustment with user control and
hysteresis, not forced maximum clocks or arbitrary CPU affinity. Measure at
startup and after sustained play. [Android ADPF guidance](https://developer.android.com/games/optimize/adpf).

### J. Diagnostics cannot yet attribute ordinary testers' frame drops

**Confirmed evidence gap.** Current startup export records version/native hash,
logs and available previous exits. Detailed CPU regions remain environment-flag
driven. No physical device is attached, and screenshots/recording have previously
triggered problems for testers.

- [Trace gate](C:/DKRPort/runtime-recomp/src/game/performance_trace.hpp:22).
- [Scene profile gate](C:/DKRPort/runtime-recomp/src/game/track_performance.hpp:18).
- [Existing export](C:/DKRPort/packaging/android/app/src/main/java/io/github/thatguymcd/dkrr/AndroidDiagnostics.java:39).

**Proposed correction:** add a one-tap, bounded **Record performance report**:
60-second lightweight capture, explicit stop/export, no adb or video required.
Record exact binary identity, CPU/GPU/driver/API capabilities, actual settings,
internal/output dimensions, scene/ROM revision, frame percentiles and missed
deadlines, simulation/task rate, CPU thread time, waits, memory, compilation,
UI cost and supported thermal state. Use asynchronous GPU queries where valid;
mark unavailable measurements, never substitute zero. Avoid per-frame disk I/O
or private ROM/save/profile/friend-code content. Include an early persistent
startup breadcrumb so failed launches can still report the last known stage.

## What the historical device evidence does and does not establish

- The retained Thor analysis symbolized a null descriptor update after allocation
  failure on Adreno 740. That high-end GPU report is not evidence that all failures
  are caused by insufficient device power. The invalid-use guard is now present;
  why that allocation failed remains unresolved without the new capability data.
- The OPPO/Mali-G68 report showed zero actual presentations and late task
  completions; user task removal was recorded, not a proven native crash.
  Host-task premature retirement was subsequently reproduced and corrected.
  It must not be used to claim the newest APK has that same scheduler bug.
- Today's reports against 1050021 establish that problems remain, but do not
  identify a particular Vulkan error, selected memory type, OOM or GPU hotspot.

## Implementation sequence for approval

### Phase 0 — freeze baseline and make failures measurable

Preserve current delivery hashes, dirty source state, policies and Android staged
patch composition without altering them. Use isolated profiles. Add the bounded
recorder and exact compatibility report, expose versionCode in Support, and retain
first failure/task/surface generation information. Capture one flagship, one
mid-range Adreno handheld and one Mali device; include the known Thor/OPPO testers
when available. Do not make missing feedback block source-correctness tests.

Deliverable: reproducible baseline with attribution, not just an average FPS.

### Phase 1 — fix correctness and prevent failed starts becoming black screens

Implement A/B/C as separate Android pipeline patches: coherent-memory contract,
resource-failure propagation, exact feature/limit/format/layout preflight.
Inject failures in isolated tests. Confirm native UI can report/export without
a working game renderer. Build a small upload/draw/readback qualification path.
Do not auto-retry a broken GPU indefinitely or force successful task completion.

Exit gate: healthy devices still render correctly; deliberately unsupported or
failed configurations produce an actionable result, not a crash/audio-only wait.
Physical startup confirmation remains required on affected drivers.

### Phase 2 — make Android surface ownership robust

Introduce an Android-owned surface-generation/lifecycle contract using existing
SDL events/adapter boundaries. Recover genuine surface replacement safely.
Qualify orientation and optionally pre-rotation as a separate change after basic
ownership works. Keep recovery and fatal device loss distinct.

Exit gate: repeated launch/return-to-launcher, background/resume, lock/unlock,
fold/rotation and system-overlay tests do not leave a permanently black surface.

### Phase 3 — reduce unnecessary rendering and memory work

Perform fixed-scene A/B captures at 1x and 2x, 30/60 presentation, stock/pack,
cold/warm and offline/online, changing one factor at a time. Prioritise actual
measured cost in native framebuffer passes/readback and touch UI, then uploads,
resource retention, shader variants and pipeline compilation. Add mobile pixel
budgets without forcing quality loss on capable devices. Keep the new water
optimisations as baseline, not a toggle to undo.

Exit gate: at equal quality, lower expensive-stage time and improved p95/p99
frame times without altered images, guest results or online behaviour. Any
quality-changing mode is opt-in and reported separately from engine speedups.

### Phase 4 — smooth delivery and sustained operation

Add qualified Android presentation pacing and display-rate coordination, then
thermal/performance-hint integration. Tune wakeups only after input/lifecycle
response tests. Add optional adaptive resolution with bounded changes if the
GPU, rather than CPU work, is the limiting stage. Do not lower game simulation
frequency or change network frame debt/rollback to hide rendering load.

Exit gate: stable frame distribution during extended play, no unbounded queue
latency or resource growth, predictable input, no sudden quality oscillation.

### Phase 5 — cross-device release qualification

Test both ROM revisions, original textures and representative packs/mips/mods;
Adventure intro/hub/races/bosses, Sherbet Island/Bubbler/Crescent Island water,
track previews, chained cutscenes, results and return to launcher. Cover 1–4
viewports, touch/controller/gyro, HUD/interpolation/shadows, offline and online
sessions, save isolation and notifications. Include 4 KiB/16 KiB-page devices
where available; retain current APK native alignment and signing identity.

Suggested minimum per target device: 20 cold/warm launches, 20 lifecycle cycles,
representative transitions and 30-minute sustained racing. Capture p50/p95/p99,
over-33/50/100ms frames, actual simulation speed, peak/resident memory and thermal
trend. Use matching settings and normal ambient conditions. Validation-layer
correctness runs and timing runs must be separate because validation adds cost.

For a qualified 60-FPS tier, use 16.67ms as the presentation budget; a practical
initial gate is p95 near that budget and p99 no worse than roughly two refresh
intervals in steady racing, with loading intervals identified separately. A
30-FPS tier uses a 33.33ms budget. These are proposed acceptance targets, not
already achieved numbers. Average FPS alone is insufficient.

Rerun Windows/Linux regression tests and build Windows plus AppImages when an
approved batch is packaged. Linux ARM validation is a separate hardware/build
gate: Docker remains untouched per the user's instruction. Never label an old
ARM package as newly built or claim it passed current runtime changes.

## Patch ownership and regression boundaries

All renderer/dependency edits must be project-owned checked patches selected by
the existing pipeline. Never directly edit RT64, SDL/N64ModernRuntime checkouts,
RecompiledFuncs or RecompiledPatches. Source references above identify read-only
evidence, not permitted edit locations.

Proposed owners:

- `patches/android/` plus `runtime-recomp/CMakeLists.txt` checked stage selection:
  Vulkan memory/resource/capability/surface/pacing work.
- `runtime-recomp/src/android/` and Android Activity/diagnostics: lifecycle state,
  reports, platform hints and lightweight touch rendering.
- Existing project-owned graphics policy/health headers: explicit safe budgets
  and failure semantics; avoid competing second configuration systems.
- Tests and isolated fault qualification: byte visibility, limits, allocation
  failures, lifecycle ordering, queue shutdown, caches and resource lifetimes.

Each phase gets its own rollback point and small reviewable patch set. Shared
engine optimisations require desktop validation; Android-only fixes remain
Android-only initially. Protect Track Lab, custom mods, HUD, scrolling water,
Taj shadows, online startup/barriers, save routing and deterministic simulation.
No blanket fast-math, unsafe fence removal, unlimited compiler threads, runtime
third-party driver replacement, resolution tricks that stretch HUD, or automatic
save/config deletion.

## Compatibility boundary and realistic outcome

The current APK is arm64-v8a, API 28 minimum, target 35, with a Vulkan 1.2 manifest
requirement. Launcher GLES rendering does not constitute a GLES fallback for the
game. Supporting all Android devices is therefore not an existing capability.
OS/API labels alone also do not prove required GPU features or driver quality.

First broaden and stabilise the current Vulkan path on hardware capable of its
requirements. For devices missing indispensable features, choose explicitly
between a separately designed lower-feature Vulkan path and a much larger new
renderer/backend effort. Neither is a surgical switch and neither should be
silently promised as part of performance tuning. Android itself recommends
considering a fallback when targeting older unreliable Vulkan implementations.
[Android native-engine guidance](https://developer.android.com/games/develop/vulkan/native-engine-support).

The intended outcome is reliable startup with correct graphics, materially lower
rendering cost, smoothly paced 60 FPS on qualified hardware and a usable lower
budget on genuinely weaker hardware. Universal perfect 60 FPS and zero crashes
on every Android driver cannot honestly be guaranteed from this source audit.
Approval is required before implementation.
