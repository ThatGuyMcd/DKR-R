# Android performance follow-up — 26 September 2026

## Status

Investigation only: no runtime, dependency, settings, save or package changes.
The user reports that APK 1.0.5-beta.12 is more stable but still performs poorly.
The latest packaged APK has versionCode 1050020. We do not yet have a
performance capture from that APK on the user's affected device. Previously
reported 15 FPS and older Thor/OPPO startup logs must not be presented as a
measurement of this new build.

The task-lifetime correction fixes reproduced premature retirement of native
work. It does not make an expensive graphics task cheaper. Nor do Windows
throughput measurements establish Android performance. The next pass must
separate sustained throughput, startup compilation, frame pacing and thermal
degradation.

## Source-confirmed findings

### 1. This is not an unoptimised native build

The actual Android `build.ninja`, rather than only the CMake cache, contains
`-O3 -DNDEBUG` for both generated ROM revisions, `game_main.cpp` and the staged
RT64 workload queue. The cache's empty CMAKE_CXX_FLAGS_RELEASE is misleading
if viewed without the NDK's effective flags. hlsl++ selects its ARM/NEON path
for `__aarch64__` (`extern/rt64/src/contrib/hlslpp/include/hlsl++/config.h:166`).
There is no evidence that enabling Release or basic ARM SIMD is the missing fix.

### 2. Mobile presets exist, but upgrades preserve costly old choices

- `runtime_ui.cpp:432`: new Android Modern preferences default to manual
  60 FPS, 2x resolution and standard precision.
- `mobile_graphics_preset.hpp:14`: Balanced explicitly selects 2x, 60 FPS,
  standard precision, no MSAA and no supersampling. Applying it also selects
  AF4 in `runtime_ui.cpp:5387`.
- `rt64_renderer.cpp:207`: Auto maps to RT64's window-integer scaling;
  `extern/rt64/src/hle/rt64_workload_queue.cpp:190` derives that from the
  swapchain height and reference height.
- Existing saved preferences are restored. An update does not imply the new
  first-use defaults are active. Quality deliberately chooses Auto/high precision.
- Merely switching from Accurate to Modern is not the same as applying
  Balanced: the general anisotropy preference starts at 16
  (`runtime_enhancements.cpp:40`); Android Accurate explicitly uses 4.
- Accurate deliberately targets original 30 Hz presentation
  (`rt64_renderer.cpp:254`). A 30 FPS cap is distinct from missing that cap.

The older OPPO report had a 1080x2245 surface, logged scale 10 and target 90.
At the same aspect ratio/reference height, 10x has 25 times the render-target
pixels of 2x. That is workload arithmetic, NOT a measured speedup, and does not
establish this user's current settings. Surface sizing/rotation must be logged
and validated; never blindly swap Vulkan extents.

### 3. Native framebuffer compatibility adds a separate synchronisation cost

`extern/rt64/src/common/rt64_emulator_configuration.cpp:13` defaults
`framebuffer.renderToRAM` to true. No project-side override was found.
`extern/rt64/src/hle/rt64_state.cpp:821` selects it unless an extended display
list command overrides it. The enabled branch renders at native 1x
(`:1159`), converts colour/depth targets (`:1457`), executes and waits for the
graphics worker (`:1471`), then copies results back into RDRAM (`:1479`).
The normal enhanced/interpolated workload rendering is separate.

This is a concrete candidate for mobile CPU/GPU synchronisation overhead,
including when the visible render scale is already low. It is not yet a
measured dominant bottleneck on this phone. Even the disabled branch retains
uploader/worker waits (`:1573`); toggling readback alone does not remove every
synchronisation point. Do not bypass it wholesale: audit actual guest,
framebuffer-as-texture and native consumers, snapshot ownership, completion
ordering, depth-based effects, menus, transitions and custom mods first.

The staged Android measurement source retains these paths. These are not
findings based solely on an unused upstream file.

### 4. Android always keeps the touch UI's renderer path alive

`runtime_ui.cpp:11781` disables the usual no-visible-overlay early return on
Android so the Menu touch button remains accessible. Consequently ImGui frame
construction/style work and modal checks still run when the main overlay is
closed. `mobile_ui.cpp:504` also retains the Menu control with a controller.
`rt64_inspector.cpp:233` holds its frame mutex from newFrame until endFrame;
presentation's draw acquires the same mutex (`:290`).

This is an intentional functionality difference from desktop, and a candidate
CPU/lock-contention cost. It is NOT proof that a few touch buttons explain the
whole slowdown. Measure build, draw, lock-wait and upload costs. Any slimmed
touch-only path must preserve immediate touch feedback, multitouch, Menu access,
notifications, online waits, keyboard/modal input and controller switching.

### 5. Android-specific display/thermal integration is not present

The project uses RT64 presentation timing plus the existing portable wait.
`presentation_wait.hpp` contains a Windows-only high-resolution timer; Android
keeps coarse sleep plus RT64's precision tail. The main SDL/lifecycle loop
still services work with a 1 ms sleep (`game_main.cpp:1043`). No project Swappy,
Choreographer pacing, Android frame-rate request, ADPF performance-hint or
thermal-status integration was found in the Android bridge/Activity.

