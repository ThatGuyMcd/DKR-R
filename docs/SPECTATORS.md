# Spectators and taking turns

Enable **Spectator-capable lobby** in Host settings before creating a room.
There are up to four racing controller ports and eight spectator places.
Adventure still supports two racers. Guests choose **Racer** or **Watch Race**
before joining; the host's disabled spectator setting cannot be overridden.

Spectators need the same compatible build, ROM and admitted session mods.
Host approval and code permissions still apply. Only racers need to ready.
While waiting, **Make spectator / Make racer** changes roles; making a racer
needs a free place and clears Ready for the changed lineup.

## Watching

- **All splits** shows the normal split-screen view.
- **Follow player** shows the selected racer's camera fullscreen.
- **T.T.-style director** changes racers automatically. Original vehicles can
  use a cinematic camera with static-wall checks. Custom GPU-rendered cars use
  safe racer cameras to retain their car, seating and animation. Scripted or
  unsupported scenes keep the native view.

D-pad left/right selects a racer; C-up changes camera mode. Use the usual
overlay shortcut for camera/lobby controls. Viewing input is not racing input
and does not occupy a racing controller port or change racers' camera state.

Joining a running race through the directory requires the service's separately
enabled late-spectator flag. The join uses the committed descriptor and a
confirmed checkpoint, then catches up. A late racing-role request stays closed.
A slow spectator is outside racing input/scene agreements and can request a
fresh checkpoint rather than holding racers indefinitely.

## Custom tracks and music

v1.0.12 includes the playtest.4 spectator fix. A spectator now prepares
the selected track's verified music when joining the confirmed race
state. This fixes the black screen and missing audio that could last
until the results screen on a fresh spectator machine.

A spectator that fails to recover five times without making playback
progress stops with the actual reason. The racers carry on. If it still
happens, send the spectator's runtime.log and the host log from the same
race, including the track and mod selection. Changing cameras does not
repair a checkpoint or missing music resource.

## Swap a racing place

A racer can offer a place; a spectator can request one. The host approves in
**Player swaps**. Racing peers agree a confirmed handover boundary and the
replacement reaches that state before control changes. Expect a possible short
pause while this finishes.

The replacement takes over the existing racer, vehicle and race progress.
Choose another character/car at the ordinary selection screens. The outgoing
racer becomes a spectator; the old machine does not relay the new racer's input.
The host can hand over their racing place but must remain connected as host.

The host can cancel a pending swap. Timeouts and scene changes cancel an
unfinished handover rather than transplanting partially loaded state.

## Saves, achievements and leaving

Watching does not overwrite personal Adventure, write personal Controller Paks
or award gameplay achievements for someone else's racing. The host owns the
online save; clients use session progress. Development progress remains separate.

A spectator leaving does not stop the race. Loss of a required racer returns
the match safely to the lobby. Closing the host closes the room; there is no
host migration. The host can also return everyone to the lobby without closing
the application. See [Online](ONLINE_MULTIPLAYER.md) and [Saves](SAVES.md).
