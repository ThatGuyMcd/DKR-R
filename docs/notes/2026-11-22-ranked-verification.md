# Ranked replay verification playtest

## Playtest builds

Use the v1.0.16-playtest.1 Ranked Replay Verification Windows ZIP or Linux
AppImage with a US v1.1 ROM. In Play, open Leaderboards to practice or submit a
recording on a qualified board. The local Replay Theatre can open Time Trial
recordings, including the post-race tail. Adventure replays use linear in-game
playback while long-run verification is in testing.

For Ancient Lake DS, install the current release through the Mod Library.
Select the installed track in Leaderboards and use Diddy in the car. For
Fossil Canyon, select Diddy and the car under Vanilla Tracks. Posting a result
queues it for independent replay. A moderator or admin reviews the verified
result before it appears in the public ranking.

## Public boards

The portal accepts Diddy Time Trial submissions for these newly qualified
selections.

| Track | Vehicle |
| --- | --- |
| Ancient Lake | Hovercraft |
| Ancient Lake DS, current Mod Library release | Car |
| Fossil Canyon | Car and hovercraft |
| Hot Top Volcano | Plane |
| Jungle Falls | Car |
| Whale Bay | Hovercraft |

The public verifier uses the existing Linux service and the same encrypted
replay storage as the previous ranked pilot. The previously queued Ancient
Lake run independently verified at 3,664 race-clock ticks and moved to
staff review.

## Evidence

- Ancient Lake DS, Diddy and car, reproduced 4,908 race-clock ticks (1:21.800)
  on Windows and Linux. Staging used the exact published Mod Library ZIP. The
  test covered the signed attempt, download and package hash, independent
  replay, altered-input rejection, staff-only publication and replay download.
- Fossil Canyon, Diddy and car, reproduced 7,724 race-clock ticks (2:08.733)
  on Windows and Linux. The staged submission checked the race and lap
  rankings, rejected altered inputs, and downloaded the original replay after
  staff approval.
- Ancient Lake hovercraft and Jungle Falls car, both with Diddy, reproduced
  5,084 and 5,626 race-clock ticks respectively on Windows and Linux. Both
  passed staged attempt, altered-input rejection and moderator publication.
- Fossil Canyon hovercraft, Whale Bay hovercraft and Hot Top Volcano plane
  passed the same checks at 7,498, 5,598 and 8,650 race-clock ticks.
- The Windows launcher navigation checks captured Play, track lists, results
  and Full Game layouts at desktop size and 1280×800.
- The live verifier's account home was hidden by its service, and Mesa could
  exhaust its thread limit. Each replay now gets a private writable home and
  bounded software renderer threads. The updated pinned AppImage reproduced
  the queued Ancient Lake run's finish and four gameplay checkpoints using
  the production service restrictions. The recorded post-race frames were
  preserved while the result was attested at the earlier finish tick.
- All 215 host tests passed, along with the pattern and identity guards.
  The broader tooling suite still reports two existing Android patch fixture
  failures.

## Qualification still in progress

Any%, 100% and All Trophy Races have separate native finish conditions and a
separate verification queue. The new checkpoint format captures all three
axes of racer position and velocity. Repeated Adventure inputs reached the
same hub and produced matching motion and save progress, while the gameplay
RNG first diverged four updates after hub entry in the traced run. One
playback consumed an extra random draw on that update and caught up five
updates later. The staging API test confirms role and proof checks.
Full-game submissions stay closed pending repeatable native playback
of a complete run in each category.

Additional vanilla track and vehicle pairs have complete test recordings.
Public qualification is applied only to pairs that pass independent
playback and the ranked submission path. Legacy patch tracks and battle
maps need their own competition rules and evidence.

