# Experimental rollback — development checkpoint, updated 1 October 2026

## Status: driver, input protocol and component replay tested; live gameplay NOT complete

This is not a playable rollback release and must not be described as one.
The independent driver now has immutable owner-input history, authenticated
encrypted transport, a scene-epoch coordinator, bounded owner-thread pacing
and a reversible EEPROM transaction component. Real retail object/water/shadow/
lighting and full CPU gameplay-mode components have been restored and replayed
against both ROM revisions on Windows and Linux. Actual retail audio CPU/RSP,
combined gameplay/audio/input/Pak/Magic replay, and confirmed scene resumption
have also passed on both operating systems. These
are closed-component tests, NOT the complete native DKR simulation/render/audio/
scene integration. The experiment is still not wired
to a live lobby. No improvement in real-game latency is claimed.

The host settings contain a distinct **Experimental rollback (in development)**
choice. It deliberately refuses to create a lobby until the missing adapter is
implemented. A placeholder selection is NOT completion of the approved request.
Do not remove that refusal merely because the driver tests pass.

The user's approval is to implement the experiment while retaining the current
online implementation, not to reinterpret current host prediction as rollback.

## Preserved working baseline and switch-back

The source, uncommitted work, SHA-256 manifest, Git diffs and working Windows ZIP
and Linux AppImage were preserved before this work at:

`G:/DiddykongWorkFolder/experimental-rollback-20260927/baseline`

Existing synchronization enum values remain exactly `Rollback = 0` and
`Lockstep = 1`. Their original simulation/commit/pacing algorithms are unchanged
in this pass. `ExperimentalRollback = 2` is a separate selection. The original
setting `online_synchronization` remains 0/1; the new opt-in is stored separately
as `online_experimental_rollback`. Selecting the experiment does not overwrite
the previous stable choice. **Use previous online mode** clears only the opt-in.
An older build that ignores the new key still reads the previous stable choice.

The host creation guard runs before online-save preparation and before retiring
an existing lobby. Separate guards reject a received experimental start and
prevent an experimental descriptor from falling into the stable game driver.
These are development safeguards, not a negotiated working experimental session.
Switching algorithms during a running race is not supported: return to the
lobby and start a new session with the desired mode.

Neither the root VERSION nor the existing team-test delivery was replaced.
No direct edits were made to dependency/submodule or generated recomp sources.
No changes in this pass to the HUD, Track Lab, mods, Android graphics, saves,
boss ownership, race/cutscene barriers, or the countdown algorithm.

## Implemented and tested

### Independent rollback driver

`runtime-recomp/src/game/netplay/experimental_rollback.*` owns a separate input
history and checkpoint ring. It does not reinterpret DirectSession commitments.

- Owner-final inputs are immutable. Duplicate identical input is harmless;
  conflicting final input is an error.
- Predictions repeat the most recent chronological input, not a future packet
  which happened to arrive first. Predictions never become confirmed inputs.
- Late different input restores the checkpoint before the first changed tick
  and recomputes all dependent predictions.
- Each call advances or replays at most one tick, allowing the eventual runtime
  caller to budget work and continue servicing UI and network events.
- Prediction stops at the configured window, rather than silently discarding
  the input which would require correction. Accepted window: 2–20 ticks.
- Effects are journalled per frame and released only after all owner inputs for
  that frame are known and the corrected frame has actually been simulated.
- A scene boundary is a reversible intent until confirmed. The driver does not
  advance speculative simulation past it; correction can remove or move it.
- Epochs, future input admission, effect bytes, history and checkpoint memory
  accounting are bounded. Memory accounting covers vector capacities but is not
  an exact measurement of allocator overhead or process RSS.
- Failures stop the experiment. They do not continue stable gameplay from a
  speculative or partially restored world.

The `SimulationContract` fields are adapter requirements, not proof that a game
implementation satisfies them. All state, quiescence and effect requirements
still need a real implementation and runtime qualification.

### Replay-equivalence checker

`replay_qualification.*` saves an isolated simulation, advances a scripted input
sequence, then restores every checkpoint and replays every suffix. It compares
checkpoint bytes, effect journals and scene boundaries. It never commits effects.
A deliberately omitted native state counter fails this check in the tests.

