# v1.0.15 release preparation — 7 November 2026

This records the local v1.0.15 preparation since the public v1.0.14 release.
Nothing is pushed, tagged, uploaded or published before the owner's review.
The external release handoff holds package hashes and detailed test evidence.

## Targeted post-install cleanup

Successful Mod Library installs queue cleanup for their own committed package
after download history is saved. The startup pass remains once per launch;
targeted cleanup does not scan unrelated caches or recovery history. Its tests
and refreshed package identities are recorded in the post-install-cleanup
handoff. Earlier sections below retain their original package scope.

## Startup cleanup scheduling

Automatic cleanup now runs once per application launch, without artwork/install
re-triggers or periodic polling after completion. It continues at background
priority during gameplay while live online resources retain their own lease.
Final scheduling tests, rebuilt package identities and validation are recorded
in the startup-cleanup handoff. The results below retain their original package
scope. See the [follow-up](docs/notes/2026-11-07-storage-scheduling.md).

## Initial storage qualification (before the scheduling follow-up)

The storage-maintenance rebuild supersedes the earlier prepared packages below.
Its final package hashes and source commit must be recorded in the refreshed
handoff before signing. Earlier network measurements remain measurements of
the pre-storage packages; they are not repeated-byte qualification of this rebuild.
Storage coverage includes verified downloads, edition recovery, interrupted
retirement, immutable history, cache references, linked files, long Windows
paths, process leases and gameplay deferral. The final storage fixture passed
on Windows and Linux. Packaged Windows and Linux idle/gameplay checks verified disposal
while idle, preservation during an actual game and unchanged synthetic saves.
Windows host coverage is 170 tests: 169 passed in the full run and the tooling
case passed after correcting the MSYS Git PATH. Linux passed 169/170 on the
Windows mount; its timed-out regression harness passed all 25 identical cases
on native Linux storage in 4.379 seconds. The failures remain in the evidence.

The rebuilt Windows full-lobby baseline completed with four racers and eight
late spectators. All 12 processes exited cleanly, all four racers recorded
zero racing input waits, and viewers replayed 1,396–3,289 actual race frames.
This is a same-PC baseline without added network latency. The earlier mixed
high-latency results below retain their pre-storage package scope. See
[storage](docs/STORAGE.md) for retention rules and limitations; the handoff
records all final platform packages and remaining physical-device checks.

## Scope and source

The release changes online input timing/recovery, checkpoint CPU work, confirmed
spectator playback, connection shutdown and live host settings. Launcher,
graphics, controllers, languages and authoring features from v1.0.14 remain.
See the [release notes](docs/PUBLIC-1.0.15-NOTES.txt),
[documentation review](docs/DOCUMENTATION-1.0.15.md) and [changelog](CHANGELOG.md).

All required source and registered third-party patches must be committed before
signing. The existing checked release pipeline retains the source capsule and
both normal/owned ROM payloads. Submodules, generated recompilation inputs and
the frozen capsule are not edited or repinned. Every platform is rebuilt for
1.0.15; a renamed private-playtest binary is not a release build.

## Final-build verification

Windows x64, Linux x86_64 and Linux ARM64 production builds completed. Source
audits, allowlisted package composition, archive readback and packaged helper
checks passed on all three. Steam Deck copies the exact Linux x86_64 AppImage.
Both Linux packages require glibc 2.38 and GLIBCXX_3.4.32, with no missing
non-system dependencies in the inspected closures. ARM64 CPU/data/helper checks
use QEMU and its target sysroot; this does not qualify a physical ARM GPU.

Windows host CTest passed 169/169 after the final transport repair. Linux passed
168/169 on the mounted Windows checkout: the regression harness timed out.
Its identical 25 cases passed on native Linux storage in 7.135 seconds. The
original timeout remains recorded. Both host suites include the new transfer
budget regression. The same online-stability checks passed under ARM64 QEMU.
All three platforms passed the retained and online production fixtures, with
2,758,829 party checks each, profile/portrait/history checks and vehicle lighting.
Native Linux vehicle sanitiser checks passed. Guard/tooling, installed hooks,
metadata and asset checks passed; the initial Windows MSYS Git tooling failure
is preserved separately from the successful native-Git rerun.

Both supported ROM revisions started successfully from the final Windows and
Linux packages. Earlier v1.0.15 preparation also checked manual mod loading,
cancellation, automatic launch, failed-mod fallback and startup captures before
the isolated transport-budget repair; these are not labelled as final-byte
checks. Updater signing and exact-v1.0.14 upgrade results are in the handoff.

## Final multiplayer checks and the transfer-budget repair

