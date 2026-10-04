# Android quality-preserving scheduling pass — 1050026

## Scope

The user explicitly rejected automatic resolution budgeting: **keep image quality
unchanged; optimise code only**. No resolution, anisotropy, framebuffer precision,
MSAA, mipmap, FPS target, preset or saved-setting changes are included here. This
is a narrow scheduling candidate, not a claim of 60 FPS on every Android device.
The public version remains 1.0.5-beta.12; Android's build code becomes 1050026.

The preceding 1050025 APK and matching Windows/AppImage remain preserved under
`G:/DiddykongWorkFolder/android-framepacing-20260927/delivery/`. New deliverables
are separate under `G:/DiddykongWorkFolder/android-realwork-priority-20260927/`.
Docker and Linux ARM recovery remain out of scope, as requested.

## What report 7 establishes

Source: the user-supplied `DKR-R-Android-startup (7).zip`, privately preserved
with this candidate. Its APK library identity matches the delivered 1050025.

* Honor MBH-N49 / Android 16, 2352 x 2172 surface, 120 Hz display, 60 FPS target.
* Automatic integer resolution; the diagnostic scale estimate is 10x. This is
  not an exact measurement of every internal render target's dimensions.
* High-precision framebuffer, 16x anisotropy, generated mipmaps, no MSAA,
  no replacement texture packs. GPU keep-awake is enabled.
* Completed 60.5316-second capture: 1,607 presentation-return intervals;
  p50/p95/p99/max = 32.3448 / 63.8516 / 73.8594 / 146.507 ms.
  770 intervals exceed 33.333 ms; 429 exceed 50 ms.
* Gameplay GPU histories reach roughly 29–32 ms median per rendered frame.
  A 60 FPS frame budget is 16.67 ms. CPU-side submission improvements cannot
  by themselves establish that the GPU will meet that budget at this quality.
* GPU-fence aggregate wall time is 71,206 ms across workers. Workload worker:
  14,537 waits / 39,859.5 ms; framebuffer worker: 7,643 / 26,715.1 ms;
  presentation: 1,614 / 4,062.15 ms; texture worker: 880 / 550.728 ms.
* Buffer upload CPU work is 237.986 ms; uploader wait only 65.8467 ms across
  the entire capture. UI generation is 430.471 ms and friend snapshots 27.9976 ms.
  Another large UI rewrite is not supported by this evidence.
* Vulkan pipeline cache loaded successfully. Initial compute creation is
  1.105 ms; total graphics creation is 1,755.49 ms, with new scene shaders still
  being encountered. First presentation is about 641 ms after launch request.
* The session records no graphics failure and returns to the launcher cleanly.
  This export is evidence of sustained stutter, not a new startup crash.

Wall times overlap, nest and run concurrently: **do not add them as CPU usage**.
GPU histories are rolling windows, not identical-frame trace correlations.
The workload-fence total includes dummy keep-awake submissions, so it was not a
clean measure of actual game rendering. The new counters separate those paths.

Report 5 used 1x internal resolution, standard precision and 4x anisotropy.
Report 7 is therefore not a controlled before/after performance comparison with
report 5. This observation does not invalidate the reported stutter and is not
used to change the user's settings.

## Confirmed code defect: dummy work can compete with frame preparation

In `extern/rt64/src/hle/rt64_workload_queue.cpp`:

1. `renderThreadLoop()` owns `threadMutex` for an entire real workload, including
   presentation dependencies, matching, CPU preparation, uploads and rendering.
2. `threadRenderFrame()` only owns `workerMutex` while recording/submitting and
   waiting for its GPU command list. The renderer can be busy outside this span.
3. `idleThreadLoop()` previously checked **only** `workerMutex`, then used the
   same Workload Graphics worker, command list and fence to submit dummy compute
   work, retrying every millisecond. It did not check pending real workloads.

This is a concrete admission defect, not proof that it accounts for all the
29–32 ms GPU frames or all the observed stutter. Its exact device cost requires
an otherwise identical phone test.

### Correction

An Android-only Patch Pipeline stage now calls `render_idle::try_submit`:

* Try the renderer mutex first; skip if a real workload owns it.
* Inspect queued work under a non-blocking cursor lock. Skip on contention,
  pending work or shutdown. Release this lock before any GPU operation.
