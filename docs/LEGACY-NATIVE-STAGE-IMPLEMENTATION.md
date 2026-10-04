# Native-stage character selector — private candidate

2026-09-09. Implements the approved same-stage direction in
`LEGACY-NATIVE-CHARACTER-STAGE-PLAN.md`. This is a private qualification
candidate, not a public Beta 6 release or a completed mod-compatibility claim.

## Changes in this pass

- The active selector no longer draws the portrait grid or accepts R-page
  navigation. Original Player Select/OK text and the native stage remain.
- Prepared characters now own their selection header, model, animations and
  validated player-sign materials in addition to the already working vehicle
  assets. Original records are retained; new records have separate IDs.
- Yooka and Haunter use additional native objects at the two outer ends of
  the lower row. Models are scaled uniformly. Their idle dance and selected
  pose follow the original music clock. Each has independent player-sign
  timing and original confirmation particles.
- Per-controller logical identities remain outside fixed-size native actor
  tables. The donor cursor is hidden only for a player selecting a custom
  actor; original Conker/T.T. remain selectable when normally unlocked.
- Directional movement follows physical screen direction. Native A/B/Start,
  ready/party confirmation, AI assignment and racer resource commit remain
  authoritative. Moving between actors retains the native music fade request.
- Select, cancel and final-confirm voices are extracted from each patch's
  sound table and its reachable sound/sample dependencies. Only these small,
  validated banks are mounted. Global sound/music banks are not replaced.
  Each player's native sound handle remains separate. Custom sequence-bank
  instrument substitutions are not enabled without separate mapping evidence.
- Native object-list and cleanup-queue capacities are checked. Objects queued
  for native destruction cannot fall through into fixed retail actor tables.
  Small relocated sound-control banks persist until guest shutdown, because
  queued audio can outlive the menu; subsequent menu visits reuse them.

## Patch ownership

All authored changes are project-owned adapters and Patch Pipeline scripts.
No submodule or generated-function file was hand-edited in this pass.

`prepare_legacy_character_menu_fragment.py` and
`compose_legacy_mod_policy.py` define and validate character-menu ABI 3:
18 data fields and nine hook boundaries, independently checked against both
retail ELFs. Existing netplay input-lock and AI/resource-commit hook ordering
is retained. Full function hashes and instruction signatures reject unknown
overlap or moved hook sites.

The hooks cover final menu initialization, collected input, original text
render return, menu exit, human/AI roster commit, native directional movement,
custom stage-object loop, stock stage cursor lookup and scoped voice playback.
The text-render hook now leaves the native presentation untouched.

No new edits were made to HUD/aspect-ratio, shadow/interpolation, racing,
netplay pacing, save routing or launcher presentation in this pass.

## Executed checks

- Windows Release runtime: build passed.
- Linux Release runtime under WSL Ubuntu 24.04: build passed.
- Focused CTest suite: **19/19 passed on each platform**.
- Native-stage adapter/lifetime harness: **5,153 assertions**.
- Audio bounds/canonical-closure harness: **141 assertions**, including
  truncated banks, invalid pointers, cyclic sound chains, sample bounds and
  invalid loops for raw and ADPCM audio.
- Real Yooka and both Haunter delta editions: **7,945 preparation/ownership
  assertions**, separately passed on Windows and Linux.
- Real import-worker/catalogue/artifact persistence: **59 assertions** on
  each platform. Reload, dependency tampering, re-preparation and original
  asset namespace preservation are checked.
- Real extracted voices through regenerated `alBnkfNew` and its original
  helper: **273 assertions per revision per platform**. These verify control,
  key/envelope, wave, loop/book and virtual sample pointer relocation. They do
  not constitute a listening test.

Private Windows executable:
`C:/DKRPort/build/dkr-runtime-rt64-lod-bias/bin/Release/DKR-R.exe`

SHA-256:
`a71adc0837af47f9ae664b09392a354de4d1f8ab17933bf0b54b9afc8e5a48e0`

Private Linux executable:
`C:/DKRPort/build/dkr-runtime-linux/bin/Release/DKR-R`

SHA-256:
`fc0c3ad7323b6c68e0b2d77379fdf79d06dbb0703cb0537a40c208c7c2adc6bb`

The Windows candidate was opened with the isolated `yooka-visual-test`
profile and `native-stage/private-selector-qualification.json`. The title
sequence runs. At this checkpoint the native stage has **not** yet received
visual/audio acceptance. Automated short keyboard taps did not advance the
title menu; the user was asked to use the connected controller. The live
runtime log remains exclusively locked while that copy runs.

## Rollback and outstanding gates

The prior accepted functional portrait-selector executable is preserved at
`G:/DiddykongWorkFolder/custom-characters-20260909/native-stage-rollback/DKR-R.exe`,
SHA-256
`de0fa27aa50b556d6133012b2505112b44f6ac0abb1833b4c54c7a12241c259f`.
Selected source files and the old private recipe accompany it; this is not a
claim that every source file or a complete distribution was snapshotted.

Before promotion, verify actual 4:3/widescreen stage visibility, floor contact,
idle/selected animation, player signs, all three voice actions, mixed stock/
custom P1-P4, rapid confirm/cancel, race entry and repeated menu return.
Also verify the stock stage with the normal unlock combinations.

The first arrangement deliberately tests **two additions only**. The stored
library and resident-asset bounds are not a claim that unlimited actors fit
on this camera. More simultaneous actors require the approved capacity
decision; do not silently introduce pages, reduced rosters or squeezed models.

Public activation/revision conversion/online agreement gates from the broader
legacy-mod phase still apply. Both builds currently require private
qualification admission. The Linux AppImage packaging command was attempted
and correctly refused `DKR_LEGACY_QUALIFICATION=ON`. That guard was not altered
or bypassed. There is therefore **no newly packaged AppImage/public release**
for this checkpoint. Matching Windows and AppImage release packaging remains
required after qualification and normal activation are ready.
