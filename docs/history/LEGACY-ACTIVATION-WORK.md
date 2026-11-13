# Legacy activation phase — v1.0.5 Beta 5

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


Implementation authorized 2026-09-08. This checklist is implementation state,
not a declaration of gameplay compatibility. Do not package as complete until
the remaining gates are actually met.

## Rollback

The accepted importer-preview packages were copied without modification to
`G:/DiddykongWorkFolder/rollback-pre-legacy-activation-20260908`.

- Windows ZIP SHA256: `62968e3b0190930bfe350f77007a86f868775c5475e3c8c3f050ff2a0f359914`
- AppImage SHA256: `9df5e5a8999d47f6bea44c455d12e492f79783a530095f44bc3421f61391f849`
- Source ZIP SHA256: `9a10ac22d341d5d61862ad361c810b8e9407ae9428387c501da4d3b08a022ce4`

The earlier accepted HUD rollback is also unchanged. Preserve all pre-existing
worktree changes. Do not edit submodules or generated recompilation sources.

## Required gates

- [x] Preserve accepted packages and matching source archive.
- [ ] Validated installed catalogue, immutable prepared banks and persistent enablement.
- [ ] Guest asset API/PI integration through checked v77/v80 Patch Pipeline policies.
- [ ] Scene lifetime, resident tables, renderer/audio/cache ownership and fault containment.
- [ ] Real custom A → stock carrier → custom B (same carrier) → A → stock qualification.
- [ ] Native Custom Tracks section, live wooden-frame previews, safe rapid selection.
- [ ] Race setup, capabilities, restart/results/return navigation.
- [ ] Custom records/ghost identity and in-memory/persistent stock save isolation.
- [ ] Additive character definitions, picker, per-racer resolution and donor coexistence.
- [ ] Supported runtime-revision materialization and explicit unsupported-content handling.
- [ ] Online catalogue/content/scene/character identity, rollback and actor qualification.
- [ ] Consented bounded peer-to-peer patch sharing and content preparation.
- [ ] Regression matrix including accepted HUD, shadows, framing and online transitions.
- [ ] Windows and Linux AppImage builds, package verification and accurate testing notes.

## Design constraints

Custom menu rows and character identities must never index beyond retail
arrays. A retail carrier is an internal execution detail, not public content
identity. No arbitrary patch-supplied executable code is run. Stock selection,
progression and save files stay intact. Characters are additional entries, not
a global replacement of their donor. Unknown dependencies fail closed.

The existing importer and asset bridge tests are useful foundations, but do
not establish real scene lifetimes, full gameplay, or online compatibility.

## Private qualification progress — 2026-09-09

Not a packaged or finished phase. The accepted rollback archives remain
unchanged. The main Windows development build is deliberately configured with
`DKR_LEGACY_QUALIFICATION=ON`; both packaging scripts reject it before staging.

- Persistent content-addressed track catalogue and worker preparation are
  implemented; 113 fixture checks passed on Windows and Linux. Hash-directory
  depth exposed MAX_PATH on Windows; private mod-storage paths now use explicit
  extended absolute paths. No process-wide filesystem setting was changed.
- Checked scene/asset hooks exercised real custom → stock → different custom
  sharing the same carrier through the guest loader. The first controlled
  90-second loader test completed 2,678 graphics tasks and exited normally.
- Additive logical menu rows preserve the retail arrays and native drawing,
  wooden frames and zoom code. Root identity accompanies an accepted background
  load; a late loader never reads the current cursor to choose its content.
- Native menu adapter: 160 focused tests on each platform. Pipeline guards
  validate 26 data symbols, complete function hashes and 19 hook sites for
  each supported revision. Existing presentation/network hook owners remain.
- The first native-menu runtime test exposed a real return-delay-slot bug in
  the new hook: `bgload_start` writes success in the JR delay instruction.
  Adapter ABI 2 derives the observed outcome from that checked epilogue,
  leaving the original instructions and return register untouched. A new test
  executes regenerated `bgload_start` for each revision, varying the previous
  return register and exercising accepted/busy same-carrier requests.