This checker is for an isolated test instance, not a live user's match: an
incomplete adapter could leak state beyond its checkpoint and cannot promise to
repair that instance following a failed probe.

### Immutable input, scenes, transport and frame service

`experimental_owner_inputs.*` implements a separate canonical DKRX v1 protocol
(at most 68 bytes). The live lane sends newest immutable owner input; a separate
repair lane sends the oldest unacknowledged gap. ACKs describe contiguous ACTUAL
input, never predictions. A client authenticates its own input to the host; the
host relays each owner's unchanged input to the other clients. History/future
admission is bounded. The current stable commitment ledger is not reused.

`experimental_timeline.*` assigns physical input once to its non-rewinding
frontier, plus the agreed input delay. It does not sample during correction or
re-date input after a late packet. Host and clients use the same rule.

`experimental_epoch.*` and `experimental_session.*` negotiate each confirmed
boundary, the locally agreed next scene and its canonical prepared baseline.
They use Prepare/Prepared/Release/ACK messages, retain one retired release ACK
transaction, reject conflicting scenes and never apply future-epoch inputs to
an old world. Preparation is one bounded increment per call. This coordinator
has passed multi-scene model tests and now drives two consecutive actual retail
scene unload/load transactions through the exact saved CPU continuation in
private tests. Those fixtures exercise both unload sites; they do not establish
coverage of every boss, menu or back-to-back cinematic sequence.

`experimental_network.*` borrows the owner session and transport; receive
callbacks never run the world. Each call receives/sends at most 32 datagrams,
and pending lanes are bounded to 40. Control, reliable repair and disposable
live inputs use separate traffic classes. Unchanged control/repair/live packets
are paced to 100/50/33 ms; failed sends have a 5 ms retry floor. Exact encrypted
repair/control bytes are retained on backpressure. An unsent LIVE packet can
be superseded by fresher immutable input under a NEW nonce; the older input
still exists in repair history. A blocked route cannot monopolize the service.

Keys are domain-separated from the stable protocol using BLAKE2b and a fresh
random launch incarnation; sequence numbers never reset per scene. A packet
sealed under the stable admitted key cannot enter this experimental lane. The
live authenticated provisioning/negotiation of that incarnation and adapter
schema is still to be integrated. Private tests provision their own keys.

`experimental_pump.*` returns without sleeps or a drain-until-caught-up loop.
Transport and confirmation remain serviceable between authored ticks. It
samples at 30 Hz, executes at most four ticks per pulse and at most one NEW
tick, and stops correction after its 2 ms inter-tick budget. A single guest
tick cannot safely be preempted: the eventual native adapter must separately
meet a measured tick budget. Frame pacing is distinguished from owner-input,
confirmation, scene-peer and preparation waits. Transport/adapter failure
halts future ticking; it does not convert speculative state into stable play.

### Reversible save ownership

`experimental_eeprom.*` owns separate working and confirmed 512-byte EEPROM
images. Reads see reversible writes; frame journals encode exact write bytes.
Capture/restore includes epoch and speculative frame cursor, cannot cross the
confirmed cursor, and can undo a partially failed open tick. Confirmation
validates an entire journal before changing confirmed bytes, rejects duplicate
or out-of-order delivery, and cannot discard an unconfirmed tail on epoch change.
Each frame is limited to 128 operations and 64 KiB of journal data.

This component has NO file access or save worker. Its four-owner delayed-input model tests compare save
reads, working state and confirmed images against a reference for 150 frames,
including exhaustive 12-tick restore/replay. A private owned import bridge now
executes the actual retail Adventure save/read routines for 150 frames, writes
all three slots and independently checks their retail checksums. All 78 suffix
replays and delayed-input correction match exactly on both revisions on Windows
and Linux. This bridge handles actual MIPS byte lanes and rejects invalid ranges
before writes; it cannot restore behind confirmation. It is NOT a live save
worker or complete live scene transaction. Separate reversible four-port Pak
images, owned input edges and launch/single-use Magic Code state are now included
in private component checkpoints and confirmation journals. Confirmed asynchronous
file persistence and full adapter wiring remain required.

### Real guest-component replay evidence

