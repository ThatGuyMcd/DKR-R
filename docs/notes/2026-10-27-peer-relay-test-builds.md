# One-hop proxy connection test builds

These builds add the optional proxy connections we discussed. The original host
still controls the lobby and game. Another player's launcher can forward the
encrypted connection if that gives a better route to the host.

## Trying it

Use the matching Windows x64 build or Linux x86_64 AppImage on every machine.
The AppImage is also the Steam Deck build. These are isolated test builds. They
cannot race with the previous release or previous Online stability test builds.

1. Close the old launcher and extract the Windows download into a fresh folder.
   On Linux, make the AppImage executable and launch it normally.
2. Before creating the lobby, open **Host Settings** and switch on **Let Clients
   host by Proxy**. The setting is fixed for that lobby. Create a new lobby to
   change it.
3. Players willing to help should open **Connection** and switch on **Allow this
   device to relay connections**. This uses extra upload bandwidth. The setting
   is saved on that device and can be switched off while connected.
4. Have at least two clients connected to the host. Initially everyone uses
   their normal connection. Leave the lobby open for around 15 seconds while
   routes warm up. A proxy is used only when its measured timing is better.
5. Use **Copy proxy connection diagnostics** on the host and clients to compare
   the routes. Keep the runtime logs from every machine if something goes wrong.
   Route selections and returns to direct are logged as `[online][proxy]`.

Both Quick Join and UDP are supported. Public/private lobby permissions stay the
same. No website, API Worker or database update is needed. Initial admission and
mod downloads keep their current connection. Only admitted racing clients can
relay; spectators do not become proxies. A spectator host is still supported.

Try direct first, then proxy on the same machines. Check stock and custom tracks,
spectators, rematches, host close/rehost, and withdrawing the volunteer setting
during a race. Watch Dynamic Input Delay as connection quality changes. Test
Windows/Linux together and Steam Deck in Gaming and Desktop Mode.

## How it works

The launcher measures the direct connection and each eligible one-hop route in
both directions. Probes use the Authoritative traffic lane. The report shows
rolling p50/p95/p99 round trips, missing probe replies, candidate readiness and
the local extra WebRTC link's selected ICE candidate types where available.
Endpoint-only probe proofs prevent a proxy from answering on the client's behalf
and pretending the complete route is faster than it is.
Missing replies are probe outcomes, not a measurement of all gameplay loss.

Each side needs at least eight samples. The candidate has to improve the worse
reported p95, including a penalty for missing probes, by at least 8 ms or 15%.
The improvement must last three seconds. A ten-second cooldown limits route
changes. All five traffic lanes must be ready. Stale or failed routes fall back
to the original connection. These thresholds are deliberately conservative for
this first test.

The extra Quick Join link is a separate native WebRTC connection between the
client and proxy. The authenticated host brokers its signaling over the current
connection. UDP uses the current sockets with host-authorized addresses and
scoped hole punching. Some NATs will refuse an extra peer connection. That
candidate stays unused and the original connection continues.

The proxy forwards the original session ciphertext unchanged. It does not get
the host/client gameplay key. A separate short-lived grant protects the relay
envelope and restricts its room, original participants, physical hop and lanes.
Grants renew while both clients remain admitted and consenting. Closing/rehosting
retires them. Replay checks, original packet authentication, controller ownership,
frame history and the existing ACK/repair paths still apply at the destination.

There is one proxy hop only. Its host-facing leg always uses the original
transport, so two selected routes cannot create a forwarding loop. Signaling,
callbacks, forwarding queues and bulk upload are bounded. The existing lane
separation and backpressure stay in place. Background callbacks hold weak
mailboxes rather than a launcher/session pointer.

Dynamic Input Delay receives the selected route's complete p95 round trip. It
does not mistake a short connection to the proxy for a short connection to the
host. A route change does not reset the game, move host authority or rewrite
simulation input. A genuine racer leaving still follows the existing match-stop
policy.

## Scope and validation

The relay lives in `runtime-recomp/src/game/netplay/peer_relay/`, with a normal
CMake module and tests. Integration uses the existing baselined Patch Pipeline
hooks during the source fold. No submodule, generated guest source, frozen source
capsule or launcher skin was edited. The earlier Online, memory, spectator,
profile, mod, fullscreen and vanilla Track Select fixes are retained.

Automated validation covers controlled route timing, lost setup messages,
renewal, withdrawal, failure fallback, ciphertext preservation, bad senders,
tampering, replay rejection and the one-hop limit. Local socket checks use actual
UDP and actual libdatachannel peer connections, authenticated host signaling,
all five traffic lanes and a full-size Quick Join checkpoint. A separate native
DirectSession check exercises three real owners through the compiled extension
handlers, grants, measurements and host-close cleanup.

Platform build checks, retained regression results, source/package audits and
archive readback results are supplied with the downloads. These do not constitute
a live cross-country race or GPU acceptance test. Proxy routing can improve a
poor Internet route; it cannot remove physical distance, fix local frame-time
problems or guarantee that a connection never stalls.

The sealed release and signed updater files remain unchanged. These test builds
use the existing compatibility ledger as authorized. Both maintainers still need
to review the compatibility change before it is merged.