- After that correction, the private native-menu 90-second test loaded exactly
  A → stock → B → stock → A → stock with no unexpected reloads or fatal errors;
  2,678 graphics tasks, clean exit. Log:
  `G:/DiddykongWorkFolder/legacy-activation-20260908/native-menu-v77-fixed-delay/logs/runtime.log`.
- The private automatic menu probe now masks only guest menu navigation while
  it owns the scripted cycle. It does not control the user's other applications.
- Standalone test suites: 9/9 passed on Windows and Linux, including actual
  regenerated background-loader tests for v77/v80. These are not visual QA,
  complete-race, online, character, or cross-revision materialization approval.

Native menu generation is reproducible via
`scripts/generate_legacy_menu_qualification.py`. All generated C remains outside
versioned sources and is produced solely by the Patch Pipeline/N64Recomp.
Public activation remains gated: stock record/save isolation, custom ghosts,
character runtime, cross-revision conversion, online content agreement/sharing,
long-session asset limits, failure UI and complete-race regressions remain open.

### First private race entry — 2026-09-09

- A separate recipe enables `private_races` without the automatic preview
  cycle. Its guest save/config/Pak directory is
  `G:/DiddykongWorkFolder/legacy-activation-20260908/native-race-v77-check-20260909`;
  it is not a normal user profile or a public activation certificate.
- Big Boo's Haunt was observed running with racers, a live race timer, lap
  counter and HUD. This establishes race entry/rendering, not a completed-race,
  player-control, results/retry, ghost, or save-isolation qualification.
  The running window was left untouched after external input was detected.
- Fixed the logical race binding being cleared by native Results menu 17.
  Seven new checks cover Results retry, unrelated menu returns and mismatched
  carrier loads; 167 adapter checks and all 9 CTest tests pass on both platforms.
- The first race probe still bypassed character selection, leaving native
  character/controller initialization unqualified. The next interactive probe
  now follows the normal title/character/menu path, selecting the first custom
  cell only once Track Select is reached. This latest probe adjustment has not
  yet been rebuilt while the user is testing the open candidate.
- Private race recipe:
  `G:/DiddykongWorkFolder/legacy-activation-20260908/private-race-qualification.json`.
  The main build remains qualification-only; package guards stay enabled.

### All-Krunch test roster diagnosis — 2026-09-09

The user confirmed that the custom races behaved as intended, but every racer
was Krunch. The completed private log records race entry, a repeat load of
carrier 5, returns to Track Select, and entry into the other custom course that
uses the same carrier, followed by a clean runtime stop. These observations
do not yet certify every restart/results path.

The first private race shortcut entered menu 15 directly and bypassed native
character/roster initialization. Native `menu_character_select_loop` assigns
the player slots and calls `charselect_assign_ai`; selecting Tracks in
`menu_game_select_loop` then calls `init_racer_headers`, which copies those
characters into `Settings::racers`. `CHARACTER_KRUNCH` is character ID zero.
The shortcut skipped these assignments; the observed all-Krunch roster matches
the uninitialized racer character fields, not an intended custom-track rule.

Interactive qualification now retains the original title/character/game-select
flow. It only positions the first custom cell after native Track Select init.
An explicit guard rejects recipes combining playable races with the automatic
preview shortcut. This changes only development qualification, not the stock
roster algorithm, custom assets, launcher, or networking.

The corrected Windows private executable rebuilt successfully. A deliberately
invalid playable/automatic-preview recipe was rejected before guest startup
with the expected explanation. An ordinary interactive-recipe boot completed
218 graphics tasks and stopped cleanly after its eight-second watchdog, without
jumping into Track Select. All 9 existing CTest tests pass on Windows and Linux;
the corrected human-selected/AI roster still requires the user's visual check.
An untimed follow-up uses the separate
`native-race-v77-player-roster-20260909` profile under the private work directory.

### Native Track Select entry and navigation sound — 2026-09-09

The user confirmed the corrected native character/AI roster and custom race
behaviour. They then reported the remaining automatic jump to Custom Tracks
and missing navigation ping.

