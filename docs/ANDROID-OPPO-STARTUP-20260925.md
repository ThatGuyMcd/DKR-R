# OPPO/Mali Android startup freeze — 25 September 2026

Implementation follow-up: `HOST-TASK-LIFETIME-20260925.md`. The findings below
describe the pre-fix investigation; the follow-up records the approved
correction, regression tests and remaining physical-device gate.

## Status and scope

Investigation of the newly supplied `DKR-R-Android-startup (1).zip`.
Archive SHA-256:
`7b639281659d805bed29a002075506e8a859db1fbc7e1c38c114dd234603707a`.
Read the text entries directly, without executing or installing anything from
the archive. No runtime-code changes were made for this report. Existing
optimisation packages and rollback copies remain intact.

The report confirms a failure to progress after startup. A controlled local
delay test additionally reproduced persistent stalled guest progress in the
current packaged v1.1 runtime. The Android report itself does **not**
contain a native tombstone, ANR thread dump, graphics validation output, or
enough timing detail to establish the complete causal chain. It is not the
earlier, symbolised AYN Thor null-descriptor crash.

## Observed evidence

| Item | Evidence in the supplied archive |
|---|---|
| Device | OPPO CPH2371, Android 13/API 33 |
| GPU | Mali-G68 MC4; Vulkan 1.1.177; driver 0x8001000 |
| App identity | 1.0.5-beta.12; no versionCode or native build ID exported, so the exact Beta 12 candidate cannot be established |
| Launcher | OpenGL ES accelerated; first frame at about 552 ms |
| Game handoff | Vulkan window created; renderer setup completed in 1,928 ms |
| Settings | Modern, automatic resolution, 90 FPS target, AF16, no MSAA, standard precision |
| Surface | Swapchain 1080 x 2245, three images, RGBA8; logged resolution scale 10.0 |
| Texture packs | Zero packs; generated mipmaps disabled |
| Guest activity | Initially sim/graphics about 0.8 Hz, subsequently both zero |
| Video callbacks | Continue around 60 Hz after guest progress stops |
| Actual presentations | All performance reports say 0.0 FPS |
| Scheduler | Seven logged late SP completions and seven late DP completions, each with task=00000000 |
| Process exit | Reason 10, status 0, descriptions beginning `remove task` |

