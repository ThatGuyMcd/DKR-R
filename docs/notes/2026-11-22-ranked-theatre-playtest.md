# Ancient Lake ranked and Replay Theatre playtest

Use the US v1.1 ROM you already have. The pilot runs Ancient Lake in the stock
car, solo, for three laps. It uses a fresh in-memory save and a fixed starting
seed. The profile's mods and Magic Codes are disabled for the attempt.

In **Play > Leaderboards**, choose a stock **Racer**, then select **Practice**
or **Start ranked run**. Ranked times are grouped by racer. Finish all three
laps and allow seven seconds for the replay to capture the finish sequence.
Use **Post last run** or choose your completed file with **Post a run**.
Open **My runs** to check the submission and watch your uploaded replay. A
single approved replay counts for both **Full race** and **Best lap** for its
racer. The recorded time and splits stop at the finish line. Staff review
still applies before a time is public.

The attempt is bound to its account, board and ruleset. A replay contains the
inputs delivered to the game and gameplay checkpoints. The verifier runs
the game with its own clean profile, compares the checkpoints and takes the
time from the reproduced race. This checks repeatability of the recorded
inputs. Moderators can also review playback.

## Run the local pilot

The local test server uses disposable accounts, storage and a loopback-only
address. Close it with Ctrl+C when finished. It needs Node.js, Python, a
fresh launcher profile directory, the rebuilt game executable and your own
US v1.1 ROM. On Linux pass `--xvfb` to run the verifier on a separate display.

```sh
python services/online-directory/build.py --output build/local-ranked-worker
node services/online-directory/run_local_ranked.cjs --bundle build/local-ranked-worker --runtime <game-executable> --rom <rom> --jobs <jobs-directory> --profile <disposable-profile>
```

Start the game executable with `--config <disposable-profile>`. Add the same
ROM in Play if the profile has no ROM selected. After a verified run has been
reviewed, enter `publish <run-id>` in the local test server console. Published
rows have a **Watch** button when a launcher profile is active.

For the automated verifier proof, replace `--profile <disposable-profile>`
with `--fixture <practice-fixture.dkrreplay>`. This wraps a known input
fixture in a disposable test ticket. It verifies and publishes the original
and checks that changed inputs are rejected. The script uses separate
processes and profiles for verification.

## Replay Theatre

In **My replays**, select **Watch** beside a local recording or download.
**Open replay folder** opens the `speedruns` folder in the active launcher
profile. Use **Open replay** for a `.dkrreplay` saved elsewhere, or select
**Watch** on a published result. Older replay files still open and end at
their recorded finish.

Use the timeline, lap starts and **Finish** to skip through a replay. The
frame buttons move one recorded simulation update at a time. Space, Escape
and controller Start play or pause. Comma steps back and period steps
forward. Select **Exit** to return to the launcher. **Inputs** shows the
recorded stick and buttons. The speed choices are 0.5x, 1x and 2x. You can
change the target, cancel the seek, or choose whether playback resumes on
arrival.

Replay Theatre prepares a temporary local frame index once each time a
replay is opened. The **Preparing replay** counter and the thin timeline
bar show how far that pass has reached. Playback can start while the index
is being built. Click or drag backwards or forwards through prepared
footage to display a frame without rerunning the race. An unprepared target
opens when it becomes available. **Finish** skips to the crossing, and the
timeline extends through any recorded finish sequence.

Shared replays contain inputs and verification hashes. The Theatre uses the
racer in the recording, with Diddy implied for the original Diddy-only files.
The presentation index lives locally and is removed on normal exit.

## Earlier local test results

The original Ancient Lake fixture reproduced all 2,534 updates and five
gameplay checkpoints on Windows and Linux. Its full race time was 1:21.666
and its best lap was 0:26.700. The local ranked test uploaded the fixture,
rejected altered inputs and required moderation before listing the valid
result on either board.

At 1280x800, Replay Theatre paused, stepped from frame 49 to 50 and back to
49 and displayed the recorded controls. The timeline jumped to frame 934,
Lap 2 jumped back to frame 931 and Start rewound to frame 1. Linux tests ran
using Xvfb on WSL.

## Earlier replay library playtest results

A Pipsy practice recording crossed at simulation update 2,501 and played to
update 2,711 in the Linux AppImage. The Windows verifier accepted the official
race time of 1:20.566 and best lap of 0:26.233. A disposable local leaderboard
placed the run on Pipsy's full race and best lap boards after moderation and
rejected the altered-input fixture.

The original 1:01.700 Ancient Lake replay still plays. In the 1280x800 Linux
test, Escape paused in the Theatre and playback stepped back from frame 874
to 873 using checkpoint 870 in about 0.3 seconds. Jumps to frames 859 and
494 required exact reloads. The replay then reached update 1,935 with the
original finish and all gameplay checkpoints verified.

The host CTest run passed 213 of 214 checks. The regression harness timed
out on the Windows-mounted checkout and passed all 25 tests from the native
Linux filesystem. The separate tooling check reports two existing Android
patch fixture failures. The guard and ranked API checks passed.

## Whole-run scrubbing checks

On Windows, the original Ancient Lake replay verified its 1:01.700 finish.
The Theatre presented both backward and forward jumps within the same race,
including frame 748 back to 84 and frame 204 forward to 998. A conflicting
racer option did not change the recorded Diddy. At 1280x800 on Linux, the
same replay verified all 1,935 frames and jumped from frame 1,800 back to 415
in about 130 ms and forward to 1,154 in about 170 ms. In the packaged
AppImage, dragging the timeline while paused showed Pipsy move from frame
138 to 31 before the mouse button was released. The current Linux host
suite passed all 214 checks. The separate Windows tooling suite still reports two
existing Android patch fixture failures.

## Agenda

- Add **Download replay** and **Race ghost** to published leaderboard runs.
  Also let players race a compatible file from **My replays**, so a saved
  ghost can be used offline.
- Start with Ancient Lake Time Trial. Require the recorded route, vehicle,
  and compatible rules and track release. Prepare the opponent from the
  recording's inputs, with its recorded racer and timing.
- Render the ghost as a non-interacting opponent. Keep collisions, pickups,
  the player's timer and save progress independent. Compare splits against
  the ghost and qualify competition with a ghost before enabling it in ranked
  attempts.

### Website notifications

The website bell appears between Moderation and Sign in or the account name. Signed-out
visitors can use it to sign in. Signed-in players see changes to their own
mod and ranked-run submissions, with a link to the relevant status page.
Moderators and admins also see mod submissions and independently verified
runs that are ready for review. Players have no access to the staff queue.

The service records a single event per committed status change and stores read state per
account across devices. The bell shows an unread count and opens a list
with timestamps, status, and Mark all as read. Give reviewers the current
queue state when they follow a notification, including items already handled
by another moderator. The tests cover deduplication, moderation role changes, cross-device read state and deletion. The bell is also checked on narrow screens.
