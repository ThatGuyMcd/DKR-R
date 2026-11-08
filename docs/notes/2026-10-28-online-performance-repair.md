# Online performance repair work

The full investigation and repair scope was approved on 28 October. Keep the
current launcher layout and every earlier online fix. Finish implementation and
regressions before making full Windows and Linux AppImage packages. Focused test
compilation is validation; release/test launcher packaging is the final stage.

The evidence and approved plan are saved outside the checkout at
`D:/DiddykongWorkFolder/online-performance-20261028/deep-dive/REPAIR-PLAN.md`.
The original failed-session logs were preserved there before a launcher restart
replaced the standard runtime.log. Do not use the new launcher-only log as the
failed session. This task must not modify submodules, generated guest code or
the frozen source capsule. Existing baselined integration remains in use; new
first-party helpers live in runtime-recomp. Both-owner review is still required
before merging compatibility changes. The approved ledger is for isolated tests.

Implementation:

- StartupTransport preserves per-owner live hints. One verified Network instance
  receives and acknowledges early input through the startup handoff without
  stepping the game before release. Loaded retries continue until Running;
  release retries require the current launch, checkpoint, save and player proof.
- Proxy forwarding has separate bounded turns for control, live input, repairs,
  spectator updates and checkpoints. Endpoint-only receipts are tied to the
  exact original encrypted payload. A proxy cannot manufacture them; a full
  receiving queue cannot acknowledge discarded data. Data-only failures and
  persistent send pressure trigger direct fallback even while probes succeed.
  Receipt requests are sampled at their bounded response rate so heavy traffic
  cannot evict proof history before a long round trip returns. A 6,000-packet,
  500 ms selected-route soak checks that a healthy proxy remains selected.
- Candidate selection requires eight successful samples, at most 10% probe loss
  and successful-sample p95 at most 600 ms. Timeouts count as loss, not measured
  RTT. One miss does not retire an established route; three consecutive misses
  can. Fallback retains hold-down timing. Existing lobby cards follow the route.
- Dynamic Input Delay reports blocked duration and actual-arrival age in every
  window, including a stationary frontier. Stale blocked input cannot look
  healthy and cause a reduction. Coordinated changes, manual mode, the guest
  clock and nine-frame cap stay. Sustained input recovery is separate.
- Mod downloads negotiate a 256 KiB to 2 MiB pre-game window. Verified drain rate
  and observed response time control its useful size and retry deadline. Short
  queued bursts are not sustained bandwidth. A gap backs it off. Sender, inbox,
  admission staging and SDK limits agree, with control headroom retained.
  Existing recipient bursts, consent, hashes, resumable files and cache reuse
  stay. Setup, payload and preparation timings remain separate.
  Windows are rounded to whole transport chunks and capped for the existing
  512-packet inbox. Retained 768-byte repair tests caught a lost alignment step
  before delivery; explicit 768/1131-byte loss and no-loss regressions cover it.
- Preparation uses up to four sequential items with shared verified ROM sources
  when disk space and available RAM permit. Low-memory or unknown platforms
  keep two. Worker limits stay; completed receipts survive a later failure.
- Track Lab retention reuses identical content or archives a differing identity
  or filename collision by content digest under online-variants, inactive.
  Existing courses are preserved. Repeat keeps reuse it; corruption is refused.
- Rendering detail logs have a separate eight-per-second wall-clock budget.
  Replay/cache invalidation, model fallback, guest-address and HUD/display-list
  checks, GPU fences and residency bounds stay. Input summaries run every five
  seconds, with a bounded 64-event failure ring. No inputs or keys are logged.

Regression evidence:

The packaged Linux immediate-close test also caught SIGSEGV in OpenSSL 3
certificate decoding on the updater thread after the window closed. The updater
was only asked to cancel; its global owner could outlive process TLS cleanup.
The debugger confirmed main was in OPENSSL_cleanup from exit handlers while
the updater still called curl_easy_perform. Store/account workers also remained
alive. An explicit main-thread process scope now retires the store HTTP workers,
online account owner and updater before main returns, retaining the store object
for normal teardown while the installation lock and log remain alive. Ordinary UI
cancel stays asynchronous. The exit-order diagnosis is supported by the captured
stack and [OpenSSL 3 cleanup rules](https://docs.openssl.org/3.0/man3/OPENSSL_init_crypto/).
Repeated immediate-close package tests cover the actual HTTP startup path.

The combined fixture uses production DirectSession, admission and Network with
a small deterministic world. Two, three and four racers all confirm the same
180 frames after delayed release and four seconds of input backpressure.
Separate traces use 150 ms RTT, up to 30 ms jitter and 1% live loss. This checks
transport and confirmation, not rendered game performance.

Production Upload/Download/ChunkInbox tests transfer and hash an 8 MiB payload:

| Simulated link | Previous window | Adaptive window |
|---|---:|---:|
| 10 Mbit/s, 20 ms RTT | 6.731 s | 6.731 s |
| 10 Mbit/s, 150 ms RTT | 8.297 s | 6.909 s |
| 10 Mbit/s, 500 ms RTT | 19.496 s | 7.734 s |

The 30 Mbit/s, 500 ms trace reaches 23.8 Mbit/s steady payload goodput. Two,
three and four recipients share one 10 Mbit/s uplink at 9.75, 9.87 and 9.70
Mbit/s steady aggregate goodput, with no false retries. Four each receive 8 MiB
in 27.344 seconds total. An injected missing chunk is repaired and fully hashed.
These exclude disk, importer, encryption and real internet costs.

The first shared-link test exposed false one-second retries under queued data.
After that fix, four-recipient completion spread was 3.3 seconds. Fairness now
uses a relative spread limit of one sixth of completion time, with a two-second
floor, rather than fixed two seconds at every speed. The separate 75% steady
utilization and zero-false-retry gates stay. Four recipients exceed the current
three active provisional uploads and provide an additional stress case.

Proxy regressions cover data-only loss, malicious substitution, send/receive
backpressure, poor candidates, consent withdrawal and the 127-repairs-plus-one-
live priority case. Native UDP/WebRTC tests exercise the extra leg with original
packet authentication. Existing party, spectator, profile, chat, menu and mod
checks must pass alongside guard/tooling, pre-commit and full host CMake/CTest.

Full launcher compilation starts only after implementation and pre-build checks
finish. Matching Windows x64 and Linux x86_64 AppImage packages are then audited
against the same source state, identities and helpers and checked for startup.
Package-specific results and build logs accompany the test builds. No stable
release, signed updater or website/API deployment is part of this task.

Everyone in a lobby needs the matching builds. Test stock with proxy off first,
then proxy on, the large mod selection, cold download and warm rejoin, spectator,
track changes, battles and return to launcher. Keep every machine's runtime log
if a wait recurs. Check useful input progress and actual route alongside ping.

Real remote Windows/Linux play, rendered frame-time percentiles, the original
upload links and Steam Deck Gaming Mode need the test machines. Injected network
tests cannot certify those conditions or guarantee no stutter through an outage.
