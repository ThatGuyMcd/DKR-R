# Online latency candidate — 1.0.5-beta.12-online.1

## Scope and rollback

This is a team-test candidate, not a claim of latency-free Internet play.
Quick Join and friend invites use the same WebRTC gameplay transport; this
work targets that transport, not just the direct-UDP fallback.

The pre-change source tree (including existing uncommitted work), SHA-256
manifest, Git diffs and dependency revisions, plus previous Windows ZIP,
Linux AppImage and Android APK, are preserved at:

`G:/DiddykongWorkFolder/online-latency-implementation-20260927/baseline`

The approved investigation is retained at:

`G:/DiddykongWorkFolder/online-latency-audit-20260927/DKR-R-ONLINE-LATENCY-INVESTIGATION.md`

No direct dependency/submodule/generated recomp changes. No changes to saves,
port ownership, boss topology, scene barriers, countdown, authentication,
immutable frame commitments, graphics quality, mods, Track Lab or HUD layout.
Wire protocol remains 47. The candidate's distinct source version deliberately
prevents mixing it with the older Beta 12 package in one online session.

## Accepted implementation

1. **Fresh pacing view.** `DirectSession::pacing_view()` reads only the fields
   required by the authored scheduler. It does not copy a room, strings, join
   requests, scan all outgoing queues, or take five transport-buffer locks.
   Both gameplay and frontend pacing use it; the UI keeps its separate cached
   diagnostic snapshot.
2. **Receive notification.** Quick Join callbacks notify a shared, session-
   independent signal. They never capture a session pointer or acquire its
   lock. The worker observes generations; route-current and callback-lifetime
   guards remain intact. The four-ms active timer remains a fallback (also
   for UDP and the small condition-variable notification race). Offline
   sessions still sleep; no busy polling or priority boost. After four
   continuously replenished receive drains, a 250-microsecond fairness wait
   releases the mutex so sustained traffic cannot keep owning it indefinitely.
3. **Aggregate SCTP admission.** Channels share a congestion/SDK-send queue.
   Bulk traffic is admitted only below a 32-KiB aggregate SDK queue; urgent
   lanes may use up to 64 KiB. Per-lane bounds are also enforced. This is not
   a claim to measure every byte in the OS/network/receiver: SDK buffered
   amount stops counting once SCTP accepts a message. Required application
   history/acknowledgements still govern delivery.
4. **Bounded send work.** Byte, packet, per-route and two-ms service budgets
   prevent a burst from holding the session lock indefinitely. Fresh input
   gets a turn before repairs and state traffic. Blocked routes rotate; saved
   reliable packets are retained unchanged. No lifecycle/ledger packet is
   thrown away because of age or congestion.
5. **Measured healthy-route floor.** Automatic delay may use two rather than
   three frames on a sufficiently low-latency route. Online launches with
   fewer than four fresh probe replies retain at least three. Probe history
   older than 20 seconds is excluded. Launch descriptors remain immutable;
   manual delay is not silently overridden.
6. **Received-frame backlog.** Catch-up target debt no longer adds half the
   RTT to a cursor which already represents *received* commitments. Jitter
   cushion, hysteresis, single-authored-tick dispatch, bounded catch-up speed,
   and full-RTT host progress-acknowledgement allowance are preserved.
7. **State encoding outside the session mutex.** Periodic live publication
   copies bounded owned bytes and its baseline, releases the session lock
   for compression/checksum, then revalidates match/input epoch/scene/lifecycle
   and delta baseline before publishing. A retired publication is discarded,
   never applied to a new scene. No unbounded job queue or native-state access
   from a worker. Required keyframes, five-Hz corrections, actor/RNG/water
   capture and lossless codec semantics remain unchanged.
8. **Avoid repeat capture.** An already-published authored frame returns before
   recapturing the same state if the outer dispatcher revisits it.
9. **Honest diagnostics and labels.** Host settings now call the existing mode
   “Host prediction (legacy Rollback)” and the window a prediction allowance.
   Stored enum values and range are unchanged. The detailed overlay shows
   bounded local sample-to-echo/consumption percentiles and enqueue timing,
   plus predicted, mismatched and superseded sample counts. These are local
   timestamps, not a subtraction of unsynchronised peer clocks. The enqueue
   metric ends at application enqueue, NOT remote receipt or physical scanout.
   Existing watchdog/failure log exports include these percentiles and seven
   application-lane queue summaries without player addresses or identities.
   Fixed histograms resolve values within four ms below one second and retain
   the exact maximum for the overflow bucket. They reset with input history.
10. **Opt-in cost profiling.** Existing performance exports can separate online
    capture, apply, encode, decode and pacing-view wall time. Regions may
    overlap; they must not be added together and presented as CPU utilisation.

## Gate results and deliberately unshipped experiments

### Simpler automatic-delay equation: rejected for now

The single-percentile-plus-one-tick candidate was tested before acceptance.
On the deterministic four-player, asymmetric, bandwidth-constrained route,
it reduced the allowance from nine to seven frames but increased missed
short edges (the two nearer clients went from zero to four) and reduced
authored progress (host 275 to 241 ticks in the same 15-second test). This
does not meet the stability/input-quality gate. The existing conservative
high-jitter/tail allowance is retained. Freshness and the proven healthy-route
floor are independent accepted changes; there is no claim that compound
high-jitter margins have been solved or that the current probe-loss metric
has become a true gameplay-loss measurement.

