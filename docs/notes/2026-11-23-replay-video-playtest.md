# Replay and video playtest

## Test scope

- The local portal runs on loopback with a disposable leaderboard database and object buckets. Its Ancient Lake entry is copied from the existing verified result. No production services are used by these tests.
- The original Ancient Lake replay reproduced a 1:01.066 finish, a 0:19.566 best lap, and all four recorded gameplay checkpoints on the rebuilt Windows runtime and the Linux AppImage.
- A separate test used the same controller input bytes with a ticket issued to a disposable local account. The verifier accepted those inputs and rejected an altered recording. The verified result went through the local moderation path.
- The AppImage opened Ancient Lake through the launcher and used all 1,916 downloaded ghost samples without preparing the replay again. The shared splash stayed visible during the renderer change. The replay and ghost downloads stayed outside My Replays.
- The two Walrus Cove loop recordings reproduced their complete checkpoints on Windows. The car finished in 2:40.000 and the hovercraft in 2:34.233.
- Treasure Caves (plane, 1:39.400) and MSUSA Chicago (car, 2:38.500) matched on a Windows build and on Linux, including the replay digest, lap splits, and final checkpoint. The local portal uses the exact Mod Library release and track hashes for MSUSA Chicago.
- The portal and launcher show the eleven Full Game categories from the supplied LiveSplit templates. The stage labels and route order come from the templates. The templates do not provide engine events or proof that a category can verify end to end.
- The cold Adventure path uses an isolated, uninitialised EEPROM image. The AppImage reproduced the native new-save intro. Existing Adventure replay versions retain their original boot behaviour.

## Local video workflow

Run `services/online-directory/render_ranked_video.py` as a dedicated unprivileged Linux user with Python, Xvfb, libX11, xauth, xdotool and FFmpeg available. Supply `--runtime`, `--rom`, `--replay` and a new `--output` directory. Custom tracks also require `--track-package` pointing to the exact recorded track package. The job creates its own display in a private network namespace. The runtime sends the RT64 presentations and their matching 48 kHz stereo interval for each replay update over a private local socket. The export highlights buttons and moves the N64 stick using the inputs from the same recorded update. The Replay Theatre spinner and transport controls stay in the interactive viewer.

The job writes `verified.mp4` and `proof.json` after checking the engine result against the replay clock, finish update and checkpoints. The queue leases each verified Time Trial once and retries failed captures. The portal checks the object's replay digest and serves a seekable MP4 after moderation publishes the run.

The original 30 Hz Ancient Lake video had 2,126 authored frames and ran for 1:10.867. Picture and audio both start at zero, and the first race picture appears at 0.067 seconds. Edge played and sought through the local copy. Nineteen frame samples, including consecutive frames at two A-button transitions, matched the replay. The controller goes neutral during the finish tail.

## Outstanding

- Full-length native recordings for Full Game qualification and personal best and leaderboard split comparisons. The native v5 Adventure recorder now writes splits triggered by gameplay milestones. The verifier independently regenerates them and checks the result before moderation.
- Qualification of the remaining course and vehicle pairs with incomplete recordings and of custom track releases with no complete proof.
- Full Game video rendering, which needs the long-session path rather than the Time Trial frame archive.

## Native video cadence and texture packs

- Website video capture reads each RT64 swap before the next swap can replace it. The
  private capture socket carries 60 Hz pictures and one 48 kHz PCM block per 30 Hz
  replay update. The encoder keeps both RT64 pictures during motion and repeats
  the authored image for transitions that supply a single picture. Check native
  paired pictures during the timed race as well as the output frame rate.
  The local Ancient Lake export produced two distinct RT64 pictures for every
  one of its 1,832 timed updates and reproduced the original 1:01.066 finish.
  Deploy the matching runtime and exporter together because the capture protocol changed.
- The planned website render profile retains a pinned copy of Sr.gu's HD pack,
  0.00 LOD bias, generated mipmaps and 16x anisotropic filtering. Keep these
  settings separate from the submitter's graphics choices. Use Ancient Lake to
  compare a native 60 Hz capture against the old blended preview.
- Allow the packs enabled in Graphics for live vanilla, custom-track, Ghost and
  Full Game runs. Keep them in the renderer's visual settings so gameplay
  mods, custom Magic Codes and unqualified track packages remain subject to the
  existing ranked rules. Honour the existing Modern presentation and pack
  priority rules. Keep the verifier's simulation independent of installed packs.
- Qualify the live texture change by comparing the inputs, splits, finish and
  gameplay checkpoint hashes with and without a pack. Include a custom track,
  a ghost run and a Full Game attempt, and confirm both US ROM revisions can
  verify evidence produced with the pack enabled.


## Remaining work and public video backfill

The native cadence is verified locally. The public video worker still needs the
matching Linux runtime and exporter, plus a managed render profile that loads
the pinned Sr.gu v0.2.8 pack with mip generation and sampling enabled, LOD bias
at 0.00 and 16x anisotropic filtering. Test 1080p60 as the standard Time Trial
output and keep the existing adaptive encoding and size limit for long Full
Game runs. Check a vanilla replay, a custom track with its exact package, and a
Full Game replay with its split panel before switching the public worker.

Re-render every existing public run video. Count and deduplicate the published
video assets in D1 by run ID and source replay digest at the start of the
backfill. The public racing and lap lists show personal bests, so the inventory
needs to include older published videos still available by their run links.

1. Record each current video digest and length, run ID, replay digest, ROM
   revision, credited runner and any exact custom track package required.
   Process the inventory in a resumable batch alongside the normal video queue.
   Keep moderation jobs ahead of the backfill.
2. Reproduce the verified finish, splits and gameplay checkpoints before
   staging each replacement. Check the RT64 cadence, widescreen presentation,
   audio sync, N64 input viewer, DKR-R intro, outro and watermark, and racer
   credit. For Full Game videos, check the split panel and controller placement.
3. Upload the replacement as a separate digest-named archive to the existing
   FTPS storage. Check the staged video and seek ranges, then update its
   current video reference only if the source replay and prior video still
   match the inventory. Keep the same public run link and preserve its leaderboard
   entry, replay and ghost. Retain the previous archive until the playback audit
   passes so an individual replacement can be rolled back.
4. Audit every inventory row and report replaced, skipped and failed videos.
   Leave the current video available for any run that needs replay compatibility
   work or another render attempt.

The in-game pack policy above is a separate launcher change. Cloud Saves are
also pending. Implement opt-in sync through the existing Discord login for
Offline, Modded and persistent Online saves. Test revision conflicts, offline
recovery and safe imports across Windows and Steam Deck before rollout.
