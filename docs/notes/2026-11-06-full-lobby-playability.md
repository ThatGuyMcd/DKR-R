# Full-lobby playability

Private playtest.6 follows playtest.5. The target is four racing owners and
eight delayed spectators. No publication or merge is authorised by this work.

## Implemented changes

- Pipeline at most 120 confirmed frames per spectator independently of the
  cumulative acknowledgement. Repair the oldest missing range on a bounded
  timer, retaining every confirmed input rather than dropping simulation steps.
- Buffer 45 frames before initial playback/rebuffering. Render steady playback
  at 30 simulation ticks per second; fast-forward only when substantially behind.
- Export an integrity-checked retained checkpoint at the confirmed frontier.
  Capture the live world only when it is already fully confirmed. A spectator
  export no longer suppresses live speculative advancement.
- Rotate checkpoint delivery order and cap aggregate active background traffic
  to 256 bytes per millisecond with a 16 KiB burst. Retain separate input/control
  lanes, queue bounds, authentication, confirmed save ownership and scene gates.
- A disposable-profile qualification switch selects the normal Watch Race join
  role, so actual packaged games can exercise all eight spectator positions.
- Refresh a running-race checkpoint once it is more than 60 confirmed frames
  old. Preserve the bounded input tail used by existing viewers when replacing
  that checkpoint. A newly joined viewer does not force other viewers to reload.
- Recheck party shutdown after a rejected send. Closing the application while
  the simulation finishes a pulse no longer reports a false queue overflow.

## Verification

Native full-lobby functional qualification and mixed Windows/Linux wire-latency
qualification passed on 7 November 2026. This is not a claim of flawless gameplay
on every network or of Steam Deck hardware performance.

### Native Windows capacity and application-delivery tests

Each accepted run used the packaged playtest.6 executable, a supported retail
v1.1 ROM, four actual racing guest simulations and eight actual watching guest
simulations. All twelve processes exited successfully without a rollback
failure. The script verified actual race playback, not just lobby membership.
All processes ran on one Windows PC using Vulkan. The racing controls change
steering every 15 simulation frames. This exercises prediction corrections.

Host settings: automatic delay ON, initial lead 1, prediction window **20**.
The normal fresh-host default remains 14; saved user settings are preserved.
These measurements must not be represented as measurements of the default
14-frame profile.

| Run | Conditions | Recorded input wait: host / racer 1 / racer 2 / racer 3 | Spectator race frames replayed |
| --- | --- | --- | --- |
| r5 | Four racers and eight late spectators, local transport | 0 / 0 / 0 / 0 ms | 713–1,792 each |
| r6 | Racers local; every viewer receives a 1,000 ms delay, ±25 ms jitter, additional 1,000 ms spikes | 0 / 0 / 0 / 0 ms | 538–1,077 each |
| r7 | Every racer receives 375 ms delay, ±25 ms jitter and 200 ms spikes; slowest viewer receives 1,000 ms plus 1,000 ms spikes | 651.122 / 1,175.181 / 1,047.074 / 870.814 ms | 466–1,082 each |
| r9 | Seven viewers join at 45 seconds; the eighth joins at 135 seconds | 0 / 0 / 0 / 0 ms | Earlier viewers 1,264–2,412; late viewer 346 |

r7's authenticated application heartbeat measured approximately 750 ms RTT.
Its later race intervals, whose ending frontier is at least frame 150, recorded
**0 / 374.519 / 223.665 / 118.731 ms** of input wait over roughly 130 seconds.
The first race interval accounts for most of the total. Client input lead
reached 9 frames (300 ms at 30 Hz). The remaining waits and added input lead
are real limitations; this test did not eliminate them.

Every accepted run recorded zero racer `spectator-baseline-idle-us`. The host
exported retained confirmed checkpoints while its live world was speculative.
For example, the later r8 admission exported frame 3,390 while live frame 3,391
continued. Existing observers retained their history during fresh exports.

### What the impairment test means

The fresh-profile CLI arms a bounded receive queue in the common transport
wrapper, covering the ordinary, ICE datagram and peer-relay paths. Each test
checks that the delay was actually applied and that packets were delivered.
Spikes last 200 ms every six seconds. Impaired runs discard every 97th
disposable Realtime/Replica message after authentication. Essential control,
authoritative and checkpoint messages are preserved.

