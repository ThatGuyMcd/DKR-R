# Mod library community update

The maintained website code is in
`runtime-recomp/src/game/mod_library/website/`. Existing sign-in, upload, batch
and hosted-file assets remain on the web host. The private API source is in
`services/online-directory/api/`. `services/library-workflow/build.py community`
packages these sources directly; it does not rewrite them or change the
launcher, dependencies or generated guest code.

## Build and verify the website package

Use Python 3.11+ and Node.js 24+ on Windows or Linux. On Windows substitute
`python` for `python3`.

```sh
npm install --prefix build/library-community-tools --no-save --ignore-scripts --package-lock=false esbuild@0.28.2 jsdom@29.1.1
python3 services/library-workflow/build.py community --output build/library-community-release/stage --baseline build/mod-library-community-20261029/baseline --archive build/library-community-release/DKR-R-website-update.zip
npm ci --prefix services/test-tools --ignore-scripts
node runtime-recomp/tests/mod_library_community_tests.cjs services/test-tools/node_modules/miniflare build/library-community-tools/node_modules/jsdom
python3 scripts/check.py --suite guard,tooling
```

The baseline directory contains the accepted website files and previous Worker
sources for rollback. Save the deployed Worker, bindings/configuration and a
D1 database export before rollout as well: local source is not a backup of
production configuration. The package refuses to replace an existing output
directory or archive. The Miniflare fixture uses a private local database and
synthetic users; no real sign-in, FTP, publication or deployed API is used.

## Deploy

1. Export the existing D1 database and back up the deployed Worker and affected
   website files. Preserve all existing bindings, secrets and settings.
2. Apply `PRIVATE-API/community_schema.sql` to the existing D1 database. It only
   adds tables and indexes; existing listings, releases and reviews are kept.
3. Deploy `PRIVATE-API/worker-single-file.mjs` to the existing mod-library
   Worker. This bundle preserves the existing profile and online routing.
   Never put the private API, SQL or rollback files in the public web folder.
4. Check `/health`: `community.revision` must be `20261029-1` and
   `community.ready` must be `true`.
5. Merge `FTP-PATCH/assets/` into the existing website assets, then replace
   `privacy.html`, then upload `index.html` last. Keep `config.js`, all existing
   sign-in/upload/batch assets, images, fonts and downloads. The updated HTML
   uses a fresh cache revision for the scripts and styles.
6. Browse anonymously and with a player account. Check a real file download,
   comment create/edit/delete/report, and staff hide/dismiss/restore. Confirm
   companion selections survive filtering, draft saving and publication.

This is a website/API deployment bundle. The same website works in Windows and
Linux browsers; no native game executable or AppImage is rebuilt.

## Counting and comments

Badges sum tracked website requests over every release of a mod. Old counts
start at zero; historical totals cannot be reconstructed from the catalogue.
The tracking route redirects to the stored, approved release URL. HEAD,
prefetch/prerender requests and obvious bots are not counted. An attempt ID
deduplicates a retried navigation, increments are atomic, and at most 30
requests per connection address per minute contribute. A failure to record a
counter does not block a validated download. Direct URLs and launcher downloads
remain compatible but do not count unless they use this route.

Comments use Discord sign-in but have no star-rating requirement. Existing
reviews remain separate. Public pages use stable, 25-comment cursor pages in
chronological order; mutations check account ownership and record revision.
Comment creation has an idempotency key for uncertain network outcomes.
Moderation requires a reason and records a staff audit entry. Reports never
hide content automatically. Editing a hidden comment cannot restore it. Account
deletion cascades to its comments and reports.

No new visitor cookie or browser credential storage is introduced. Attempt
identifiers expire after 24 hours and address hashes after two hours, with
scheduled/opportunistic cleanup. The public privacy page documents this.

## Rollback

Restore the backed-up Worker and the files under `ROLLBACK/FTP-PATCH`, with
`index.html` last. Newly added community assets can remain unreferenced. Keep
the additive database tables so recorded counts and discussions are retained
for recovery; do not drop them or restore an older database over new records.
Restore production settings only from the actual deployment backup.

## Motion and compatibility

Page, category and mod-section entrances use a 320 ms fade with an 8 px rise
and a 60 ms stagger capped after eight groups. Featured mods crossfade every
30 seconds. Hover, keyboard focus and hidden browser tabs pause the timer.
Reduced-motion preferences disable entrances and automatic rotation by default.
The companion picker keeps checked inputs mounted while filtering so existing
draft/preview serialization includes selections outside the visible results.

## Online and ranked replay review

The Online page at `#online` reads `GET /v1/online/public`. The private
directory Worker creates the projection from the same live account leases and
room list as the launcher. This endpoint never forwards a browser credential
or a private room. The browser polls every 30 seconds while visible. A missing
count is shown as unavailable, and an available count of zero is shown as zero.

The Online card shows currently connected unique accounts against the number
of active accounts that have signed in through the shared library sign-in.
People with several launcher instances count once. Live public lobby, racer
and spectator totals only include publicly listed lobbies.

Player activity has 7-day, 30-day and All-time views. Each view shows the
peak and days with activity. The recent views also show the busiest hour, new
sign-ups and daily peaks. All time keeps the first timestamp of the lifetime
peak, the total active days and a lifetime history chart. The chart groups peaks by month at first and by year once activity spans more than 12 months. Activity history starts when the directory Worker starts collecting
it. Date buckets are UTC and peak times display in the visitor's time zone.

Copy a public lobby link and choose **Find lobby from website** under **Public
lobbies** in the updated launcher. The link selects a room from the current
signed-in launcher list. The existing host, compatibility, mod and slot checks
still govern requests to join or spectate.

Leaderboards at `#leaderboards` use the existing library account. The flow is
**Sign in → attach replay → check the detected event and recorded time → Post
run**. My runs tracks the upload, independent verification and review. Only
published verified PBs count on each board. The website is staged with no
qualified boards or upload access by default.

The launcher and in-game overlay use the same Leaderboards page. Open it
next to Achievements on Play. Its category tabs, track cards and rankings
read the public website API. Post a run opens the signed-in website flow.
The launcher keeps the last successful catalogue in memory for the session.

Custom tracks are discovered automatically from the live catalogue, including
newly published releases. A board identifies an exact release and the server
checks the catalogue's verified SHA-256. Publish a new board after qualifying a
new release. Previous eligible versions stay available under their own boards.

See [Ranked replay review](notes/2026-11-22-ranked-replay-review.md) for the
replay format, open qualification work and checks before opening submissions.
