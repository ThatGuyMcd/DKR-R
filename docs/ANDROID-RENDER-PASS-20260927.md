# Android quality-preserving renderer pass — build 1050027

## Scope and rollback

Public version remains 1.0.5-beta.12. Android versionCode becomes 1050027.
The preceding 1050026 APK, symbols, Windows ZIP and Linux AppImage remain intact
in `G:/DiddykongWorkFolder/android-realwork-priority-20260927/`.
This candidate is kept separately in `G:/DiddykongWorkFolder/android-renderpass-20260927/`.

No change to resolution, framebuffer precision, anisotropy, mipmaps, MSAA,
FPS target, saved settings, shader mathematics, game simulation, interpolation,
online timing, mods, HUD or touch controls. No native framebuffer/readback is
removed. Docker and Linux ARM recovery remain out of scope.

All renderer edits are checked Patch Pipeline inputs in `patches/android/`,
applied to isolated build copies. Dependency checkouts and generated game
functions are not edited. Normal desktop builds do not enable these stages.

## Evidence and limits

The latest supplied phone capture is build 1050025, not a measurement of this
candidate. Its gameplay GPU histories approach 29–32 ms median; 60 FPS allows
16.67 ms. The capture used high-precision rendering and a large automatic
resolution on the Honor. We retain that quality rather than silently lower it.
See `ANDROID-REALWORK-PRIORITY-20260927.md` for the complete report-7 analysis.

The Vulkan backend ended the active render pass before examining every nonempty
barrier request, including repeated requests to read an already-readable image.
It also ended a pass when simply rebinding the same framebuffer. The raster
renderer reset depth access at every scene boundary even when the next draw
immediately requested the previous read-only state.

These are identifiable redundant operations, **not proof that they explain all
the handset slowdown**. A first 45-second private desktop game test exercised
1,325 graphics tasks and shut down cleanly. It encountered zero same-target
rebind or first-decal boundary opportunities. Those two changes alone therefore
have no demonstrated benefit in that sample. This finding led to the more
relevant read-barrier audit rather than treating a synthetic test as an FPS win.

## Changes

### Command-list-local read visibility

`vulkan-read-barriers.patch` tracks visibility established by real barriers in
the current command-list recording, using an allocation-free 64-entry cache.
It only elides an image barrier when all of these hold:

1. Old and requested layouts are both `SHADER_READ` (not GENERAL, depth-read,
   transfer or attachment layouts).
2. A real barrier for this resource was recorded in this same command list.
3. Its stage scope covers every requested consumer stage.
4. The resource's tracked stage scope still matches that established scope.

The cache resets at command-list begin, invalidates on other layouts and never
elides buffer barriers. Hash collisions only cause conservative misses. A
mixed barrier call still emits every nonredundant dependency. The active render
pass is retained only if the entire resulting barrier list is empty.

This is not blanket removal of same-layout barriers: GENERAL can contain
writes, new consumers need visibility, and depth attachment load/store semantics
need separate treatment. Existing read/write barriers, fences, semaphores,
copies and queue ordering remain intact. The dependency reasoning follows the
[Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html).

### Other narrowly proven pass continuations

`vulkan-pass-retention.patch` keeps an already-active framebuffer binding if
the exact same non-null target is requested. Null unbind, actual target changes
and explicit barriers still terminate the pass.

`render-pass-retention.patch` skips READ → WRITE → READ only when a scene starts
with a nonempty indexed/raw/rectangle decal draw, the same framebuffer has a
colour attachment, and depth is already read-only. Unknown commands, clears,
compute/test-Z, empty first draws and all writable starts retain the original
path. No depth-writing or shadow draw is removed.

### Measurements that distinguish the remaining GPU work

`render-stage-timing.patch` adds opt-in enhanced GPU setup versus
raster/transfer intervals. A small query pool is owned by the renderer thread
and reused; it is destroyed when that thread exits before device destruction.
All results are read after the already-existing workload fence. There is no
additional submission, GPU wait or per-frame allocation. Missing, stale,
backwards or implausible results are not reported as valid zero-cost work.

