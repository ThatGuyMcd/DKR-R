# Sound settings

Use **Sound** to set the overall volume and balance the parts of the game.
Master, Music, Sound effects, Vehicles and Nature & ambience have separate
controls, with EQ presets and bass/mid/treble adjustment.

**Multiplayer music** keeps music on for three/four-player scenes where the
original game would silence it. Modern audio settings do not speed up the
original audio clock.

## Online differences

Master volume remains local and live in Rollback. Individual category mixing
still uses the authored mix there; saved category preferences remain available
offline. The old online backend selector is no longer offered in v1.0.12. A separate output
category mixer is deferred, not silently claimed complete.

Confirmed audio is kept separate from replay. Correcting old inputs must not
play the same event twice or write speculative save/audio state as confirmed
progress.

## Custom music and voices

Custom characters use replacement selection and race cues when their patch
actually supplies them. A missing or unchanged cue still uses the donor's
sound; DKR-R cannot invent a voice that isn't in the mod.

Track Lab supports a track's own MP3/WAV music and converted MIDI/native
sequences. Keep custom music with its matching track package.
See [custom tracks](CUSTOM_TRACKS.md) and [legacy mods](LEGACY-MODS-BETA.md).

## If music disappears

Check Master/Music, multiplayer music and any custom-track music first.
Note whether effects/voices still play, where the music stopped and whether it
returns at the title menu or on another track. Attach the relevant runtime
log, not just the next clean launch. Use
**About DKR-R → Support summary → Open logs**.

Audio lifetime and restart fixes are included in 1.0.5, but a new dropout still
needs its own evidence. Avoid deleting settings or saves as a first step.
