# Vanilla online Track Select, lobby progress, Dynamic Input Delay and battle fullscreen

Implementation update: the approved work is packaged as isolated test builds.
See [the implementation and test handoff](2026-10-27-online-stability-test-builds.md)
for completed checks and the remaining multiplayer/GPU acceptance. The evidence
below records the state before these changes.

27 October 2026. Investigation and proposed work only. The separate profile
refresh fix is packaged for testing; the four changes below are not implemented.
The newly confirmed vanilla Track Select failure takes priority.

## What is ready to test

Matching Windows x64 and Linux x86_64 AppImage packages are in
`D:/DiddykongWorkFolder/DKR-R-Lobby-Profiles-Test-20261027`. The AppImage is also
the Steam Deck build. No API or website deployment is needed for that fix.

The package readback verified all 57 Windows and 252 Linux payload files. Only
the game runtime differs from the previous v1.0.12 package on either platform.
Both retain the same network compatibility IDs. The sealed release, GitHub tag
and signed update files have not been replaced.

Validation includes 88 Windows and 105 Linux profile checks, 24 native directory
lifecycle checks on each platform, 105 local Worker/D1 profile checks, the full
140-test host baseline and the final five relevant tests after rebuilding.
Linux ELF inspection found 99 ELF files and no unresolved non-system dependencies.
Guard/tooling and pre-commit passed. No live multiplayer/GPU acceptance was run.

## Evidence from the current build

I checked the final compiler source bindings as well as the authored sources.
Some first-party code still comes from the verified source capsule through the
existing Patch Pipeline. A root-file edit alone would not necessarily reach a
release binary.

### Vanilla Track Select rejects a valid input notification

The supplied screenshot identifies `Replay-safe tick failed:
dkr_legacy_track_menu`. The now-readable local v1.0.12
`runtime-previous.log` supplies the missing detail:

```text
[lobby-start][bootstrap] ... players=3 resources=stock ...
[rollback][menu-refused] event=24 full-scenes=1 offline=1 canonical=1
component tick blocked: dkr_legacy_track_menu ...
  mode_menu
  menu_loop
  menu_track_select_loop
```

The session's admitted resource identity is all zeros. The launcher also logs
locally installed tracks and texture packs; those startup listings do not mean
they were admitted as online gameplay resources. This log proves the stock
session path failed, not that the machine had no mod files anywhere on disk.

- `scripts/stage_magic_code_payload.py:214` defines Track Select option hooks
  through event 25: 24/25 wrap `trackmenu_input` entry/exit. Its wrapper at
  lines 248–263 executes those notifications even when the code hook is inactive.
  Turning Magic Codes off therefore does not remove these normal hook calls.
- `patches/online-performance/native/menu_notifications.h:5` admits only
  0–23 and 100–105 in the full-scene no-owner fallback. Events 24/25 are absent.
- The final compiled `probe_native.c` uses that exact helper and calls
  `dkr_probe_block` after refusing the event. Both revision targets use it.
- The final `probe_driver.cpp` creates `ModWorld` only when admitted mod
  resources exist. Its dispatch at line 1310 returns no handler without that
  world. With an admitted world, the menu dispatch runs first and returns
  before the restrictive fallback. The compiled menu adapter handles paired
  option notifications, including 24/25. This explains why active mods can
  mask the stock-only failure; it is not a requirement for vanilla play.
- External diagnostic fixtures extracted the unchanged final dispatch function
  for revisions 77 and 80. In both, 23 succeeds without an owner, 24/25 are
  refused, and an admitted-owner stub takes precedence. Unknown events 26,
  99 and 106 remain refused. These are dispatch-level reproductions, not live
  multiplayer acceptance or a reproduction of every menu transition.
- The existing revision 77/80 menu checks each pass 514 checks because their
  expected set in `patches/online-performance/tests/menu_fixture_main.c:8`
  incorrectly requires 24/25 to fail. They verify the old list against itself
  rather than the complete current producer surface.

The direct cause of this logged stop is established. The safe correction is to
admit the two reviewed no-owner notifications under the existing full-scene and
canonical checks, retain owned dispatch precedence and keep unknown calls
blocked. Disabling the replay fence or creating a dummy mod world would address
the wrong boundary. The modal title "Preparing Custom Game" does not establish
that the failing session contains mods.

### Joining progress is host-local

- `runtime-recomp/src/game/mods/online_mod_sync.cpp:117` owns progress in
  `ModSync::host_loop`. Progress reports update `state_.peers` on the host.
