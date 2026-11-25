# Adventure and installed track replay playtest

## Recording in the launcher

Select a US v1.1 ROM in Play. Open Leaderboards, choose Full Game, and
select Practice under Any%, 100%, or All Trophy Races. The timer starts
when the fresh Adventure reaches the central hub. It continues through
travel, races, menus and cutscenes after that point. The game checks the
final Wizpig victory, the 100% checklist, or the fourth Adventure gold
cup depending on the category. These sessions start with a blank memory
save and disable the usual game content, Magic Codes and save writes.

Open Custom Tracks and scroll to Installed tracks to practice a compatible
three-lap, standalone .dkrmap. The session freezes the selected track,
excluding the rest of the local mod collection. The replay identifies the
track by its ID and a digest of the ordered gameplay assets. It loads the
level assigned to that package in the new session. The available vehicles
come from the track's authored header.

Completed practice runs go in the local speedruns folder. Use the Replay
Theatre for Time Trials. The command-line verifier can check Adventure runs
from a fresh profile. For a custom track, pass its exact .dkrmap directory
with `--track-package`. For a full Adventure run, pass a `--timeout` in
seconds greater than the run's playback duration.

## Leaderboards

The launcher and overlay share the website's category order, track search,
world/vehicle/status filters, course-wide rankings, racer and vehicle
filters, lap and race views, pagination, and replay downloads. The launcher
also offers Watch, local practice and Replay Theatre. My runs follows the
signed-in account. Review runs uses the existing moderator API and role
checks. The Windows layout capture covers 1920×1080 and 1280×800.

## Verification

The Windows ZIP and Linux AppImage replayed the same Ancient Lake DS car
recording to 4,908 race ticks (1:21.800). Both reproduced the three lap
splits and all five gameplay checkpoints. The verifier rejected a changed
track package before playback and rejected changed inputs in the race.
The sample came from the ordinary installed Track Lab archive. The
automated driver produced the recording and a separate run verified it.

On Windows, all three Adventure categories reached a fresh hub at update
869 and rejected an unearned finish. The Linux AppImage also reproduced
the Any% start and rejected the unearned finish. The clock matched at
update 1,800, but the gameplay hash varied between runs. A diagnostic
Windows/Linux pair matched when comparing RNG, Adventure state, racer
motion and animation separately. A subsequent run from the delivered
Windows package diverged from a previously measured prefix. That
intermittent divergence blocks ranking before qualification of complete
Adventure runs.

All 215 host checks, the replay verifier tests and the ranked API tests
passed. The broader tooling check reports the same two Android patch
fixture errors in the integration checkout.

## Submission gate

The new formats are local practice formats. The public API and the
production verifier still use the existing qualified Time Trial rules.
A public custom board also needs the native content digest bound to the
published release, retrieval of the exact package by the verifier, and
signed attempt and moderation tests for that release. Battle maps and
legacy patch-based tracks need their own rules. Each Adventure category
needs a complete replay that reaches its genuine objective and repeats
on an independent verifier before enabling uploads.