Android documents reason 10 as user-requested termination, including removal
from Recents. This is consistent with closing an unresponsive game, not
evidence of a recorded native crash. It does not rule out a preceding hang.
[Android ApplicationExitInfo](https://developer.android.com/reference/android/app/ApplicationExitInfo#REASON_USER_REQUESTED).

The archive's current `runtime.log` is the subsequent launcher-only run;
`runtime-previous.log` contains the failed game launch. Do not diagnose only
the shorter current log.

## Source findings

### 1. Retail scheduler timeout can invalidate host work that is merely slow

`extern/dkr-decomp/libultra/src/sc/sched.c`, `__scHandleRetrace`:

- Increments counters while SP/DP tasks are active.
- Once a counter exceeds ten retraces, clears the corresponding active task.
- The DP branch sends `gBootBlackoutMesg`, whose payload requests a skipped
  framebuffer swap, before clearing the task pointer.

Eleven retraces are approximately 183 ms at 60 Hz. This is a hardware watchdog
for the original console, not a host shader-compilation/GPU-startup budget.
Slow host work can therefore outlive the retail task it is meant to complete.

`runtime-recomp/src/game/runtime_stubs.cpp`, `dkr_scheduler_sp_event_valid` and
`dkr_scheduler_dp_event_valid`, guard null/invalid task pointers. The SP guard
also acknowledges the host wait. These prevent invalid native accesses; they
do not restore a task retired earlier by the retail watchdog. Null completion
messages alone do not prove timeout: duplicated/misassociated edges remain
alternative causes until task identity and retirement are traced together.

`extern/dkr-decomp/src/rcp_dkr.c`, `gfxtask_wait`, waits on the graphics task's
message queue. `extern/dkr-decomp/src/video.c`, `fb_update`, honours the skip
message. This creates a credible route from a temporary host delay to broken
guest progress. See the controlled reproduction below. We have reproduced
the failure signature locally, but not the OPPO's original driver/workload
trigger on its hardware.

The current `game_main.cpp` comment assumes Release tasks finish within DKR's
watchdog. That assumption is not safe for all Android GPUs and cold drivers.

### 2. Automatic resolution is an unnecessarily expensive startup choice here

`RT64Renderer::get_resolution_scale` computes ceil(surface height / 240).
The reported 2245-pixel height therefore yields ten. RT64's workload path
similarly derives automatic scale from swapchain height and VI reference
height. At a 240-line reference, 10x versus 2x is 25 times the target pixel
count at the same aspect ratio, not a measured 25x frame-time difference.

The reference height can vary by VI, so the diagnostic scalar is not a
capture of every allocated target's exact dimensions. The 90 FPS target adds
pressure, but neither setting alone proves the permanent stall's cause.

The manifest requests landscape, yet this launch creates a portrait-shaped
surface. The current surface patch uses Vulkan's reported currentExtent and
identity presentation transform. The report does not include requested SDL
dimensions, surface transforms or orientation-change timing. We must check
those before deciding whether this is a transient rotation state, OS/window
behaviour or incorrect application sizing. Do not simply swap width/height
against the Vulkan surface contract.

### 3. One startup marker currently overstates success

`RT64Renderer::update_screen` emits `first-game-frame-presented` after its
first `application_->updateScreen()` call. Presentation is asynchronous: that
call does not prove a swapchain image was presented. The separate
`totalPresentations`-based performance counter is the more useful evidence
here, and remains zero.

Future diagnostics should call the former event `first-vi-update-completed`
and report an actual first presentation only after the successful-present
counter advances. This is a logging correction, not a rendering fix.

## Controlled delay reproduction

Used the already packaged Windows Vulkan executable, fresh isolated profiles
and normal ROM files. No injected gameplay-code edits. A diagnostic script
started its own process, located only that process's named Gfx Thread, paused
it for a bounded interval and resumed it in a finally block. It did not touch
any user game instance. The test runtime stopped itself after 30 seconds.
Debugger-style suspension can pause a thread while it holds a lock; this is
an intentionally adversarial delay test, not a measurement of real GPU cost.

- v1.0, 500 ms delay at roughly three seconds: two late SP/DP warnings each;
  recovered to about 60 presentations/s and completed 865 graphics tasks.
- v1.1, 2,000 ms delay at roughly 1.5 seconds: reached the eight-message logging
  cap for both stale completions. After the graphics thread resumed, all
  subsequent reports remained at zero simulation, graphics and presentations,
  while VI stayed around 60 Hz. Only 26 graphics tasks completed in the whole
  run. Exit remained functional and shutdown was clean.
- v1.0, the same 2,000 ms delay at roughly 1.5 seconds: also reached the
  warning cap but recovered to about 60 presentations/s, completing 826
  graphics tasks before clean shutdown. The retained failure is therefore
  revision/timing-sensitive; warnings alone are not a freeze diagnosis.

The ordinary v1.1 smoke run immediately beforehand completed 1,776 graphics
tasks in 60 seconds with healthy presentation. Therefore a temporary host
graphics delay can turn into a persistent guest stall; this is not an
intrinsically unbootable ROM or a thread left suspended by the harness.
It does not establish that every late completion freezes the game, nor that
the OPPO's first delay was caused by resolution rather than shader/driver work.

Evidence under `G:/DiddykongWorkFolder/further-optimisation-20260925`:
`Test-OwnedGraphicsDelay.ps1`, `delayed-gfx-500ms.log`,
`delayed-gfx-v80-2000ms.log`, `delayed-gfx-v77-2000ms.log`, their isolated
runtime logs, and `v80-final.log`. All owned test processes have exited.

## Precise next steps

1. When available, have this tester compare the latest APK in landscape using Balanced
   (2x/60 FPS/no MSAA/standard precision/AF4), pressing Apply before Play.
   Keep saves/imports untouched. Export a fresh ZIP whether it succeeds or
   fails. A successful lower-budget launch narrows the trigger, but does not
   prove scheduler correctness. The user confirmed the tester cannot do this
   at present; physical confirmation remains pending, not a prerequisite for
   the local reproduction above.
2. Add bounded Android startup tracing for task submission, SP acknowledgement,
   graphics parse/fullSync completion, retail watchdog retirement, and actual
   presentation. Include elapsed time and task identity. Log surface requested
   and selected dimensions/transforms, plus APK versionCode/native build ID.
   Avoid per-frame logging once startup is healthy.
3. Extend the controlled reproduction with task-boundary fault injection.
   Exercise delayed SP, delayed DP, a late completion after retirement,
   and more than one queued task. Check both ROM revisions. Do not infer the
   fix from the null-task guard tests alone.
4. Use those task-identity traces and before/after tests to qualify a Patch
   Pipeline correction preventing the
   retail hardware watchdog from retiring a still-owned host task. Preserve
   task identity, completion order, acknowledgements, and normal retrace/audio
   processing. Add a separate bounded host-stall report with a usable exit/log
   path. Never manufacture successful DP completions, bypass GPU fences, or
   remove null-task guards. A permanent driver hang must remain distinguishable
   from slow but progressing work.
5. Recheck OPPO/Mali and Thor/Adreno physically, including cold/warm starts,
   landscape handoff, background/resume and repeated launch/exit. Then rerun
   desktop/ARM suites and launch tests before promoting a compatibility fix.

The exact first slow/blocking operation and the reason guest progress never
recovers remain open. The current four optimisation packages should not be
advertised as having fixed this OPPO report.
