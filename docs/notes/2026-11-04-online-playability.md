# Online playability investigation

This work is a separate `1.0.15-beta.1-playtest.1` candidate above main
`fbae505f9fe2dae63d3a351aa3a82da2b4e7dae1`. It is not a published release.
The accepted checked Patch Pipeline remains the integration route for the
existing capsule consumers. No submodule or generated guest source is edited.

## Changes

- Keep stick and analogue pedal samples current while retaining the order of
  queued digital button edges. Previously an older edge could also carry old
  steering through recovery and input-lead reduction.
- Use per-owner p90 delivery lateness and measured blocked time for Auto delay.
  A lone old/reordered packet or a brief polling wait must not buy another
  complete 30 Hz input-delay tick. Exhausted-window pauses and sustained replay
  pressure retain their protective paths.
- Exclude deliberate confirmed menu/resource transactions from race-delay
  feedback while continuing to capture menu input and negotiate capabilities.
- Service unfinished correction replays in bounded slices, with transport and
  fresh physical-input service between steps. A slice permits at most four
  pulses and checks a four-millisecond budget between guest ticks. It cannot
  preempt an individual tick. Input, pacing and GPU waits still sleep.
- Record executed-tick CPU cost separately from idle polling, with distinct
  network-input, confirmation, presentation, scene, replay and scheduling-gap
  attribution. Existing blocked-poll counts are not pause durations.

Scene agreement, conservative RAM checkpoints, GPU retirement, the gameplay
clock, input ownership and confirmed saves remain enforced.

## Verification and its limits

Focused tests cover current analogue samples with queued button edges, p90
outlier rejection, measured-wait thresholds, confirmed-gate feedback, bounded
replay slices and pause accounting. The retained multi-racer delivery fixture
adds periodic 180 ms bursts to a 100 ms RTT, jittered, lossy two-racer case.
Both worlds must confirm the same state and retain bounded input lead.

An actual two-process Windows WebRTC/GPU race was compared with stable 1.0.14
using the same added 70 ms receive delay at each endpoint and up to 15 ms
jitter. Both ran the retail simulation at about 30 Hz without a rollback
failure. The candidate host stayed at zero local lead during the measured
race; the stable host ranged from zero to three ticks. The candidate client
ranged from zero to two ticks; the stable client ranged from one to three.
The candidate client's renderer-submission response still reached the 150 ms
p95 upper bucket. These are local fixture results, not scanout measurements,
WAN acceptance, or a physical Steam Deck result. They do not establish a locked
60 FPS on Deck or eliminate network propagation delay.

## Verified 500+ ms packet-delay tests

The unchanged packaged Linux AppImage was exercised with two real retail-ROM
processes and WebRTC peers in private network namespaces. A userspace TUN
forwarder delayed the peer UDP traffic; a scoped firewall prevented an
unimpaired alternative peer route. Independent UDP echo measurements verified
the imposed delay. Neither the packaged binary nor its input protocol was
modified for these tests. WSL lacks the standard netem qdisc, so the failed
qdisc attempt is not counted as a latency test.

These are the built-in fresh-profile qualification races: Auto input delay,
two players, six-frame rollback window, v1.1 ROM, Modern presentation and a
software Vulkan GPU. The normal lobby's default fourteen-frame window and
manual six-frame input delay were NOT exercised by this entry point. Results
must not be represented as benchmarks of those normal defaults, a physical
Deck, Wi-Fi, or GPU frame-rate performance.

After allowing twenty seconds for adaptation, the measured results were:

| Connection | Host simulation Hz | Client simulation Hz | Network-input idle per five-second reporting interval |
| --- | --- | --- | --- |
| Unimpaired control | 29.88–30.20 | 29.91–30.20 | 0 ms on either peer |
| 500.9 ms measured median RTT, no added loss/jitter | 19.38–21.56 | 19.80–21.37 | About 2,084–2,521 ms |
| 300 ms each way, independent uniform ±40 ms jitter, 1% packet loss each way | 8.80–13.60 | 9.79–14.00 | About 3,253–4,159 ms |
| After removing the latter impairment | 29.99–30.00 | 29.99–30.19 | 0 ms on either peer |