- Removed the playable qualification recipe's custom-cursor positioning. The
  original menu owns fresh entry and ordinary remembered selection. Only an
  explicitly non-playable automatic preview probe may still drive the cursor.
- Also found and fixed a title-screen re-entry edge case in the menu adapter:
  native `menu_track_select_init` resets to Dino Domain when
  `gTitleScreenLoaded` is set. The adapter must not restore a remembered custom
  row over that native reset. Direct race returns still retain the custom row.
- The title flag is read, not consumed, by the adapter. It comes from a checked
  symbol in each revision's ELF. Menu ABI 3 now validates 27 data fields and
  the same 19 hook sites. Older ABI fragments are explicitly rejected.
- Custom navigation masks the stock stick input to protect the retail arrays.
  That also bypassed the native successful-movement sound branch. A committed
  custom move now requests original `sound_play(SOUND_MENU_PICK2, NULL)` via
  the revision's payload callback. Calls run outside the host adapter mutex
  with a copied register context. No new sound asset or launcher audio path.
- No additional ping for blocked edges, idle input, confirmations, delayed
  navigation or native stock movement. Both supported revisions expose the
  same verified native sound callback; SOUND_MENU_PICK2 is 0xEB.
- 316 adapter checks pass on Windows and Linux, including title-entry reset,
  remembered selection and exactly-once navigation notifications. All 9 CTest
  tests pass on each platform, including the regenerated v77/v80 background
  loader tests using ABI 3. Visual/audio confirmation is still the user's test.

Custom Characters implementation has not started in this turn. The proposal
and source evidence are in `LEGACY-CUSTOM-CHARACTERS-PLAN.md`; approval is
required before character-runtime changes. Existing release/activation gates,
accepted rollback packages, stock saves and qualification packaging guards
remain unchanged.

The ABI 3 private Windows candidate rebuilt successfully at
`C:/DKRPort/build/dkr-runtime-rt64-lod-bias/bin/Release/DKR-R.exe`.
This is not a new public package or AppImage release. No test game is left
running by this turn. The user will perform the next visual/audio check using
the private race recipe and a separate profile; the normal native title,
character and game-select path is required.

### Approved Custom Characters implementation and Yooka fixture — 2026-09-09

The user approved `LEGACY-CUSTOM-CHARACTERS-PLAN.md` and supplied
`E:/Downloads/yooka in dkr.xdelta` as an additional test fixture. The earlier
"implementation has not started" paragraph above describes the previous
track-only turn, not current status.

Before these character edits, the private Windows binaries and source were
snapshotted in `G:/DiddykongWorkFolder/rollback-pre-custom-characters-20260909`.
The accepted public rollback packages remain unchanged.

Implemented project-owned preparation and runtime foundations:

- Structured reviewed character roots; bounded dependency traversal through
  vehicle headers, model LODs, animations, textures, sprite wheel/attachment
  resources and portraits. Original ROMs and unrelated patch changes are not
  installed or modified. Unknown behaviour/header layouts remain blocked.
- Yooka's exact reviewed reconstruction relocates its asset directory from
  `0xecb60` to `0xe1240`. Recognition is pinned to the reconstructed image hash;
  it does not scan arbitrary offsets or execute the patch's rebuilt code.
- Normalized character IDs deduplicate both Haunter editions. The complete
  patch is not the identity of an individual character's resource graph.
- A boot-owned append-only asset namespace relocates all included model,
  animation, texture, sprite and object-header references. Original record IDs
  remain unchanged. Expanded tables are installed before native allocation;
  course changes cannot introduce or mutate that boot-owned namespace.
- A logical human roster selects distinct per-vehicle headers; stock racers,
  unrelated objects, bosses and AI retain their original headers. Two distinct
  customs derived from T.T. can have separate identities alongside stock T.T.
- Three revision-verified Patch Pipeline boundaries handle character-menu
  reset, native roster commit, and the original object header store/load pair.
  Full function hashes, symbols and instruction words are verified. The
  existing online AI seed hook is explicitly retained before the new roster
  callback. Unknown hook conflicts fail composition. No generated source or
  submodule was hand-edited.

