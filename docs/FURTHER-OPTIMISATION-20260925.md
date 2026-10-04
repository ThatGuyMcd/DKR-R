# Further optimisation pass — 25 September 2026

Status: four builds packaged; primary comparison and timing-spike follow-up
completed. A newly supplied OPPO/Mali startup-stall report remains unresolved;
see `ANDROID-OPPO-STARTUP-20260925.md`. These packages are not a claimed fix for
that newly reported failure.
Version remains 1.0.5-beta.12.

## Scope and preservation

The previous Android-stability delivery and all modified/untracked source were
copied to `G:/DiddykongWorkFolder/further-optimisation-20260925/baseline` before
editing. No saves/settings were reset. Dependency changes use checked patches
applied to isolated build-stage copies. No direct edits to submodules,
RecompiledFuncs or RecompiledPatches. Networking, physics, interpolation, HUD,
mod compatibility and image-quality defaults remain unchanged in this pass.

## Implemented

### Windows presentation wait

The previous coarse wait stopped 2 ms before each deadline. Windows' measured
short-sleep tail could spend a significant fraction of that interval spinning.
A fresh Modern/60 FPS baseline showed RT64 Present consuming approximately
81–109 ms of CPU per second in the measured ten-second windows.

`presentation_wait.hpp` now uses a reusable thread-local high-resolution
waitable timer to wait until 500 microseconds before the deadline. The existing
RT64 precision tail still handles the remaining time. The handle is closed
when its thread exits. Unsupported Windows timer flags or errors use the old
coarse wait automatically. `DKR_LEGACY_PRESENT_WAIT=1` explicitly selects the old
path for controlled comparison/recovery. Linux and Android wait algorithms
are unchanged; their scheduler behaviour must not be inferred from Windows.

This affects presentation waiting only, not simulation/input/network timing.
The microbenchmark now measures actual process CPU time with GetProcessTimes
or getrusage, rather than std::clock (which is elapsed time on MSVC), and uses
the same 1 ms Windows timer request as SDL.

### Bounded opt-in diagnostics

`DKR_POWER_PROFILE=1` records cumulative counts, wall time and byte totals for:

- immutable graphics snapshot copies;
- RT64 full-sync work, GPU fence waits, and framebuffer readback copies;
- friends UI snapshot publication;
- Vulkan compute and graphics pipeline creation.

Successful swapchain-return intervals are also summarised in fixed 300-sample
windows (p50/p95/p99/max). These are not physical display scanout measurements.
No per-frame log spam, heap allocation or extra renderer locks are introduced
by the diagnostics. Counters use relaxed atomics; simultaneous reads are
approximate. Nested/concurrent region wall times MUST NOT be added up as CPU
time. CPU attribution still comes from OS process/thread counters.

## Evidence and gates

An instrumented Vulkan intro run, with compilation still occurring elsewhere
on this machine, recorded the following cumulative diagnostics at about 35 s.
It is a hotspot observation, not a controlled before/after benchmark:

| Region | Calls | Wall ms | Bytes |
|---|---:|---:|---:|
| Snapshot copy | 1,040 | 738.226 | 8,724,152,320 |
| Full sync | 1,040 | 2,649.883 | — |
| GPU fence wait | 10,732 | 3,592.444 | — |
| Readback copy | 2,072 | 80.124 | 318,259,200 |
| Friends snapshot | 357 | 3.299 | — |
| Vulkan compute creation | 23 | 1.886 | — |
| Vulkan graphics creation | 56 | 18.632 | — |

