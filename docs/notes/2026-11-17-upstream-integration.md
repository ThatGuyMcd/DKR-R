# Contributor sync and v1.0.16 playtest

The integration starts from upstream `adfd02e377d3eba6736a40682d5e3e95422b3560`,
including the 131 commits after the public v1.0.15 tag. The earlier local work
was based on `f1c0c3581e517c720499b15444a0139dc6ea1b70` and an uncommitted tree.
Both are retained in the recovery bundle, along with an exact authored-file
archive. Restoring that backup reproduced all 3,071 recorded file hashes.

## How the work was combined

Upstream is the base. None of its commits were reverted or replaced with an
older release layer. Our additional changes live in the maintained first-party
modules, with the old networking changes ported by behavior rather than by
copying an old DirectSession over the new one.

- Keep upstream's cross-architecture manifest matching, state checks, named
  guest hooks, symbol tables and GuestCallContext conversions.
- Keep its relay owner/glue, reliable checkpoint path, deferred session logs,
  close/wakeup fixes, TLS changes, settings codecs and Track Lab work.
- Add authenticated direct racer inputs, consented waiting-lobby host handover,
  cached-mod promotion and transfer of verified session state. Preserve the
  local architecture, original save owner and existing controller leases.
- Version the optional session-links capability. Older local playtests retain
  ordinary host delivery and cannot begin the newer handover.
- Keep usable immutable pictures during ordinary correction, without relaxing
  scene/resource retirement or the four-image memory limit. Keep replay-cost
  protection in Auto delay and use forward progress for late-input samples.
- Keep the existing launcher layout. The rejected resolution-dependent redesign
  has not been restored.
- Keep the maintained Wayland wrapper and architecture/closure checks. The old
  helper and old first-party build layers have not been restored.

The shipped v1.0.15 rollback identities remain unchanged. The compatibility
reason is recorded by `scripts/identity_guard.py` in the ordinary input record.
New optional control extensions do not change the simulation input format;
default menu ownership is unchanged. A transferred menu owner is agreed by all
capable members before a new owned world is constructed.

## Verification and build notes

The combined Windows host run passed all 214 tests. The final Linux host preset
also configured, built and passed all 214 tests after resuming the paused build.
The installed pre-commit checks passed for the integration changes. A separate native
four-racer/eight-spectator test passed sequential handovers to a racer and a
spectator, including mixed-architecture metadata, preserved seats/save owner,
and a synchronized start. Its impaired run added 375 ms one-way delay, 25 ms
jitter, 200 ms spikes and loss of every 97th disposable packet, and also passed.

Local directory tests passed 95 ordinary-lobby and 101 running-spectator checks.
The native/profile integration passed 107 checks with synthetic identities.
These tests used no deployed service or real player credentials.

Windows portability fixes retain the checks: use extended paths for deep cache
fixtures, keep relative executable tests on the interpreter's drive, normalize
schema paths, compare review text with universal newlines, encode the iOS bundle
entry point's executable mode explicitly, and use native paths for the local
esbuild and CMake command construction. CMake warning checks ignore line wraps
caused by different checkout and temporary-directory lengths.

The clean Linux baseline and the first combined Linux run both exposed the
same regression-harness timeout. Git status on the NTFS source checkout took
23.14 seconds; the test's outer timeout is 20 seconds. Linux verification moves
to a normal Linux checkout instead of increasing or disabling that deadline.

Both Release builds completed. Windows passed all 243 Release tests, and Linux
passed all 243 in a normal Linux checkout. The prepared-input lock check passed
all ten inputs on both systems against the unchanged upstream lock. The private
generator suite also passed all 53 tests. The cached-mod handover fixture passed
37 checks using the real mod worker, including a fresh download from the promoted
host and rejection of a damaged cache.

The actual Windows game was tested with twelve separate processes: four racers
and eight spectators. All twelve completed host handover and reached the race.
The ordinary run recorded no racing input waits. The impaired run injected
375 ms of outgoing delivery delay, 25 ms jitter, 200 ms spikes and loss of every
97th disposable packet. The last spectator also had 1,000 ms delay and spikes.
All twelve completed this run too. Each racer recorded 2.38 to 2.66 seconds of
input waiting in the first two race pacing samples, then zero across the next
23 samples. This is injected application delivery delay on one Windows PC,
not twelve physical machines or a measured intercontinental connection.

An earlier 90-second run ended while the spectators were still receiving the
new scene checkpoint. Diagnostics confirmed that delivery was progressing.
The complete 180-second run passed without changing the admission protocol or
increasing its background bandwidth allowance. That allowance protects racing
traffic, but eight spectators can still take a long time to receive a new scene.
This remains a known limit; passing this test does not mean instant spectator
scene loading or a guaranteed frame rate under every network condition.

## Testing packages

The Windows ZIP and x86_64 AppImage are built as `1.0.16-playtest.1`. Their
archive readback checks match every staged file, with no missing or changed
payloads. The package scans passed the version, feature, frozen identity and
private-data checks. Receipts, checksums and test logs stay outside the source
tree in the integration workspace.

The packaged Windows runtime passed its install-layout check. The actual
AppImage extracted and ran its settings round-trip check successfully. The
Linux library audit inspected 100 ELF files and found no missing dependencies
outside the documented host-library policy. The AppDir closure check also
passed, including the file picker that deliberately uses host GTK libraries.
This AppImage requires glibc 2.38 or newer; its C++ runtime needs GLIBCXX 3.4.32.
These checks do not replace testing the package on a physical Steam Deck.

## Local checkout and recovery

The combined branch in `C:/DKRPort` is `codex/upstream-integrated-v1016`.
The original branch is retained as `codex/network-host-handover`, with an extra
preservation ref and a named stash for its uncommitted files. The recovery
bundle and exact file archive remain outside the repository under
`D:/DiddykongWorkFolder/DKR-R-Upstream-Integration-20261117/recovery/`.
All 3,071 recorded file hashes and the original status were checked again
before switching branches; none had changed.

Old dependency and build caches are preserved under
`C:/DKRPort-pre-integration-20261117/`. The current `extern` directory is a local
junction to the verified pinned checkouts in the integration workspace on D:.
The verified Release build trees are also on D:, rather than reusing the old
build caches. Neither path is a source or release-package dependency for other
users: fresh checkouts use the maintained bootstrap and prepare scripts.

Nothing has been pushed, published, signed for the updater or deployed. Public
directory ownership transfer remains capability-gated until its matching
service update is deliberately deployed. Physical Steam Deck acceptance is
separate from local Windows/WSL functional testing.