The jittered echo samples ranged from 537.2 to 675.3 ms RTT, median 583.8 ms.
The steady case had 21 settled pacing reports per peer; the jittered case had
10; the recovered interval had 5. Reported idle is accumulated waiting within
each interval, not the duration of a single pause. These simulation rates are
not rendered FPS. All four impaired-run processes exited successfully and
logged no rollback failure; that does not make the races acceptably smooth.

Both peers reached nine frames of local input lead in the steady 500 ms case.
After the jitter/loss impairment was removed, the host reduced its lead to
zero but the client still reported nine at the end, roughly 45 seconds later.
This is a recovery problem to investigate; the logs do not yet establish its
root cause. At 30 Hz, nine frames represent 300 ms of scheduled input lead,
before further execution/presentation delay. Histogram overflow markers are
not literal multi-billion-millisecond response times and are not used here.

These measurements reproduce sustained input waiting in the tested candidate
configuration. They do not justify claiming smooth 500 ms play or blaming the
public lobby defaults on the six-frame qualification result. The next
qualification needs representative fourteen-frame settings, independently
controlled input lead, phase/clock drift measurements, and a longer client
delay-recovery check. Increasing delay or history without measuring their CPU
and response costs is not an adequate repair.

Private logs, timing JSON, the packet forwarder, and cleanup evidence are under
`D:/DiddykongWorkFolder/online-playability-20261104/implementation/high-latency/`.
Each test removed its namespaces and six scoped NAT/forwarding rules; the
system forwarding setting was not changed. Windows ZIP and Linux AppImage
SHA-256 values still match the previously verified candidate packages. No
new release or gameplay code change was made for this investigation.

## Vanilla cars and road wheels

The user reports missing vanilla car textures and remote wheels in both view
layouts, with Session mods off, and no improvement with maximum detail off.
Whether it starts in the first race or follows a track change is uncertain.

The initial mod-free v1.1/D3D12 start-grid capture contains valid car textures,
four live road-wheel attachments per racer and corresponding display-list
calls. Its red wheel surfaces match the original sprite assets. The initial
visual reading of those surfaces as texture corruption was incorrect. This
does not reproduce or disprove the user's intermittent missing-part report.
No speculative car-rendering repair is claimed from this evidence.

A later v1.0/Vulkan fullscreen race capture also retains the source car and
wheel textures. Vulkan here runs on the available Windows GPU, not Deck's AMD
GPU. Source-material and attachment checks are not proof of every remote
wheel's visibility at every distance or of subsequent track transitions.

The explicitly requested developer GPU readback is gated by
`DKR_ONLINE_RENDER_PROBE` and the isolated race epoch. `DKR_ONLINE_RENDER_FRAME`
chooses its first eligible frame. It drains the queues and is intrusive:
captures must be excluded from timing comparisons. The adjacent RAM file is
private ROM-derived investigation material, never a distributable or an
automatic crash-report attachment. No readback is performed by default.

All racers in a candidate lobby must use matching candidate builds. Changes
remain unmerged; cross-play/compatibility-breaking integration still needs the
required owner review. Physical Deck LCD Wi-Fi hosting and a remote client's
playability remain acceptance checks for the user.

## Follow-up: latency recovery candidate, 2026-11-05

The representative fourteen-frame configuration also reproduced the original
transport stalls. At steady 500 ms RTT, the earlier candidate advanced about
20.6-28.0 simulation frames per second and spent 0.70-1.61 seconds per five-second
sample waiting for inputs. Those numbers are aggregate waiting, not the length
of a single pause. Merely increasing the prediction window was not the repair.

The final candidate adds bounded client clock recovery, a 512-byte unsent
realtime queue budget, timed live delivery, replaceable monotonic progress and
receipt reports, and pipelined eight-frame repair batches. Immutable inputs and
the existing controller codec are retained. The client continues servicing its
final input tail while scene agreement is pending. Heartbeat measurements keep
eight locally issued probes for up to five seconds instead of invalidating a
probe at the next one-second heartbeat.

