# Local achievements

Open **Achievements**, below Textures, to browse 94 personal goals. No account,
internet connection or Discord sign-in is needed. Tracking and progress are
local to DKR-R; there is no RetroAchievements login, account sync, global
leaderboard or Hardcore mode.

The objectives cover 20 T.T. times, 20 developer times, 20 Adventure Two
silver-coin races, five fully upgraded power-ups, ten five-balloon character
challenges and 19 adventure/key/trophy/boss/collection goals. Objectives are
inspired by Delmaru's original DKR achievement set; implementation, wording,
badges and sound are DKR-R work, without RetroAchievements integration.

## Browse and configure

Search, filter by category/status and sort the list. Pin an unfinished goal to
keep it visible during play. Controller A opens an entry, B returns and LB/RB
changes status filters. Search has an on-screen keyboard.

Settings controls notification position, size, duration, sound and motion.
Earned goals show a rounded notification and optional chime.

## Progress and existing saves

Progress lives in `achievements/progress-v1.json` in your active user-data
folder, separately from Adventure EEPROM. Updating DKR-R, changing ordinary
mods or deleting an Adventure does not automatically reset the collection.
Backups are separate. Import/export uses `DKR-R-Achievements.json`; importing
merges earned achievements rather than deleting your own.

Load an existing Adventure, then use **Recognise existing progress** if you want
provable save milestones credited. Recognised entries are labelled. Time
trials, power-ups and five-balloon session challenges still need qualifying
gameplay. Max Out Save does not itself generate unlocks.

T.T./developer goals need the specified original, three-lap, non-mirrored Time
Trial and vehicle, using the race timer rather than presentation FPS. Silver
coins need eight coins and first place in the same original Adventure Two race.
Trophies require gold. Balloon goals belong to one adventure/session, not totals
combined across save slots or five previously loaded balloons.

Cosmetic mods can retain eligible goals; custom courses/cups do not replace
original-course requirements. Custom character models retain their base driver's
gameplay identity. Multiplayer item goals count local human racers. Rollback
uses confirmed observations. Spectator viewing and development tools do not
award gameplay achievements.

Tracking observes game state without editing the save. Progress writes are
atomic with backups; storage failures appear on the page while play can continue.
Portable mode selects a different profile and therefore a different local
collection. See [Saves and data locations](SAVES.md).