These are investigation candidates, not proof of bad frame pacing or thermal
throttling. Pacing cannot create 60 FPS if rendering genuinely costs 60 ms.
Do not stack a new pacing clock on top of the existing one, pin arbitrary CPU
cores, increase sleeps globally, or change simulation/network timing.

### 6. Detailed performance evidence is not exposed to ordinary APK users

`rt64_renderer.cpp:746` emits Android FPS/simulation/task-rate summaries.
However `performance_trace.hpp:21` enables detailed regions only with
`DKR_POWER_PROFILE=1`; `track_performance.hpp:18` similarly requires
`DKR_TRACK_PROFILE=1`. Neither flag is set by the normal Android export flow.
Exporting Startup Logs therefore does not itself enable these profilers.

Add an explicit, bounded in-app performance recording rather than asking
testers to set shell environment variables. Keep timings nonblocking, avoid
per-frame log spam, and distinguish overlapping wall waits from actual CPU
time. GPU timestamps must be collected asynchronously and only when supported;
unavailable data is not zero GPU cost.

### 7. Pipeline caches are a stutter candidate, not a sustained-FPS cure

The final staged Android `plume_vulkan.cpp` passes VK_NULL_HANDLE as the cache
to vkCreateComputePipelines and vkCreateGraphicsPipelines. Raster shader
specialisation already exists, with bounded compilation workers. A persistent
application-managed Vulkan cache is absent; the driver may still have its own
cache. Measure cold/warm compilation separately from steady racing. If justified,
use a device/driver/pipeline-cache-UUID/shader-version-compatible bounded cache,
atomic storage and clean corruption fallback. Do not claim this will cure
continuous low FPS when compilation is already finished.

## Proposed focused correction sequence

1. Preserve the current four delivery artifacts and source state. Keep changes
   Android-only where possible, via checked isolated patch stages for dependency
   code. Preserve the task-lifetime and descriptor-safety fixes.
2. Add one-tap **Record performance** / **Export performance report**. Capture
   exact version/native hash, effective target and internal/output dimensions,
   scene, settings, bounded frame percentiles, task stages, process/thread CPU,
   waits, readbacks, UI cost, memory and supported thermal status. Exclude ROMs,
   saves, friend codes and private profiles. No screen recording required.
3. Establish the same scene at Balanced/2x/60/no MSAA, original textures and
   no CRT; then compare 1x vs 2x vs current settings, 30 vs 60 presentation,
   cold vs warm and the first minute vs 15 minutes. Hold aspect ratio and
   gameplay constant. These are diagnostic comparisons, not forced resets or
   a decision to solve the issue by permanently lowering quality.
4. Use those measurements to choose the first small engine change. Priorities
   are redundant framebuffer work/synchronisation and the always-active touch
   UI path, but neither receives an unqualified bypass. Batch or defer only
   work whose consumers and lifetimes have been proved safe. Maintain fallback
   behaviour for unclassified mod/custom framebuffer use.
5. Address display pacing if the trace shows pacing/queue mismatch; use exactly
   one pacing authority, feature/API-level gates and lifecycle handling. Request
   a matching display rate where supported. Keep input sampling and simulation
   deadlines independent. Apply compile-cache work only if cold/warm data
   identifies it; use thermal feedback only after observing sustained behaviour.
6. Test each change independently against the preserved build at identical
   settings. Run both ROM revisions, offline/online, touch/controller, 1–4
   viewports, texture packs/mods, menus/race-end and fold/rotation/resume. Preserve
   saves, HUD, shadows and interpolation. Run all desktop/ARM regression suites
   and rebuild all four targets when an approved runtime batch is ready.

Acceptance target: sustained, evenly paced 60 FPS on capable Android hardware,
with original simulation speed, correct graphics and no growing stalls; judge
lower-end hardware from measured capability, not from the game's N64 age.
Record frame-time percentiles and heat-related degradation, not merely an FPS
counter or successful compilation. No universal 60 FPS guarantee is justified.

## Immediate tester comparison

Use Graphics -> Performance preset -> Balanced -> **Apply**, then restart the
game so launch-time filtering/precision are effective. Compare one offline
race with texture packs and CRT disabled, and export Android Startup Logs after
playing. This establishes settings and progress with the existing APK, although
the proposed performance recorder is still needed for full attribution. Do not
uninstall, clear app data or delete personal settings/saves.

## Primary technical references

- [Android performance diagnosis and A/B testing](https://developer.android.com/games/optimize/gameperformance)
- [Android power, refresh-rate and frame-pacing guidance](https://developer.android.com/games/optimize/power)
- [Khronos tile-based rendering considerations](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html)
- [Vulkan pipeline-cache behaviour](https://docs.vulkan.org/guide/latest/pipeline_cache.html)

These references justify measurement and design choices, not a claim that any
one of these mechanisms is the measured cause on the current user's phone.