- `runtime-recomp/src/game/netplay/online_mod_admission.cpp:37` permits mod packets
  only on approved provisional admission routes. An already-admitted client is
  deliberately not another mod-transfer recipient.
- `runtime-recomp/src/game/runtime_online_ui.inl:3255` calls the joining gate only
  when `host` is true. Its progress bar reads the host's local mod-sync snapshot.
- These restrictions remain in the final generated mod-sync source and launcher
  UI. There is no shared joining-progress snapshot in the current lobby protocol.

This is missing status replication, rather than evidence that other clients'
downloads are broken. Forwarding provisional mod packets to admitted clients
would widen the wrong boundary.

### Automatic delay stops at launch

- `runtime-recomp/src/game/netplay/direct_session.cpp:6233` explicitly freezes
  delay into the launch descriptor. Its recalculation requires no descriptor.
- `patches/spectators/native/link_service.inl:20` recalculates party delay only
  before `party->launch`; the accepted start repair also excludes countdown and
  launch transactions. The final generated source retains those conditions.
- `patches/spectators/native/party_runtime.cpp:589` authors future input using
  the launch's fixed `input_delay_frames` throughout play.
- `runtime-recomp/src/game/netplay/netplay_types.cpp:30` includes that delay in
  the launch descriptor hash. It is not a freely mutable live setting.
- `runtime-recomp/src/game/netplay/experimental_latency.hpp:9` maps RTT and the
  prediction window to 1–9 delay frames at 30 logic ticks per second. Party
  estimation also applies a 100 ms RTT floor. In a policy probe, a two-frame
  prediction window and a measured 20 ms RTT produce one frame from the policy
  directly, but four frames with that party floor. This does not establish the
  right setting for a particular live connection or other window sizes.
- `runtime-recomp/src/game/netplay/latency_metrics.hpp:21` uses a lifetime
  histogram. It is useful for diagnostics but lacks the aging needed for a
  responsive live controller.

The reported fixed-delay behaviour is confirmed. Merely renaming the switch or
assigning a different integer during a race would be unsafe and incomplete.

### Fullscreen explicitly excludes challenge modes

- `runtime-recomp/src/game/online_view_policy.hpp:38` accepts view events only
  when the race type is 0 or 3, in addition to the other eligibility checks.
- The event producer records the real race type and separate shared-scene veto
  flags in `runtime-recomp/tests/replay_probe/probe_presentation.c:125`.
- `runtime-recomp/src/game/modern_camera_policy.hpp:29` identifies challenge
  types by bit `0x40`. An external read-only C++ probe of the actual policy
  accepted types 0/3 and rejected 64/65/66/67 for 2, 3 and 4 players: 18 cases.
- The policy header matches the one used by the compiled source capsule. The
  compiled RT64 view pass still selects `Scenario::Race` for HUD anchors at
  its line 125. The project already has Battle, Treasure and Eggs HUD scenarios.
- The compiled pass additionally preserves Camera++ world-end marker handling
  from `patches/camera-plus/integration_pipeline.py:172`. That must stay intact.

The eligibility rejection is proven without running a game. Per-course camera
and HUD event coverage still needs acceptance in actual battles/minigames.

## Proposed order

### 0. Restore stock-only online Track Select first

Correct the existing reviewed notification policy for 24/25 through the
accepted integration. Do not blanket-accept arbitrary event numbers, bypass
native ABI checks, fabricate resources or touch protected generated outputs.
Keep the vehicle picker and paired custom-track option scopes intact.

Replace the stale expected range with a producer-aware regression: enumerate
the ordinary menu notifications, the four option-hook pairs from the current
`TRACK_OPTIONS` mapping, and the reviewed vehicle notifications. Check the final
composed dispatch for both revisions with and without an owner. Verify 24/25
return the neutral result without reading fields or changing guest registers
when no custom menu exists; unknown events and invalid service modes must still
fail. Include an actual wrapped `trackmenu_input` call path so a producer adding
a new pair cannot silently outrun the fallback list again.

Then run the full online stock path: boot, Character Select, Track Select,
browse and confirm stock courses, race, results, rematch and return to Track
Select. Test completely empty mod directories separately from installed-but-
disabled mods, then characters-only, vehicles-only, codes-only and mixed mods.
Cover both ROM revisions, 2/3/4 racers, public/private joins, spectator-enabled
and spectator-disabled rooms, a watching client, Windows/Linux and Steam Deck.
Record admitted resource identity and menu event on any failure.

