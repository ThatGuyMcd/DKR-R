# Android stability and follow-up optimisation — 25 September 2026

## Scope and confidence

This is a Beta 12 Android compatibility candidate, not certification of all
Android devices. The supplied AYN Thor evidence identifies a concrete native
crash path. It does not establish that every reported black screen has that
cause, nor prove why the driver rejected the allocation. Physical-device
confirmation is still needed. Desktop gameplay, online synchronisation, saves,
HUD, custom mods, interpolation and the accepted CPU optimisations are not
being redesigned in this pass.

Previous packages remain in
`G:/DiddykongWorkFolder/cpu-power-pass-20260925/packages`.
The source snapshot and matching old Android binary are retained in
`G:/DiddykongWorkFolder/android-stability-20260925/baseline`.

## Evidence

The supplied ZIP was read without executing its contents. Its native tombstone
is binary protobuf, not a text stack trace. Decoding it and symbolising against
the exact retained library identified:

- AYN Thor, Android 13/API 33, Adreno 740.
- Old native build ID `9504c19a97bad00e1edb6b26478d51526b31d92d`.
- Vulkan device and swapchain setup succeeded; RT64 setup took 204.805 ms.
- Three descriptor allocation errors reported `0xC4642878`.
- SIGSEGV, null address, in Adreno `vkUpdateDescriptorSets`, reached through
  `VulkanDescriptorSet::setDescriptor`, framebuffer texture-view updates,
  `State::fullSync`, and DKR's graphics task processing.
- Accurate mode chose automatic resolution, producing 5x scaling, with 16x
  anisotropy. No texture pack or generated mipmaps were enabled in this run.

The original descriptor constructor in
`extern/rt64/src/contrib/plume/plume_vulkan.cpp` returns after allocation failure
but leaves a C++ descriptor object available. Its setter did not validate the
handle before the Vulkan update. Vulkan specifies null handles for failed
allocations: [allocation contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkAllocateDescriptorSets.html).
The failure-then-invalid-use chain is strongly supported; the unusual error
number is **not sufficient evidence of out-of-memory**.

The complete symbolised chain and diagnostic script are retained in
`G:/DiddykongWorkFolder/cpu-power-pass-20260925/ANDROID-THOR-CRASH-FINDINGS.md`
and `inspect_android_tombstone.py`.

## Implemented changes

### 1. Descriptor failure containment and compatibility

`patches/android/vulkan-descriptor-safety.patch` is applied to an isolated
build copy after the existing Android surface patch. No dependency checkout
or generated recompiled function was edited.

- Do not update or bind a null descriptor handle.
- Retain the first failure's stage and result across threads.
- Within DKR's display-list boundary, unwind the failed task; drain its CPU
  uploaders while its snapshot and draw data remain alive. Return normally so
  the runtime can complete the task's DP notification and shut down.
- Valid queued GPU work still uses normal submission, completion and teardown.
  There is no global shortcut that skips GPU fences or frees in-flight work.
- For a failed smaller variable-count allocation, make at most one retry using
  a pool reserved for the layout's full declared range. The layout, shader
  indexing, binding flags and actual descriptor allocation count are unchanged.
  Do not retry host-memory exhaustion, device-memory exhaustion or device loss.
  This is a compatibility attempt, **not a device-verified allocator fix**.
- Recognise descriptor-indexing and scalar-layout features promoted to Vulkan
  1.2 even when their old extension names are not advertised.
- Validate the sampled-image update-after-bind and nonuniform-indexing features
  actually needed by the texture path, as well as its existing requirements.
  Unsupported devices fail with diagnostics rather than proceeding knowingly
  with missing features. This does not add a fallback rendering backend.

### 2. Two additional Android-only stability corrections

- The submission path previously pointed `pWaitDstStageMask` at one scalar
  while accepting multiple wait semaphores. It now provides one mask per
  semaphore. [Vulkan submission contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubmitInfo.html).
  This is source-proven incorrect array handling, but not the supplied crash's
  symbolised cause; the log does not establish a multi-semaphore occurrence.
- `patches/android/buffer-worker-lifecycle.patch` sets the worker's running
  state before starting its thread, and changes the stop predicate under its
  wait mutex. This removes a start-after-stop race and a lost-wakeup window.
  It does not alter uploads, data ordering, worker count or normal throughput.

### 3. Android Accurate-mode workload

`mobile_graphics_preset.hpp`, `runtime_ui.cpp` and `rt64_renderer.cpp` now apply
an explicit 2x/no-MSAA/standard-precision budget for Android Accurate mode, with
4x anisotropy instead of inheriting 16x. Accurate retains its original timing
and aspect behaviour. Modern and its selectable mobile presets remain
available; Windows/Linux defaults are unchanged.

At the same aspect ratio, a 2x target has 16% of the pixels of a 5x target
(`2²/5²`). That is an 84% reduction in that render-target pixel count, **not an
84% measured CPU, memory, power or frame-time reduction**. This deliberately
trades some render resolution/filtering quality for a bounded mobile load.

### 4. Useful failures instead of unexplained black screens

Native startup errors can reach Android's own dialog UI. The first graphics
failure is persisted outside the renderer and offered again on reopening,
with a log-export action. Duplicate dialogs are suppressed. The native main
loop also checks after joining the runtime so fast startup failures are not
missed. Actual driver hangs remain a separate unresolved failure category;
these changes do not promise recovery from an unresponsive GPU driver.

