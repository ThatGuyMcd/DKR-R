# Saves and Save Manager

## Vanilla and Modded Adventure

In **Play**, turn **Session Mods** off to play Vanilla Adventure or on to play
Modded Adventure. Each has three Adventure slots with independent balloons,
unlocks and built-in course records. All ordinary offline mod sets share the
same Modded Adventure. Your existing Adventure stays in Vanilla. Modded starts
with empty slots on first use.

Cloud Saves syncs each Adventure independently. Sign in with the same Discord
account and enable Cloud Saves on both devices. Online Adventure, custom-course
records and Controller Paks have their own sync entries.

Older saves under `saves/mods/<fingerprint>/` are kept for recovery. They are
not automatically combined. Open **Save Manager > Backups & Recovery**,
inspect the copy you want and confirm the restore. The previous personal
save is backed up first. Do not delete the old folders to tidy your profile.

## Automatic Cloud Saves

Enable **Cloud Saves** beside the active save on Play. Select the cloud status
icon for a manual sync. The client checks the cloud copy before online hosting
and offline play, when returning to the launcher, and after a successful
gameplay save. While a write completes, the game briefly shows **Saving Progress**.

An upload uses the cloud revision the device last synced from. If the copies
diverge, the cloud copy takes priority and the replaced local image is backed
up under **Backups & Recovery**. The switch controls this device's sync.
Disable it before testing changes in an isolated profile.

## Save Manager

Select a slot from the Adventure list to open its editor. **Progress** shows
initials, balloons, Trophy Races, amulets, keys and course progress. Use
**Max Out This Save** or **Max Out This Slot** at the top of Progress.
**Unlocks** and **Files** sit alongside Progress. **Apply Changes** and
**Discard Changes** stay visible while the editor scrolls.


- **Vanilla Save:** manage the Vanilla Adventure slots, progress and unlocks.
- **Modded Save:** manage the Modded Adventure slots, progress and unlocks.
- **Edit progress:** choose the intended slot before changing progress. Max Out
  and unlock actions save after confirmation. Manual edits need Apply Changes.
- **Transfer & Paks:** a bundle includes the selected Adventure and any shared
  custom-course records and Controller Paks. Import an older bundle from the
  Adventure tab where you want its progress to go.
- **Reset Save:** reset the selected Adventure's slots, unlocks and built-in
  records. The shared custom-course records and Controller Paks are kept.
- **Online:** inspect the host's separate Adventure and choose progress for hosting.
- **Backups & Recovery:** find older saves and choose Vanilla or Modded as the restore destination.
- **Development:** tools such as the Dev Menu use isolated development progress.

Writes are checked by reading the saved bytes back. A draft opened before
another change cannot overwrite the newer save. Edits/restores are blocked
while an active game or lobby owns that save. Leave the game/lobby first.
Max Out changes stored progress and unlocks; it does not enable every Magic
Code, install mods or bypass the game's current scene/menu lifecycle.

## Online and development saves

The host owns separate online progress under `saves/online/host/`. A client
receives a session copy, not a replacement for their personal Adventure.
Confirmed rollback state is the source of persistent online writes. Speculative
frames and disconnect cleanup must not write into your personal save.
Online Controller Paks remain separate too.

A new development sandbox starts from saved personal progress. Existing
development progress is kept unless you deliberately copy personal progress
again. The Dev Menu does not overwrite your normal Adventure.

## Data locations and portable mode

Windows normally uses `%APPDATA%/DKRPort`. Linux uses `$XDG_CONFIG_HOME/dkr-port`
or `~/.config/dkr-port`. Desktop portable mode uses `dkr-runtime-data` beside
the application. Android uses private app storage; use its export controls.

Portable mode takes effect on the next application launch. It selects another
location; it does not silently move saves, mods or settings. A config override
can also select another profile. Check the selected location before assuming
progress has disappeared. Vanilla uses `saves/dkr.us.v77.bin` and Modded
uses `saves/modded/dkr.us.v77.bin`. Both supported US revisions use the
compatible stock EEPROM layout.

Back up your data before updating. To recover, export and inspect the surviving
copy first, then restore it deliberately. No editor can recreate progress when
no valid copy survives. See [Troubleshooting](TROUBLESHOOTING.md).

## Spectators and achievements â€” v1.0.14

Watching uses session-owned state, not your personal Adventure or Paks. The
host owns communal online progress; confirmed handovers do not transplant a
save into another participant's personal profile. Development remains isolated.

Achievements have separate `achievements/progress-v1.json` storage. Max Out
does not itself award gameplay goals. Recognising provable existing milestones
is an explicit achievement setting. Spectating and development do not award
gameplay progress. See [Achievements](ACHIEVEMENTS.md) and [Spectators](SPECTATORS.md).

## Play indicators and session mods

Open Save Manager from **Play > Save Manager**. The three-light summary in Play
describes Vanilla, Modded or Online, with Using for a running session and Next
for the next launch. The save chosen at launch stays active until the game
or lobby ends. Session Mods changes the save and content for the next offline
launch while preserving your individual mod selections.

Save Manager keeps its pending-changes bar against the panel edges while the
editor scrolls. Discard and Apply retain their existing backup and conflict
checks. When creating an Adventure in the game, the horizontally scrolling
initials keyboard now draws across the widescreen viewport; its selection and
saved initials are unchanged.
