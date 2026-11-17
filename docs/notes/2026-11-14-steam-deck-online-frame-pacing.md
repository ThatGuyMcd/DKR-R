# Steam Deck online frame-rate investigation

The report concerns public v1.0.15: a Steam Deck host showed severe frame-rate dips, but audio continued and there were no reported waits for inputs. The owner subsequently confirmed two racers and no spectators. Spectator streaming and twelve-member lobby load therefore cannot explain that particular session. The supplied logs establish the view, resolution and active mods below; power settings remain unconfirmed. This is separate from the earlier standalone launcher software-renderer repair.

## What the current code does

The party simulation runs at 30 Hz. The renderer can produce interpolated pictures at a higher rate. Those are different clocks and budgets; a 30 Hz simulation result does not prove 60 FPS presentation.

Late input can trigger correction without exhausting prediction history. In that case the input-wait counter can stay at zero. The driver restores a retained state, replays the affected ticks, and captures and hashes a checkpoint after each executed tick. The current ordinary vanilla state is about 17 MB. The direct snapshot pool already avoids a second full-state copy and paged-store assembly when the state fits its bounded memory budget.

`PartyRuntime` publishes a replacement picture only when correction has finished and no scene boundary is pending. This prevents exposing a half-corrected timeline. A correction taking several expensive ticks can therefore hold the last completed image while confirmed audio keeps playing. The renderer's immutable snapshot leases already let ordinary correction proceed without waiting for a GPU drain; that earlier repair must not be removed. Water asset residency also already distinguishes real asset-scene changes from rollback identity, so a correction is not by itself proof that water assets are being rebuilt.

Replay is serviced in bounded slices, at most four pulses or four milliseconds between ticks. A single guest tick is not preemptible, so a four-millisecond slice limit does not guarantee a four-millisecond maximum correction cost. Automatic input delay already receives executed-step and replay-cost reports. It raises protective input lead under sustained replay pressure and keeps each published input immutable. Increasing lead trades some response latency for less speculative work; increasing prediction history alone does not remove replay cost.

## Local evidence

The R27 full-lobby Windows stress run completed with four racers and eight spectators after host transfer. It recorded only one nonzero race input-wait interval, totaling 101.286 ms on one racer; all later reported intervals were zero for every racer. This establishes that the network-wait problem and visible frame pacing must be assessed separately. It does not establish a 60 FPS lock.

A separate four-process Windows run used the audited R27 runtime, fresh disposable profiles, the actual game, four racers, no spectators, the Modern Vulkan renderer, a requested 60 Hz presentation rate, Automatic input delay and a fourteen-frame prediction window. All four processes exited zero and entered the race. All four reported zero racing network-input wait. The host's last race performance sample recorded 143 corrections and 259 replayed ticks over roughly 1,849 simulation frames, with a largest correction of four frames. The other racers recorded 8-11 corrections.

One settled host interval recorded approximately 2.36 ms for state capture, 1.79 ms for hashing and 6.83 ms for the guest/audio tick, before the separate picture-publication and renderer work. The party pacing sample recorded an 11.38 ms mean executed-step cost and a 20.39 ms maximum. These are wall-time stage observations with four applications sharing one Windows PC, not Steam Deck measurements or independent additive GPU timings.

The existing timing reporter can run on both rendering and simulation threads, with separate report windows over shared counters. Their windows overlap. Summing their interval counts would double-count work, so the investigation uses individual intervals rather than claiming aggregate FPS or a CPU percentage from their sum. Presentation intervals are CPU observations and are not a GPU timestamp or physical scanout measurement.

An additional four-player impaired-delivery profiling attempt did not enter gameplay. Its disposable CLI automation requested Start before all two-way connection proofs were ready, then kept its local `started` flag set after the asynchronous start request was rejected. It never retried. The normal launcher permits another Start attempt. This attempt exited with code 5 and is excluded from frame-rate evidence; it does not demonstrate a gameplay slowdown or override the successful full-lobby handover runs. The automation needs retry-aware state handling before using that entry point for further startup impairment comparisons.

The mixed Windows/AppImage test is functional evidence only. Its Linux VM clock is skewed and its renderer explicitly uses lavapipe. Neither its FPS nor its software rasterizer says anything about the physical Deck's active driver.

## Two-racer follow-up

After the owner confirmed two racers and no spectators, two additional ordinary-startup runs used the same audited R27 Windows binary, one host and one client, no spectators, no mods, Modern Vulkan, My racer fullscreen, a requested 60 Hz refresh rate and a fourteen-frame prediction window. Both members entered a normal race and exited zero in each run, with no reported rollback failure. This is two applications on one Windows GPU, not a physical Deck test or a fresh measurement of the public v1.0.15 binary.

With normal delivery, both racers recorded zero network-input waiting. The host's final performance report recorded 161 corrections, 480 replayed ticks and a largest correction of four frames over roughly 2,417 race frames. The client recorded zero corrections and zero replayed ticks. Nevertheless, that client's last rendering report had long intervals: approximately 29.89 ms mean display-list processing wall time and 21.59 ms mean presented-frame interval, with a 100 ms histogram upper bound at the 99th percentile. Processing wall time can include waiting and backpressure; it is not a GPU-duration measurement. This is reason to inspect rendering and presentation independently of rollback, not proof of the Deck's bottleneck.