* Try the worker mutex next. Submit the existing identical dummy commands only
  if both admission checks pass.
* Hold both admission locks through the existing fence wait, then release them
  with RAII, including exception unwinding. Do not remove any GPU fences.
* Keep the user's opt-in switch and the existing true-idle polling interval.
  No graphics commands, samples, pixels or interpolation frames are dropped.

Lock order matches the real renderer: renderer then worker. The cursor probe is
non-blocking and never held across GPU work. It cannot create a wait cycle with
the producer or the render thread. A new frame arriving just after admission
may still follow one already admitted dummy operation; in-flight GPU work is
not preempted. This change does not promise strict real-time scheduling.

The new stage is composed **after** the workload barrier wait and water history
stages. No dependency checkout, recompiled game function or generated patch
source is hand-edited. Windows/Linux keep their existing renderer scheduling.

### Evidence and regression checks

`DKRRenderIdlePolicyTests` exercises real mutex contention, not sleep-dependent
timing assertions. During a held real-frame preparation lock and free worker,
the original admission admits 10,000/10,000 attempts; the new admission admits
zero. This is a scheduling regression test, **not an FPS benchmark**.

Other cases cover genuinely idle submissions, pending work, worker/cursor
contention, exception lock release, in-flight fence ownership, 2,000 concurrent
real workloads with no idle overlap, and clean termination. The production
Android workload translation unit compiles the same helper through the staged
patch. Performance-counter tests check both new labels and zero-duration events.

`idle-gpu-submit` reports dummy submit/wait wall time; `idle-gpu-skipped` counts
rejected admission attempts. These are bounded opt-in aggregates with no
per-frame log spam. Submit time is nested inside workload/GPU fence totals;
again, do not sum them. Skipped count is not a number of dropped game frames.

## Changes deliberately not made

* No resolution cap or automatic quality preset migration.
* No native framebuffer/readback removal: the game's memory semantics and
  existing shadows, HUD and transitions depend on those paths.
* No blanket barrier or render-pass removal. Depth sampling, blits, resolves
  and N64 framebuffer reuse require explicit ordering and retained contents.
* No speculative GPU load/store discard or global shader precision reduction.
* No network timing, mod, controls, launcher-layout or simulation changes.

Tile-based render-pass traffic remains a worthwhile follow-up, but the general
[Vulkan tile-rendering guidance](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html)
does not prove that any particular DKR render pass can safely be fused. That
requires an on-device frame capture identifying the relevant attachments and
dependencies, followed by image comparisons, especially water, transparency,
Taj's shadow, HUD and split screen. It is not safe to advertise such changes
without that evidence.

## Phone acceptance test

1. Install 1050026 without clearing app data; verify its build code in About.
2. Keep the same graphics settings, orientation and scene. Do not screen-record
   during this comparison; recording/device loss is a separate issue.
3. Allow scene shaders to warm, then record the same 60-second slow-track route
   with the existing performance report control. Export Startup Logs afterward.
4. Compare interval percentiles, actual graphics task/simulation rates, GPU
   histories and the two new idle counters. Check shadows, water, HUD, touch
   controls, overlay and transitions visually for unchanged rendering.
5. Test once with keep-awake enabled (the reported case). The disabled path
   should remain unchanged; this patch cannot improve a path it never enters.

No physical Android device is connected here. Desktop tests and successful APK
assembly do not establish Android FPS, thermal behaviour or recording stability.
Further optimisation remains necessary if real GPU frame time remains above
the target after avoidable submission contention is removed.

## Completed build validation

Android native Release build and APK assembly succeeded; versionCode 1050026,
same preview signing certificate, APK signature v2 and 16-KiB alignment passed.
All bundled native libraries have 16-KiB ELF LOAD alignment. Windows and Linux
x64 builds each passed all 90 DKR CTests and their packaging runtime checks.
The new scheduling test also passed AddressSanitizer/UBSan and ThreadSanitizer
on Linux. A repeated visible Windows Vulkan game test completed 1,328 graphics
tasks over 45 seconds and shut down cleanly. The initial visible run exited
cleanly before the watchdog marker and was not counted as a timed-test pass.
Package identities and hashes are recorded in the delivery `VALIDATION.md`.
