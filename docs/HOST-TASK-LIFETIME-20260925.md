# Beta 12 — host task lifetime correction

This is the follow-up to `ANDROID-OPPO-STARTUP-20260925.md`. The user approved
implementation after the temporary graphics delay had reproduced a permanent
v1.1 startup stall in the previous package. This report distinguishes the
locally reproduced defect from unverified Android driver behaviour.

## Correction and boundaries

The retail scheduler considers eleven retraces without completion a hardware
failure. It clears active SP/DP task pointers and can send a skipped-swap
message. A native task can legitimately take longer than that original-console
budget. Retiring it early lets the guest recycle state while its host completion
is still outstanding. Null-completion guards prevent a crash, but cannot repair
the lost ownership. Those guards remain enabled.

The project now records an ownership token for each submitted host task:

1. Submission registers the canonical guest OSTask address and a monotonically
   increasing generation before it is queued. Graphics tasks own SP and DP;
   audio/other RSP tasks own SP. Catch-up tasks use the same lifetime.
2. The graphics and RSP queues carry their token. The existing snapshot,
   SP acknowledgement, parsing/fullSync and DP notification order is unchanged.
3. Entry to the valid retail completion handler consumes that phase of the
   token. This is deliberately before the handler can schedule another task
   at the same address. The real SP acknowledgement stays at its existing
   common exit. A stale generation cannot retire newer work.
4. Immediately before the normal retail retrace handler, reset a timeout
   counter at ten only if that engine still has a matching host-owned task.
   Unowned tasks retain retail handling. Retrace/audio/VI work is not skipped.
5. After fifteen seconds without a task-stage change, report the identity,
   pending engines and last stage. Do not synthesize completion, skip a fence,
   free live buffers, or retry the task concurrently. The report itself does
   not prevent subsequent recovery.

The registry is fixed-capacity, mutex-protected, and never holds its lock while
parsing, delivering a message or waiting. It is reset only at event-system
initialisation after the preceding workers have stopped. It is a native
sidecar, not new authored game state or an addition to network/save formats.
Duplicate live address reuse fails explicitly rather than hiding corruption.

### Patch ownership

| Owner | Change |
|---|---|
| `runtime-recomp/src/game/host_task_lifetime.hpp` | Bounded task ownership, stages, generation protection and stall diagnosis |
| `patches/performance/runtime-host-task-lifetime.patch` | Checked isolated copy of runtime `events.cpp`, after the existing snapshot-pool stage |
| `runtime-recomp/src/game/runtime_stubs.cpp` | Existing SP/DP guards consume ownership; new pre-retrace helper protects owned tasks |
| `dkr.us.v77.recomp-policy.json` | `__scMain` hook at `0x80079648`; s2 scheduler; counters `0x800DE754`/`0x800DE758` |
| `dkr.us.v80.recomp-policy.json` | Equivalent at `0x80079A98`; counters `0x800DECD4`/`0x800DECD8` |
| `scripts/host_task_policy.py` | Checks actual ELF call, delay-slot register, counter loads and eleven-retrace comparisons before generation |
| `runtime-recomp/src/game/game_main.cpp` | Low-frequency bounded native stall reporting, independent of renderer completion |

Both fully composed policies were compared with the previous build: the only
new hook is the host-task guard; all prior hook text, manual functions,
instruction patches, ignored/renamed functions and stubs are retained. Track
Lab, custom tracks/characters and HUD work are not removed. Generated C and
dependency checkouts were not manually edited.

No networking, input, physics, game-speed, save routing, HUD, resolution,
framerate defaults, surface transforms or quality settings were changed in
this correction. In particular, the OPPO's portrait-shaped Vulkan surface is
not blindly transposed, and a user's saved graphics settings are not reset.

## Android diagnostics and recovery UI

- A native Android dialog offers **Keep waiting**, **Export logs**, and
  **Close app…** when a task stops progressing. Close requires confirmation
  and warns about unsaved progress; there is no automatic process termination.
  It uses Android UI rather than depending on a functioning game renderer.
- The old first-update marker is now `first-vi-update-completed`.
  `first-successful-presentation` is emitted only after the renderer's actual
  completed-presentation counter advances. Neither claims visual correctness.
- Surface logs include requested/current/selected extents and rotation flags.
- Exports now include APK versionCode and the installed native library's
  SHA-256. The hash is computed on the opt-in export worker, not at startup.
  This is exact binary identification, not an ELF build-ID claim.
