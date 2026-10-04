# Android frame-pacing pass — build 1050025

27 September 2026. Public version remains **1.0.5-beta.12**.

## Scope and honest outcome

This candidate targets confirmed shader-compilation and CPU upload overhead,
and improves the evidence available for the remaining frame drops. It is **not**
a claim that Android now matches desktop performance or that screen recording
is fixed. No physical Android device is connected to this build environment.

All dependency modifications are checked Patch Pipeline inputs applied to build
copies. The RT64/N64ModernRuntime/N64Recomp checkouts and generated game functions
are not edited. This pass does not change simulation speed, network policy,
water geometry, HUD layout, controls, graphics defaults or saved quality choices.
The separately mentioned Rocket-R camera-settings move was explicitly excluded
by the user after identifying the screenshot as another project.

## Evidence from the two new reports

Sources: `DKR-R-Android-startup (5).zip` and `(6).zip`, privately retained under
`G:/DiddykongWorkFolder/android-framepacing-20260927/`.

Both identify the Honor MBH-N49, Android 16/API 36, MagicOS 10.0.0.168,
build 1050024. Its actual APK library hash matches the reported hash:
`37a397eb4c3d520efbf7b725f7d5775b0699030535e6369a121dbb8dc15d206d`.
The older build-1050023 VI native-exit records remain in both exports. They must
not be counted as new 1050024 crashes.

### Report 5: real sustained stutter

| Captured measurement | Value / interpretation |
| --- | --- |
| Capture length | 60.7337 seconds, 2,645 presentation-return intervals |
| Frame interval p50 / p95 / p99 / max | 16.715 / 38.5756 / 50.2586 / 110.235 ms |
| Long intervals | 244 over 33.333 ms, 27 over 50 ms, 2 over 100 ms |
| Graphics pipeline creation | 58 calls, 6,493.8 ms aggregate wall time; the first 26 already account for about 5,363 ms at startup |
| Compute pipeline creation | 23 calls, 180.1 ms aggregate |
| Full-sync | 1,455 calls, 21,940.5 ms aggregate |
| GPU-fence waits | 30,565 calls, 57,163.8 ms aggregate across workers |
| Native framebuffer CPU readback | 2,901 calls, 191.035 ms, 445,593,600 bytes |
| Graphics snapshot copies | 1,455 calls, 2,378.27 ms, 12,205,424,640 bytes |
| UI generation | 2,939 calls, 496.554 ms |
| Friend snapshots | 598 calls, 26.3 ms |
| Process CPU | 48.552 CPU seconds during 59.701 sampled seconds: roughly 0.81 core-equivalents, not 81% of an eight-core chip |
| Memory / thermal report | Steady PSS about 255–265 MiB; reported thermal status 0 throughout. Neither is proof that every hardware limiter was inactive. |
| Surface | 2352x2172, four swapchain images, 120-Hz display with a 60-FPS game target |

The saved settings show 1x internal resolution, no MSAA, standard precision,
Modern presentation, generated mipmaps, 4x anisotropy and original textures.
Internal 1x resolution does not make the final presentation surface 320x240.
The foldable's native surface still contains about 5.1 million pixels.

In one gameplay window, graphics-task submission fell to about 20.47 Hz.
The recorded decode p50/p95 was 35.454/45.385 ms, render CPU 13.286/15.586 ms,
GPU 8.230/8.469 ms and workload 28.376/32.933 ms. Those are asynchronous rolling
histories, not a set of identical-frame timings. The decode scope includes
full-sync/wait work and is not a pure CPU decoder cost.

**Do not sum these wall-time regions.** They overlap, nest and run concurrently.
Likewise, presentation-return intervals are not physical display scanout times.
The evidence establishes missed deadlines and shader stalls, but does not yet
identify the dominant owner of every GPU-fence wait.

### Report 6: screen recording and device loss

The fresh runtime session reaches presentation after about 2.433 seconds, then
logs `vkQueueSubmit ... 0xFFFFFFFC` and the guarded GPU-submission failure.
That value is `VK_ERROR_DEVICE_LOST` (-4). Earlier query-pool result `0x1` is
`VK_NOT_READY`, not the same failure. The game runtime subsequently stops cleanly
and returns to the launcher; there is no new native exit trace proving a crash
in this session. Vulkan result meanings are documented in the official
[VkResult reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html).

The included completed performance report is **byte-identical** to report 5
(SHA-256 `64b7a62c661cf1a4c5780e2fd7f231db3ce63cfa45621b4445336a35d905fd69`).
It cannot measure the additional cost of the recording session. No new active
native performance capture was running during that short session.

Device loss is confirmed; its cause is not. The export does not prove an OOM,
thermal shutdown, surface-loss error, or a specific driver bug. We retain the
existing guarded failure path rather than dropping necessary synchronization
or concealing errors. On-device recording stability remains a separate gate.

## Implemented changes

### 1. Persistent Vulkan pipeline cache — Android only

Previously both compute and graphics pipeline creation passed `VK_NULL_HANDLE`
for the cache. RT64's in-memory shader cache did not persist Vulkan's driver
pipeline data across launches.

`patches/android/vulkan-pipeline-cache.patch` now supplies one internally
synchronized Vulkan cache for the active device. It loads once at device setup
and saves once during orderly teardown, after shader-creation workers stop.
There is no disk I/O or cache serialization inside the frame loop.