The impaired run applied 375 ms receive delay, 25 ms jitter, 200 ms spike windows and periodic realtime/replica loss to each member. Logged receive instrumentation verified that the delay was active. These are application-delivery impairments, not measured internet RTT. Host/client whole-race input-wait totals were 753.841 / 607.391 ms, so this run does not meet an absolute zero-wait criterion. The final reported pacing samples had zero input wait. The final host/client correction counts were 155 / 146, with 765 / 405 replayed ticks and largest corrections of 14 / 5 frames. Automatic local input lead ended at 7 / 9 frames. That is a material response-latency trade-off and does not justify increasing delay blindly.

The impaired run's last presentation intervals averaged about 17.9 / 17.7 ms, with 99th-percentile histogram upper bounds of 66 / 50 ms. These are not locked-60 results. The normal and impaired runs did not follow identical camera positions and trajectories, and share one PC's GPU, so their renderer means must not be presented as an improvement caused by adding latency.

An earlier two-player warm-up experiment temporarily paused one thread of its own disposable host process while connecting the client. It did not reach a race and recorded a heartbeat disconnect, so it is excluded. Its thread was resumed and both processes retired. The accepted runs above use ordinary process startup without thread suspension. Logs and per-process exit codes for all attempts are retained outside the repository.

## Captured Deck session, received 14 November

The owner supplied `dkrr steamdeck logs.zip`. The latest `runtime.log` contains launcher activity only. The last online session is in `runtime-previous.log`, with a ZIP modification time of 14 November at 00:35:58. It contains one two-racer host runtime and multiple scene epochs/races. Earlier history files include older sessions and are not substituted for this one. The archive and extracted raw logs are retained outside the repository.

The ELF build ID is `5c2394d9a4e89c26d560172e700b2682c6f50217`. It matches the retained public v1.0.15 Linux executable exactly by GNU build ID. Gamescope reports an old `1.0.14` AppImage filename, but the version banner and binary identity identify v1.0.15; the filename alone is not evidence of an old runtime.

The session used the actual AMD Custom GPU 0405 / RADV VANGOGH Vulkan driver. The standalone launcher also reported accelerated OpenGL with the AMD rasterizer. This was not the earlier launcher software-renderer problem. Gamescope and RT64 requested a 60 Hz presentation rate.

The internal render size was 1920x1200 and the output window 1600x1000, with scale 5 and downsample 1. Most racing pictures used My racer fullscreen; menus and shared scenes used the shared view. Mods were active, including three code packages. The serialized world was 18,119,892 bytes, and the log confirms the direct full-image checkpoint store was already active: `direct=1`, retaining 398,638,296 bytes within a 398,703,160-byte budget. Reimplementing that earlier optimization would not repair this session.

There are 393 pacing samples and 388 presentation timing samples. Every pacing sample records zero network-input waiting, zero presentation-drain waiting and zero measured long scheduling gap. That supports the owner's report of no input-wait pauses. It does not prove that the GPU was idle or that the device was never throttled; no GPU-duration, clock, power or temperature trace was supplied.

Host route RTT samples after owned startup have a median of 142 ms, a 95th percentile of 148 ms and a maximum of 477 ms. The nearby route sample during the worst rendering interval was 141 ms with empty realtime, authority, control and checkpoint queues. That interval does not look like an exhausted input window or a bulk download saturating the link.

One particularly bad five-second interval in epoch 107 contains:

| Measurement | Recorded value |
| --- | --- |
| Network-input waiting | 0 microseconds |
| Executed ticks, including replay | 253 |
| Replayed ticks | 104 |
| Time executing replayed ticks | 1,753.443 ms |
| Mean executed-step cost | 13.529 ms |
| Reported presentation rate | 29.77 FPS over 5.005 seconds |
| 95th/99th presentation interval bucket | Above 100 ms |
| Newly tagged pictures | 158 |
| Accepted queued pictures | 76 |
| Rejected queued pictures | 82 |

The last three numbers are differences between consecutive reports at raw log lines 16604 and 16671. In the renderer report, the field named `retired` is `present_gate::Statistics::rejected`. It counts queue-admission rejection, not ordinary successful resource reclamation. Rejections can include generation retirement, expired leases or slot-identity mismatch; the log does not distinguish all causes. The code's conservative generation invalidation during correction is a concrete candidate for avoidable rejection. It must not be bypassed wholesale.

The very large percentile number `18446744073709551615` is the histogram's overflow bucket. It means an interval exceeded 100 ms, not an actual multi-century pause. Across the session, 128 presentation reports have a 99th-percentile overflow bucket; nine also have a 95th-percentile overflow bucket. These are repeated visible hitches, not just a low overall average. Other samples, including the final shared scene, return to approximately 60 reported FPS.