Evidence as of this checkpoint:

- Haunter: 237 dependency records, 18 models; both delta editions normalize to
  `20bb9bf1dc0f5389bacd460c4ccd1e683f3ed3d339e07877050088ebefc2b642`.
- Yooka: Conker-derived car, hovercraft and plane resources; 219 dependency
  records, 18 models; normalized identity
  `9182dee4f94ff73f35a0cf73d729fb8453feaba16aaf93cecd9b4bc8e5413323`.
- Combined namespace: two characters, 456 appended records, fingerprint
  `367ec86af8735ea541a43213ce489cb826ca255d435d3014d670b12227e168da`.
  Windows and Linux produce the same result; 7,689 fixture assertions pass
  on each. These include unchanged original records, relocated dependencies,
  deduplication, course/character coexistence and rejected unsafe changes.
- All 12 focused CTest suites pass on Windows and Linux. Regenerated native
  `spawn_object` prologue tests cover 72 header-loading routes per revision,
  preserving its header-lifetime store, original entry pointer and saved
  registers. The loader is deliberately stubbed to return allocation failure
  after this boundary: this is not a full-object/gameplay test.
- The Windows private runtime compiles with the new pipeline output. A
  ten-second isolated startup with Yooka and Haunter admitted completes 276
  graphics tasks and stops cleanly. This verifies initial expanded-table
  loading, not visible custom racer models, race playback or animation.
- Private evidence and generated inputs are under
  `G:/DiddykongWorkFolder/custom-characters-20260909`. The startup profile is
  `startup-proof`; no test game remains running after its watchdog exit.

Remaining gates are substantial: native custom-model/animation coexistence
in real races, paged additive character selection, real library preparation
and activation, portraits/results/lifecycle/persistence, revision conversion,
online manifest agreement and final Windows/AppImage release qualification.
The current private recipe requires choosing a matching retail behaviour for
its resource proof; it is **not** the promised additional character-selection
slot and must not be presented as the completed feature. Character preparation
still reports `runtime_certified=false`; production packaging guards remain.
No new public Beta 5 package has been issued at this checkpoint.

### Character library persistence and first native racer retry — 2026-09-09

The requested next release is **v1.0.5 Beta 6**. It has not been packaged yet.
This checkpoint supersedes the earlier combined-namespace hash/test counts.

- Added bounded character artifact read/write and complete dependency-graph
  revalidation, including behaviour/header provenance, animation ownership,
  hashes, decoded bounds and canonical content identity. Forged manifests may
  not turn arbitrary records into character dependencies.
- Added isolated worker `prepare-characters` transactions, separate
  `prepared-characters` storage and `character-catalog.json`. Track catalogue
  data remains separate. Interrupted/failed preparation cannot enable a
  partial character. Launcher/selector integration is still outstanding.
- The user authorized a private Yooka visual test. The first selection failed
  the expected-base guard; the next selected Conker correctly, committed Yooka
  and remapped the car root from 5 to 312, but stopped at the DMA alignment
  guard. That was a real loading failure, not successful gameplay proof.
- A new fixture check reproduced unaligned animation tail placement: appended
  section 32 record 500 had decoded length 2704 and packed length 1690. Retail
  loading uses `allocation + decodedSize + 0x80 - packedSize`; this produces an
  unaligned destination. Some copied legacy animations lacked trailing packed
  padding. Appended animation copies now gain zero padding to eight-byte
  alignment. Decoded keyframes are checked byte-for-byte unchanged. Original
  records and both DMA alignment/bounds guards remain unchanged. The asset API
  failure now also records section, offset, destination and length so another
  failing route can be distinguished from this packing defect.
- Windows and Linux pass all 13 focused CTest suites, 7,786 real fixture
  character preparation/ownership assertions, and 59 persisted character
  catalogue/worker checks. Both platforms produce combined namespace hash
  `c7bc022a0c43e65bc9919282a840f9da6e54676b6db452e8557852d0e68ad755`.
