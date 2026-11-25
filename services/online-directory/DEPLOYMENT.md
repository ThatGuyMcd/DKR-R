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


## Ranked replay pilot

The public catalogue covers the 20 vanilla Time Trial courses and lists
current Custom Tracks releases. Qualification is separate for each track,
vehicle, ruleset and build. Ancient Lake in the car is the first qualification.
Pending submissions and raw evidence never appear in the public ranking.
Only moderators and administrators can open the review queue and publish or
reject verified runs. Players can see and download their own submissions.

Apply the additive 003-speedruns.sql migration to the existing library D1.
Use the same mod-host FTPS account to write encrypted replay archives in
downloads/Replays. The Worker uses the existing FTP_USERNAME and FTP_PASSWORD,
with SPEEDRUN_FTP=true and a distinct 32-byte hex SPEEDRUN_FTP_KEY. Keep an
offline backup of that key so old replays remain readable. Access to a stored
file needs both a valid file authentication tag and the game's API permission.
SPEEDRUN_TICKET_SECRET signs ranked attempt tickets and is separate from
SPEEDRUN_VERIFIER_SECRET, which grants access only to verifier routes.

Run `run_public_ranked.py` on Linux with a pinned AppImage, Python, Xvfb and
unprivileged network namespaces. Pass the private US v1.1 ROM with `--rom`
and the private US v1.0 ROM with `--rom-v1-0` for both verification queues.
The revisions share leaderboards, and the verifier uses the exact revision
recorded in the replay. Store the verifier token in a private file and put the
AppImage SHA-256 in SPEEDRUN_VERIFIER_RUNTIME.
The process submits proof only after standalone game replay, and the Worker
checks the proof against the evidence. The same verification pass exports a
portable ghost for Time Trials. The Worker checks its replay digest and writes
it to the same encrypted archive directory. Moderation remains a separate step
and the launcher can download the ghost when the score is published. Use the
matching verifier build with ghost export support.

For an existing independently verified Time Trial, run the verifier with `--backfill-run`
and its run ID. It checks the archived replay again and adds the ghost without
changing the published time, rank or moderation status. Older downloadable
replays still work through the launcher's local ghost preparation.
The script has --probe for a private archive write/read/delete test and --once
for a single queue poll. The in-game executable does not need FTP or verifier
credentials. Each verification uses a private, writable home and XDG directories
under its job directory. This allows ProtectHome on the service. Mesa's software
rendering thread count is capped so replay jobs stay within the service task limit.

Keep SPEEDRUN_UPLOADS_ENABLED=false while testing. Exercise a known replay and
an altered-input replay in isolation, check player and staff permissions,
then check the production archive with --probe. Enable submissions only while
the verifier is running. A stale heartbeat closes attempts and new uploads
after 150 seconds. Review, rankings and public replay downloads remain available
when submissions are paused. Turn off SPEEDRUN_UPLOADS_ENABLED to close intake.

The Launcher can link eligible unticketed recordings through the local-claim
endpoint when `SPEEDRUN_LOCAL_CLAIMS_ENABLED=true`. Apply the updated
`003-speedruns.sql` before enabling it. The endpoint stores the original replay,
locks the claim to the first authenticated submission, and uses the existing
verification, moderation and quota. Leave the flag unset on the public Worker
until local testing is complete.
The verifier renews the replay claim while checking it, so a long verification
keeps the intake health check current without another verifier claiming that run.

For native custom tracks, give the verifier --track-origin with the HTTPS
Mod Library download origin. Record each qualified current release in
speedrun_custom_packages with the published ZIP hash and the native track
ID and content hash. A qualified board binds a specific vehicle and racer to
that release. The verifier retrieves that ZIP from the configured origin,
verifies the published size and digest, and loads the matching .dkrmap in the
isolated replay process. For each new release, confirm the native runtime can
start the course and records the expected package fingerprint. Record its
actual lap count in `speedrun_custom_packages.lap_count`. Three-lap boards
continue to use `custom-time-trial-us80-1`. Other one-to-five-lap boards use
`custom-time-trial-us80-2` and attest every lap. On an existing database, add
the `lap_count` column with `NOT NULL DEFAULT 3 CHECK (lap_count BETWEEN 1 AND 5)`
before updating the Worker.
Enable SPEEDRUN_CUSTOM_UPLOADS_ENABLED after a representative submission has
passed the signed attempt, native verification, moderation and replay download
end to end. Each submitted replay still needs independent verification and
staff approval.

Full Game uses `adventure-us80-7`. The vehicle, Trophy and minigame routes
start in Tracks Mode with one completed save slot. All Bosses uses that same
slot in Adventure for the six rematches. The runtime creates the slot in
memory for each run and counts events from that run. The remaining Adventure
categories start from a missing save with the opening cinematic.

