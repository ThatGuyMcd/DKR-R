# Storage and cleanup

DKR-R runs one automatic cleanup pass per application launch, starting about
ten seconds after startup. This is enabled by default. It continues while you
play, including Skip Launcher, using a low-priority worker and shorter work
slices during gameplay. After the pass finishes the worker sleeps; loading
artwork, finishing downloads or installing mods does not trigger another pass.

Open **About DKR-R > Support > Storage** in the launcher or overlay to turn
automatic cleanup off, check storage and preview what can be cleared. Press
**CLEAR DISPOSABLE FILES** after checking the preview to run cleanup yourself.
Manual cleanup also works from the overlay. Active installs and downloads can
briefly defer it. An active online lobby pins its online files, preparation
jobs and referenced package storage; unrelated cleanup can continue.

Saves, ROMs, installed mods and texture packs, settings, accounts, recordings,
texture dumps and your exports are kept. Cleanup does not decide that a ZIP is
unneeded just because it is old or has already been opened.

## What gets cleared

| Files | When they can go |
| --- | --- |
| Mod Library download archives | After the installed content matches its receipt. Stored editions and the previous edition retain a verified archive. Duplicate archive filenames retain one verified copy when recovery needs it. |
| Completed installation preparation | After both the preparation copy and installed target match the recorded checksum. Current and previous transaction journals and recovery backups stay pinned. |
| Unreferenced completed transactions | Beyond 20 operations, 256 MiB or 30 days. Referenced recovery is kept even when over budget. |
| Completed unused downloads | After 30 days, with a matching download history entry. Failed unused downloads and abandoned partials expire after seven days. Queued and paused downloads are kept. |
| Mod artwork | 128 MiB cache with oldest-used artwork removed first; unused artwork expires after 30 days. New cached artwork is reduced to the size needed by the launcher. |
| Reusable online content | 1 GiB soft cache budget, with a one-day minimum age; unused content expires after 30 days. Retained profile references protect their dependent files. |
| Abandoned imports and worker jobs | Recognised application staging older than seven days. Vehicle upload preparation expires after 30 days. |
| Immutable code and vehicle history | Keep the latest 20 snapshots; older snapshots expire after 30 days. Current, retained-history and Mod Library references protect packages. Only verified, unreferenced packages older than 30 days are removed. |
| Update files | A completed or recovered handoff releases its verified downloaded package. Old copied helpers expire after a day. Keep the newest verified recovery backup; unresolved updates protect their recovery files. |

The cache and history budgets are soft limits. Recovery references, uncertain
ownership or unfinished work can leave them over budget. Download/install
history remains visible after a disposable archive has been cleared. Activating
an older edition uses its retained archive by checksum, or requests the exact
edition again if its archive is missing.
New disposable files created after the startup pass wait until the next launch
or a manual cleanup. Turning automatic cleanup off and back on does not repeat
an already completed startup pass.

**CLEAR CACHED ARTWORK AND UNUSED ONLINE FILES** only clears those caches.
**REMOVE OLD MOD HISTORY** explicitly trims unreferenced history; it keeps
current and previous Mod Library recovery. Neither button removes installed
content. Original crash dumps are a separate manual choice: tick the option
and press **REMOVE OLD CRASH DUMPS** to remove recognised originals older than
30 days. Automatic cleanup keeps these originals. Crash-report consent and its
bounded upload queue remain separate; see [crash reporting](CRASH_REPORTING.md).

## Recovery and limits

Malformed receipts, edited content, unknown files, symbolic links, Windows
junctions and shared hard-linked files are kept. An unfinished mod publication
protects mod recovery until it is resolved. Removed profile files are first
retired into a private journalled folder so interrupted deletion can resume.
New builds register a process lease and defer maintenance when another instance
is using the same profile. Older builds do not provide that lease; close other
copies before manually clearing storage.

The first cleanup of a large existing profile can take time. Verification reads
files in bounded chunks so it can yield for foreground work or cancellation.
The status names the current stage and retains the amount already cleared.
Check Storage measures the managed categories, not every file in AppData. A
manual cleanup does not schedule another full storage measurement afterwards.
Keep personal working files out of DKR-R's private staging folders.

The same rules apply to Windows AppData, native Linux profiles and portable
profiles. Portable mode chooses a location; it does not move existing data.