The Patch Pipeline tool `scripts/generate_replay_probe.py` reads protected
generated inputs and emits NEW private copies in a separate directory. Guest
memory accesses, operation/depth budgets and native import ABIs are checked.
Unknown operations trap; they are not replaced by fake scheduler success.
Standalone probe targets link no launcher, native audio or rendering workers.

Audited private services preserve exact no-mod/default-unity branches, guest
RNG delegation, object lifecycle journals and owned synchronous retail ROM
asset reads with their owned DMA queue (plus v80's uncontended asset mutex).
Foreign queues, blocked/full queues, invalid DMA/ranges and unknown imports
remain fences. Immutable canonical retail ROM fingerprints are checked.

The closed tick executes actual guest input edges, `obj_update`, deferred
particle deletion, AI-node maintenance, active waves, contact-shadow meshes,
lighting/fog, particle/track texture phases, shadow buffer flip, debug drain
and frame-end allocator aging. Calling object update alone was insufficient:
omitting the particle drain overflowed its deferred deletion allocation;
omitting the debug drain overflowed its debug-text buffer on longer runs.
Both were corrected by calling their actual retail phases, not disabling them.
The additional mode probe executes actual `mode_game`, including CPU camera,
HUD, geometry and weather draw, sound-queue updates, dialogue/transition draw,
viewport copying and frame-end cutscene-camera reset. The authored CPU probe
preserves the reviewed retail main-loop CPU span and its exact post-mode timer
reset/framebuffer-copy tail. Display-list, matrix,
vertex and triangle cursors are reset in their retail order. Its logical NTSC
clock advances exactly once per authored frame and is checkpointed, not read
from a native wall-clock thread. Actual loads now have a separate confirmed
transaction owner; native rendering workers are still excluded from CPU replay
qualification. Two/three/four input streams on a single-player active-wave
fixture are NOT separately loaded multiplayer water-scene qualification.
Additional tests on actually loaded four-player scenes passed on v77 Windows
and v80 Linux; varying owner counts in those tests does not change the loaded
four-player scene into a separately initialized two/three-player scene.

The private Patch Pipeline also exposes the two audited `mode_game` unload
sites as reversible scene intents. Six-tick exhaustive suffix replay, a late
input which removes the predicted unload, and confirmed resumption of the
exact guest stack/continuation all pass on both revisions and OSes. The confirmed
transaction performs the actual retail teardown and constructor only after
immutable render consumers drain. It resumes the saved `mode_game` and main CPU
tail, validates object-lifetime/save/Pak/Magic journals, and advances a fresh
epoch with preserved fractional audio cadence. Unknown imports and foreign
queues remain fences; no native scheduler completion is fabricated.

The latest combined Session/Network/Pump check drives epochs 91 -> 92 -> 93,
with two real reloads and twelve actual ticks in the final scene, under the same
encrypted impaired UDP conditions. Both first-unload sites pass for 2/3/4 owners,
v77/v80 and Windows/Linux. All peers match full canonical state, construction
journals and exactly-once confirmed effects. Each renderer-consumer fixture is
deliberately held across eight preparation pulses; the old world and lease remain
unchanged while preparation waits. Twelve Windows/Linux reference pairs are
byte-identical (SHA-256). These are actually loaded four-player fixtures with
varying input-owner counts, not independently loaded 2P and 3P scenes. Extra
old-scene physical samples may be discarded after a corrected boundary, but no
epoch/frame assignment is sampled twice or retimed during replay.

The real-state experiment now uses encrypted LOOPBACK UDP with the paced
owner pump, simulated one-way delay of 100–148 ms, 20% loss, duplicated,
reordered and tampered sealed packets. At 120 ticks, every peer's complete
component checkpoint and confirmed effect journal matches the confirmed-input
reference for 2/3/4 peers, both revisions, both x86-64 operating systems.
Wait slices occur and recover; input sampling remains once per logical frame.
Six private Windows/Linux reference pairs have identical SHA-256 hashes.
This is not WAN, cross-ARM, actual cutscene or actual presentation qualification.

### Actual audio CPU/RSP ownership (private only)

The `--audio-services` Patch Pipeline mode retains 19 precisely addressed guest
audio callbacks, verified against both revision symbol files and emitted
instruction addresses. Unknown indirect targets trap. Read-only `aspMain.cpp`
and RSP helper inputs are copied into NEW private outputs; the protected files
are not edited. The copied DSP has checked DMA ranges and execution budgets.
The two canonical ROMs contain byte-identical audio microcode (SHA-256
`3e7baf98602f869e1f7294f68a474de3f2db13927403180ea39f5324ce3db5cc`).

The private owner executes real `__clearAudioDMA`, sound-player callbacks,
`alAudioFrame` and the audio RSP synchronously. It owns the exact 50-message
audio ROM-DMA queue and actual copied ROM data, not fake AI/SP completion. RSP
DMEM, full guest RAM, audio CPU registers, fractional sample cadence and native
audio-event guard counters are checkpointed. Unknown/blocked queues still trap.
The audit exposed the existing native event-queue and voice/bus guards; their
private equivalents preserve the reviewed healthy/recovery branches and own
their state separately. Malformed event pointers remain explicit fences.

On **both revisions on Windows and Linux**, 150 frames plus all 78 12-frame suffix replays
pass. Delayed input changes actual `SOUND_SELECT` synthesis; every corrected PCM
byte matches a confirmed-input reference, with audible output and exactly
110,240 samples at 22,050 Hz. Journals are delivered exactly once to a private
recording sink; duplicate delivery and restoration behind confirmation refuse.
There is no SDL output, live audio worker or production audio handover here.

Combined mode tests run gameplay and DSP against the **same RAM**, using two
checkpointed CPU contexts rather than unrelated component copies. For each
revision on both operating systems, 120-frame correction checks pass for 2/3/4 owner streams.
Encrypted paced loopback tests on actually loaded four-player fixtures also
pass for both revisions, with 100–148 ms simulated one-way delay, 20% packet
loss, duplication/reordering and tampered packets. Every peer's full checkpoint
and confirmed composite object/PCM journal matches the reference. This is NOT
real multi-machine/WAN gameplay or a measured player-visible latency result.

The earlier WSL/disk interruption was recovered. The new audio/input/Pak/Magic,
authored CPU and real scene transaction paths have subsequently passed on Linux
as well as Windows. Private generated payloads, fixtures and comparison evidence
remain on G:. The baseline, saves and Docker data were not removed or reset.

### Presentation ownership, not yet live presentation qualification

`experimental_presentation.*` publishes completed immutable 16-MiB CPU images
and rejects malformed display-list/framebuffer descriptors before replacement.
Correction retires their non-rewinding generation before the first restored write.
A slow consumer can finish reading its old lease but cannot acquire a replacement
until it releases it. Epoch preparation waits for that lease to drain.

RT64 decoding may write scratch/readback data, so `DecodeWorkspace` gives exactly
one consumer a separate mutable image; immutable snapshots are never const-cast.
Its lifetime retains the lease through asynchronous consumers. Payload bounds are
48 MiB for mailbox replacement plus 16 MiB for the active writable decoder; this
does not include GPU resources, checkpoint history or process RSS. Scope, exception,
concurrent-claim, correction and scene-drain tests pass on both operating systems.

The new excluded-from-default-build `DKRExperimentalGpuProbe` target exercises
the actual staged RT64/F3DDKR bridge against private retail CPU output without
starting the launcher, guest scheduler, save workers or live lobby. Its result
must be recorded separately before claiming actual GPU qualification. A generation
check immediately before queueing is not sufficient to retire work which was
already queued; production present-queue retirement/Modern identity ownership
remain required. Do not enable the live experiment based on storage tests alone.

### Validation obtained (latest suite reruns recorded separately)

- Windows Release game executable: compiled.
- Linux x86-64 Release game executable: compiled.
- Latest complete main suites on 1 October: Windows **100/100**, 225.35 seconds;
  Linux **100/100**, 100.91 seconds. These precede the private audio extension.
- Earlier full suites: Windows **97/97**; Linux **99/99**, 124.05 seconds.
- The subsequent Windows 99-test run had one SDL3 client initialization timeout
  while concurrent builds were running. Three isolated reruns passed. That
  failed full-suite log is retained; the later clean 100-test runs above are
  reported separately rather than treating isolated reruns as 99/99.
- The current configuration contains **100 DKR tests**, including owned EEPROM.
- Paced network/Quick Join component tests passed on both OSes. Real Quick Join
  uses a PRIVATE local WebSocket rendezvous and actual ICE/DTLS/SCTP channels;
  its scene worlds are MODELS. No production discovery service is modified.
- New driver test: **108 deterministic synthetic scenarios**, 350 frames each,
  covering 2/3/4 owners, windows 2/6/20, delayed/reordered/duplicate input, and
  comparing final state and effects against confirmed-input reference runs.
- Timeline/session models cover 2/3/4 owners, delayed/lost/duplicate/reordered
  packets, differing input delay, asynchronous scene preparation, canonical
  baseline disagreement and chained model scenes. Encrypted transport models
  cover 18 impaired-network combinations and real UDP/Quick Join loopback.
- A 1000 Hz UI-clock test produces exactly 31 samples over 0–1000 ms, preserves
  correction without resampling, parks at its prediction window, bounds a CPU
  hitch and halts ticking following transport failure or backward clock input.
- Experimental checkpoint integrity uses existing read-only XXH3 sources,
  independently of stable FNV hashes. Scalar/SIMD equivalence covers 0–16 MiB
  and offsets 0–7. A local 16 MiB hash microbenchmark improved from roughly
  15–16 ms FNV to 0.88–0.98 ms XXH3. This is NOT a real-game FPS improvement.
- Additional cases cover transition correction, missing state contract, capture,
  restore/tick/commit failure, excess effects, stale epochs, conflicting inputs,
  mode persistence and distinct descriptor hashes.
- DirectSession tests verify refusing the experiment leaves an existing stable
  lobby's invite, match, port, rules and save readiness unchanged. Existing
  consecutive cinematic/hub/boss/race tests still pass.

Initial Windows validation exposed an MSVC parser nesting limit and a test
fixture stack overflow (`0xC00000FD`). The opt-in parser was moved outside the
long legacy else-if chain; new large test temporaries were isolated in no-inline
helper functions. Neither fix changes the production scheduler. The full suites
were rerun after these corrections; the earlier failed logs are retained.

Logs and internal packaging checks are retained under:
`G:/DiddykongWorkFolder/experimental-rollback-20260927/`.
Internal Windows/AppImage artifacts are **not** a new gameplay rollback release.
The test-only DirectSession transition harness now yields to its worker while
asserting its existing bounded committed-handoff behavior. Its previous tight,
unpaced loop could fill a 6144-commit queue before the worker ran. This is a
harness change, not a relaxation of production timeouts or a stable algorithm.
No WAN gameplay, cross-architecture native replay or visual-prediction pass has
been completed for the new driver. Private RAM/reference files contain retail
assets and must never be distributed with Windows ZIPs or AppImages.

## Concrete reason the DKR adapter is still missing

1. `runtime_netplay.cpp::advance_host_authoritative_frame` invokes the entire
   retail `main_game_loop`, not a pure physics function. The decomp source
   `extern/dkr-decomp/src/thread3_main.c` submits graphics at line 264, updates
   the audio queue at 317, waits for graphics at 336 and updates/waits for the
   framebuffer at 357. Replaying it is not just recalculating racer positions.
2. `rollback_simulation_state.cpp` delegates to portable authority capture/apply.
   This is a sparse correction contract, not a complete allocator/native state
   rewind. The retired `#if 0` Gekko driver is not a safe implementation to enable.
3. `RuntimeState` copies 16 MiB of guest RAM and guest register contexts. It does
   not capture the native execution continuation, thread synchronization objects
   or native message queues. `ultramodern/src/threads.cpp` creates actual native
   threads; `mesgqueue.cpp` owns native deques and condition variables.
4. Host graphics-task leases, queued SP/DP completion and presentation identity
   state also live outside the guest snapshot. Restoring guest bytes alone while
   these owners remain in the future recreates the lifetime/deadlock risk.
5. The canonical Accurate/4:3 CPU mode now passes, including four-player
   gradient-sky and reviewed void-basis policies. Launch/single-use Magic Code
   state now has private reversible ownership. The full original main-loop entry
   still includes unowned scheduler/thread participants; the qualified private
   authored span is not a replacement for that live entry. Presentation
   hooks have mixed behavior: some only record native markers, while HUD,
   projection, sky and enhancement hooks also write guest state. Blanket no-op
   whitelisting would hide missing ownership and is not an acceptable adapter.

The new read-only tool `scripts/audit_replay_boundary.py` audits generated code
without editing it. On the current generated payloads it reports:

| Root / revision | Directly reachable guest functions | Native boundaries | Indirect-dispatch functions |
|---|---:|---:|---:|
| main_game_loop / v77 | 1,416 | 181 | 1 |
| main_game_loop / v80 | 1,418 | 183 | 1 |
| obj_update / v77 | 780 | 46 | 0 |

These are potential call paths, not execution counts or a performance profile.
Only direct calls with `rdram` as the first argument are traversed. Indirect
targets and native helpers without that argument need separate inspection.

Taking only `obj_update` is not a sufficient shortcut. The audit records paths
from it to:

- `race_check_finish -> postrace_start -> dkr_netplay_postrace_barrier`;
- `race_check_finish -> race_finish_adventure -> dkr_netplay_adventure_finish_barrier`;
- `mode_init_taj_race -> spawn_object -> load_object_header -> asset_load -> dmacopy -> osRecvMesg_recomp`;
- `audspat_update_all -> sndp_set_param -> alEvtqPostEvent -> osSetIntMask_recomp`;
- native object-free/presentation hooks and finish-camera hooks.

Full generated caller paths and line numbers are in `replay-boundary-v77.json`,
`replay-boundary-v80.json` and `object-boundary-v77.json` in the work directory.

## Remaining implementation, in dependency order

1. **Replay-safe DKR tick and state ownership.** Use the Patch Pipeline to define
   the experimental-only simulation entry/boundary for both revisions. Inventory
   all written globals/heap blocks and native participants; include allocator
   metadata, actors, collision, RNG, input edges, water and simulation-relevant
   camera state. Separate or quiesce other owners before capture/restore. Do not
   assume a guest-memory copy captures native stacks or queue continuations.
2. **Transactional external effects.** Turn speculative audio, rumble, saves and
   scene loads into validated intents. Effects need deterministic identifiers and
   confirmed delivery; suppression alone is not enough. Reversible object lifetime
   must not free a resource still leased by rendering. Preserve existing scheduler
   completion fixes; never substitute inline fake SP/DP completion. Scene changes
   must drain at a confirmed boundary, perform the existing verified transition
   transaction, then start a fresh experimental epoch.
3. **Qualify the actual game adapter offline.** At every rewind depth test
   save/advance/restore/replay on real car, hovercraft and plane scenes, projectile
   creation/destruction, collisions, water, finish, boss/hub and back-to-back
   cutscenes. Compare complete owned state and effect intents. Run both ROM
   revisions and architectures; profile checkpoint/replay costs on weaker devices.
4. **Live experimental admission/transport integration.** The separate protocol,
   coordinator and transport components now exist; connect them to authenticated
   Quick Join/friend lobbies ONLY with the qualified full native world. Current local-history
   refresh/host commitment rules must NOT be reused as owner-final rollback input.
   Freeze each local sample once assigned/published; retain and repair it until
   acknowledged. Distinguish actual input, predicted input, confirmed cursor and
   presentation cursor. Negotiate schema/build/window/epoch, reject mixed modes,
   and budget replay so the overlay and input/network service remain responsive.
5. **Presentation and confirmation.** Publish only complete corrected render
   states, manage interpolation identities and camera correction, and bound any
   visual smoothing. Do not smooth gameplay collision state. Local visual
   prediction is a separate renderer integration and remains unimplemented here.
6. **Real multiplayer qualification and team-test packages.** Test 2/3/4 peers,
   delay/jitter/loss/reordering, hitches and sustained play across transitions.
   Assert no input loss or duplicate effects; verify responsive overlay at the
   prediction limit. Verify fresh-lobby switching back to both original modes.
   Only then replace the development admission refusal and create distinctly
   versioned Windows and Linux AppImage team-test packages.

There is no remaining approval question: the user approved this work. The work
above remains engineering and validation to complete, not a reason to advertise
the current foundation as working gameplay rollback.
