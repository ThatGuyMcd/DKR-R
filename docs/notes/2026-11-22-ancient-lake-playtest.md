# Ancient Lake replay playtest

## Play and watch

1. Add or select a US v1.1 Game Pak in **Play**.
2. Open **Leaderboards** from the Ready to Race card and keep **Vanilla Tracks** selected.
3. Choose **Practice** to race Ancient Lake with Diddy and the car. The overlay shows the running clock and last lap. Finish all three laps to save a replay.
4. Choose **Watch last replay** for the completed attempt, or **Open replay** to play the supplied `Ancient-Lake.dkrreplay`. Playback starts the real track and feeds the recorded controller inputs into the engine. The launcher displays the result when playback finishes.

Practice uses a blank, in-memory save and no gameplay mods. Completing or leaving a practice run preserves your normal save and mod selections. Finished runs are stored in the profile's `speedruns` folder. These playtest replays are local practice files, so the public upload form does not accept them.

## Qualification

The packaged replay was recorded on Windows x64 using the retail Time Trial timer and the local test driver. It completed in **1:21.666**. The cumulative splits were **0:28.233**, **0:54.966**, and **1:21.666**. Fresh Windows and Linux x86_64 runs reproduced all **2,534** simulation updates, **4,900** race-clock ticks, and all five gameplay and random-state checkpoints. A copy with altered throttle inputs failed at the first checkpoint. A real Steam Deck hardware check is separate from the Linux software-renderer result.

The replay comprises a bounded manifest and one effective pad sample per simulation update. A pad sample is four bytes, holding the buttons and two stick axes. The game consumes 30 simulation updates per second and scores in 60 Hz units. The initial seed comes from the Time Trial profile. Audio, HUD and other presentation code use their own random stream during the attempt.

`scripts/verify_time_trial.py` runs the native verifier in a fresh local profile, requires a successful engine finish and reports the input file digest, recomputed clock and checkpoint hashes. It accepts a local runtime and a legally obtained US v1.1 ROM. On Linux, use `--xvfb` to verify with a local virtual display and software Vulkan.

## Release gates

The playtest covers Ancient Lake, one vehicle and one racer. The independent verifier covers this rules profile. Public ranking still needs account-bound attempts and native verification connected to the submission queue, followed by the moderation pilot. Other courses, ROM revisions, custom releases and the full-game categories need their own qualification. Replay playback currently runs at normal speed from the start.

The local API test checks that deleting a runner removes their records immediately and queues the private replay for deletion. It also checks that storage cleanup retries after a failure, and that deleting a moderator preserves other players' published times.
