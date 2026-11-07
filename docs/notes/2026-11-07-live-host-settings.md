# Live lobby host settings

Private build: `1.0.15-beta.1-playtest.7`.

The host-settings page previously edited defaults for the next lobby. An active
host can now open the same page from the lobby or the in-game Online overlay,
edit a draft and press **Apply changes**.

Prediction window (2–20 frames), automatic/manual input delay (0–9 frames) and
new spectator admission apply to the current session. Capacity, menu ownership
and replay changes made during play are queued for the return to the lobby.
Queued changes can be cancelled. Development mode still requires a new lobby;
it changes consent and save ownership. A lobby created without spectator
capability must be recreated to enable spectators. Loading and return
transactions reject edits until the transition completes.

## Runtime behavior

- Existing authenticated lobby-rule updates carry timing changes. There is no
  new network message or mid-race launch-descriptor replacement.
- A bounded checkpoint pool can grow without copying existing images. A shrink
  retains outstanding speculative checkpoints and the correction watermark,
  including when a correction is already partway through replay.
- Previously admitted corrections can finish beyond the new smaller limit.
  Further prediction respects that limit. Confirmed input, effects, saves and
  guest clocks are preserved.
- Manual/automatic delay changes retain immutable authored input and queued
  physical button edges. Delay negotiation can restart without resetting the
  race or reusing stale negotiation sequence numbers.
- Closing spectator admission keeps existing viewers connected. New native
  joins, directory grants and advertised free spectator places follow the same
  host policy. No server deployment is needed.
- Host requests are scoped to the current room and checked on its owner thread.
  Clients cannot submit them. Unchanged Apply preserves Ready status.

The previous directory implementation remains unchanged. Its live-admission
integration is in the current checked online-performance policy, with explicit
compiler-binding assertions. No historical contract or pinned source was
repinned. Test-build policy authorization is retained; no merge or publication
is implied.

## Verification

- Windows host suite: 169/169 passed.
- Linux host suite: 168/169 on the Windows-mounted checkout. The regression
  harness hit its filesystem-dependent timeout; the same three input files,
  verified byte-for-byte, passed all 25 cases on Linux's native filesystem in
  4.394 seconds. This is not a claim that the mounted-filesystem run passed.
- Guard/tooling and pre-commit checks passed.
- Both production party fixtures passed 2,758,829 checks. New cases cover live
  authenticated rules, unchanged Apply, range/loading/client rejection, growing
  history, shrinking before correction and shrinking during partial correction.
- Both package source-chain audits, payload allowlists, archive readback and
  helper probes passed. Linux ELF dependency checks passed. Revision-specific
  compatibility IDs match across platforms.
- Four rendered Windows racer processes in a lobby without spectator
  capability passed the complete live-edit sequence. Each active driver applied
  `14 → 20 → 2 → 20 → 6 → 20`, all five host requests ran during play, manual and
  automatic delay switched, client requests were denied, and queued changes
  were cancelled. All four exited successfully without a rollback failure.
  This used 375 ms delay per receiving endpoint, ±25 ms jitter, periodic 200 ms
  spikes and selective disposable-message loss (1/97), giving approximately
  750 ms host/client application round trip. It is application delivery shaping,
  not an additional real wire-loss qualification.

- The rendered Windows full-lobby baseline passed with four racers and eight
  late-started spectators. All 12 processes exited successfully, with no
  rollback failures. Logged racing input-wait time was 0 ms on each racer;
  each spectator played 714–1,847 actual race frames. This was a local baseline
  with a 20-frame window and automatic delay, not a 750 ms network test or a
  spectator frame-rate/delay guarantee.

- The rendered full-lobby live-edit stress run also passed. Four racers and
  eight late-started spectators all exited successfully, without a rollback
  failure. Every racer applied the exact five-step window sequence above.
  Automatic/manual delay switched, client edits were refused, spectator
  admission closed/reopened, and the queued menu/replay edits were cancelled.
  Each viewer played 442–894 actual race frames. Racers used the same 375 ms
  receiving delay/jitter/spikes/loss settings as the four-racer check. The last
  viewer instead had 1,000 ms receiving delay and periodic 1,000 ms spikes.
  The deliberate two- and six-frame settings produced substantial waits:
  logged racing input-wait totals were 32.802, 35.511, 34.931 and 34.488 seconds
  for host/racers 1–3. This is explicitly not a zero-wait stress-test result.

All rendered checks above used the packaged Windows runtime on this machine.
Physical Steam Deck/controller and separate-machine acceptance remain for the
owner. Playtest.6 mixed-platform measurements do not automatically qualify this
changed build. Linux was rebuilt and package-checked with matching compatibility
IDs; this turn did not repeat the mixed-platform rendered qualification.

Reducing the window deliberately reduces latency coverage and can increase
input waits on a slow connection. The narrow-window stress run verifies safe
live changes; it is not a zero-wait performance qualification. The initial
default remains 14 frames.
