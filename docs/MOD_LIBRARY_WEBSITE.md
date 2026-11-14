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
node runtime-recomp/tests/mod_library_community_tests.cjs build/library-workflow-d1-runtime/node_modules/miniflare build/library-community-tools/node_modules/jsdom
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
