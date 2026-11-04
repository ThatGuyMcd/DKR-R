# Online stability test builds — 27 October 2026

These are matching Windows x64 and Linux x86_64 AppImage test builds based on
v1.0.12. The AppImage is also the Steam Deck build. This is a test handoff, not a
replacement GitHub release or an automatic update.

## What changed

Vanilla Track Select comes first. The real `trackmenu_input` wrapper emits
notifications 24 and 25 even when there is no custom track menu. The fallback
accepted only up to 23. It now accepts that reviewed pair without touching guest
registers or fabricating a custom resource owner. Unknown events and invalid
service modes still fail safely. Both ROM revisions have producer-aware tests.

The client profile fix is included. Picture and friend-link publication can
finish inside a lobby, retry failed saves and refresh renewed friend links.
Account pictures and sharing preferences are retained.

Admitted clients and spectators receive a small authenticated snapshot of
joining players. It uses the host's real mod-transfer worker and admission
state, even when the host is viewing another tab. Approval, review, queue,
download, preparation and entry are separate stages. Transfer bytes are shown
only during download. A peer claiming verification cannot bypass native
admission. Terminal attempts disappear; stale snapshots expire. Clients gain
no approval controls. Paths, credentials and mod contents are not broadcast.

Dynamic Input Delay replaces the visible Automatic setting and reuses its saved
preference. Both runtime paths use the same controller. It monitors recent
delivery and input waits, raises lead when racing connections need more
headroom and reduces it one frame at a time after stable periods. Spectators do
not vote on delay or inflate it with their download speed. A spectator host can
still coordinate the racers.

Changes use scoped proposal, acknowledgement and commit messages with a future
activation frame. Published inputs stay immutable. Held states fill an increased
lead; sampled press/release transitions survive a reduction. Manual mode keeps
its original scheduler. Scene agreement, rollback correction and seat changes
hold policy changes. The launch descriptor, 30 Hz game clock, rollback window
and memory limits are unchanged. The lobby shows the current added delay and
whether support has been negotiated.

Fullscreen focus accepts the real stock Battle, Treasure and Eggs race types
64, 65 and 66, with the corresponding HUD scenario. Shared-screen vetoes,
complete viewport coverage, owner checks, the three-player T.T. camera and
Camera++ markers remain in place. Unknown challenge types remain excluded.

## Integration and review

New code is in `runtime-recomp/src/game/online_stability/`, directly compiled
through `runtime-recomp/cmake/OnlineStability.cmake`. Existing baselined Patch
Pipeline integrations connect the frozen runtime during the source fold. No
new first-party patch layer was created. Protected dependencies and generated
guest outputs were not edited by hand.

The owner explicitly authorised the existing compatibility ledger for isolated
test builds. These builds deliberately have new peer identities. Everyone in a
test lobby must use the matching packages. Both-maintainer review is still
required before merging; no merge or push is authorised by this test handoff.

No website, Worker or database deployment is required. The existing deployed
profile API is sufficient. The sealed v1.0.12 release and signed updater files
are kept separate.

## Checks and limits

The host suite passed 141 tests. The final menu fixtures passed 520 checks per
ROM revision. The shared-status regression uses real authenticated localhost
DirectSession transports, rather than only testing a packet codec. The delay
tests also exercise the actual owned Timeline and party runtime with synthetic
state. Detailed final results accompany the packages.

Both native runtimes and the retained online, spectator, offline-copy, memory,
rendering, water and vehicle fixtures passed. The actual owned Timeline ran
1,800 confirmed ticks, reduced added delay from four frames to zero and retained
82 sampled button presses. Two party-runtime traces confirmed 1,800 ticks on
both racers: the 250 ms RTT excursion stayed within its existing headroom;
the 500 ms excursion raised delay before returning towards a lower setting.
Published input samples stayed immutable. The controller codec tests also cover
lost/reordered control messages, unsupported peers, manual mode and scene/control
generation resets.

The 105 local API/profile checks, guard/tooling and installed pre-commit checks
passed. Both packaged archives were read back against their payload lists. Only
the runtime executable differs from the prior package on each platform. Their
two ROM-specific peer identities match. Linux ELF inspection found no unresolved
non-system dependencies, and both packaged ModWorker handshakes passed.

These checks do not execute a ROM, GPU or a live distributed multiplayer match.
They cannot establish a zero-stutter network envelope. Unexpected outages or
an overloaded CPU/GPU can still stall; existing bounded safety stops remain.

## Test this together

1. Use empty mod folders, then installed-but-disabled mods. Enter stock Track
   Select, browse courses, race, finish and rematch. Repeat with spectators on
   and off, then try the usual modded lobby.
2. In public and private lobbies, check pictures and Add Friend as a client,
   including another client. Try a fast join after remembered Discord sign-in
   and a player with picture sharing off.
3. Keep one client in the lobby while a third player joins. Check queued,
   downloading and preparing stages, cache hits, cancellation, admission and
   host close/rehost cleanup. Repeat while the host is on another tab.
4. Try Dynamic and manual delay on the same connection. Record the live value,
   `[online][dynamic-delay]` changes and existing party/input diagnostics. Check
   normal racing, changes in connection quality, rematches and spectator seat
   swaps. Report the runtime logs from every racing machine.
5. Check fullscreen and normal split view in Battle, Treasure and Eggs, with
   2, 3 and 4 racers. Check each local player, counters, shared announcements,
   results and spectator cameras. Include Steam Deck's 16:10 screen.

Keep the full multiplayer/GPU acceptance separate from the automated results.
