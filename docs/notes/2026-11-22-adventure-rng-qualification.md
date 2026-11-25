# Adventure RNG and Time Trial qualification

The intermittent replay divergence began just after entering a fresh
Adventure hub. Taj's ambience chose random timers based on the audio
channel mask. Wheel effects and visual particle emitters could also draw
from the gameplay stream. Adventure replay ruleset `adventure-us80-3`
uses the existing presentation RNG for those calls. The gameplay
checkpoints still include the gameplay seed, racer motion and Adventure
progress. Earlier practice replays keep their original rules.

## Hub verification

Four Windows and four Linux playbacks reached the clean hub at update
869 and independently produced the same gameplay checkpoint at update
1,800. The inputs exercised the opening of all three categories.
The packaged Windows build and Linux AppImage also matched both
checkpoints through 3,600 updates with steering and acceleration after
entering the hub. Both rejected the replay when the input ended before
the objective. The staged submission test rejected those same signed
inputs before moderation.

Both builds reproduced the existing Ancient Lake car replay. The host
suite passed all 215 tests. The broader tooling suite still reports two
pre-existing Android patch fixture failures.

## Recording complete qualification runs

Use a US v1.1 ROM with the Adventure RNG playtest. In the launcher select
**Play → Leaderboards → Full Game**. Select **Practice** in the category
to start with a clean Adventure save. Capture a complete `.dkrreplay`
for each objective.

- **Any%** ends on the final Wizpig win in Future Fun Land.
- **100%** needs 47 balloons, all four keys, both full amulets and four
  gold trophy cups before the final Wizpig win.
- **All Trophy Races** ends after the fourth gold cup in Adventure.

An independent runtime must reproduce the objective, input checkpoints
and exact finish. Use the staged Worker test with a completed practice
replay.

```text
node services/online-directory/tests/adventure_ranked_api.cjs BUNDLE RUNTIME ROM PRACTICE_REPLAY
```

It issues a local attempt, keeps the recorded controller bytes, reads the
archived replay back, and requires a native engine proof before it can
check moderator approval and the replay download in staging. Without a
replay argument it checks only the API contract.

Verify each of the same captures independently with the Linux AppImage
using `scripts/verify_time_trial.py` with `--xvfb` and
`--isolate-network`. Compare the finish, clock ticks and every
checkpoint with the proof from Windows.

Full-game submissions remain closed pending all three completed proofs.

## Time Trial boards

Each new board opened after Windows verified a signed replay through all
three laps, rejected altered controls and passed moderator review in
staging. The live-pinned Linux verifier independently reproduced the
finish, lap splits and all gameplay checkpoints.

| Course | Vehicle |
| --- | --- |
| Ancient Lake | Plane |
| Boulder Canyon | Hovercraft |
| Everfrost Peak | Plane |
| Fossil Canyon DS | Car |
| Frosty Village | Car |
| Greenwood Village | Car |
| Haunted Woods | Car |
| Hot Top Volcano | Hovercraft |
| Pirate Lagoon | Hovercraft |
| Snowball Valley | Car |
| Spacedust Alley | Car |
| Splashdown Pass | Car |
| Star City | Plane |
| Treasure Caves | Car |
| Windmill Plains | Car |

The public catalogue now has 32 boards. It covers 20 of the 47 retail
course and vehicle combinations, with additional racer boards and three
custom boards. The two new custom boards bind the exact current Mod
Library releases. A new package revision needs its own qualification.
The automated practice
driver did not finish Crescent Island car, Darkmoon Caverns hovercraft,
Spaceport Alpha plane or Walrus Cove car in this pass.
