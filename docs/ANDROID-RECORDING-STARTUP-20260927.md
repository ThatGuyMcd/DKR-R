# Android recording-enabled startup crash — build 1050024

Date: 27 September 2026. Public version remains **1.0.5-beta.12**.

## Outcome and qualification boundary

The supplied `DKR-R-Android-startup (4).zip` contains two matching native crashes
from Android build **1050023**. They identify a null video-interface (VI) mode
pointer, not an out-of-memory report or an exception in the performance recorder.
Build **1050024** initializes that state before any worker can use it and corrects
the associated session-start and VI synchronization hazards through the Patch
Pipeline.

The candidate passes the desktop regression suites and has been packaged for
Android, Windows and Linux x64. **It has not been run on a physical Android
device here.** Neither the crash correction nor successful desktop testing proves
an Android FPS improvement, complete lifecycle stability, or desktop performance
parity. Recording-enabled launch still requires the user's device check.

## Evidence from this device

The archive identifies an Honor MBH-N49 (Magic V5), Android 16/API 36,
MagicOS 10.0.0.168, running app version 1.0.5-beta.12 / code 1050023.

The native library matches the retained symbols exactly:

- Packaged library SHA-256:
  `64a9f50324e0162e2758da5f72f5cce89f8dc9ee1880a8639008a05986c46472`.
- ELF build ID: `33c97d685ece591d380c0cf4967c18066d47c3a0`.

Both Android native-exit traces report:

| Evidence | Value |
| --- | --- |
| Signal | SIGSEGV / SEGV_MAPERR |
| Fault address | `0x1c` |
| Crashing thread | `VIThread` |
| Library-relative instruction | `0xb95ca0`, `vi_thread_func()+0x2cc` |
| ARM64 operation | `ldr w11, [x26, #0x1c]`, with `x26 = 0` |
| Symbolized source | `ViContext::update_vi()`, reading `common_regs->hStart` |

The recorded crash times are 02:43:21 and 02:40:16, both UTC+01:00 on
27 September. The process uptimes are 184 and 303 seconds respectively; those are
process lifetimes, not time spent successfully rendering gameplay.

Memory captured around the VI context shows **both VI mode pointers null** and
current state index zero. The total-VI counter is 27/28; it is time-derived and
must not be interpreted as 27/28 completed render iterations. The runtime log
shows renderer initialization taking approximately 425 ms before the failing
startup sequence.

The previous runtime log also contains:

- A session-2 marker and an already long-running process clock.
- Old DKR guest-thread identities alongside the newly created VI/SP threads.
- An old-task SP-delivery watchdog message before task-registry initialization.
- A successful two-pattern buffer-upload qualification, followed by game startup.
- A 60-FPS target, 1x resolution, no MSAA, stock textures and the Modern renderer.
- A portrait-sized 1060x2376 surface on this foldable device.

These observations support investigating initialization and process/session
reuse. They do **not** establish the precise Android Activity event that initiated
the reuse, nor prove that the portrait surface caused this crash. There is no
completed performance recording in this ZIP from which to calculate a reliable
Android bottleneck or FPS gain.

The traces are different from the earlier **1050022 colour-copy qualification
crash**. That previous correction remains intact. No Java exception, GPU-driver
crash stack or memory-kill diagnosis is established by these two traces.

## Source-level failure mechanism

### 1. Worker-visible VI state started with null modes

In the existing runtime events source, `reset_event_state()` zeroed both
`ViState` instances. The VI thread then relied on reaching
`if (!is_game_started())` to install a dummy mode during its first iteration.
`update_vi()` subsequently dereferenced the mode without a null check.

This is an invalid bootstrap dependency: a worker must receive usable initial
state even if the session status changes before its first iteration. The fault
instruction and captured null pointers directly match this unsafe dereference.

### 2. A stale Quit status could bypass dummy-mode setup

`is_game_started()` tests whether the status differs from `None`; that includes
`Quit`. `quit()` stores `Quit`, while the old `recomp::start()` reset `exited` but
did not reset the game status at entry. Teardown normally resets the status, but
the caller's SDL/lifecycle pump can still deliver a quit after that reset.

Consequently a later start can inherit `Quit`, skip the dummy-mode branch and
reach the null dereference. This is a **confirmed source-level route**, not a
claim that the exact value of `game_status` was captured in the tombstones: it
was not. The correction does not depend on proving that this was the only route.

### 3. VI publication and guest updates did not share full synchronization

Guest VI setters acquired `message_mutex`, but the VI thread's mode reads,
double-buffer swap and copy were outside that lock. The framebuffer getters were
also unlocked. This left a data race and the potential to lose or combine guest
updates. It is an independently identified hazard; the dumps alone do not prove
it triggered the reported incident.

The VI loop additionally checked shutdown before sleeping but not immediately
after waking, allowing one more iteration after a quit during sleep.

## Surgical corrections

All dependency changes are expressed as project-owned patches applied to isolated
build staging files. No RT64/N64ModernRuntime checkout, `RecompiledFuncs` or
`RecompiledPatches` file was directly edited for this correction.

