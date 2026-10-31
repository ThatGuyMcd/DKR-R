# Saves and Save Manager

Your normal Adventure now stays with you when you change mods. There is one
personal save with three slots, not a new playthrough for every mod list.
Online and development progress still have their own owners.

## Your personal Adventure

Normal play, character mods, custom tracks and ordinary Custom Magic Codes use
the same personal 512-byte EEPROM. Installing a .dkrmap must not change the
stock EEPROM layout. Added-course records are stored in a separate companion
file, so extra tracks cannot shift your initials or balloon totals.

Older saves under `saves/mods/<fingerprint>/` are kept for recovery. They are
not automatically combined. Open **Save Manager > Backups & Recovery**,
inspect the copy you want and confirm the restore. The previous personal
save is backed up first. Do not delete the old folders to tidy your profile.

## Save Manager

- **Personal save:** manage the three Adventure slots, shared unlocks and records.
- **Edit progress:** choose the intended slot before changing progress. Max Out
  and unlock actions save after confirmation. Manual edits need Apply Changes.
- **Transfer & Paks:** import/export saves, Controller Paks and supported bundles.
- **Online:** inspect the host's separate Adventure and choose progress for hosting.
- **Backups & Recovery:** find older mod-set saves, history and recovery copies.
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
progress has disappeared. Personal desktop EEPROM is `saves/dkr.us.v77.bin`;
both supported US revisions use the compatible stock layout.

Back up your data before updating. To recover, export and inspect the surviving
copy first, then restore it deliberately. No editor can recreate progress when
no valid copy survives. See [Troubleshooting](TROUBLESHOOTING.md).

## Spectators and achievements — v1.0.12

Watching uses session-owned state, not your personal Adventure or Paks. The
host owns communal online progress; confirmed handovers do not transplant a
save into another participant's personal profile. Development remains isolated.

Achievements have separate `achievements/progress-v1.json` storage. Max Out
does not itself award gameplay goals. Recognising provable existing milestones
is an explicit achievement setting. Spectating and development do not award
gameplay progress. See [Achievements](ACHIEVEMENTS.md) and [Spectators](SPECTATORS.md).
