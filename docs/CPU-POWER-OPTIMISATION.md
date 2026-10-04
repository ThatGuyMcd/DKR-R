# Beta 12 CPU/power follow-up — 25 September 2026

This is a rebuild of **1.0.5-beta.12**, not a new gameplay or graphics preset.
The intended comparison is the same game, image settings and 60 FPS target.
CPU time is measured; degrees-Celsius or Android-device gains are not inferred.

## Confirmed root cause

The packaged baseline's `DKR-1` guest idle thread consumed approximately
9.3–9.6 CPU-seconds per ten seconds. Instrumenting the actual external-message
delivery path found 71–77 million failed retries per five seconds. The target
was `0x801210E0`, `sSIMesgQueue` in the v77 symbol map: the controller/SI queue.
It holds one message. Requeuing a reliable event immediately onto the same
native ready FIFO made the guest idle thread continuously runnable while that
guest queue was full. `pause_self` did call a blocking dequeue, but its own
immediate requeue prevented that dequeue from ever blocking.

This is not fixed by reducing the rendered FPS, removing interpolation,
changing CPU affinity, changing an operating-system power plan, or weakening
SP/DP completion. None of those changes was made.

## Implemented changes

1. **Deferred external events.** A failed reliable send goes into a separate
   blocked list. Receiving from or reinitialising the destination guest queue
   makes its pending events eligible again. New external events still wake
   the guest immediately. Both lists are drained on runtime reset, including
   delivery-state ownership. Null shutdown wakeups no longer enter `do_send`
   as if they were real guest queues. Native producers never inspect RDRAM.
2. **Renderer queue waits.** Present/workload producers wait on the existing
   cursor condition variable instead of a mutex spin. Barrier release notifies
   waiters; shutdown changes the predicate under the relevant mutexes and
   notifies. Ring sizes, ordering and framebuffer ownership stay unchanged.
3. **Shader drain.** Cache teardown waits for compiler completion notification
   instead of repeatedly locking and polling. Worker startup no longer races
   its constructor's initial `threadRunning` assignment. Stop notification is
   coordinated with the queue mutex.
4. **GPU keep-awake compatibility setting.** Default off, with a persisted,
   controller-accessible Graphics checkbox. The old dummy-submission workaround
   remains available for drivers which downclock too aggressively. Live changes
   are applied under the renderer presentation mutex through `updateUserConfig`,
   not from the UI directly into GPU workers. No image-quality setting changes.
5. **Offline session worker.** A truly offline worker with no pending replay
   completion sleeps until a lifecycle/job event. Host, join, disconnect and
   shutdown wake it. The existing online receive cadence and all input/rollback,
   barrier, save and security rules remain unchanged. Offline UI pumping cannot
   repeatedly wake this worker without work to do.
6. **Controller inventory.** SDL2 hotplug/remap events mark inventory dirty.
   Gameplay and UI still sample input normally, but no longer enumerate and
   reconcile unchanged inventory on every poll. Explicit configuration calls
   force a refresh; a one-second safety rescan covers missed driver events.
   SDL3 and gyro sampling deadlines were not reduced.
7. **Diagnostics.** Optional `DKR_IDLE_PROFILE` reports bounded retry counts;
   `DKR_POWER_PROFILE` reports actual presentation/simulation rates. The Windows
   CPU comparison scripts record process and named-thread CPU time and reject
   a test whose effective presentation profile is not Modern/60 FPS.

All dependency changes are checked patches applied to isolated build copies.
No RT64/N64ModernRuntime/N64Recomp checkout or generated recompiled source was
edited. The additional application changes are confined to settings publication,
input inventory, offline-worker waiting and measurement.

## Validation and measurement caveat

Initial diagnostic runs used an incomplete settings fixture, which correctly
fell back to Accurate/30 FPS. Those matched runs established the hot loop and
showed mean process CPU time changing from 12.708 to 3.329 CPU-seconds per ten
seconds for the deferred-event fix alone (about 74%). **They are not 60 FPS
measurements.** Modern/60 FPS comparisons use a complete settings fixture,
alternate baseline/candidate ordering, and check the effective-profile log.
See the accompanying measurement report for the final numbers.