APK version name remains `1.0.5-beta.12`; version code increases to `1050018`
so this candidate can upgrade the previous preview. Same preview application
ID and signing identity; no save/profile migration or settings reset.

## Follow-up optimisation audit: priority order

These are reviewed candidates and acceptance gates, not unmeasured speedups
claimed as delivered.

| Area | Source/evidence | Next action and safety gate |
|---|---|---|
| Mobile render cost | Thor's 5x/AF16 Accurate log; `ApplyConfig` and profile settings | Explicit budget implemented. Compare same scene on Thor and Honor, cold and warm, over 15 minutes. Measure actual presentation rate and thermal degradation. |
| Shader compilation | `plume_vulkan.cpp` creates compute/graphics pipelines with a null Vulkan pipeline-cache argument; `rt64_raster_shader_cache.cpp` already has bounded compiler workers | Measure cold/warm startup and compilation time first. Consider a device/driver/shader-version-keyed pipeline cache with atomic writes, bounded size, corruption fallback and no cross-driver reuse. Not added speculatively. |
| Snapshot bandwidth | `runtime-snapshot-pool.patch` still copies a complete 8 MiB snapshot per graphics task | Pooling is already retained. At 30 tasks/s this is about 240 MiB/s of copied payload, not measured memory-bus traffic. Measure task rate/copy time before considering finer-grained copies; display lists can dereference arbitrary guest memory, so partial copying without dependency tracking is unsafe. |
| Background wakeups | Main game event loop in `game_main.cpp` retains its 1 ms service interval | Measure wakeups while preserving SDL event ownership, online wait presentation and gyro/controller responsiveness. Introduce event-driven deadlines only with complete wake sources and input-latency tests. Do not simply increase sleeps globally. |
| Worker pressure | `render_resource_policy.hpp` already limits mobile raster/uber workers to two and texture worker to one; BufferUploader owns additional sleeping threads | Lifecycle fix implemented. Measure runnable/blocked thread time before consolidating upload workers; sleeping thread count alone does not prove CPU load. |
| Textures/mipmaps | Thor failed with both replacement packs and generated mipmaps off | Do not blame either for this crash. Separately profile large packs, memory peaks, import/decode concurrency and cache misses before imposing budgets that could damage compatibility. |
| Presentation/driver lifecycle | Existing Android surface-capability patch and error reporting; previous device capture/background problems | Qualify background/resume, screen lock, rotation/fold, fullscreen and capture separately on Adreno and Mali. Do not treat successful APK assembly or Vulkan setup as proof that pixels reached the screen. |
| Netplay, input and simulation | Existing full regression suites and earlier measured offline-worker/event improvements | Retain their accepted behaviour. No input-delay, rollback, simulation-rate, save-sync or interpolation shortcut is justified by this Android crash. Profile offline first, then equivalent online sessions. |
| Desktop CPU/heat | Previous controlled Ryzen 3900X Modern/60 test showed 83.306 to 23.917 CPU-seconds/minute; see `CPU-POWER-OPTIMISATION.md` | Preserve the measured fixes. This pass does not claim a new desktop speedup or a specific temperature drop. Repeat matched scene/settings comparisons before further desktop changes. |

## Acceptance and remaining uncertainty

Automated checks cover feature/retry policy, no retry on OOM/device loss,
first-error retention, scoped task-abort behaviour and mobile budget isolation,
alongside the existing application suites. They are not driver fault-injection
tests. Android Vulkan execution, failure shutdown and native dialog usability
still require device testing.

First tester pass:

1. Upgrade in place, retain personal saves and imported ROMs.
2. On Thor, start with original textures and Accurate; confirm game imagery
   and audio, not only launcher operation. Export logs if startup fails.
3. Verify logged 2x budget/AF4 and feature diagnostics. If present, check whether
   the full-pool retry succeeded or failed; do not infer that from a black screen.
4. Run the same track for 15 minutes; then test Modern/Battery and Balanced
   explicitly. Compare Honor separately rather than using it as universal proof.
5. Exercise exit/relaunch, background/resume, controller/touch input and overlay.
6. Only after baseline stability, test texture packs, mipmaps and online play.

The remaining allocation cause, other devices' unrelated crashes, sustained
thermal behaviour and comprehensive Adreno/Mali compatibility are open until
the candidate supplies that evidence.

## Build/validation result

- Android native build and Gradle assembly passed; APK v2 signature and 16 KiB
  ZIP alignment passed. Native build ID is
  `298f5c6e2b90e5cedbcb05c1f034982ee7a7c90a`.
- Windows: 81/81 tests passed; package self-tests passed.
- Linux x64: 81/81 tests passed; AppImage helper, pak and input-switch checks passed.
- Linux ARM64: native cross-build completed. Subsequent tests and AppImage
  packaging were blocked when WSL stopped accepting connections with
  `HCS_E_CONNECTION_TIMEOUT`. Do not substitute the old ARM package and call it
  a newly verified build.
- No Android device is attached to adb, so no physical-device result is claimed.
- `git diff --check` passed. Dependency checkouts and recompiled-source trees
  have no changes from this pass.

Build output/evidence root:
`G:/DiddykongWorkFolder/android-stability-20260925`.
Recoverable, unused RelWithDebInfo build artifacts were moved out of the Windows
build tree to `unused-debug-tree` and `unused-rt64-debug-artifacts` in that root
to relieve very low system-drive space. Source, Release artifacts, saves and
rollback packages were not deleted. WSL still requires recovery; low disk space
alone is not proven to be the cause of its connection failure.