`android_pipeline_cache.hpp` validates vendor, device, driver version, Vulkan
cache UUID, header version, size and a corruption checksum. Payloads are bounded
to 32 MiB. A missing, truncated, incompatible or corrupt file falls back to an
empty cache; driver rejection retries without old data. Cache setup failure
falls back to the previous uncached behavior. Save/allocation/filesystem failures
are optional, and a device-loss session does not replace the previous cache.
The private temporary file is atomically renamed on Android.

No externally-synchronized flag is set: concurrent pipeline creation uses the
driver's cache synchronization as specified by
[VkPipelineCacheCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheCreateInfo.html).
One device/driver identity uses one file; files for older driver identities are
not reused or automatically deleted in this pass.

Expected benefit: subsequent launches and recurring pipelines can reuse driver
work. The first cold compilation still has to happen; the cache is not a cure
for sustained rendering costs. Actual cold/warm timings require phone testing.

### 2. Small upload batches stay on the submitting thread — Android only

`patches/android/buffer-small-upload.patch` runs CPU staging copies totaling up
to 64 KiB directly instead of waking a sleeping worker and immediately waiting
for it. Larger batches retain the asynchronous worker. The budget applies to
the combined valid batch, not separately to every upload, and accounts for the
full reupload required after buffer growth.

Write-only staging maps now specify an empty read range, avoiding unnecessary
invalidation of an entire non-coherent mobile allocation before a write. The
actual written range is still flushed on unmap. GPU copies, barriers, fences
and resource-lifetime ordering are unchanged.

The `workAvailable` predicate is now read and written under the same mutex.
Previously the producer/worker and waiter used different mutexes for that
non-atomic value. New submission also waits for the preceding CPU upload before
reusing its pending-upload vector.

### 3. Background shader compilation yields to gameplay — Android only

`patches/android/background-shader-priority.patch` implements the priority
demotion already requested by the shader compiler but previously ignored by
the Linux/Android default scheduler path. Idle uses nice +10; Lowest uses +5.
The current shader worker requests Idle. Gameplay threads are not elevated,
and no affinity pinning, root privilege or global scheduler change is used.

### 4. Better bounded diagnostics

- GPU waits now report separate framebuffer, workload, presentation, texture
  and other counters. These are subdivisions of the aggregate, not extra costs
  to add to it.
- CPU upload and upload-wait regions are measured when capture is enabled.
- Android logs the effective GPU keep-awake state; it is not silently changed.
- Completed Java performance reports include capture start/end UTC and start
  uptime, with a warning that a retained report may predate the latest session.
- Existing recording-start VI correction, device-loss guard, startup transfer
  qualification, mobile UI, touch controls and DKR-R app icon are preserved.

## Verification and limits

The pure cache tests cover all byte truncations and single-byte corruptions of
a fixture, wrong vendor/device/driver/UUID, size bounds, an unset path, actual
file save/load, and POSIX replacement of an existing file. They do not emulate
an Android vendor driver.

The optional GPU test uses the **actual staged BufferUploader implementation**
with a real desktop Vulkan device: 16-byte, 16-KiB, 64-KiB, 128-KiB and 256-KiB
uploads; full/partial/full updates; buffer growth; copies back to CPU memory
checked byte-for-byte. Small synchronous and large asynchronous paths pass.

Five paired desktop microbenchmarks of 10,000 16-KiB submit/wait pairs measured
baseline 41.889 / 74.879 / 67.029 / 52.222 / 33.556 ms versus candidate
20.719 / 12.483 / 10.279 / 10.930 / 10.328 ms. Medians: **52.222 vs 10.930 ms**.
This isolates the CPU upload scheduling path, not gameplay or Android FPS;
concurrent build/test load means it is not a pristine whole-system benchmark.
The baseline target includes the existing worker-lifecycle fix and is never
packaged. No speedup multiplier for the game is inferred from this result.

Full suite and packaging results are recorded in the delivery validation file.
Linux ARM was not rebuilt: Docker remains untouched at the user's request.

## Device acceptance and next bottleneck

1. Update in place to **1050025**, without clearing app data, settings or saves.
2. First test normal launch, the same slow water scene, and a clean return to
   the launcher **without screen recording**.
3. Launch the same scene again with unchanged settings. The first launch may
   compile pipelines; the second can reuse the saved cache. Compare startup and
   recurring stutter separately.
4. If stable, collect one 60-second in-app performance report, then export logs.
   The new per-worker counters identify whether native framebuffer sync,
   workload submissions, texture transfers or presentation dominate remaining
   waits. Stop repeating a failing capture path and export ordinary logs instead.
5. Screen recording is a separate optional check after normal gameplay passes.
   Do not call the earlier device-loss issue fixed unless that test succeeds.

Further optimization must follow that evidence: reduce redundant work at the
specific expensive submission path; measure native-surface composition costs;
and assess snapshot-copy cost independently of overlapping waits. A new render
resolution cap, temporal shortcuts or removal of correctness fences has not
been slipped into this candidate. Those are materially different tradeoffs.

The previous 1050024 artifacts remain intact at
`G:/DiddykongWorkFolder/android-recording-crash-20260927/delivery/`.
New packages are separate under
`G:/DiddykongWorkFolder/android-framepacing-20260927/delivery/`.
