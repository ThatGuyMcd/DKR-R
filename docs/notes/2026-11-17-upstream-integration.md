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

The initial combined Windows host run passed all 214 tests. A separate native
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
esbuild and CMake command construction.

The clean Linux baseline and the first combined Linux run both exposed the
same regression-harness timeout. Git status on the NTFS source checkout took
23.14 seconds; the test's outer timeout is 20 seconds. Linux verification moves
to a normal Linux checkout instead of increasing or disabling that deadline.

Windows and Linux game builds, prepared-input verification, actual-game tests
and final package scans are still being completed. This note will be updated
with their results before the testing packages are handed over.

Nothing has been pushed, published, signed for the updater or deployed. Public
directory ownership transfer remains capability-gated until its matching
service update is deliberately deployed. Physical Steam Deck acceptance is
separate from local Windows/WSL functional testing.
