# Additional Time Trial qualification

Each public board requires an exact three-lap replay, signed submission
and moderator approval in staging, rejection of altered inputs, and
matching finish time, lap splits and checkpoints on the pinned Linux
verifier. An automated drive runs in practice and its time does not go
on the public leaderboard.

## Retail loop sections

Walrus Cove and Darkmoon Caverns use a temporary retail vehicle mode
for loops. The verifier treated that mode as a change of vehicle.
It now accepts the loop mode on those courses when the original vehicle
recorded by the game still matches the attempt. The selected course,
racer, base vehicle and gameplay checkpoint requirements still apply.

The old executable rejected the same Walrus Cove replay at update 315
for car and at update 333 for hovercraft. Both complete captures passed
the staged Windows submission and altered-input tests on the rebuilt
game. The rebuilt Linux AppImage reproduced the car's nine checkpoints
and 9,600-tick finish, and the hovercraft's eight checkpoints and
9,254-tick finish.

The public verifier uses the previous pinned package. Both Walrus Cove
boards remain closed pending qualification against a live verifier that
can reproduce the loop sections.

## Boards opened in this pass

The public catalogue now has 54 boards, including five custom boards.
The 49 retail boards cover 40 of the 47 course and vehicle pairings.
Racer variants account for the additional retail boards.

| Course | New vehicles |
| --- | --- |
| Crescent Island | Car (Pipsy), hovercraft |
| Everfrost Peak | Car, hovercraft |
| Fossil Canyon | Plane |
| Frosty Village | Hovercraft, plane |
| Greenwood Village | Hovercraft |
| Haunted Woods | Hovercraft |
| Jungle Falls | Hovercraft, plane |
| Snowball Valley | Hovercraft |
| Spacedust Alley | Hovercraft, plane |
| Spaceport Alpha | Plane (Pipsy) |
| Star City | Car, hovercraft |
| Treasure Caves | Hovercraft |
| Windmill Plains | Hovercraft, plane |
| Luigi's Raceway | Car |
| Sherbet Land | Hovercraft |

Some Windows moves from the temporary proof filename failed after
the Linux verification had finished. Publication checked the complete
proof JSON against the pinned runtime digest, replay bytes, splits and
checkpoints in either filename.

## Remaining retail pairings

| Course | Vehicles | Status |
| --- | --- | --- |
| Walrus Cove | Car, hovercraft | Both verified on the rebuilt packages |
| Darkmoon Caverns | Car, hovercraft | Need completed three-lap replays |
| Spaceport Alpha | Car, hovercraft | Need completed three-lap replays |
| Treasure Caves | Plane | Needs a completed three-lap replay |

The other custom car attempts in this pass did not yield a full
recording. Those were Mario Circuit, MSUSA Chicago, Royal Raceway,
Toad's Turnpike and Wario Stadium.

The source guard and all 215 host tests passed. Both playtest
packages match their recorded SHA-256 checksums.
