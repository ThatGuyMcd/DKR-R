# Public lobbies playtest 1: validation record

Version: `1.0.9-public-lobbies-playtest.1`. No live service deployment.

## Backend checks completed

The tests execute the generated main library API, its private service binding,
SQLite Durable Object and the existing D1 account flow in Miniflare
`4.20260730.0`. They use actual WebSocket upgrade responses and real local lease
timers, rather than replacing the directory with an in-memory mock.

| Run | Result |
| --- | --- |
| Default feature configuration | 83 checks passed |
| Separately gated running-spectator configuration | 89 checks passed |
| Lease expiry and local load/concurrent admission | 3,092 checks passed |
| Existing library D1, full API entry point and FTP picker | 21 checks passed |

The load run connected 1,000 sessions, published 100 rooms, paginated the list,
and sent 100 concurrent requests for one remaining racing place: exactly one
reservation succeeded and 99 were refused as full. The measured load phase took
106.468 seconds. This is a local correctness/load exercise, not a worldwide
latency measurement or a Cloudflare production capacity guarantee.

The suite covers authentication, one-use ticket replay, account-count deduplication,
private-room omission/code resolution, ownership, idempotency, reservation expiry,
cancel/disconnect/reconnect, stale revisions, start/return transitions, account
revocation, malformed-message rate limits, feature disable and the spectator-only
running gate. The normal and gated runs overlap; their counts are not independent
test-case totals. Existing FTP transfers and website frontend files are unchanged.

## Native and packaging validation

The delivery includes Windows x64 and Linux x86_64 AppImage clients. Their
`BUILD-RECEIPT.json` files record the final compilation inputs, binary checks,
retained Patch Pipeline receipts, package contents and archive hashes. Inspect
the delivery's `DELIVERY.json` for the matching final contracts and results.

The layer includes 19 compile-time admission assertions. Static audits recompose
the generated sources, verify actual compiler bindings, explicit TLS trust roots,
retained Scoreboard and online repair layers, and the 2,304-file authored-source
baseline. Package checks inspect PE/ELF files, Windows ZIP contents/CRC and Linux
AppImage/SquashFS contents without starting a client or a game.

## Owner acceptance remaining

Follow `PLAYTEST.txt` on real Windows and Linux desktops with real Discord
pairing, controllers, multiple networks, mod preparation and remote matches.
Compilation and local service tests do not establish those results. Linux Secret
Service availability and native certificate trust must be checked on the actual
pilot machines. Keep `RUNNING_SPECTATORS_ENABLED=false` in production until its
separate staging playtest passes.

Both supplied service feature flags start disabled. Existing peer signalling,
STUN and rollback remain responsible for the peer session; TURN and universal
restrictive-NAT connectivity are outside this delivery. The owner deploys the
private worker and API as described in `DEPLOYMENT.md`; no website frontend is
needed.