The pacing samples record about 206.6 seconds of replay execution and 10,553 replayed ticks over the session. Five-second samples straddle scene boundaries, so those interval sums should not be equated exactly with each epoch's last cumulative counter. Detailed per-stage rollback profiling was not enabled. The evidence identifies expensive correction and interrupted presentation, but does not split the Deck's replay cost into guest execution, capture and hashing or establish the GPU's share of every slow frame.

## Reproduced Auto measurement problem

The source review found a specific timing error. `LiveDelay::pulse` retains a monotonic `clock_frontier_` but also updates `frame_` to the current execution cursor. That cursor moves backwards during correction. `LiveDelay::arrival` measures lateness against `frame_`, so fresh input arriving during replay can be classified as on time even though it is behind the furthest simulated frame.

A small external diagnostic linked the actual `DKROnlineStabilityCore` and reproduced the calculation without a ROM or networking:

1. Execute to frame 100 in one epoch.
2. Rewind execution to frame 90.
3. Receive twelve fresh latest-input samples for frame 95.
4. Read the ordinary delay report.

The report gives twelve samples, zero lateness and zero owner lateness at the 90th percentile. Against the retained live frontier, these samples are five frames late. This is a reproduced undercount in the current calculation. The device log does not expose the individual replay cursor and arrival pair, so it does not prove how many of its samples were affected.

The current policy also primarily responds to sustained work ratios and reduces protective lead after a short stable interval. The device trace repeatedly shows low/zero configured lead and frequent corrections without input waits. Correcting the lateness reference is a more precise first step than increasing everyone's delay or just increasing prediction history.

## Repair and validation plan

1. Measure fresh input lateness against the monotonic simulation frontier in the same epoch. Keep exact historical repairs excluded from fresh jitter measurements. Add regression coverage for input arriving during a rewind, future input, returning to frame zero in a new scene, duplicate/repair batches and per-owner isolation. Do not change packet layout, final input values or the shared simulation clock.
2. Review correction retirement separately from real scene/resource retirement. An immutable queued picture strictly before the first corrected frame can still describe an unaffected past frame. Explore retaining that eligibility while rejecting the corrected frame and all later affected pictures. Preserve full invalidation for resource/scene changes, exact queue-slot identities, GPU completion fences and bounded snapshot storage. Add delayed-consumer and repeated-rewind tests before changing eligibility.
3. Measure correction burst duration as well as sustained replay load before tuning Auto. Aim to prevent repeated catch-up bursts with the smallest necessary per-owner lead. Avoid learning false stability while the driver is rewound, and avoid oscillating between zero lead and repeated corrections. Do not merely redraw a frozen image more often to inflate FPS counters.
4. Verify the Gamescope/window resolution choice and the automatic render-size policy. The observed internal size has 44 percent more pixels than the output window. Offer a clear device/window-native automatic choice while preserving explicitly selected supersampling resolutions. Resolution is an additional load factor here, not a proven sole cause.
5. Requalify unchanged simulation/save results, correction boundaries, two-racer play, four racers plus eight spectators, late packets/loss and the renderer's actual presentation intervals. Build audited Windows and AppImage test packages. The physical Deck must then verify the improvement with the same mods and graphics settings; local desktop/WSL results cannot certify a 60 FPS lock on it.

The initial investigation reproduced the timing error without changing production behavior. R27 remains the earlier network/host-handover baseline. The approved repair is prepared separately as v1.0.16 Playtest 1.

## Approved repair

Fresh arrivals and delay reports now use the monotonic frontier, including while execution is rewound. Exact historical repairs and local input remain excluded from delivery jitter. The existing 96-byte delay protocol is unchanged. Regression checks cover rewind, future input, a new epoch, per-owner samples and repair exclusion.

Auto estimates forward tick cost from existing reports after subtracting measured replay work. Repeated expensive corrections can raise an owner's protective lead even below the old sustained-work threshold. Separate growth and reduction thresholds retain that protection while correction remains costly. Quiet or inexpensive replay does not by itself request extra delay. This estimates correction pressure; it does not measure GPU duration or promise a fixed rendering rate.

Ordinary independent CPU correction retains an immutable picture only if it precedes every subsequent affected frame. Invalidated pictures cannot become eligible again after another rewind. Scene/resource retirement still invalidates every picture. Exact queue identities, previously admitted GPU work, epoch retirement and the four-image memory limit remain enforced. The new lifetime records hold weak references, never more image RAM, and do not acquire a last strong reference under the domain lock.

The presentation implementation is integrated through the existing checked Patch Pipeline and authored `online_presentation` module. The checksum-pinned presentation base files and retained capsule remain unchanged. A staged production-library test passed 48 queue-picture checks, including a consumer delayed at actual admission, repeated corrections, slot-identity rejection and full retirement. Final packaged-game results are separate from this test.

Window-matched automatic resolution is labelled as recommended. Existing settings and explicit higher resolutions are preserved. Display policy checks cover the captured 1600x1000 output, its old 1920x1200 integer render size, and a native 1280x800 window.