The graphics workload is not evidence of a runaway friends loop. Broad dirty
generation caching would risk missing expiry/cancellation/presence mutations
to save a very small observed cost, so it is not being enabled speculatively.
Likewise, pipeline creation is not a measured dominant cost on this machine.
Persistent Vulkan cache implementation remains gated on cold/warm evidence
from affected devices. Vulkan does support driver-managed cache reuse and
persistence, but that alone does not establish a performance benefit here:
[Khronos pipeline-cache guide](https://docs.vulkan.org/guide/latest/pipeline_cache.html).

Framebuffer copies and waits are exercised. They have real consumers and
resource-lifetime contracts: no global readback/fence bypass is made. Full
8 MiB immutable snapshots remain necessary until access/dependency tracking
proves a selective copy safe. The allocation pool and existing worker budgets
are retained. No fast-math, culling, dynamic-resolution or simulation change.

The main SDL loop's 1 ms servicing also handles the separate SDL3 input
backend, online wait rendering and lifecycle/watchdog work. An SDL2-event-only
wake replacement would not cover all those sources; it is not substituted
without that wake contract. The existing Linux accelerated-launcher opt-in
is retained until physical X11/Wayland/Gamescope handoff qualification.

## Test-harness correction

The ARM latency harness could stop its fake transport's worker between the
worker checking its offline wait predicate and actually sleeping. It set the
stop flag outside the condition-variable mutex. The production destructor
already sets that flag under the mutex. Both manually stopped test cases now
follow production's locking contract, without removing assertions or changing
game networking. The hung owned QEMU test was terminated, then rebuilt and
rerun. The complete ARM suite subsequently passed. This was not fixed by
increasing a timeout or disabling the latency tests.

## Controlled Windows result

Three alternating old/new pairs used the **same final executable**, settings,
ROM and instrumentation, with `DKR_LEGACY_PRESENT_WAIT` selecting only the
wait implementation. Each run lasted 60 seconds; process CPU measurements
cover five ten-second samples, normalised to CPU-seconds/minute. Builds and
package jobs had finished before the comparison. Modern presentation,
60 FPS target, DX12 and clean shutdown were checked on every run.

| Run | Old wait CPU s/min | New wait CPU s/min |
|---|---:|---:|
| 1 | 22.614 | 20.112 |
| 2 | 22.589 | 20.851 |
| 3 | 23.887 | 20.587 |
| Mean | 23.030 | 20.517 |

This is approximately **10.91% lower process CPU consumption** in this intro
workload, on top of the changes in the previous delivery. RT64 Present's
measured thread CPU dropped approximately **48.99%** across the named-thread
samples (the first sample cannot track threads created after sampling began).
This is not a measured temperature, wall-power or all-platform improvement.

Most 300-presentation windows had p99 return intervals around 17 ms. The full
observed window-p99 ranges were 16.822–18.438 ms for the old wait and
16.818–32.028 ms for the candidate. The 32.028 ms value occurred in one window
of candidate run 3, despite its neighbouring windows staying near 17 ms. It is
retained in the evidence, not filtered away. The extra pair completed: old/new
CPU was 31.843/24.242 CPU-seconds/minute, with presentation-thread CPU averaging
851.563/414.063 ms per ten-second named-thread sample. Its window-p99 ranges
were 16.905–48.439 ms for the old wait and 16.841–18.593 ms for the new wait.
The candidate's 32 ms window did not recur in this pair. However, the old
wait also showed considerably more workload/timing variability than in the
primary runs. Keep this follow-up separate from the primary average; it is
not proof that the wait change eliminates stutter or explains every spike.

Evidence: `wait-comparison/*/{cpu.jsonl,logs/runtime.log}`,
`wait-confirmation/*/{cpu.jsonl,logs/runtime.log}` and
`pacing-microbench-500us.log` under the work directory. The synthetic timer
test (600 deadlines/run) showed candidate p99 deadline lateness of
92–236 microseconds, versus 6–38 microseconds for the old tail. This small
precision/CPU tradeoff is distinct from full-game frame-time spikes.

## Build and automated validation

- Windows: 81/81 tests pass, including the revised harness and profiling checks.
- Linux x64: 81/81 tests pass.
- Linux ARM64: 81/81 tests pass under QEMU (not physical GPU validation).
- Both AppImages pass packaged helper/accessory/input-switch checks.
- Windows package passes its helper/accessory/input-switch checks.
- Android: rebuilt arm64-v8a APK, Android 9+, versionCode 1050019; signature
  and 16 KiB zip alignment verified. The existing preview certificate is retained.
- Final packaged Windows/Vulkan smoke runs completed 1,775 graphics tasks
  for v1.0 and 1,776 for v1.1, each running for 60 seconds and stopping cleanly.
  These normal-load runs do not cover the delayed-worker failure below.
- Protected dependency/generated-source directories have no direct edits.

All four outputs are in
`G:/DiddykongWorkFolder/further-optimisation-20260925/delivery`.
`packages` in the work directory contains earlier internal candidates, NOT
the final delivery. The original rollback build remains under `baseline`.

## Validation still required

Hardware acceptance is distinct from builds/tests:

- The AYN Thor owner has not yet reported on the previous stability APK.
- A second user's OPPO CPH2371/Mali-G68 logs show zero successful presentations
  and halted guest progress after repeated stale scheduler completions. Its
  automatic resolution, portrait-sized surface and 90 FPS target warrant a
  conservative-preset comparison; the retail scheduler's timeout/retirement
  path needs targeted tracing. An isolated two-second graphics-thread delay
  during v1.1 startup reproduced persistent zero guest/graphics/presentation
  progress even after the worker resumed. This is a concrete remaining
  task-lifecycle defect, not merely slow rendering. No fix for it is included
  in these packages. The tester cannot run the preset comparison yet.
- Android/ARM GPU correctness, sustained thermal behaviour, rotation/resume,
  screen capture and driver diversity require physical devices.
- Per-track, texture-pack, online/WAN and 1–4-viewport visual acceptance is not
  established by the unattended intro sequence or policy tests.
- No temperature reduction in degrees is claimed without sensor evidence.

WSL/Docker were restarted with explicit permission after Ubuntu repeatedly
failed to start. Ubuntu is now available again, and Docker Desktop reopened.
This permits Linux x64/ARM build and test work to resume.

## Disposition of every approved investigation area

| Area | Outcome of this pass | Outstanding gate |
|---|---|---|
| Matched profiling | Preserved baseline; real-game alternating wait comparison and bounded in-path/interval diagnostics | Physical per-track/pack/viewport/WAN matrix and thermal sensors |
| Frequent wakeups/social copies | Measured friends snapshot cost; retained immediate cancel/expiry semantics and complete input/online service loop | Event-driven rewrite only after all non-SDL2 wake sources are covered |
| Presentation pacing | Windows high-resolution wait, legacy fallback and real-game qualification | Linux/Android timing changes need platform measurements, not Windows extrapolation |
| GPU sync/readbacks | Counted real calls/bytes/wall cost; retained dependencies and fences | Consumer/dependency trace before any bypass |
| Shader compilation | Measured Vulkan creation separately; observed small cost here | Cold/warm affected-device evidence before persistent-cache work |
| Memory bandwidth/cache | Measured snapshot and readback traffic; retained pool and budgets | No partial snapshots or adaptive cache sizing without access/eviction evidence |
| Linux launcher backend | Both AppImages rebuilt and packaged helpers tested | Physical compositor/launcher-game handoff before changing backend default |
| Android/device stability | Retained latest descriptor/surface/uploader safety work; rebuilt signed preview | AYN Thor feedback, other GPU families, sustained load and lifecycle tests |

The code/build batch is not a claim that the entire hardware qualification
programme is complete. Gates deliberately left open above are not implemented
features or measured speedups.