This is **application-delivery impairment**, not raw wire packet loss, SCTP
congestion, a real WAN, or independent physical computers. The synthetic
production-party tests separately exercise loss and repairs. These results are
separate from the mixed-platform wire tests below.

Wait measurements sum the production five-second pacing reports for race
epoch 92. They omit the final partial reporting interval. They are not
individual-pause duration measurements. The later-race numbers exclude the
first mixed startup/race interval; the table also gives the inclusive totals.

### Performance and viewing-delay limits

Forty-five frames is the playback buffer target, not a guarantee of 1.5 seconds
end-to-end delay. Eight simultaneous checkpoint downloads share approximately
256 KB/s of background bandwidth and can take much longer to start. Catch-up
also adds viewing delay. These tests prove that viewers play and do not gate
the racers; they do not prove that every viewer remains within two seconds.

Twelve renderers sharing one PC caused resource contention. Baseline race-rate
samples ranged from about 26.5 to 30.4 simulation Hz; r7 ranged from about 21.1
to 29.1 Hz. The game simulation target is 30 Hz, distinct from the requested
60 Hz presentation. There is no locked-60-fps claim and no defensible per-device
CPU improvement percentage from this arrangement. A physical Steam Deck LCD
and separate-machine testing remain necessary.

### Rejected runs and clock investigation

- Earlier r3 exhausted D3D12 command-queue resources while starting twelve GPU
  applications on one PC. Two viewers failed, so the run was rejected. Selecting
  supported Vulkan in the test profiles allowed the twelve-process runs above.
- r4 found the viewer shutdown/send race. It was rejected, repaired and covered
  by a regression before r5 and subsequent accepted runs.
- r8's last viewer joined at 150 seconds. It verified and admitted a fresh race
  checkpoint, but exited before a race-playback report arrived. It failed the
  playback assertion and is not counted as a pass. r9 provided a longer late-join
  observation window without weakening the replay requirement.
- Two mixed-platform attempts were rejected before gameplay because WSL's
  monotonic time ran 5–9% differently from Windows. An isolated kernel trace
  identified a `chronyd` instance outside the Ubuntu process namespace changing
  kernel tick/frequency. Temporary diagnostic settings were restored. Windows
  initially denied the WSL debug shell because the command runner was not
  elevated. These attempts remain rejected; the later qualified runs below
  resolve the clock blocker without changing game timing.

