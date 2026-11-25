# Ranked replay expansion

## Retail Time Trial

The local launcher can select any of the 20 retail Time Trial courses and
the 47 course and vehicle combinations used by the retail save records.
The recording keeps its selected racer. Ancient Lake in the car retains
its original format and board IDs. Other combinations bind the course and
vehicle into a versioned replay and confirm them against the loaded race.

A new board requires an independently reproduced race. Verify the same
recording against the Windows build and pinned Linux verifier, compare the
finish, lap splits, input digest and gameplay checkpoints, and confirm that
a replay with altered controls fails. Register a board for its specific
course, vehicle and racer after this check. Returning an attempt ticket from
the API is separate from verifying that player's replay and moderator approval.

### Local proof

Windows and the Linux AppImage reproduced the same Fossil Canyon car
fixture: 3,946 updates, 7,724 race-clock ticks (2:08.733), 2,534 best-lap
ticks, three lap splits and seven matching gameplay checkpoints.
The native verifier rejected altered input at checkpoint 600. The local
submission test issued an attempt ticket, queued the authentic recording,
verified it in the Windows package, and confirmed that it appeared on the
leaderboard after staff approval. The published replay matched the submitted
bytes. The expanded Windows package also verified the original Ancient Lake
recording. A separate Ancient Lake hovercraft smoke test confirmed the vehicle
loaded and reached checkpoint 600. Hovercraft needs a full-race proof.

The packages are local playtests. The public submission service still
accepts the existing Ancient Lake car qualification.

## Adventure

Any%, 100% and All Trophy Races need a full-session format. The recorder
must include the inputs consumed in menus, during races and between levels.
Start from an isolated new Adventure save and define the exact timer start
and finish event for each category. Recent checkpoints should include RNG,
game mode, course, player state and Adventure progress from the engine.

The existing Time Trial input hook skips menu frames. Adventure needs the
regular `input_update` call in the game thread, with the separate boot sample
kept out of the run clock. Capture the exact step rate and verify checkpoints
before and after level loads.

For Any%, reproduce the final Wizpig victory in Future Fun Land. The retail
boss finish path records it only for first place, and the separate central
island victory cannot count. For 100%, require that victory, the 47 balloons,
the four world keys, both complete amulets and first place in the four cups.
For All Trophy Races, watch the Adventure rankings menu write the fourth
first-place cup. A bronze or silver trophy and a cup won in Tracks mode
cannot count. Derive elapsed time and progress from the clean simulation.

The verifier now renews the active job claim and advertises its health while
a replay is running. The local API tests confirm renewal, reclaim after
expiry, and rejection of old claims. Full sessions still need a streaming
input format, separate job and archive limits, and isolated playback that
can run while short Time Trials remain eligible for verification.

## Custom tracks

Pin each ranked board to the exact track release and a digest of the
simulation content. Independently install that release in a fresh
verifier profile, admit only that track, and derive the map ID after loading
the track. Runtime map IDs for installed custom tracks are local assignments
and can differ across installations.

Test native racing maps and legacy patch-based tracks separately. Battle maps
have different finish conditions from Time Trial. A locally installed track
with no identical downloadable release needs a way to supply that exact
package to the verifier before it can accept public records. A changed upload
creates a separate qualification for the new release.