Separating SCTP streams did not isolate their shared association's recovery
behaviour. Several private alternatives still stalled under 600 ms RTT,
independent jitter and packet loss. Reliable live delivery and changing SCTP's
minimum retry timer were not retained. A first batched-repair attempt also
stopped at eight frames per round trip; the regression now proves later missing
frames remain in flight before earlier ones are acknowledged. Disposable
repairs alone still performed poorly in the harsh SCTP-only experiment.

The selected addition is a separate ICE datagram route for already sealed race
packets, negotiated through the authenticated existing lobby extension. It uses
libjuice's public API without dependency edits. The original receiver still
checks AEAD, room, sender and replay window. Only realtime/replica packets up to
1200 sealed bytes use this route; setup, commands, lifecycle and larger packets
keep their existing transport. Receive queues are bounded to 128 packets and
128 KiB per peer. Sends have a 32 KiB/s budget and 4 KiB burst limit per peer.
Unestablished or silent routes retain the existing transport fallback.

Fresh profiles use Automatic input delay, a one-frame initial lead and a
fourteen-frame prediction window. Saved settings are preserved; existing users
must select Auto and fourteen frames for equivalent testing.

Two actual v1.1 game processes were tested with the private TUN forwarder. Both
directions of usable peer UDP crossed that forwarder; independent UDP echo
measurements verified the imposed latency. These runs use Vulkan llvmpipe and
dummy audio, not a physical Steam Deck. All rows below use Auto and fourteen
frames, with identical code in both peers:

| Case | Settled host/client simulation rate | Network input waiting in settled samples | Outcome |
| --- | --- | --- | --- |
| 600 ms RTT, independent +/-40 ms each-way jitter, 1% UDP loss | 28.19-30.93 / 28.76-30.53 Hz | Zero in both peers | Both exit successfully; healthy-link recovery returns to 29.99-30.00 Hz and zero local lead |
| 140 ms RTT, +/-15 ms each-way jitter, 1% loss, +200 ms each-way bursts for 200 ms every six seconds | 29.94-30.00 / 29.99-30.00 Hz | Zero in both peers | Both exit successfully; zero settled local lead |
| New race datagrams deliberately blocked, existing fallback at 500 ms RTT | 29.73-30.20 / 29.93-30.19 Hz | Zero in both peers | Both exit successfully; client retains three-four lead frames during impairment, then returns to zero |
| Final packaged AppImage: 500 ms RTT, +/-25 ms each-way jitter, 1% loss, +200 ms each-way bursts for 200 ms every six seconds | 29.91-30.16 / 29.80-30.19 Hz | Zero in both peers | Both exit successfully; two-three settled lead frames return to zero after recovery |

Settled samples exclude the first twenty seconds after changing impairment.
The 600 ms case forwarded 13,492 sealed race datagrams; the recurring-spike case
delayed 189 UDP packets in bursts. The blocked-route case dropped all 12,685
new-path race packets, so its success cannot be attributed to that fast path.
The final packaged AppImage forwarded 14,446 sealed race datagrams, dropped 69
UDP packets and delayed 234 UDP packets in bursts. Its tested SHA-256 is
`c9c1ce57775d643e1a3d1ca16256b44936f139226ca0e8c570ec7b37ce58de07`.
These are separate bounded runs, not an identical captured-packet A/B trace.
No runtime failure was reported in these four selected runs. Earlier rejected
experiments, including receipt-history failures, remain in the external evidence.

Simulation rate is not rendered FPS. Local lead is not total controller-to-screen
latency. Software rendering and replay CPU cost remain visible in these tests;
neither a locked 60 rendered FPS nor universally invisible corrections is proven.
Outages longer than the prediction budget can still require waiting.

Real ICE connection tests pass on Windows and Linux, including sealed delivery,
tampered-packet authentication rejection, scope/size/lane restrictions, peer
removal and silent-route fallback. The actual-party fixtures also exercise
asymmetric clock recovery, blocked ordered control delivery, lost final scene
inputs and two-four-racer byte-exact synthetic confirmation. They do not replace
physical multiplayer acceptance. The relay socket fixture now recognizes the
new authenticated negotiation extension as metadata; its gameplay byte checks
remain unchanged.