The corrected three-round Modern/60 FPS comparison is now complete on the
Ryzen 9 3900X. The previous package used **83.306 CPU-seconds per minute**;
the rebuilt package used **23.917**, a **71.29% reduction** in this controlled
intro-sequence workload. Individual runs were 85.263/82.517/82.138 for the
baseline and 23.648/24.038/24.064 for the candidate. Each run captured nine
ten-second samples during a 100-second launch. Ordering alternated, both used
the same complete settings fixture, and all six logged Modern/effective=60
and clean shutdown. Candidate presentation samples generally report about
60 FPS, with some loading dips; p95/p99 displayed-frame latency is not proven
by these periodic samples. No performance guarantee for every track/device
or specific temperature reduction follows from this result.

Windows, Linux x64 and Linux ARM64 each pass all 81 application tests. ARM64
tests run under QEMU, not physical GPU hardware. The previous ARM failure was
a test-harness race: the manually flushed fake transport also had a live
worker mutating its non-atomic counters. Only that manually driven case now
stops its worker; its exact count assertions remain. The pump-free admission
case still exercises real workers. No assertion was disabled or relaxed.

New queue checks cover blocked-versus-ready separation, unrelated queue
delivery, per-deferred-list order, waiting/retry notification, timeout and reset
draining. Existing suites cover networking, friends, input, snapshots, audio,
HUD, interpolation, saves and mods. Passing them is not a claim that every
visual scene, device failure or real WAN session has been exercised.

Additional Windows startup smoke checks completed with the alternate v80 ROM
on DX12 (1,775 graphics tasks) and v77 on Vulkan (1,722 graphics tasks). Both
ran for 60 seconds, logged Modern/effective=60 and stopped cleanly. These
checks establish rendering progress and shutdown, not visual correctness or
physical Linux/Android GPU compatibility.

## Disposition of the wider plan

| Phase | Disposition |
|---|---|
| 0: baseline and attribution | Four previous packages and dirty source preserved. Named-thread and in-path counters identify the SI retry loop. Corrected Modern/60 comparison added. No temperature/power sensor data claimed. |
| 1: wasted waits | Guest retry loop, present/workload waits, shader drain and GPU keep-awake addressed. Existing pacing tail retained: changing it without physical-platform frame-time evidence could introduce judder. |
| 2: service overhead | Offline parking and hotplug-driven inventory implemented. Main/graphics/SP/cleanup timed waits and broad social dirty-generation caching retained until their complete wake/mutation contracts can be qualified. |
| 3: real hot paths | Full immutable RDRAM snapshots, their pool and worker budgets retained. No selective-copy, physics/culling/interpolation, persistent Vulkan pipeline-cache or fast-math change was justified by the current profile. These remain gated work, not secretly completed features. |
| 4: platform qualification | Four architectures compile/package; desktop runtime and automated tests available. Physical Android/ARM GPU, thermal, capture, fold/rotation and Deck compositor qualification remain open. No default launcher-backend switch, ADPF or dynamic-resolution feature added without those measurements. |
| 5: delivery | Four rebuilt artifacts, checksums and test evidence. Real-device/visual/WAN and sustained-temperature acceptance is not established by compilation or QEMU. |

Thus this delivery implements the confirmed low-risk optimisation batch;
it does **not** certify the entire broader device-qualification programme as
complete. The outstanding gates require hardware/profiling evidence rather
than speculative changes to working gameplay.

## Rollback and local artifacts

Work/output root: `G:/DiddykongWorkFolder/cpu-power-pass-20260925`.
`baseline` contains the previous four packages, their checksums/manifest and
the modified/untracked source snapshot relative to HEAD
`ab141cb0c031a8f2986d1fa15dadc98b076e8bcc`. Existing changes from other work were
preserved. No automatic user-save migration or settings reset is performed.

The build drive became nearly full. Unused `RelWithDebInfo` libraries/binaries
from `C:/DKRPort/build/dkr-runtime-rt64-lod-bias` were moved, not deleted, to
`unused-debug-artifacts/libraries` and `unused-debug-artifacts/bin` in the work
root. Current Release outputs and source were left in place.

Android remains an arm64 preview (Android 9+), signed with the same local
preview certificate. Its version code is 1050017 to permit an upgrade over
the previous Beta 12 APK. Package signature/alignment checks do not demonstrate
that every Android GPU/driver can render the game.
