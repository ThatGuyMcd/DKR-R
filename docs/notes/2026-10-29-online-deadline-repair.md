# Online deadline repair

The user approved the SM64CoopDX comparison and implementation plan on 29 October.
The approved immediate work is the six repair stages, their qualification and
matching isolated Windows x64/Linux x86_64 AppImage test packages. Direct peer
fan-out remains conditional on measured host-hop cost. A state-authoritative
replacement needs its own design and approval. Preserve the launcher layout and
every retained repair. Both-owner compatibility review is still required before
merging; the existing ledger is authorized only for isolated tests.

The investigation and original reproductions are outside the checkout at
`D:/DiddykongWorkFolder/online-architecture-20261029/`. Work and evidence for this
implementation are at `D:/DiddykongWorkFolder/online-deadlines-20261029/`.

The initial new regression failed against the unchanged delay library. It covers
the impossible measured reduction at window 3. Added coverage includes every
window from 2 to 20, protective growth at a frozen frontier, scoped epoch
carry-over, controller lease reset, compact live-batch bounds and analog
coalescing with button-edge retention.

Implemented repairs:

- Delay increases affect only newly authored future input after agreement. They
  no longer require the stopped world to reach a distant effective frame.
  Reductions retain the coordinated future drain. Same-room/lease scene changes
  retain learned lead; old frames and epochs remain immutable.
- Rollback corrections no longer cancel proposals while acknowledgements are
  in flight. Only actual scene/player transitions hold negotiation. Presentation
  retirement does not prevent adaptation. This fixed the repeated waits in the
  300 ms, three/four-racer fixture without changing the prediction window.
- Input recovery identifies silent required owners. A healthy third player's
  route is not quarantined by an unrelated input stall. Candidate diagnostics
  explain measurement, quality and hold-down decisions.
- Party racing uses compact redundant live batches with eight consecutive final
  inputs. Exact holes and development commands keep their authority repair lane.
  Unsent batches merge only for the same target, owner, epoch and lease. Empty
  commands no longer transmit 64 zero bytes.
- Local confirmation is separate from host receipt. The failed outage fixture
  had discarded the client's only repair copy before the host received it.
  Explicit scoped receipts now govern retirement. Exact repairs use actual
  destination receipts and RTT-aware retry pacing, with bounded histories.
- Useful live batches are measured at their newest sample. Old repair does not
  inflate fresh-delivery lateness. Ordinary repair has a separate authenticated
  plaintext kind; the SDK command kind is preserved. These isolated test builds
  therefore require the matching compatibility ledger on every racer.
- Owner-local metrics distinguish admitted application bytes from transport
  overhead, and show delay negotiation/application, required-owner age and
  queued transition age. Input flight events use a local monotonic timestamp.
  Party traces include exact holes, queue age until SDK admission, actual
  receipt progress and frontier rate. Authored-to-host-receipt includes the
  acknowledgement trip; it is not one-way latency. SDK queue/retransmissions and
  IP/DTLS/SCTP overhead are excluded from the application counters. The existing
  opt-in CPU profiler is now reported in party sessions too.
- Mod preparation reuses validated catalogue metadata within one transaction.
  The cache expires at job exit; runtime loading still validates asset bytes.
  Exact immutable runtime hits use the canonical manifest, ROM hash, loader
  identity and version. Already validated resident bytes can be reused without
  rescanning their original files. Mutable game worlds stay separate. Misses
  retain deep validation and the existing memory budget. Existing damaged-cache
  repair and completed-output retention remain intact.

Completed qualification before packaging:

- Full host checks passed: 149 Linux tests and 148 Windows tests. Retained
  production checks cover memory, HUD/render handoff, water, spectators, scene
  release, offline retention, vehicles, profiles, portraits, chat and shutdown.
- Actual production ordinary and party paths confirm identical state under
  loss, jitter, reordering, backpressure, delayed release and outage recovery.
  These use synthetic RAM, not a rendered remote race. Watchers stay outside
  the racing input quorum.
- Two/three/four-racer party traces have zero input-wait time after the first
  12 seconds at both 150 ms RTT/roughly 1% live loss and 300 ms RTT/roughly 3%
  loss, with up to 20 ms jitter. All peers confirm 1,800 identical frames.
  A one-second outage recovers instead of losing the required repair input.
- Native Windows real-ROM qualification used 26 selected roots, including
  portable vehicles. Cold preparation of 25 roots took 29.9 seconds. A fully
  prepared repeat took 228 ms with no reconstruction. One new dependency took
  3.16 seconds with one reconstruction. Installed repeat reconstructed nothing;
  a fresh installed index reused catalogue metadata. Runtime preparation took
  2.32 seconds and immutable reuse took 74 ms. One damaged private cache rebuilt
  once. Cancellation kept complete outputs; the original library was unchanged.
- Linux real-ROM repeat/new/damaged/cancelled qualification passed too. The
  fixture accessed Windows files through WSL/NTFS, which made cold reads much
  slower. Those timings are not representative of a native Steam Deck disk.
- The existing checkpoint delta store is unchanged. An isolated 17.3 MB
  store/load measurement stayed below 3.3 ms save p99 with bounded allocation.
  Capture, hashing, guest ticks and rendering are excluded from that measurement.
- The source audit caught an initial edit to a pinned base that the final
  pipeline does not compile directly. Those base bytes were restored. The
  helper is a normal first-party module bound through the existing transitional
  pipeline. No new layer, pinned dependency change or guest rewrite was added.

Linux harness qualification initially timed out because Linux Git took 28.88
seconds to inspect this Windows checkout, exceeding a subprocess test's
20-second limit. Native Git returned the identical 68 status records in 1.02
seconds. Validation uses native Git only for metadata about that Windows
checkout; Linux repositories and tests retain Linux executables. No test or
guard was skipped.

Package-specific readback, matching identities, helper/startup/exit checks and
rendered-check evidence belong with the delivered artifacts. Real remote matches
and Steam Deck playtesting remain acceptance work. No universal crash-free or
stutter-free claim is made.

Direct peer fan-out remains the conditional later extension: the repaired
host-forwarded fixtures meet their current delivery target, and there is no
measured pairwise deployment topology justifying a new mesh in these packages.
Replacing rollback with a state-authoritative mode still needs separate approval.

Qualification must include actual production sources, both ordinary and party
paths, full host checks, retained memory/HUD/spectator/profile/chat regressions,
and package startup/exit. Synthetic-world impairment results are not evidence of
rendered remote Internet performance. Do not package while a qualification gate
is failing, or claim real remote testing that has not happened.