Ubuntu documents the possibility of competing WSL time synchronizers in its
[WSL time-sync explanation](https://ubuntu.com/wsl/docs/stable/explanation/time-sync/).
Clock qualification must pass before accepting a mixed-platform timing result.

The follow-up harness check exercised all eleven Windows-to-WSL UDP paths in
parallel with 150/450/750 ms RTT profiles, ±25 ms per-direction jitter, 1% wire
loss and 200 ms spikes. The echo checks passed and their scoped shaper rules
were removed. This qualifies the network test harness only, not gameplay.
One normal-OS clock sample was within 13 ppm, but subsequent calibrated and
read-only preflights failed again by approximately 8–9%. No mixed race was
started or accepted from those attempts. The wrapper now fails explicitly
when clock qualification fails and leaves normal OS synchronization unchanged
unless temporary calibration is explicitly selected. Its run-label handling,
parallel RTT probes and late-viewer socket-readiness checks were repaired;
native test profiles select Vulkan to avoid the twelve-context D3D12 limit.

### Accepted mixed Windows/Linux wire tests

The user restarted Codex with Administrator access. Its command runner still
received a standard token, so a helper obtained an Administrator token through
normal Windows elevation. The first diagnostic shell exited its one-shot WSL
login; the idle test VM was restarted to restore that console. A temporary
lease then paused only the identified global `chronyd` process. The lease
included an independent timeout that resumes the same process, checking its
start time to avoid signalling a reused PID. Ubuntu's synchronizer, Hyper-V
implicit synchronization and kernel tick/frequency were restored after each
test; the global daemon was resumed after both tests. Its final status was
`S`, with implicit synchronization `Y` and `systemd-timesyncd` active. No
persistent clock configuration was changed.

Both runs used the unchanged packaged playtest.6 AppImage as a Linux host and
the matching Windows ZIP for three Windows racers. The second added eight
Windows viewers. Linux used software Vulkan; all twelve applications shared
one physical PC. This is an actual mixed-platform guest-game test, but not a
physical Steam Deck, Wi-Fi or separate-machine benchmark.

All host UDP paths were shaped, including encrypted race datagrams and
checkpoint/control traffic. The three racers received 150 / 450 / 750 ms RTT
profiles, ±25 ms jitter per direction, 1% wire packet loss, and extra 200 ms
per-direction spikes lasting 200 ms every six seconds. Spectators received
the same rotating RTT profiles. The last spectator additionally received
2,000 ms per direction for 15 seconds, then recovered. The impaired phase
lasted 65 seconds after parallel echo proofs, followed by normal transport.
Every racing path carried more than 18,000 sealed race datagrams through the
shaper; echo probes alone were not sufficient for acceptance.

Independent Windows QPC / Linux monotonic comparisons passed before, twice
during, and after each game run. These qualification samples differed by at
most approximately 37 ppm; the additional initial calibration sample was
within 136 ppm. The qualification tolerance was 0.2%. Both wrappers exited
successfully and explicitly recorded `clock_qualified=true`.

| Mixed run | Recorded input wait: Linux host / Windows racer 1 / racer 2 / racer 3 | Spectator playback |
| --- | --- | --- |
| Four racers, no viewers | 0 / 0 / 62.927 / 27.501 ms | None |
| Four racers, eight viewers | 0 / 35.862 / 58.091 / 0 ms | All eight replayed 352–1,233 race frames each |

All sixteen game instances across the two runs exited with code 0. No party
or rollback failure was recorded. Every racer recorded zero spectator-baseline
wait. The Linux host also recorded zero input wait throughout the spectator-only
burst. The full lobby did not reintroduce constant input waiting in these runs;
the small client waits remain real, and one run per condition cannot establish
a universal performance difference.

The same Auto ON, initial lead 1, **20-frame prediction window** applies here.
Local input lead reached nine frames under impairment and returned to zero
after recovery. Avoiding waits can therefore still add up to 300 ms of local
input lead; these results do not mean zero input latency. Fresh-host defaults
and saved preferences were not changed to match the test profile.

The full-lobby Linux host's settled impaired simulation rate ranged from
27.92 to 29.93 Hz. Whole-race samples across the four racers ranged from
19.77 to 30.72 Hz, including transitions and one-PC contention. Spectators
were still catching up when the test ended. Neither a locked 60 fps display
nor a two-second end-to-end spectator delay has been established. All scoped
network shaper rules were removed; global forwarding was unchanged.

## Build and regression verification

- Windows host suite: 169/169 passed.
- Linux host suite: 168/169 passed on the mounted Windows filesystem. The sole
  failing regression harness hit subprocess timeouts; its exact unchanged 25
  cases passed from native Linux storage in 5.244 seconds.
- Final production party fixture: 2,758,684 synthetic checks passed. Retained
  spectator/admission, online, profile, portrait, mod-history and vehicle-lighting
  checks passed on Windows and Linux.
- Pattern guard, tooling suite and pre-commit checks passed. The tooling suite
  uses native Git for Windows; an earlier MSYS Git invocation was rejected after
  its path/ref conversion broke the temporary-repository fixtures.
- Both packages passed source-chain, package allowlist, dependency, helper and
  archive/AppImage readback checks. Their v1.0/v1.1 peer identities match.

Evidence is retained outside the repository at
`D:/DiddykongWorkFolder/online-playability-20261104/implementation-r6/`.
Accepted logs, assertions and metrics are under `native-full/capacity-r5`, `r6`,
`r7` and `r9`; rejected-run evidence is retained separately alongside them.
Mixed logs are under
`high-latency/final6-four-w20-spectators{0,8}-jitter-loss-spikes-admin-r5`.
The matching `FULL{0,8}-W20-admin-r5-RACE-QUALIFICATION.json` records retain
clock measurements and restoration, and `high-latency/MIXED-METRICS.json`
contains the cumulative wait and playback measurements.

| Artifact | SHA-256 |
| --- | --- |
| Windows x64 ZIP | `d6a59f84cdf9bab6c843ca7ec37489e8dd03f96f3fcd117f19f69c3b38f138c6` |
| Linux x86_64 AppImage | `7a78530c008f6860bb9d9d34c2ad5984c3db1042bd4ea05237ce6609c8bd103d` |

No push, merge, publication or owner/hardware playtest is implied by these
automated results. Physical Steam Deck Wi-Fi and separate-machine acceptance
remain open.