This is a prerequisite for accepting the broader online work. The already
packaged profile builds do not include this newly investigated correction.
Keep them clearly labelled for profile testing. Review the existing engine
compatibility ledger before producing the vanilla fix builds; if it changes
cross-play behaviour, follow the two-owner identity rule rather than retaining
a misleading old identity or inventing the pending fold path.

### 1. Check the supplied profile builds

Use the matching builds on the host and clients. Check public and private-code
joins, including a quick join immediately after remembered Discord sign-in.
Use accounts that are not already friends. Verify client-to-host and
client-to-client pictures, requests and acceptance. Turning picture sharing off
must leave initials and a working friend action. Existing lobby close and leave
cleanup must still work.

Keep this result separate from the new networking work so that its test outcome
is easy to identify.

### 2. Share joining progress with admitted lobby members

Create a small, host-authored joining-status snapshot. Reuse the existing mod
progress as its source. Send the snapshot over the authenticated lobby control
transport to admitted racers and spectators; the website is not needed.

Each entry should contain the current lobby/epoch, a join-attempt identifier,
revision, bounded display name, stage, received bytes and total bytes. Stages
should cover approval/consent, queued download, downloading, preparing/verifying,
verified/entering, cancelled and failed. Cached preparation should be represented
honestly. A download reaching 100% must not imply that preparation has finished.

Publish on stage changes and at a capped rate during a transfer, initially
around twice a second. Use bounded snapshots, coalesce intermediate updates and
send a full snapshot to a newly admitted member. Do not send a message per chunk.
Never broadcast file paths, credentials, capabilities, raw mod payloads or
unbounded client-provided stage text.

The host and clients should render the same status model. Add a compact
"Joining players" area using the existing card/progress styling. Clients get
read-only rows; approval, decline and blocking remain with the host. Existing
admission proof remains the only way to join, regardless of reported progress.

Scope everything to the current room and attempt. Ignore stale or reordered
updates. Remove an entry when its real player card appears, and retire it on
cancel, failure, departure, host close or rehosting. Do not make joining clients
part of the racing roster before admission.

Test multiple joins, queued transfers, cache hits, cancellation, reconnects,
late status packets and a host closing/reopening the same launcher. Verify that
an existing client sees the same stages as the host without gaining host actions.

### 3. Extend fullscreen to active battles/minigames

Use a checked scene classification that includes supported active challenge
modes. Keep the complete-view mask, owner, layout, game-mode, address validation
and shared-scene vetoes. Menus, cutscenes, Track Select, results and transitions
must retain their intended shared presentation.

Carry the correct Battle/Treasure/Eggs HUD scenario into the render-only focus
pass. Preserve challenge counters, portraits, player health/bananas, shared
announcements and results. Keep native camera selection, camera orientation,
aspect/projection scopes, scissor restoration, the three-player extra viewport
and the Camera++ world-end marker handling intact. Do not change guest physics,
controller ownership or camera logic to obtain fullscreen.

Add direct policy tests for supported/unsupported scene types and negative
tests for shared screens and incomplete event sets. Check the render event
sequence in real courses before declaring the HUD work complete. Test every
stock minigame, 2/3/4 players, each local port, both ROM revisions, normal split
view, fullscreen and spectator camera modes. Include Deck 16:10 and widescreen.

### 4. Replace Automatic Input Delay with a real live controller

This is the largest part. Its objective is the lowest delay that keeps the
current racing connections stable, including changes during gameplay.

First separate the causes of a blocked frame. Count network input waits and
prediction-limit waits separately from presentation/resource waits and CPU
simulation/replay cost. Reuse existing input echo/consumption, queue, rollback,
prediction and party performance diagnostics. Add bounded recent windows for
arrival slack, delay/jitter tails, loss bursts and backlog. Use local timing and
acknowledgements; do not assume synchronized clocks or infer exact one-way
latency from half an RTT.

One host-owned controller should consider required racing routes. Spectator
download speed must not raise racing delay; a spectator host still coordinates
the decision. Feed both the ordinary and party runtime adapters from the same
policy so one path does not remain frozen.

The initial policy should react upward promptly to sustained shrinking
headroom, and reduce one frame at a time only after a stable period. Add
hysteresis and a minimum interval between decisions. Tune those timings with
repeatable traces rather than claiming arbitrary values are optimal. Evaluate
zero added frames on routes where prediction safely covers delivery; otherwise
select the lowest validated stable value. Keep the existing bounded delay range
and prediction/checkpoint memory budgets initially. Do not silently enlarge the
rollback window or remove safety limits to hide a poor connection.

