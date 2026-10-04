# v1.0.5 Beta 5 — legacy importer preview

This is an **import-workflow testing candidate**, not the completed legacy
modding feature. Importing a patch does not yet make its track playable.
The previous accepted HUD build remains the rollback build.

## What to test

1. Extract the complete Windows ZIP, or run the Linux AppImage. Keep the private
   `DKR-R-ModWorker` executable with the package; copying only DKR-R is insufficient.
2. Import your original Game Pak(s) in Play. A patch must match its exact source
   revision/checksum. An imported v1.1 ROM cannot stand in for v1.0 patch input.
3. Open **Mods / Hacks → Custom Tracks → Import Legacy Patch / ZIP**.
4. Select an `.xdelta`, or a ZIP containing one or several independent patches.
   Watch the animated progress indicator; Cancel should return without changing
   original tracks, ROMs or saves.
5. Check the detected names and Game Pak revision. The supplied Big Boo sample
   should identify one track; Community Track Pack six; Haunter one character
   entry, deduplicating its two editions. Characters remain **Coming Soon**.
6. Reimporting the same archive must not duplicate entries. Restarting the
   application must retain completed reviews. A wrong-ROM or invalid archive
   should produce a readable failure while leaving the launcher responsive.

These reviews are stored under the active configuration directory's
`mods/legacy/reviews`. Temporary importer jobs are separate. No review is an
enabled catalog entry, and interrupted jobs never become installed content.
No patched ROM is saved or booted; no patch scripts or executable game changes
are executed. Use trusted patches: process resource limits are not a complete
operating-system sandbox.

## What is deliberately not enabled yet

- Additional Custom Tracks entries in in-game Track Select and actual course mounting.
- Runtime qualification of dynamic assets, scene switching, music and cache lifetimes.
- Cross-revision conversion, custom records/ghosts, content-matched online play
  and consented peer-to-peer patch sharing.
- Additional custom character slots or Lua modding.

Stock gameplay, the accepted HUD, shadows and online simulation patches are
unchanged in this preview. A review may identify a legacy hack without being able
to support its executable changes. Compatibility with every hack is not claimed.