| Project-owned file | Change |
| --- | --- |
| `patches/performance/runtime-vi-bootstrap.patch` | Initialize both VI buffers to a valid black dummy mode; initialize the published registers before workers start; synchronize VI state publication and getters with guest setters; recheck quit after sleep. |
| `patches/performance/runtime-session-bootstrap.patch` | Reset the current game and status under the existing game mutex at start, before clearing shutdown and creating workers. |
| `runtime-recomp/cmake/PerformancePatches.cmake` | Compile the new isolated staged sources, preserving the existing host-task-lifetime patch ordering; register the real-source regression executable. |
| `runtime-recomp/tests/vi_bootstrap_tests.cpp` | Exercise the actual staged VI implementation with controlled clock/event/renderer boundaries. |
| `runtime-recomp/tests/android_startup_contract_tests.py` | Check that the launch gate reset precedes workers and that CMake selects the patched sources. Retain the earlier colour-copy regression checks. |
| `packaging/android/app/build.gradle` | Advance Android code 1050023 to 1050024 without changing the public beta.12 version name. |

The VI lock is released before calling the VI callback and is not held across
sleeping or renderer execution. This avoids introducing a callback/getter lock
recursion. The initial framebuffer uses the existing dummy-mode address; it is
not a new framebuffer allocation.

No online protocol, input-delay setting, simulation rate, ROM gameplay patch,
HUD layout, resolution preference or rendering-quality preset was changed in
this pass. The existing DKR-R Android icon and preview signing identity remain.

## Verification performed

| Check | Result and scope |
| --- | --- |
| Android native build and APK packaging | Passed; native library rebuilt and APK signed. |
| Windows project CTest suite | **88/88 passed**, 181.12 seconds. |
| Linux x64 project CTest suite | **88/88 passed**, 67.60 seconds. |
| Actual-source VI test | 100 initialization cycles with native capture armed before reset; both modes and first present valid. These are VI bootstraps, not 100 full game launches. |
| Event cadence | 60 ticks retain 30 VI events at retrace divisor 2 and 60 AI events; the existing 1200-milli catch-up case retains 36 VI events. |
| Shutdown and callback safety | Quit during sleep produces no present; callback can call a framebuffer getter without deadlock. |
| Concurrent guest updates | 4,000 VI iterations against 10,000 setter/update passes complete; reset discards guest mode pointers. |
| Negative control | Same harness against the pre-correction staged source fails the initial-mode assertion as expected. |
| Linux ThreadSanitizer harness | Passed without a reported race. GCC warns that atomic fences in the queue dependency are not supported by TSAN, so this is not an exhaustive concurrency proof. |
| Python startup contracts | Four checks passed, including applying the real patches to disposable source copies. |
| Visible Windows smoke | Isolated Vulkan/Modern 60-FPS-target run completed 1,172 graphics tasks over 40 seconds and exited cleanly. Not an Android benchmark. |
| Package checks | Android signature, version and 16-KiB alignment verified; Windows/Linux package helper checks passed. |

Android native ELF build ID:
`bddb1cb674faf13d34131bf3dd4ab5499ff452e2`.
Native staging library SHA-256 (before Gradle's final stripping):
`346fc0a1b1292fb9fb71f2e788c81526153176fc6e52cc5b152ea2876cfb7700`.
The actual library inside the delivered APK has SHA-256
`37a397eb4c3d520efbf7b725f7d5775b0699030535e6369a121dbb8dc15d206d`.
The subsequent device reports match that packaged library. This distinction was
verified during the 1050025 investigation; it is not a different installed build.

## Delivery, rollback and next device check

New artifacts and checksums are in:
`G:/DiddykongWorkFolder/android-recording-crash-20260927/delivery/`.

The preceding 1050023 artifacts are preserved separately in:
`G:/DiddykongWorkFolder/android-startup-correction-20260927/delivery/`.
That is a rollback reference, not a claim that its recording-enabled launch is
safe. The exact previous unstripped Android library is also retained privately
for symbolization. Crash traces, ROMs, private test profiles and symbol files are
not included in the delivery packages.

Linux ARM has not been rebuilt in this pass; Docker recovery remains out of scope
at the user's request. Do not present an older ARM artifact as this correction.

Phone validation order:

1. Update the existing preview app in place to **1050024**; do not clear saves or
   app data. Check normal gameplay launch without recording first.
2. If normal launch succeeds, arm the recorder in the launcher and launch again.
   Check startup, gameplay and one completed capture/export.
3. If either path crashes, stop repeating it and export ordinary Startup Logs
   after reopening, without arming recording again.
4. Only after startup and capture are stable, collect a controlled slow-track
   sample to guide further Android performance work. Use the same track, camera,
   graphics settings and thermal conditions for before/after comparisons.

The Java sampling and full Android Activity lifecycle are not covered by the VI
harness. They remain explicit device-validation boundaries; no broad speculative
lifecycle, shader or scheduling rewrite was added to this targeted correction.