Prototype the live input scheduler before connecting the setting to it.
Already-authored inputs and committed frames must remain immutable. Increasing
lead must not leave unassigned input frames; decreasing it must not renumber or
discard queued inputs. Specify how held controller states and sampled button
edges cross a change, and prove the mapping in a deterministic harness.

Negotiate support among racing peers. Use a versioned, host-authored delay
schedule with room, scene/control generation, sequence and an agreed future
activation frame. Updates need idempotent proposal/acknowledgement/commit and
recovery rules. An unacknowledged proposal must retain the last committed policy
rather than pause the match to change delay. Do not mutate the immutable launch
descriptor in place. Preserve/reconstruct the relevant policy at scene changes,
player swaps and confirmed spectator baselines.

Freeze policy transitions during scene agreement, restore, seat transfer or
match retirement; resume evaluation during active gameplay. Do not alter the
30 Hz simulation clock or stretch the game's ticks to disguise a stall.

Replace the current switch and its explanatory text with "Dynamic Input Delay".
Keep manual delay when it is off. Migrate the existing preference deliberately.
Show a small live value such as "2 frames (67 ms added delay)" and a plain reason
when it changes. Handle unsupported older hosts/peers explicitly; do not label
static behaviour as Dynamic. Prefer negotiated compatibility. If a compatibility
identity change is necessary, AGENTS.md requires both owners' approval. Its
future `netplay/legacy_identity.hpp` path is not present in this checkout, so the
approved identity integration must be resolved rather than inventing a stand-in
or splicing a digest into another string.

## What “no stutter” can mean

Dynamic delay cannot predict the first unexpected packet blackout or compensate
for an overloaded CPU/GPU. The current rollback driver deliberately stops new
prediction when its bounded window is exhausted. Removing that protection would
risk correctness and memory stability.

The target is no stalls caused by delay transitions, no input/state corruption,
and stable play with the lowest delay inside a published, tested network
envelope. Severe outages must recover or stop safely. Input-delay tradeoffs and
prediction-limit stalls are also described by the primary
[GGPO developer guide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md)
and [Libretro netplay implementation documentation](https://docs.libretro.com/development/retroarch/netplay/).
These explain the general constraints; DKR-R's own scheduler remains the design
and test target.

## Evidence required before release

- Stock-only online Track Select must pass the complete route above without
  enabling any track mod. The existing 514-check menu fixtures are insufficient
  until their stale rejection expectation is corrected and producer coverage is
  added. Retain the safe-stop behaviour for genuinely unowned service calls.
- Extend the existing equal-30-Hz paced input harness and the actual party
  runtime fixtures. A policy-function test alone is insufficient. The current
  ExperimentalRollback, ExperimentalSession and NetplayInputLatency baselines
  were rerun and all three passed; they do not prove live renegotiation.
- Compare current automatic delay, fixed manual delays and the new controller
  against the same latency traces. Include stable LAN/WAN, asymmetric routes,
  latency rising and recovering mid-race, jitter, reordering, loss, constrained
  host upload, spectator checkpoint traffic and temporary CPU hitches.
- Run 2–4 racers with and without spectators, a spectator host, live watcher
  joins, rematches, swaps and scene transitions. Cover vanilla and representative
  mod-heavy sessions on Windows/Linux/Deck and both ROM revisions.
- Prove identical confirmed inputs/state, bounded queues/history/memory, no
  changes to authored input frames, no duplicated/lost already-sampled edges,
  no control chatter, and no starvation from status/spectator traffic.
- Report p95/p99 added input latency, blocked frames by reason, render frame
  times, rollback/replay cost, delay changes and recovery time. A lower delay
  number alone is not a pass. Compare stable intervals with the best stable
  manual result for those intervals, and measure transition cost separately.
- Agree the normal network envelope from the measurements and require zero
  delay-transition-induced stalls. Explicit blackout tests require safe bounded
  recovery, not an impossible guarantee of uninterrupted new remote input.
- Run the required guard/tooling, installed pre-commit hooks, host builds/CTest
  and matching Windows/Linux builds through the existing Patch Pipeline. Keep
  protected dependencies and generated guest outputs untouched by hand.

Deliver each stage as a clearly identified test build with its changelog and
diagnostics. Prepare release/update metadata only after the changes are approved
and the multiplayer acceptance results are satisfactory.
