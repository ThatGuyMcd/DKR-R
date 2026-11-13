# Optional client relays — investigation and implementation proposal

27 October 2026. Investigation of the current working tree and the final
Windows/Linux online stability test sources. No gameplay, transport, API or
launcher code was changed for this investigation.

## Recommendation

Client relaying is feasible and worth a controlled experiment. Start with an
optional, measured, one-hop relay for existing host/client connections. Keep the
original host responsible for admission, input validation, scene agreement,
checkpoints, saves and membership. Every racer continues to run rollback locally.

The aim is better packet delivery and lower required input delay. Equalising ping
by slowing down nearby players would work against that aim. Internet routing can
make an indirect path faster or steadier than the default route, but a relay
cannot remove the physical delay between continents. The RON research
demonstrated that measured application-level routing through another node can
improve some paths, often with one intermediate node. Its historical results
establish feasibility, not an expected improvement for DKR-R's players.
[RON, Andersen et al.](https://www.angio.net/res/ron-sosp2001.html)

There are no pairwise route measurements from the affected players in this
investigation. It establishes where the feature could fit and which invariants
it must preserve. It does not establish that a useful proxy exists in a specific
lobby.

## What the current code actually does

| Evidence | Finding and consequence |
| --- | --- |
| `runtime-recomp/src/game/netplay/session_transport.hpp:55` | Packet-preserving transport API with nonblocking send/receive, traffic classes, logical route retention and departure events. This is the best boundary for an adapter. |
| `runtime-recomp/src/game/netplay/experimental_owner_inputs.cpp:120` and the compiled SDK2 capsule implementation at line 133 | Ordinary clients accept owner input from the host. ACKs, repairs and bounded history also assume that topology. Direct client-to-client input would require a deliberate redesign. |
| `patches/spectators/native/party_runtime.cpp:259` | The host validates member, controller port, lease, epoch and immutable input, then forwards it. Clients accept these messages from the host. The final composed runtime retains this behaviour. |
| Final `generated/online-directory/direct_session.cpp:5819` | Key selection binds an authenticated member to its expected source address. A raw forwarded packet from another client's address is rejected. A relay needs a validated logical route, not a relaxed source check. |
| `runtime-recomp/src/game/netplay/secure_channel.cpp:219` | Sender, sequence and match are authenticated with the encrypted packet. An opaque relay can preserve this entire packet. |
| Final `generated/online-directory/direct_session.cpp:5849` | Replay rejection is per sender and traffic lane. Duplicate packets during a route handover must retain this single logical acceptance history. |
| Final `generated/online-performance/DKRNetplayCore/quick_join_transport.cpp:1232` | New incoming PeerJS offers are accepted only by the host. Client-to-proxy links do not exist just because WebRTC supports them. |
| Same final Quick Join source at lines 873 and 988 | ICE is configured with STUN; the current configuration does not provide TURN. Racing input/repair and bulk traffic use separate channels. Relaying must preserve those classes on both legs. |
| `runtime-recomp/src/game/online_stability/owner_service.inl:14` | Current route metrics describe host/client links. The host cannot discover an advantageous client-to-client route from these metrics alone. |
| `runtime-recomp/src/game/online_stability/live_delay.cpp:141` | Dynamic delay considers the required racers' recent delivery reports together. Improving one path may not lower the shared delay if another remains the limiting path. |
| `runtime-recomp/src/game/netplay/netplay_types.hpp:144` | A `relay_only` field exists, but the first-party search finds only its declaration and protocol encode/decode uses. It does not implement peer relaying and should not be repurposed without defining compatibility and semantics. |

The final generated paths above are build outputs inspected read-only at
`D:/DiddykongWorkFolder/custom-magic-codes-20261015/windows-build/`.
Protected generated code and dependencies were not edited.

## Relays, a mesh and multiple hosts are different changes

The proposed first version routes `Client A <-> Proxy P <-> Host` when that
complete route outperforms `Client A <-> Host`. Other clients can stay direct.
An available link is a candidate, not an instruction to forward everything
through it. With only two participants there is no other client to use as a
proxy, so the feature remains direct.

For illustration only, suppose a direct round trip takes 220 ms. The two proxy
legs take 40 and 70 ms, and forwarding adds 4 ms across the round trip. A 114 ms
proxy route could help. If the direct round trip is already 80 ms, that same
proxy would hurt. Real selection must probe the complete tunnel; summing
separately measured percentile RTTs does not establish its actual tail latency.

A true input mesh also allows A's original input to reach Client B without
passing through the host. That can help when A and B are close together and the
host is far away, even when neither has a better indirect route to the host.
The first relay version does not remove that host detour or the host's fan-out
work. A mesh needs authenticated input ownership at every receiving peer,
different receipts/repair responsibilities and changes to both runtime paths.

Multiple machines independently authoring saves, starts, scenes or membership
would introduce a much larger authority problem. Nothing in the recommended
relay requires that change. This is also separate from host migration: closing
the original host still closes the lobby. A racing member leaving still follows
the existing match-stop policy.

## Proposed implementation order

### 1. Add measurement before automatic routing

Use a diagnostics-only prototype to collect a bounded connection matrix between
the admitted racers. Four racers have six undirected peer pairs. Do not build a
mesh between every spectator or unrelated public-lobby player.

Record the selected ICE candidate type where available, complete-path RTT and
variation, loss bursts, late owner inputs, input/repair waits, queue age,
forwarding delay and bandwidth. Separate these from simulation/replay time,
resource preparation and GPU presentation stalls. Keep clocks local: RTT probes
and receiver-local input deadlines do not require synchronised machines.

Probe candidate paths at a capped rate. Use a clean interval before selecting
one, and longer rolling windows for tail statistics. Validate under real racing
and checkpoint traffic, not only idle ping packets. Prefer an improvement in
worst required delivery and missed deadlines over a lower average ping.

Also calculate whether a different player would be a better host for the group.
A pre-race recommendation can be useful without implementing live host migration.
Do not proceed to automatic switching unless measurements show repeatable gains
that outweigh forwarding overhead and variation.

### 2. Establish authorised client-to-client transport links

Keep one logical game session. The host grants a particular admitted client and
proxy permission to establish a specific route, with room incarnation,
membership generation, endpoint identities, short expiry and resource limits.
These grants do not admit a new racer or grant access to saves or mod contents.

Exchange bounded offers, answers, ephemeral keys and ICE candidates through
existing authenticated host control messages. WebRTC already supports this
connection establishment, but DKR-R needs its own client-side acceptance and
lifetime rules. Use a separate authenticated client/proxy link; never distribute
the host/client session keys to the proxy.

ICE validates candidate connectivity; STUN does not guarantee that two NATs can
communicate. A failed extra link must leave a healthy direct connection usable.
[ICE specification](https://www.rfc-editor.org/rfc/rfc8445.html),
[libdatachannel documentation](https://github.com/paullouisageneau/libdatachannel/blob/master/DOC.md).

Initially use admitted, consenting racers as proxy candidates. Spectator
endpoints and spectators volunteering to relay can follow after the racing
paths pass. A spectator host must still be supported as the coordinator.
Initial joining/mod downloads remain on the existing admission path; this
first version does not promise faster large mod downloads or admission through
a proxy when the original connection never worked.

### 3. Forward unchanged encrypted packets below the session

Implement normal first-party modules under
`runtime-recomp/src/game/netplay/peer_relay/`, with direct CMake sources and
tests. Integrate at `SessionTransport` using the existing approved Patch
Pipeline transition hooks as needed. No new overlay layer, dependency edits or
generated guest-code rewrites are required by this design.

Wrap an already encrypted session datagram in an authenticated relay envelope.
The proxy forwards the original bytes without decrypting, predicting,
re-dating, acknowledging on behalf of the destination or generating game input.
Only the final host/client endpoint validates the original packet. Original
sender, match, nonce, logical frame, port and lease remain unchanged.

The receiver maps the packet to its existing logical peer only after validating
the installed route grant and outer authentication. The original AEAD, replay,
ownership, content and frame checks still run. Arbitrary source-address
rewriting or accepting any packet that names another player is not sufficient.

Preserve independent control, input/repair, checkpoint and disposable lanes on
both forwarding legs. Heartbeats and required control exchanges must measure
and use the selected logical path too. A route cannot be called healthy merely
because racing input works while mandatory scene/checkpoint channels cannot.
Keep initial pending mod-transfer peers outside the relay permissions.

Negotiate a safe inner packet limit before forming packets, accounting for the
outer envelope and both legs' limits. Initially use the same transport backend
on both legs. Do not silently put a large WebRTC message into a small UDP
datagram or shrink the session's advertised packet size halfway through a race.
Retain bounded buffers, backpressure and fair scheduling so checkpoint traffic
cannot consume a proxy's racing-input budget.

### 4. Select and switch paths conservatively

Keep direct as the default, probeable fallback. Enable one intermediate relay
only after the whole bidirectional tunnel has passed validation and wins for a
sustained interval. Starting thresholds and cooldowns are experimental values
to tune from traces, not a claimed optimal latency policy.

Use one intermediate hop maximum. Relay legs must use their pinned underlying
connections, never recursively consult another relay route. That prevents
`A -> P -> A` loops and unbounded routing chains. Limit concurrent forwarding,
bytes, queue age and work per pump. Reject routes to arbitrary Internet targets.

Prepare and acknowledge a new route before using it. Preserve one end-to-end
sequence/replay ledger across direct and relayed delivery. A short, bounded
overlap can duplicate the exact ciphertext of important packets while ordinary
duplicate rejection admits it once. Do not regenerate conflicting input or
reset the game timeline to switch paths. Discard stale route generations.

Retire a failed route and use another validated route when available. Closing a
proxy link is not automatically a logical departure of every player using it.
The proxy's own genuine departure still follows the current membership policy.
Revoke grants on leave, host close, rehost or account/session replacement. Test
late callbacks against the new room, and keep relay work independent of the
currently selected launcher tab.

### 5. Coordinate with Dynamic Input Delay

Feed the existing controller actual end-to-end delivery on the selected paths,
including forwarding queueing and host fan-out. Never feed it just the low ping
to the nearest proxy. Age route-specific measurements when the route changes;
discard reports from old route generations without resetting simulation state.

Let routing settle and establish fresh delivery evidence before reducing
delay. Keep the existing cautious reductions and immutable-input scheduling.
Allow increased delay when new delivery needs more headroom. Avoid two feedback
loops continuously lowering delay and changing routes in response to each
other. Preserve the current scene, correction and seat-transfer holds.

This combination can improve play when a better route reduces late inputs or
loss. It cannot compensate for a player losing connectivity or a machine unable
to simulate/render on time. The existing bounded safe stops must remain.

### 6. Add the requested Host setting without redesigning the launcher

**Let Clients host by Proxy**

Suggested description: “Allows players to relay connections when a better route
is available. Can improve stability on some long-distance connections.”

Default off during testing. Explain that participating proxies use additional
upload bandwidth. Give clients a separate remembered permission to volunteer
their device; the host toggle allows the feature but does not force forwarding
on someone else's machine. Peer relaying does not conceal participants' IP
addresses.

Choose the room policy before starting a match; automatic route choices can
change during gameplay. Use the existing Connection panel for `Direct`,
`Via <player>` and `Using direct: no better relay available`, with detailed
metrics behind connection diagnostics. Keep the existing cards and layout.

Use explicit feature negotiation. Off must preserve the existing direct path.
Unsupported peers must get a deliberate direct fallback where compatible,
rather than a partially active relay. Do not reuse `relay_only` as an unexplained
shortcut or alter the immutable launch descriptor during play. Any required
cross-play identity/protocol change needs the repository's both-owner review.

### 7. Test the adapter and then real lobbies

Compare the same input traces and confirmed state hashes with proxy off/on,
including ordinary rollback and spectator-capable party play. Exercise three
and four racers, a spectator host, watching clients, both ROM revisions, vanilla
and mixed mods, rematches, seat changes, Windows/Linux and Steam Deck.

Cover direct already best, proxy best, asymmetric routing, jitter, reordering,
loss, saturated proxy upload, CPU hitches, NAT failure, ICE replacement,
forwarding withdrawal, host close/rehost and stale packets. Include a relay
that stops forwarding while remaining a racing participant, so route failure
is tested separately from the existing racer-departure policy.

Require unchanged confirmed inputs/state, no lost sampled button edges, no
extra irreversible save/audio effects, no accepted forged owner/lease/epoch,
bounded memory/queues and no forwarding loops. Reject replay, oversized and
unauthorised forwarding attempts. Exercise all required traffic lanes and MTU
boundaries on both transport backends.

Measure p95/p99 end-to-end delivery, added input latency, missed input deadlines,
repair/replay work, route changes, forwarding CPU/bandwidth and fallback time.
Proxy mode must remain direct when the alternative is worse, and show a
repeatable gain on a documented adverse route before wider testing. Passing a
codec test or seeing a smaller ping number is not enough. Run the required
guard/tooling, pre-commit, host tests and matching native builds before handoff.

## Website and infrastructure

The first admitted-player prototype can broker the extra connection through
the current host and existing signalling infrastructure. No website front end
is needed. The website remains the directory, not a frame relay. Cold joining
through a proxy would be a separate admission/rendezvous extension.

Some NAT combinations will still need an Internet relay. A managed regional
TURN service is a separate future option with its own credentials, operating
costs and route measurements; TURN provides controlled relaying, not a promise
of better ping. [TURN specification](https://www.rfc-editor.org/rfc/rfc8656.html).

Valve's relay service is another architectural example, not something unlocked
by running the launcher on a Steam Deck. Its documentation distinguishes the
open-source networking library from access to Valve's relay network and sets
requirements for use outside Steam.
[Steam Datagram Relay requirements](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay#Requirements_for_using_SDR_on_other_platforms_and_stores).

## Decision

Proceed with measurement, then an opt-in one-hop transport relay if real routes
justify it. Keep a true input mesh as a separately reviewed follow-up. Relaying
does not intrinsically cause desynchronisation; preserving origin, ownership,
logical frames, authentication and the existing confirmation rules is what
keeps it compatible with deterministic rollback. The immediate engineering
risks are extra queueing, route churn, NAT reachability and incorrect failure
handling. The plan above isolates those from the game simulation.