- APK code is **1050020**, version **1.0.5-beta.12**, same preview application
  ID and signing certificate. The previous APK is retained.

## Tests completed

All delayed runs used owned Windows Vulkan processes, fresh isolated profiles,
60 FPS and both original ROM revisions. No user save or running instance was
modified. These test native scheduling, not the physical OPPO GPU.

| Fault | Result |
|---|---|
| v1.0: delay SP notification 2 seconds | Recovered ~60 FPS, 726 graphics tasks, clean exit |
| v1.0: delay DP notification 2 seconds | Recovered ~60 FPS, 728 tasks on qualification rerun, clean exit |
| v1.1: delay SP notification 2 seconds | Recovered ~60 FPS, 727 tasks, clean exit |
| v1.1: delay DP notification 2 seconds | Recovered ~60 FPS, 727 tasks, clean exit |
| v1.1: delay SP notification 16 seconds | Report at 15 seconds; recovered ~60 FPS, 728 tasks, clean exit |

There were no stale SP/DP completion warnings in these corrected runs. SP
delay exercised both retirement guards. During DP-only delay, audio tasks can
keep running; retail `__scExec` resets **both** counters on every new SP task,
so that test must not falsely claim to have exercised timeout retirement.
It instead verifies retained DP ownership and eventual recovery with concurrent
audio work.

The registry test covers address aliases, invalid addresses, duplicate live
reuse, independent SP/DP retirement, stale generations, session reset, one-shot
stall reporting without fake completion, and four concurrent workers with
8,000 combined task lifetimes. Both retail ELF signatures passed validation.
Four Python policy tests additionally reject changed instructions, incorrect
registers/counters and duplicate ownership. The Android log-export snapshot
test passed with missing/empty/small/oversized/error-flood logs and confirmed
the source log is not modified.

- Windows x64: **82/82 tests passed**; packaged importer, Pak and input-switch
  checks also passed.
- Linux x64: **82/82 tests passed**.
- Linux ARM64: **82/82 tests passed under QEMU**; not physical GPU testing.
- Both AppImages packaged and passed their packaged Pak/input-switch checks.
- Android native build and APK assembly passed; signature and 16-KiB ZIP
  alignment verified. Signing certificate SHA-256 remains
  `b4e82ec59de3b8cdbe2ad1c7e84e0eba271cde79da64aea1811c81aa3b82e262`.
- Full Android lint is **not clean**: 48 errors in the existing SDL2 Java
  Bluetooth/audio permission paths. No lint errors were reported in the
  changed DKR Android classes. These were not suppressed or rewritten in a
  scheduler-focused patch.

The private delay injection is compiled only with `DKR_TASK_QUALIFICATION`.
All three packaging scripts reject builds configured with that switch enabled.
The Windows production rebuild completed with fault injection disabled. An
exact repeat of the original graphics-thread suspension (2,000 ms after
approximately 1.5 seconds) recovered v1.1 to ~60 FPS and **816 tasks** in
30 seconds, with no stale completion warnings and clean exit. The prior
package reproduced a permanent stall and only **26 tasks**. The production
executable contains neither the fault-injection marker nor its environment
variable names. Ordinary 60-second production smoke runs also completed:
v1.0 processed **1,777 tasks** and v1.1 processed **1,775 tasks**, both at
approximately 60 presented FPS with clean shutdown and no stale SP/DP warnings.
These smoke checks do not replace sustained gameplay or device-driver testing.

All four production packages were created and their SHA-256 hashes verified
against `delivery/SHA256SUMS.txt`. Final local verification completed on
26 September 2026. No owned DKR-R test process remained running at handoff.

## Rollback and outstanding hardware gate

Previous complete builds remain unchanged under
`G:/DiddykongWorkFolder/further-optimisation-20260925/delivery`.
New outputs and validation evidence are under
`G:/DiddykongWorkFolder/task-lifetime-20260925`.

The exact OPPO first slow/blocking driver operation is still unproven. AYN Thor
feedback also remains outstanding. Test the new APK on both: cold/warm starts,
landscape handoff, repeated launch/exit, background/resume and sustained races.
Export a new Startup Logs ZIP for failures; the new code/hash fields identify
which binary was tested. Keep the conservative Balanced preset comparison as
a separate workload test when the tester is available.

This fixes and tests the reproduced premature-retirement path. It is **not** a
claim that every Android driver now works, that 60 FPS is achievable on every
device, or that CPU temperatures have been measured lower.