Windows host verification passes all 166 CTest cases. Linux's mounted-tree
regression-harness test still encounters its tooling timeout; the exact unchanged
25-case harness passes on native Linux storage. The relay WebRTC fixture passes
on Linux after rebuilding its metadata dispatcher. Protected dependencies,
generated guest code and source-capsule bytes were not edited.

Private builds are `1.0.15-beta.1-playtest.2`, with matching v77/v80 identities
on Windows and Linux. They remain unmerged and unpublished. This work does not
claim to fix the separate intermittent vanilla-car texture/wheel report.

Evidence for the follow-up, including packet counters, failures from rejected
experiments and cleanup records, is under
`D:/DiddykongWorkFolder/online-playability-20261104/implementation-r2/`.
The final AppImage's combined latency/spike check and all scoped network cleanup
records are included there.

## Four-player mixed-platform qualification, 2026-11-05

The owner's requested four-player check uses the same final private playtest.2
Windows ZIP and Linux AppImage above; no game code or packaged bytes changed
for these runs. All four actual v1.1 ROM instances go through the normal owned
Quick Join lobby, four-owner countdown, character/track menus and race admission.
The unattended check holds acceleration and changes steering every fifteen
simulation frames. This is an automated race, not four human playtesters.

The host is the Linux binary under WSL2 on a Ryzen 9 3900X Windows workstation.
Windows clients run the native Windows binary with D3D12. Linux rendering uses
Vulkan llvmpipe. There is no physical Deck, remote WAN, macOS device or four-device
hardware acceptance in these results. Three runs use fresh isolated profiles,
Automatic delay, initial lead one and the fourteen-frame prediction window.

The external harness applies different delays to each host/client connection:
150, 450 and 750 ms nominal RTT. Selected host UDP sockets are routed through a
private TUN for egress; ingress copies are queued before reinjection, with the
original selected packets dropped. Encrypted application payloads are unchanged.
An independent UDP echo measurement checks both directions of each delayed
connection. The optional datagram route and the original UDP/WebRTC fallback
share the impairment. Windows clients' socket-owner records disambiguate their
connections when all three run on the same Windows host. No receive-delay hook,
dependency edit or privilege change to Windows is used.

| Run | Four-player composition | Independent median RTT, in client order | Settled Linux host simulation | Settled Linux host network waiting per five-second sample |
| --- | --- | --- | --- | --- |
| `mixed4-steady150-450-750-r4` | Linux host, two Linux clients, one Windows client | 151 / 452 / 820 ms | 24.97-29.58 Hz | Zero |
| `mixed4-3windows-steady` | Linux host, three Windows clients | 151 / 452 / 751 ms | 24.17-27.10 Hz | 0-58.217 ms |
| `mixed4-3windows-jitter-loss-spikes` | Linux host, three Windows clients; independent +/-25 ms each-way jitter, 1% loss and +200 ms each-way bursts for 200 ms every six seconds | 166 / 493 / 822 ms | 26.58-28.31 Hz | 0-58.086 ms |

The first and third runs' measured slow-link RTT exceeds the nominal 750 ms
added delay; do not report those as exact 750 ms measurements. Burst settings
are additional one-way packet delay, not an asserted measured peak RTT. The
stress run actually dropped 379 selected UDP packets and burst-delayed 1,131.
All three connections carried thousands of sealed race datagrams.

All twelve game processes exited with status zero and no reported rollback
failure. The final logs confirm all four racers reached the same race scene,
and input confirmation continued advancing. This does not assert byte-identical
guest RAM across the four instances; the deterministic input/state fixtures are
separate evidence. Earlier harness setup attempts failed before usable race
measurements and are retained separately, not counted as successful game runs.

These checks do **not** pass a consistently smooth four-player acceptance gate.
The two Linux clients in the first run measured 24.91-29.43 and 24.93-29.57 Hz
in settled impairment samples. Windows clients across the runs also fell below
the thirty-Hz simulation target. The stress run's Windows clients accumulated
2.210, 4.654 and 3.121 seconds of network input waiting over the entire race.
These are totals across the race, not single multi-second pauses. The steady
three-Windows-client run accumulated 0.543, 1.259 and 0.427 seconds respectively.

