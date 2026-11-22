# Ranked replay review

## What to review

- **Online** shows the launcher's active-account count and public rooms. Test a
  private and a public room and check whether copying a link identifies the
  intended room in the launcher. Selecting the room asks for the normal host
  approval.
- **Leaderboards** has Vanilla Tracks, Custom Tracks and Full Game. Tracks
  appear from the library as soon as the listing has a published release.
  Ranked results require a qualified board for that exact version.
- **Post a run** uses the existing library account and attachment flow. The file
  fills in the board and recorded time. Submissions and their private replays go
  into the same queue whether uploaded in the browser or by an authenticated
  client. A queued time has no public rank.
- **My runs** shows submission state and staff feedback. **Review runs** gives
  authorised staff the replay and verified time. A reviewer can publish or
  reject an independently verified run. The route does not accept a claimed
  verifier result from the reviewer or player.

## Time Trial rules for qualification

Start with solo offline US Time Trial. Board identity includes the game
engine, rules revision, track, exact release hash where applicable, category
and legal vehicle. Use separate boards for completed races and best laps when
the engine can derive both from a completed run. Start, split, finish and lap
durations come from the guest race clock.

Use an isolated game profile with the normal retail racer and vehicle for the
board. Other Custom Magic Codes, retail Magic Codes, cheats, dev tools,
save or state injection, macros, scripted inputs and timing adjustments are
disallowed. Reject a run that changes a gameplay setting or pauses the clock
during the attempt. The verifier checks the resulting simulation and allowed
settings. Replaying an input stream cannot establish whether it came from a
person, so human moderation remains part of the workflow.

The ranked Magic Code must only expose approved timer and run actions. The
runtime owns the locked profile, inputs and eligibility. Graphics choices are
separate from gameplay settings. Qualify each revision, OS and architecture
combination for tick and state parity before combining scores. Pixel comparison
requires a separately fixed renderer, assets, camera, viewport and output
format.

## Replay and trust boundary

The preview format starts with ASCII `DKRREPL1` and a little-endian 32-bit
manifest byte length. It then holds a UTF-8 JSON manifest and four bytes per
applied controller tick, with the 16-bit button bitset in little-endian order
and signed X and Y axes. The manifest names the board, engine digest, ruleset,
release digest, ROM revision, platform, architecture, tick count, recorded
race clock, initial RNG seed, ordered splits and state digests at 600-tick
intervals and on the last tick. The browser and API share format validation.
The manifest is untrusted. The one MiB limit applies to the Time Trial preview
format. Full-game runs need a separately qualified larger format.

Only the independent verifier may set `verified_ticks`, `verifier_digest`
and `verified` after reproducing the run from a trusted clean start and
checking every required event, RNG transition and state digest. The public
Worker has no verifier-writes endpoint. D1 stores the queue and decisions.
R2 objects are private and served by the authorised download route. Neither
the replay format nor the published object includes a ROM or a guest snapshot.

## Before opening ranked submissions

The runtime recorder, ranked Magic Code, split overlay, playback controls and
independent verifier are still required. The existing owned-engine probe is
for private snapshots and rollback testing. First prove a completed legal
vanilla Time Trial from inputs alone on Windows and Linux, then qualify one
native custom track release. Compare applied input, engine checkpoints, splits
and completion. Verify renderer parity separately. Add Adventure categories
such as Any%, 100% and All Trophy Races after equivalent coverage.

Keep `SPEEDRUN_UPLOADS_ENABLED` unset until that qualification is complete.
Configure a private `SPEEDRUN_REPLAYS` R2 binding during the ranked pilot.
Run the additive speedrun D1 migration and check the acceptance, privacy and
review cases in staging. The Online page and read-only track catalogue can
be reviewed independently of ranked uploads.

## Staging review

The review package contains website and Worker sources plus screenshots from
a local test. The screenshot times and lobbies are fixtures. The package's
rollback sources are the base checkout used to build the review. Collect the
actual deployed website files, Worker revisions and a D1 backup before rollout.
Keep the current Cloudflare settings and bindings. The directory Worker must
understand the public projection before the public API and website use it.

Use the existing documented deployment commands only after review. No
production changes are required to run the local tests.