Release testing found that resetting the background-budget timestamp on every
network-pump call discarded fractional milliseconds. Frequent pumping could
therefore starve spectator downloads. The shared production helper now retains
that time, with unchanged bandwidth and burst limits. The regression checks
100 ms of pumping at 10 kHz, the bounded idle burst, initialisation and clock
reversal. No guest clock, frozen source or transfer safety check was replaced.

The repaired Windows full-lobby baseline passed with four racers and eight
late-started viewers. All 12 exited cleanly, with no party/rollback failures.
All racers logged 0 ms racing input waits; each viewer replayed 1,241–2,843
actual race frames.

The final mixed Windows/Linux full-lobby run also passed. The Linux AppImage
host and three Windows racers used measured 150/450/750 ms wire RTT profiles,
±25 ms jitter per direction, 1% packet loss and 200 ms spikes. Eight Windows
spectators joined after racing began; one also received a temporary 4-second
RTT burst. All 12 exited cleanly without party/rollback failures, and all eight
viewers replayed 835–3,041 race frames. Cumulative logged racing input waits were
0 / 0 / 16.677 / 10.114 ms for host and racers 1–3. Spectator-checkpoint waits
were zero for the racers. Auto delay was ON and the prediction window was 20;
the fresh-profile default remains 14. These waits are totals for the controlled
run, not controller-to-screen latency or a universal pause-free guarantee.

Independent Windows/Linux clock probes passed before, twice during and after
the final wire test, within 5 ppm. Temporary timing calibration was restored,
the clock daemon was verified running, and scoped network shaping was removed.
All game processes ran on one PC. This is mixed-platform qualification, not
separate-device, physical Steam Deck Wi-Fi, WAN or locked-60-FPS acceptance.

The earlier pre-repair late-viewer attempts failed their minimum playback
requirement and remain in the evidence; they are not counted as passes. A
separate early-viewer variant failed its test auto-start before racing and is
also excluded. The pre-repair full-lobby live-edit stress verified all requested
Driver transitions, but two viewers missed the ten-second playback minimum.
The final four-racer run without spectator capability passed all five live
prediction changes (14 → 20 → 2 → 20 → 6 → 20), Auto/manual switching, client
rejection and cancellation of queued settings. All four exited cleanly. It used
approximately 750 ms application delivery RTT, jitter and spikes; the deliberate
small windows cause waits. This is live-edit safety coverage, not another wire
loss test or a zero-wait result. Detailed results are in the handoff.

## Earlier private qualification

The technical records retain their original build numbers:

- [Online playability](docs/notes/2026-11-04-online-playability.md): timing,
  checkpoint work, transport and measured recovery.
- [Full lobbies](docs/notes/2026-11-06-full-lobby-playability.md): playtest.6
  four-racer/eight-spectator tests, including mixed Windows/Linux wire shaping.
- [Live settings](docs/notes/2026-11-07-live-host-settings.md): playtest.7,
  four racers and full lobbies, with real Driver window changes during play.

Playtest.6 mixed-platform profiles used 150/450/750 ms RTT, jitter, 1% wire loss
and spikes with Auto ON/window 20. Logged cumulative racing input waits were
0 / 35.862 / 58.091 / 0 ms. Those one-PC results do not automatically qualify
the later live-settings source or a physical Steam Deck/WAN session.

Playtest.7's local full-lobby baseline logged 0 ms racer input waits. Its live
stress deliberately used two- and six-frame windows under high latency and
logged roughly 33–36 seconds of waits per racer. That verifies safe live edits,
not zero-wait performance. The fresh-profile default remains 14, not 20.

## Remaining acceptance and known issues

Physical Steam Deck LCD Wi-Fi (Gaming/Desktop), separate machines/WAN, ARM GPU,
controller feel, audio and suspend/resume need actual-device checks. QEMU covers
CPU/data/helper behaviour; software-rendered Linux tests do not prove hardware
GPU frame rates. Simulation tick rate, rendered FPS and controller-to-screen
latency are separate measurements.

The reported missing vanilla online car textures and remote road wheels remain
open. Network waiting is reduced, not eliminated. Spectators target 45 frames of
initial confirmed buffering; loading/catch-up may add much more viewing delay.
Neither locked 60 FPS nor pause-free play at arbitrary latency is guaranteed.

Existing draft translations, experimental ray tracing/haptics and custom battle
map requirements remain. The earlier supplied-log investigation did not prove
the first corruption point without its matching dump. Open GitHub reports are
listed in the handoff without being closed automatically.

## Review and publication

The proposed tag is `Version1.0.15`. The signed updater must bind the exact
reviewed commit and finished package bytes using the existing trusted key.
The [v1.0.14 validation record](docs/history/RELEASE-VALIDATION-1.0.14.md) and
earlier release notes remain available. Final owner review precedes pushing,
tagging, uploading and publication.