Four-racer replay work is a material remaining cost: the host commonly spends
roughly 17-22 ms on an executed simulation step and about 1.2-1.7 seconds per
five-second sample replaying steps under impairment. Software rendering and
multiple game processes on one workstation limit hardware conclusions, but
the lost simulation headroom cannot be described as a successful smoothness
result. Reducing transport input waits alone has not removed the slowdown.

Automatic lead reaches nine frames, about 300 ms at the thirty-Hz simulation
rate, in every run. That is configured input lead, not total controller-to-screen
latency. After removing impairment, the Linux peers recover to approximately
thirty Hz and zero settled network waiting, but retain six-nine lead frames
in the observed recovery period. Windows clients still show replay and reduced
frontier rates near the end of their logs. Four-player replay headroom and
recovery of delay/client phase remain open work; this build must not be presented
as uniformly smooth or low-latency at 750 ms RTT.

Settled Linux samples discard the initial un-timestamped log-history flush and
allow twenty-five seconds after a shape change: twenty seconds to settle plus
the full five-second reporting interval. Windows holds its active log exclusive
until exit, so Windows numbers above use complete race logs, without assigning
retrospective observation times to earlier samples. Each successful run lasts
about three minutes including menus, with approximately eighty-two-eighty-six
seconds of impairment and a subsequent healthy-link recovery period. These are
bounded runs, not endurance tests or identical captured-packet comparisons.

All scoped TUN, socket-routing, firewall and namespace state was removed.
The original global forwarding setting was retained. No game processes or
test network rules remained after qualification. Complete logs, independent
RTT samples, port-owner maps, observations, metrics and cleanup records live in
`implementation-r2/high-latency/` under the external evidence directory above.

## CPU checkpoint investigation and playtest.3, 2026-11-05

The four-player timing breakdown identified repeated full-state memory passes
as a material simulation cost. Every executed or replayed step captures and
hashes the complete state, then the former paged store compares/assembles its
delta history. A budget-selected full-image ring now captures directly into
reusable empty slots. Checkpoint hashing, restore validation, complete guest and
native state, input immutability and confirmed effects are retained. Oversized
mod states still use the existing bounded paged store. This trades RAM within
the configured budget for CPU; it does not eliminate rollback simulation work.

The first actual playtest.3 run uses the same four-ROM setup described above:
Linux host and three native Windows clients. Independent median RTT measurements
were 151.27, 451.28 and 751.27 ms. The ordinary fourteen-frame prediction window
and one-frame initial Auto lead were retained. The vanilla snapshot contract is
17,074,312 bytes; total retained checkpoints plus staging occupy 273,189,352 bytes
inside the 273,254,528-byte budget. Production logs confirm the direct ring is
active. Store interval means dropped from approximately four milliseconds in
playtest.2 to 2.4-2.5 microseconds in this run. Capture and full hashing remain
approximately 2.5 and 2 milliseconds respectively in the sampled intervals.

Settled host simulation improved to 29.54-30.00 Hz with zero measured input
waiting. Its healthy recovery samples were 29.99-30.00 Hz and zero waiting.
This is not a whole-session zero-wait result: host accumulated input waiting
was 272.285 ms, and Windows clients accumulated 721.661, 1,003.197 and 735.844 ms
over their complete races. Client simulation still fell below thirty Hz and
clock reports continued requesting approximately five percent slowdown after
RTT returned to normal. Host Auto lead still ended at seven frames. All four
processes exited zero without a rollback failure. Thus playtest.3 proves the
storage CPU saving, but does not pass the requested smooth four-player gate.

Complete evidence is under `implementation-r3/high-latency/snapshot4-3windows-steady/`
in the external evidence directory. No physical Deck or remote WAN was used.