Run the verifier with `--queue adventure` separately from the Time Trial
process so a long replay cannot block the race queue. Enable
`SPEEDRUN_ADVENTURE_UPLOADS_ENABLED` when this queue is healthy and the
matching ranked boards are registered. The native verifier compares every
submitted replay's checkpoints, finish and splits before staff can approve
it. Glitchless evidence also needs a moderator to check banned techniques.

### Hosted replay videos

Install the three `speedrun_video_*` tables and the video deletion trigger
from the ranked schema. Configure `SPEEDRUN_FTP_VIDEO_KEY` as a new 32-byte
hex secret. Run `run_public_ranked_video.py` alongside the Linux verifier.
Set `SPEEDRUN_VIDEO_RUNTIME` to the capture AppImage digest when it differs
from the verifier build. Use a private file for each credential. The
video process needs Python cryptography, FFmpeg and Xvfb. Use the Mod Library
FTPS account with access to `downloads/Replays`, and give it a private writable
job directory for temporary render files. Pass both ROM paths to each video
worker with the same arguments as the verifier.

The queues prioritise independently verified runs awaiting review and also
backfill older published runs missing a video. Update the private API Worker,
portal assets and both renderer processes together. The renderers now use the
authenticated `video-evidence` route to retrieve the replay for an active job.
Run separate video workers with `--queue race` and `--queue adventure` so long Full Game movies
do not block Time Trial videos. Both capture the controller and game audio
on the authored replay clock. Full Game movies also show the live split
panel in the opposite corner from the controller. The worker renews its
job lease throughout rendering and uploading. Full Game encoding adjusts
resolution and bitrate to fit the existing archive limit.

Keep `assets/ui/Icons/diddy_kong_racing_recompiled_ico_by_pooterman.png` in
the renderer checkout. The movie adds the translucent logo at the bottom right
and the submission's credited Discord display name at the bottom left.
Two-second logo cards appear before and after the replay. The audio gets the
same padding as the picture and the verification clock only covers the run.

The worker encrypts the MP4 in authenticated 1 MiB chunks and uploads it to
the existing archive. The Worker checks the proof before it registers the
video. Website moderation requires the video before publishing. Moderators
and administrators using the Launcher can watch the verified replay in-game
and publish while the video is still preparing. The render queue finishes
the video and attaches it to the published run. Time Trials also require the
verifier-generated ghost. The website's Review runs page shows **Watch Video**
as soon as the MP4 is available, using a short-lived link tied to the active
staff session. Only published runs serve their videos publicly. Rejection
remains available during rendering.
The encrypted files use the same host as mod downloads. Use `--once` to
render or clean one queue cycle.


## Portal notifications

Apply 004-notifications.sql after 003-speedruns.sql and before deploying the
Worker and matching website assets. Existing mod requests and verified runs
awaiting staff review are included. The migration creates events in the same
transaction as status changes and keeps read history separate per account.
The existing scheduled Worker cleanup expires personal notices after 90 days.


## Cloud Saves

Apply `005-cloud-saves.sql` to the existing private API database before setting
`CLOUD_SAVES_ENABLED=true` on the matching Worker. The Launcher requires
Discord sign-in and an explicit opt-in on each device. Use the Cloud Saves switch
beside the active save on Play or open the Cloud Saves section in Save Manager.

Apply `006-modded-adventure.sql` before deploying the split Adventure API. It
adds a separate table for Modded Adventure saves, with the same account and pilot
isolation as the existing saves. A `?format=2` inventory advertises and lists the
new slot. Older launchers receive their usual inventory.

For private testing, set `CLOUD_SAVES_ENABLED=pilot` and put the tester’s
internal account ID in `CLOUD_SAVES_PILOT_ACCOUNT`. The Worker checks the
authenticated account and keeps pilot saves separate from public saves. Use
disposable profiles for the Windows and Steam Deck test. Set the mode to
`true` only after both devices have passed the test.

Each save slot has an account-scoped revision and SHA-256 digest. The Launcher
syncs personal Adventure, personal Controller Paks, modded course records and
persistent hosted Online progress. When the saves diverge, the Launcher applies
the verified cloud copy to the active save and preserves the previous local file
in save history. An empty cloud slot can be seeded from the local save. Local
progress can update an existing slot when it was made from the current cloud
copy. The Launcher syncs before choosing the host's save for a new lobby and
on return from play. Within a retained lobby it can publish hosted progress and
defers cloud replacements until the lobby closes. Confirmed gameplay writes
schedule background uploads for their save slots. The worker can combine writes
that arrive before its current transfer finishes, and gameplay defers cloud
replacements until the launcher. Guest session
snapshots and replay profiles stay outside Cloud Saves.

The Cloud Saves fixture uses two Discord-authenticated test sessions for one
account and a separate account for access checks. Run it against an owner
bundle with `node services/online-directory/tests/cloud_saves_api.cjs <bundle> services/test-tools/node_modules/miniflare`.
