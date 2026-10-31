# DKR-R public lobby service deployment

This bundle adds the Online directory to the existing Cloudflare library API.
You deploy it. No FTP upload paths or website frontend files change. Keep your
current API variables, secrets, D1 database bindings, routes and scheduled cleanup.

The main API is built from the maintained modules in `api/`. Compare any newer
owner changes with those modules before deployment. `BUNDLE-RECEIPT.json`
records the bundler, authored modules and every supplied output. Optional
`--rollback-api` and `--rollback-directory` files are copied byte for byte;
otherwise the bundle does not claim to contain backups of your live services.

## Profile database migrations

Before deploying this API, apply `PRIVATE-API/001-launcher-discord-profiles.sql`
and then `PRIVATE-API/002-launcher-profiles.sql` to the existing private API D1
database. Both are idempotent. Back up that database and the current API and
directory Worker code/configuration first. Exercise the migrations and bundle
with isolated staging storage before production. Existing account sign-in and
native gameplay remain independent of optional profile-picture metadata.

## Install with the feature disabled

1. Back up the currently deployed main API source and configuration. Save the
   current Windows/Linux client packages as the client rollback baseline.
2. Create an isolated staging copy of the API and Online Worker first. Use
   separate D1/DO namespaces and staging-only accounts. Never bind staging to
   production account storage for automated tests.
3. Deploy `ONLINE-WORKER/directory.mjs` using the supplied
   `wrangler-online.jsonc`. Its migration creates a SQLite-backed Durable Object
   namespace. The worker has no public route and `workers_dev` is disabled.
4. Add the `ONLINE_DIRECTORY` service binding to the private directory Worker
   and `ONLINE_ENABLED=false` in the existing API configuration; preserve every
   other configuration field. Upload `PRIVATE-API/worker-single-file.mjs` as the main
   API source. Do not upload Worker files to FTP.
5. Confirm the existing library, Discord pairing, image/mod uploading, existing
   FTP picker, categories and batch entry workflows still operate.
6. Confirm `/v1/online/capabilities` reports protocol 1 and `enabled:false`.
   No additional Discord OAuth scopes, bot token or new Discord secret is needed.

## Enable a pilot

Set `ONLINE_ENABLED=true` in both the private Online Worker and the main API.
Deploy the Online Worker first, then the API. Install the new client on pilot
Windows and Linux computers. Existing account sign-in is reused; otherwise use
Sign in with Discord in DKR-R Online. Start with private rooms, then public rooms
across separate networks. Follow `PLAYTEST.txt` before wider availability.
Create pilot lobbies after connecting to the enabled service. A lobby already
running in legacy mode remains intact; recreate it before publishing it publicly.

The native WebSocket library uses a short-lived, one-use ticket in the negotiated
subprotocol header. It never sends reusable account tokens in a WebSocket URL.
Only the existing authenticated API can issue a ticket. The private service
binding supplies verified account metadata; clients cannot nominate an account.
Connections renew account authorization every minute and expire that authority
after 125 seconds without renewal. Clean disconnect removes presence promptly;
an abrupt crash can remain counted for up to 60 seconds. Existing races continue
when the directory is unavailable.

Public summaries contain no reusable codes, admission keys, IP addresses or raw
account identifiers. Private room records stay inside the service. Counted
accounts are deduplicated across up to four other connected instances. Appear
offline hides friend presence and changes public hosting to private; Disconnect
Online removes this instance from aggregate presence.

Running spectator entry has a separate `RUNNING_SPECTATORS_ENABLED=false` switch
in both supplied configurations. For a controlled staging test, set it to `true`
in the staging API and private Online Worker, then reconnect the pilot launchers.
The native host still requires a spectator-only bound admission and a genuinely
running party; racer entry remains closed. Test committed-descriptor validation,
checkpoint catch-up, leaving and race continuity before enabling it in production.
Waiting-room spectators work with the normal pilot settings. Keep the production
switch false until that owner acceptance is complete.

## Operations

Watch active sessions/rooms, join failures, stale removals, service errors and
resource usage. Logs must omit account tokens, ticket/subprotocol headers,
bootstrap material and admission keys. The main library's configured account
authorization remains authoritative. There is no public chat or moderation UI.
The private directory keeps a bounded audit of room visibility/closure and join
bootstrap events: at most 10,000 entries, expiring after 24 hours. It stores
account and room identifiers, not credentials, codes, titles or packet contents.

The directory uses the fixed global object name `dkrr-worldwide-v1`. Do not derive
it from country or Cloudflare location, which would split the worldwide list.
Room and reservation records live in its own namespace. The profile tables
added to library D1 are separate from that namespace. Treat both as private
operational data.

This bundle retains existing PeerJS signalling and STUN. TURN is not included;
test different networks and restrictive NAT separately. Directory connectivity
is not proof that a peer game connection will succeed.

## Disable or roll back

Set the main API's `ONLINE_ENABLED=false` first. Then disable the private Online
Worker. Clients show directory unavailability and stop new service-backed joins;
already admitted peer sessions retain gameplay and local return/leave controls.
Let active matches end before deliberately restarting clients.

Restore your saved pre-deployment API source/configuration if needed. The supplied
`ROLLBACK/` files exist only when you passed explicit backups to the build;
verify they match your intended rollback point. Retain the DO namespace for diagnosis; do not
delete production storage as part of an ordinary rollback. The library database
and existing FTP files are unchanged; the added profile tables may remain
when rolling back Worker code.

Distribute a previously accepted Windows/Linux client if client rollback is
needed. Public discovery requires the new authenticated protocol; old clients
cannot join directory-backed rooms by bypassing the directory with a raw code.