The next private candidate separates Auto pacing from future input timestamps:
the authenticated host reports a monotonic simulation frontier in its existing
periodic delay hello. A current input lead cannot reliably date an input authored
before that lead changed. Replay cannot move the reported frontier backwards.
An initial smoothed implementation failed the existing one-second-outage gate
with a three-frame history: repeated equal-frontier reports did not cancel
extrapolation through the host pause. Explicit reports now anchor directly,
including an unchanged frontier; inferred fixed-lead clocks retain smoothing.
The pacing period remains bounded and no simulation frame is skipped. Unit
coverage includes stationary reports, stale scenes, rewind and normal recovery.
Final production integration and four-player qualification follow separately.

## Explicit host clock and playtest.4 qualification, 2026-11-05

Production Auto pacing now consumes an authenticated host frontier, independent
of future input lead. Its hello interval is 500 ms. An explicit stationary
frontier cancels extrapolation through a host pause; replay cannot move this
clock backwards. Manual fixed-lead inference remains separate. Historical
repair retries allow one measured RTT plus 100 ms, rather than two RTTs plus
100 ms. Deterministic party, outage, clock recovery and exact checkpoint tests
passed on Windows and Linux. Host tests passed 167/167 on Windows; Linux passed
166/167 with the mounted-filesystem regression-harness timeout separately
passing its unchanged 25 cases from native Linux storage.

The four-ROM tests below use the final playtest.4 Windows ZIP and Linux AppImage,
Linux host plus three native Windows clients, Auto and a fourteen-frame window.
The machines are still processes on one workstation, not a physical Steam Deck
or remote WAN. The controlled impairment is introduced during active racing,
then removed; whole-race totals include initial adaptation to that sudden change.

| Case | Independent median RTT, client order | Host recorded input waiting, whole race | Client recorded input waiting, whole race |
| --- | --- | --- | --- |
| `verifiedclock4-w14-steady` | 151 / 451 / 751 ms | 364 ms | 454 / 832 / 583 ms |
| `verifiedclock4-w14-jitter-loss-spikes` | 167 / 453 / 746 ms | 588 ms | 954 / 2,257 / 1,846 ms |

The stress case adds independent +/-25 ms each-way jitter, one-percent packet
loss and +200 ms each-way bursts for 200 ms every six seconds. All eight game
processes exited zero without a reported rollback failure. These are aggregate
logged waits, not the longest single pause; the final partial reporting window
is not flushed. They explicitly fail an absolute zero-wait acceptance criterion.

In the steady case the host's settled five-second samples recorded zero input
waiting and 29.34-30.20 simulation Hz, recovering to 29.94-30.13 Hz. Stress
samples recorded 0-19.696 ms waiting and 27.53-30.40 Hz; recovery recorded zero
waiting and 29.88-30.20 Hz. All owners returned to zero configured input lead
after recovery. Windows logs remain exclusive until exit: their whole-race
rates are usable, but retrospective wall-clock phase assignment is not. These
numbers measure thirty-Hz simulation progress, not sixty-FPS presentation or
end-to-end controller latency.

The test VM sometimes develops a substantial monotonic clock-rate error under
load. The preceding/following cross-platform clock probes for the above two
runs agreed within nine ppm, and independent UDP measurements matched their
intended latency range. Later twenty-frame comparisons showed clock instability
again, including a concurrent monotonic/raw discrepancy. Those comparisons
are inconclusive for cross-platform pacing and do not justify changing the
default window or claiming a zero-wait fix. Temporary clock/time-service probes
are scoped by the external harness and restored; no product clock shim is used.

The full-image ring removes roughly four-five milliseconds of store work per
executed/replayed step in the sampled four-game runs. Direct store means are
approximately two microseconds. Full capture and hashing still cost several
milliseconds. This is a checkpoint-stage improvement, not a measured percentage
reduction in whole-application CPU. Larger history uses more RAM, and large mod
states keep the paged fallback. Physical Deck LCD Wi-Fi acceptance remains open.

## Idle secure-WebSocket shutdown deadlock, playtest.5

An earlier stress run (`frontier4-w14-jitter-loss-spikes`) timed out while a
Windows client exited. Its racing thread had retired. Private minidump/PDB
evidence pointed to the social worker waiting in TLS send and another worker
forwarding an idle notification from the MbedTLS read callback. These are
offline stack candidates rather than a complete formally unwound trace.