### Presentation prediction: isolated prototype only

`local_presentation_prototype.hpp` contains a disabled-by-default, read-only
mathematical sidecar prototype. Tests cover a capped 50-ms horizon, bounded
translation, owner/scene/vehicle/camera identity and discontinuity reset.
It has **no renderer adapter** and cannot change a shipped frame. Its
`kRendererQualified` flag is false. Retail velocity units, camera-relative
matrices, projected shadows, rescue/contact cues and all vehicle/split-screen
owners still need visual validation. Enabling it without that evidence would
risk recreating the floating-shadow/scenery and camera problems already fixed.
No in-game benefit is claimed for this prototype.

### Genuine gameplay rollback: feasibility gate fails with current contract

The retired block in `runtime_netplay.cpp` remains `#if 0`. The evidence is
structural, not just missing confidence:

| State owner | Existing support | Missing proof/contract |
|---|---|---|
| Portable racers/actors/RNG/water | `authoritative_state.cpp`, validated sparse correction | Not a snapshot of every emulated byte or native mutable owner |
| Rollback envelope | `rollback_simulation_state.cpp` wraps portable state | Restore delegates to sparse authority apply; cannot rewind native execution |
| RDRAM/guest registers | `runtime_state.cpp/.hpp`: 16 MiB and up to 32 register contexts | No native call stacks, instruction continuation or scheduler quiescence |
| Scheduler/event queues | Outer authored dispatch with patched VI catch-up | No restore/replay-safe scheduler boundary demonstrated |
| Renderer/object/native allocation | Separate native ownership and asynchronous graphics | No reversible lifetime/ownership transaction |
| Audio/rumble/save/external effects | Pipeline patches 0019–0022 suppress replay effects | Suppression alone does not prove exactly-once effects or re-entry |
| Cross-architecture determinism | Portable codecs and per-revision tests | No long native replay equivalence across x64/ARM/Android |

Consequently this candidate does not add a separately negotiated rollback
mode or make local actions authoritative ahead of the host. The next required
project is a quiescent native simulation boundary and complete state/effect
inventory followed by save → N ticks → restore → replay equivalence and
worst-case weak-device cost testing. Re-enabling the excluded loop is not a
safe shortcut. Per-peer frame-estimation/deadline wire changes and aggressive
commit coalescing are likewise not enabled without their own proof.

## Test interpretation

The expanded production-DirectSession harness has equal normal 30-Hz clocks,
the real catch-up controller and host backpressure, independently shaped
routes, shared host bandwidth, reordered/lost input, a client hitch, an outage,
background queue load and 80-ms press/release pulses. It checks exact immutable
commit agreement, bounded queues and recovery, and counts missed pulses rather
than averaging only successful ones. It does not emulate a real game's CPU,
GPU or physics. Background bytes model pressure, not native state application;
codec/native state correctness has separate tests.

Healthy zero/20-ms routes in the simulation reduce the client p95 from roughly
168 to 134 ms with no missed short edges. This is an approximately one-authored-
frame improvement in that model, not a universal WAN or button-to-photon number.
The healthy zero-RTT gates are 104 ms for the host and 138 ms for clients;
each explicitly rejects an added 33-ms frame.

A four-player shared 1-Mbit/s upload stress case still shows slow authored
progress and missed taps. It is a resilience case with a separately stated
1.5-second response ceiling, not evidence of smooth four-player play on an
overloaded route. Required-input traffic alone and host uplink demand matter;
this work cannot eliminate geographical latency or insufficient bandwidth.

The real loopback WebRTC suite additionally sends sustained input alongside
checkpoint/replica load on a constrained SCTP buffer, with p95 <150 ms and
max <500 ms gates, and tests receive notification, reconnection and teardown.
Those bounds tolerate OS scheduling; measured values are retained in logs.

Final build/test/package results belong in the delivery `VALIDATION.md`.
Unperformed hardware, WAN and gameplay visual tests must remain marked pending.

## Team acceptance sequence

Use this candidate on **every peer**, retaining previous packages and existing
separate saves. Do not clear profiles. Start with two players and automatic
delay; capture the detailed overlay on host and client. Compare the same route
and scene against the saved baseline. Then test the requested manual values,
and repeat with three/four players and a different host.

Exercise fresh/continued Adventure, regular races, minigames, bosses, results,
balloon then key/back-to-back cutscenes, hub return, track/character select,
retry, leave/rejoin and short Wi-Fi interruption. Check that waiting overlays
remain usable, assigned controls return after boss scenes, no single-player
save changes, and debt returns to its normal range without a new race.

Report the displayed version, mode/delay/allowance, player count, p95/max local
consume/echo, missing/predicted samples, frame debt, queue age and any wait
reason. Do not share friend codes, profile keys or raw saves. World-wide WAN,
physical Android/Deck/ARM performance and collision-sensitive visual prediction
are not validated by desktop unit tests.