Captures now include actual last enhanced target dimensions, sample count and
format, native-plus-enhanced framebuffer count, raster/dither draws, pass starts
and each optimisation's hit count. Draw totals are batched per scene, avoiding
per-draw clocks/atomics. Counters are opt-in; reports are bounded aggregates,
not per-draw logs. GPU stage totals are not whole-display latency and must not
be added to overlapping CPU/fence wall times. Native work and presentation are
outside the enhanced GPU interval.

## Validation design

* Policy tests exhaust the depth continuation guards, framebuffer identity/null
  handling, cache invalidation/reset/collisions and visibility-stage subsets.
* A real Vulkan test compiles the exact staged backend. It verifies 3,003
  redundant binds and 3,003 redundant read barriers, with 3,000 pass breaks
  avoided, then checks every colour/depth pixel against exact expected data.
  It also tests new consumer stages, mixed buffer/image barriers, explicit null
  unbind and different render targets. Readback uses Vulkan directly because
  plume does not implement image-to-buffer `copyTextureRegion`; the initial
  harness used that unsupported API and was corrected, not the application.
* The GPU harness is run with Khronos validation and synchronization validation
  enabled on Linux as well as desktop Vulkan on Windows.
* Private `DKR_ANDROID_RENDER_QUALIFICATION` applies the Android stages to a
  desktop game for runtime checks. All packaging scripts reject that option;
  desktop releases are rebuilt with it OFF.
* Android builds use the normal composed capability, resource, cache, upload,
  surface-recovery, worker-priority and water patch stages as before.

### Completed real-game qualification

The final staged renderer completed a second visible, isolated 45-second
Windows Vulkan game run, with 1,328 guest graphics tasks and a clean watchdog
shutdown. At the last 2,400-sample enhanced-GPU summary it had eliminated
14,404 redundant read barriers. The new path is therefore exercised by actual
game rendering. It reported **zero avoided active-pass breaks**, zero redundant
framebuffer binds and zero depth excursions in this run; those synthetic
opportunities must not be presented as observed game savings.

The last actual target in this run was 1536×960, unlike the first run's
2694×1440 target. Consequently, the runs' GPU timings are not an equal-quality
before/after benchmark, and no FPS improvement is inferred from them. Both are
desktop safety/coverage checks, not handset measurements. The private profile
and logs are retained as `private-readbarrier-smoke` in the candidate directory.

The final isolated Vulkan backend test also passed on Windows and Linux, with
exact colour/depth readback and the expected 3,003 redundant-read and binding
counts. Linux synchronization validation reported no validation errors. Linux
policy tests passed under AddressSanitizer/UndefinedBehaviorSanitizer and
ThreadSanitizer. The Linux regression suite passed all 91 DKR tests.

The normal Windows build was then regenerated with private qualification OFF,
rebuilt, and passed all 91 DKR tests. The Windows ZIP and Linux AppImage passed
their staged runtime packaging checks. The Android APK passed signing,
16-KiB ZIP/ELF alignment and packaged-library identity verification; its saved
unstripped symbols match the packaged library's build ID. These are completed
build/safety checks, not a substitute for physical Android performance testing.

## Acceptance still required on a physical device

No Android device is attached here. Desktop success and synthetic pass counts
cannot establish a phone FPS improvement, universal startup compatibility,
thermal behaviour or screen-recording stability.

Install 1050027 without clearing data. Use the same orientation, graphics
settings and slow scene as the previous build, warm the scene shaders, then
compare a 60-second report without screen recording. Inspect water, transparent
effects, shadows, HUD, split screen and scene transitions for unchanged output.
Report 1050027's actual target size, pass reductions and enhanced GPU stages.
If frame time remains above budget, that identifies the next measured target;
it does not justify lowering quality against the user's request.

Build identities and completed check results are recorded in the candidate's
delivery `VALIDATION.md`. Sustained 60 FPS on the phone is not yet verified.