A focused loopback TLS test reproduced the Windows hang with the actual pinned
production library: initial echo succeeded, then an idle ping/echo/close sequence
failed to finish within twelve seconds. The MbedTLS read callback holds the TLS
mutex while forwarding an empty TCP idle notification. WebSocket responds by
sending a ping synchronously through the same TLS mutex. This re-entry blocks.

The fix is the registered dependency patch
`patches/libdatachannel/0002-dispatch-mbedtls-idle-notifications-outside-read-lock.patch`.
It delivers only idle notifications on the existing worker pool with retained
transport ownership. TLS verification, payloads, ping/pong, outstanding-ping
limits and connection detection are unchanged. The dependency checkout is
modified only by `scripts/apply_dependency_patches.py` after manifest hash and
pinned-commit validation. No submodule source is edited directly. The focused
test exercises four independent connect/initial-echo/idle/echo/close cycles;
production integration and final packaging are recorded in the external r5
evidence, separately from the unpatched r4 race results.

## Final packaged playtest.5 comparison

Both final packages passed their source-chain, archive readback, file allowlist,
helper and peer-identity checks. Windows host tests passed 168/168. Linux passed
167/168; the mounted-filesystem harness timed out, then the unchanged 25 cases
passed from native Linux storage. The new TLS idle test and production replay,
checkpoint, online-party and retained integration checks passed on both systems.

The exact packaged binaries then completed two four-game stress runs. Each used
a Linux/WSL2 host and three native Windows clients, Auto input delay, one-frame
initial lead, +/-25 ms each-way jitter, one-percent loss, and +200 ms each-way
bursts for 200 ms every six seconds. Impairment began during active racing.
Clock probes before, twice during, and after each run agreed within three ppm.
No kernel tick/frequency adjustment was needed in either accepted run; the time
service was temporarily stopped and restored. A prior preflight harness attempt
failed before creating any game processes and is excluded from these results.

| Prediction window | Measured median RTT, clients 1/2/3 | Host recorded input wait, whole race | Clients' recorded input wait, whole race |
| --- | --- | --- | --- |
| 14 | 146.0 / 454.3 / 748.9 ms | 780.127 ms | 1,438.887 / 2,969.775 / 1,752.271 ms |
| 20 | 146.8 / 459.3 / 762.1 ms | 56.970 ms | 0 / 73.107 / 55.322 ms |

All eight processes exited zero without a reported rollback failure. Twenty-frame
settled host samples recorded zero input waiting, but simulation still ranged
28.36-30.39 Hz. Recovery samples returned to 29.99-30.00 Hz and zero waiting.
Configured lead returned to zero on every owner in both runs. During impairment
Auto still used up to nine lead frames: this is not a claim of zero controller
delay, thirty-Hz lock, sixty rendered FPS, or flawless play.

After the first seventy process seconds, Windows game CPU means were
1.309/1.297/1.323 CPU-seconds per wall-second at window 14 and
1.341/1.297/1.349 at window 20. One CPU-second per wall-second means one logical
core fully occupied; child helper processes are excluded. This is one bounded
comparison with different delivered packet sequences, not a statistically
controlled whole-CPU improvement claim. The larger direct history uses roughly
100 MB more memory per process, remaining within the 512 MiB cap.

Auto plus a twenty-frame prediction window is the better high-latency test
setting from this comparison. The saved preferences and fourteen-frame fresh
default remain unchanged pending physical Steam Deck LCD Wi-Fi acceptance.
The zero-wait goal remains open: even the better run recorded short waits, and
arbitrary loss can exhaust bounded deterministic history. Missing vanilla car
textures/remote wheels also remain a separate open issue.

Complete packaged-byte hashes, CPU samples, timing observations, clock checks,
logs and cleanup records are in external `implementation-r5/TEST-RESULTS.txt`
and `implementation-r5/high-latency/`. No physical Deck/WAN was tested. These
post-test notes are documentation, not changed build inputs or packaged bytes.