- The Windows private game rebuilt successfully and was reopened for the
  user's model/animation test. These assertions and compilation do not replace
  that runtime check. The separate profile is `yooka-visual-test` under the
  existing private evidence directory; no normal user saves are involved.

The public activation, additive selector, lifecycle/portrait, revision and
online gates above still apply. No public release-package guard was bypassed,
and this private Windows retry is not a completed Beta 6 release/AppImage.

### Additive character-select candidate — 2026-09-09

The user confirmed that the corrected private Yooka model/animation test
worked perfectly, then requested character selection. The accepted proof
executable was preserved before this work at
`G:/DiddykongWorkFolder/custom-characters-20260909/selector/accepted-model-proof/DKR-R.exe`,
SHA-256 `375167f9c14e71f899c26104d560f9c8b7e46c4290ac19d6f5a5faec08e06489`.
This is model-test acceptance, not acceptance of the new selector below.

- Added project-owned `CharacterMenuAdapter` and `CharacterMenuRenderer`.
  The initial page remains the original roster. R shoulder opens a separate
  Custom Characters page, with eight uniformly scaled portrait cards per page,
  name labels, page ownership and P1-P4 selection/ready indicators. L returns
  to the previous page or Originals. Custom choices are independent; page
  changes do not clear another player's selected character.
- Original menu indices stay within native arrays. At the original human
  roster commit boundary, custom identities resolve to their verified retail
  behaviour plus the separate logical resource identity. The existing native
  AI/ready/launch path remains in use; no forced Conker selection is required
  in this candidate. Stock movement only uses the small logical-uniqueness
  adapter when a custom selection exists. DOUBLEVISION is respected during
  selection and roster validation.
- Custom page input, native audio calls, rendering, portrait loads and frees
  remain on the guest execution path. No guest callback is made under a host
  mutex. Menu portrait references and scratch memory are released through
  the original texture/heap functions before leaving character select.
- Six checked Patch Pipeline hook boundaries are authored separately for
  v77/v80: final menu initialization return, post-menu-input, final character
  text render return, menu free entry, native roster/AI commit entry and native
  movement entry. Full function hashes, instruction signatures and 14 data
  fields are validated. Composition preserves the existing input lock and
  AI seed/resource commit hooks in their explicit order, rejecting unknown
  overlaps. Generated sources and submodules were not hand-edited.
- Both platforms pass all 16 focused CTest suites. The pure menu/renderer
  harness passes 700 assertions; regenerated native movement tests pass 17
  checks per ROM revision. These cover four players, paging, ready/cancel,
  logical duplicates, compacted controller ownership, checked guest memory,
  uniform portrait scale and resource ownership. Rendering callbacks in the
  pure harness are stubs; they do not establish actual screen appearance.
- The private Windows runtime compiled and opened with
  `selector/private-selector-qualification.json` and the isolated
  `yooka-visual-test` profile. Candidate executable SHA-256 is
  `de0fa27aa50b556d6133012b2505112b44f6ac0abb1833b4c54c7a12241c259f`.
  The user can select Yooka or Haunter directly. Visual/launch acceptance of
  this selector is pending. Its live log is exclusively locked while open;
  no claim of log-verified menu rendering is made.

This checkpoint does not complete the public Beta 6 release. Private recipe
admission is still required; real launcher activation, complete portrait and
lifecycle coverage, long-name presentation, cross-revision asset conversion,
online agreement and Windows/AppImage release qualification remain gated.
The current resident-character budget is 16, distinct from the larger stored
catalogue. No public package guard was bypassed and no public package was
relabelled as a completed release.

### Native-stage replacement — 2026-09-09

The portrait-page design above has been superseded at the user's request.
The approved same-stage implementation and its Windows/Linux candidate are
recorded in `LEGACY-NATIVE-STAGE-IMPLEMENTATION.md`. That record contains the
current hashes, native audio/animation ownership checks and the uncompleted
visual/release gates. The old portrait selector is preserved as the rollback,
not the active presentation. No release-packaging guard was bypassed.
