> Historical milestone record. The original text is preserved below; it is
> not the current feature/qualification list. See [README.md](README.md).

# Legacy mod import and asset-bank tests

This began as an **isolated implementation work area**. The main runtime now
includes `cmake/LegacyMods.cmake` for the **review-only importer preview**.
Playable course mounting and its experimental Patch Pipeline remain disabled.
No generated recompilation files or protected submodules are edited here.

## Implemented boundaries

- Exact locally owned ROM validation and canonical byte-order conversion.
- Bounded ZIP reading, path/name/type validation, independent xdelta decoding,
  checksum verification and duplicate-edition detection.
- A private worker executable with cancellation, wall/CPU/memory budgets and
  failure isolation. Resource limits are **not** a complete OS security sandbox.
- Review-only, exclusive staging; content-addressed patch/data files; no ROM or
  save modification; no executing code carried by a patch.
- Track-root discovery and structural geometry checks, including the additional
  collision allocations performed by the retail loader.
- Static object-map → object-header → model/sprite → texture/animation reference
  inspection. Dynamic spawns, object-specific entry semantics, music/particles,
  scene links and shared effects still require their adapters and qualification.
- Immutable record owners and coherent per-bank section/lookup-table views;
  checked partial reads and owner-pinned addresses; no mutable cartridge swap.
- Guarded numeric guest asset addresses, no-allocation word-swapped RDRAM
  copies and an o32 PI-call adapter with bounded arguments/queue configuration
  and exactly-one completion. The adapter is not installed in game policies yet.
- Scoped offline preparation for Big Boo and the six Community Pack roots.
  Include Big Boo's dynamically spawned missile and three HUD icons; rebuild
  selected music over original sequences without importing unrelated edits.
- A scene-ownership prototype testing outstanding borrowers, stale asynchronous
  completions, integrity binding, cancellation and stock restoration.
- Separately composed experimental Patch Pipeline fragments for both revisions:
  verify function bounds, both PI JALs/delay slots and six asset API entries.
  Existing release policies remain unchanged. The optional generator granularity
  flag is used to test freshly regenerated functions, not edit generated C.
- A resident-cache namespace retaining pointers/refcounts while hiding entries
  with different content. Model/sprite identities include their dependent
  textures/animations. 3D textures use section 2; 2D textures use section 4.
- An isolated resident-table transaction for six texture/sprite/model/animation
  lookup tables plus relocated sequence pointers and rounded sequence lengths.
  Busy readers, stale/cancelled plans, wrong RDRAM identity and overlapping table
  pointers cannot partially publish new routing. Stock restoration returns to
  ordinary cartridge addressing.

**Not implemented/verified:** installing these banks into the running game;
binding the resident transactions to actual scene/scheduler lifetimes and fault
containment; retiring all actual render/audio/cache references; real-game A → stock → B → stock
testing; full dependency closure; cross-revision materialization; additive Track
Select UI; custom records/ghost storage; content-bound online readiness/scene
handoff; consented P2P patch transfer; final Windows/AppImage packaging. All staged
packages remain disabled and uncertified. No standalone test result certifies a
track as playable or safe for online gameplay.

## Build

Run from the repository root, supplying a separate build directory with space:

```text
cmake -S runtime-recomp/tests/legacy_mods -B <build-directory> -DCMAKE_BUILD_TYPE=Release
cmake --build <build-directory> --config Release --parallel 4
ctest --test-dir <build-directory> -C Release --output-on-failure
```

On Visual Studio generators executables are in `Release/`; on the tested Linux
Makefiles generator they are in the build directory itself. Run tests from that
build directory: temporary generated fixtures are created and cleaned up there.
The inspector and corpus runner are developer tools, not packaged launcher UI.

The corpus runner requires private local files, never checked into the repository:

```text
DKRLegacyModCorpusTests <absolute-worker-path> <owned-v77-rom> <owned-v80-rom> <bigboo-zip> <haunter-zip> <community-pack-zip>
DKRLegacyModInspect --assets <owned-rom> <us.v77-or-us.v80>
DKRLegacyModInspect --geometry <owned-rom> <us.v77-or-us.v80>
```

On WSL use `--exec` when passing Windows-mounted paths containing parentheses or
spaces. Do not pass private ROM paths through an extra shell command string.

For Linux sanitizer runs use AddressSanitizer/UndefinedBehaviorSanitizer flags
and `-DXZ_SANDBOX=no` (XZ's CLI Landlock configuration rejects sanitizer builds;
its CLI tools are disabled here). Run the instrumented corpus **controller** with
the normal release worker: ASan's virtual address reservation intentionally does
not fit inside the worker's 1 GiB address-space budget.

## Dependency provenance

- Decoder: upstream xdelta **v3.2.0**, commit
  `ff322e592383227b0d65ddfde7e0e5bbc504dc15`, per-file SHA-256 pins in CMake,
  Apache-2.0. Encoder/main/external decompression are disabled. LZMA and DJW
  decoding are enabled; upstream-default-disabled FGK is not enabled.
- liblzma: upstream XZ **5.8.1**, pinned source archive, 0BSD. DKR's project-owned
  wrapper limits the secondary LZMA decoder to 64 MiB. CLI tools are disabled.
- miniz and SHA-256 primitives: unchanged existing pinned project dependencies.

Before adding the worker to release packages, include its upstream license
notices and review linked dependencies. Before linking this core into the main
runtime, resolve duplicate miniz/mbedTLS providers explicitly; the standalone
primitive target is not a license to link duplicate global implementations.

## Rollback and qualification

The accepted HUD build remains the rollback baseline. Import tests must keep the
original ROMs and input ZIPs byte-identical. Review receipts exclude machine paths
and timestamps; their hashes should match between Windows and Linux. Passing
these tests does not authorize replacing the accepted game executable with an
unfinished mod-enabled build.

### Checkpoint — 8 September 2026

After disk space was freed, WSL Ubuntu-24.04 started successfully again. Fresh
standalone builds were placed on the G: work drive, not in the formerly-full C:
build directory.

- Windows Release and Linux Release: **1,024 format/process/audio assertions passed**.
- Both platforms: original/corpus files remained byte-identical; invalid input
  failed without a completed import review. Big Boo produced one track, Haunter
  one character with two editions, and Community Pack six tracks.
- Read-only leaf-asset validation: v77 **2,890** texture/sprite/model records;
  v80 **2,915** records; zero structural rejections in either original image.
- Static dependencies traversed: Big Boo 96; Froggy Haze 182; Winter Holiday
  118; SubDrag Test Track 95; Blue Streak Road 93; Green Saturn 62; Coockie 93.
  These are static references, not full runtime or online certification.
- Both revisions passed synthetic same-carrier bank A → stock → bank B → stock
  ownership, outstanding borrower, stale completion and cancellation tests.
  The immutable directory also passed original-section identity, rebuilt offset
  table consistency, cross-record partial reads and pinned-address lifetime tests.
- Numeric bank addresses and guest DMA copies passed for all original sections
  on both revisions. Tests also cover concurrent lookups, retired addresses,
  address remounting, guest byte order, protected destination bounds and PI
  completion dispatch without retaining the bank mutex.
- Scoped preparation: Big Boo 25 records including music sequence 10; Winter
  Holiday five records including sequence 12; the other Community roots four
  records each. Their unrelated courses/names and nonselected music retain the
  original bytes. These are prepared memory banks, **not gameplay tests**.
- Linux AddressSanitizer + UndefinedBehaviorSanitizer: unit suite, direct real
  corpus decoding/analysis, and corpus controller/ownership tests completed with
  `halt_on_error=1`, without a reported sanitizer failure. The resource-limited
  child used the Release worker; no claim of exhaustive fuzzing is made.
- All **421** files in the pre-mod source snapshot still matched the working
  tree byte-for-byte. The accepted Windows ZIP and AppImage hashes also matched.
  Work consists of added isolated module/test files; the playable runtime has
  not been changed or repackaged.

Cross-platform review receipt SHA-256 values:

```text
Big Boo    c4a6d679ff64fd30ee63a76cb5608100216c92c4ef139d0daa8aded746fbb8e2
Haunter    1c990bdd712af368b32a21e17998733862ca9d0a197b751bc7c4cea2f08b9910
Community  a1764e590dd20f69bdd0d2fcefdd7bbd76f973ef9598bc2e2c054293bb8dc897
```

The next gate is actual offline runtime ownership/cache/audio integration and
the real-game switching test. Launcher integration, online content contracts,
sharing and the requested mod-enabled Beta 5 packages remain subsequent work.

### Concrete runtime integration constraints

- `AssetBus` intercepts tagged offsets **before** librecomp's PI physical-address
  masking. DKR has both `dmacopy` (v80 `dmacopy_v1`) and `__amDMA` call sites;
  both need verified Patch Pipeline bridges with their delay slots preserved.
  Nonvirtual calls must retain the ordinary Pi completion/scheduler path.
- The bus never reuses an address for different content. It keeps small address
  tombstones until game shutdown and bounds distinct banks/address space.
  Actual caches/audio must retain their mount handles; a stale virtual address
  is an error, never an invitation to fall through into cartridge memory.
- Startup-resident texture/model/sprite tables and cache keys still require
  integration. Model/sprite cache identity must include their texture/animation
  dependencies, not only their model/sprite record bytes. Reference counts and
  live pointers must not be discarded to force a reload.
- Big Boo changes sequence 10 (Ancient Lake), 4,957 → 4,752 bytes. Community
  Pack changes sequence 12 (Secret Tune), 6,696 → 6,004, and sequence 57 (Boss
  Challenges), 7,712 → 7,672. Sequence 57 is not a selected course's header music
  in these six roots, so it is not installed by this preparation profile.
- Both sample track packs retain their instrument/sample/sound banks exactly.
  Their replacement songs fit the original maximum **13,032-byte aligned**
  sequence buffers. This evidence allows a narrow sequence-only first adapter;
  it does not justify replacing instrument banks underneath active voices.
- Music preparation validates sequence tables, allocation bounds and track
  start offsets. Compact-MIDI event/loop/program semantics and actual playback
  remain uncertified. Cross-revision conversion also remains unimplemented.

### Subsequent checkpoint — generated guest and resident-table qualification

- **Windows Release and Linux Release, both v77/v80:** regenerated retail DMA,
  asset API, audio-cache, sequence-relocation and texture-free functions passed
  the isolated harness. The generated sources were consumed without edits.
  Scheduler/message queues, heap allocation and the v80 mutex endpoint were
  test doubles; these results do not prove real scheduler or renderer behavior.
- Asset API tests cover size/address/full/partial reads, untouched stock fallback,
  preserved v80 serialized entry, allocation failure, bounded failure cleanup,
  changing a selected bank during allocation, and rejection of uncertified
  whole-section compressed loading. The latter is an unused retail API, not a
  claim that all compressed assets are unsupported.
- Resident tests cover independent 2D/3D dependency identity, hidden retained
  entries, pointer-based retail freeing, exact original table/music restoration,
  reader drain, stale/cancel/replay rejection, overlapping buffers and a plan
  submitted against another guest's RDRAM. Original sequence-pointer bytes are
  cross-checked against the regenerated retail `alSeqFileNew` function.
- Big Boo and all six Community Pack courses fit the resident lookup/audio
  preparation constraints on Windows and Linux. The import receipts remain the
  same hashes shown above. They remain **not runtime-certified**.
- Linux ASan/UBSan guest harness passed both revisions. Signed shift-base
  checking is excluded only for generated C because its MIPS LUI translation
  deliberately uses signed shifts. Other checks remain, and new project-owned
  C++ retains the full enabled sanitizer set. The real-corpus controller also
  passed with the normal resource-limited worker.
- Five synthetic Python tests reject changed instruction signatures, missing
  coverage, conflicting policies, branches entering a bridge and incorrect
  function bounds. Both real ELF files also passed composition checks.
- Snapshot audit: **420 of 421 original source files remain byte-identical**.
  The sole difference is the optional `--functions-per-output-file` argument in
  the project generator. With that argument omitted, output from both revision
  policies was compared byte-for-byte against the snapshot's generator and
  matched. All three accepted release-artifact hashes still match.

To reproduce the private generated-function harness, compose each experimental
fragment with `scripts/compose_legacy_mod_policy.py`, then run the normal
`scripts/generate_recomp_config.py` / N64Recomp pipeline into separate build
directories. Use `--functions-per-output-file 1`; entrypoints are `0x80065D40`
(v77) and `0x80065F80` (v80). Configure these standalone tests with
`-DDKR_LEGACY_PIPELINE_ROOT=<directory-containing-RecompiledFuncs-v77-and-v80>`.
Run `DKRLegacyGuestPipelineV77 <owned-v77-rom>` and
`DKRLegacyGuestPipelineV80 <owned-v80-rom>`. No private ROM or regenerated payload
belongs in a public test fixture or release source package.

**Full milestone remains incomplete.** See the subsequent importer-preview
checkpoint below; earlier statements about the main runtime/build refer to
their dated checkpoints, not the current import UI.

### Launcher importer-preview checkpoint

- Main runtime builds the private `DKR-R-ModWorker` and the asynchronous review
  library. The launcher/overlay has Custom Tracks and Custom Characters - Coming
  Soon disclosures, file picking, a cancellable progress modal and a persistent
  read-only list of discovered content. It explicitly says content is not activated.
- A single controller-owned job runs at a time; duplicate requests are rejected,
  never accumulated on a UI/network thread. Review snapshots perform no disk I/O.
- Published reviews are content-addressed. Interrupted jobs cannot appear in
  the library. Duplicate imports merge by content identity; cancelled/failed jobs
  leave the last valid view intact. Metadata counts, text length, JSON depth and
  directory scans are bounded.
- The game reuses its existing RT64 miniz and Mbed TLS providers. A separate
  worker archive uses the same object code but links only the small decoder,
  miniz and hashing providers, not RT64/SDL/windowing/network services.
- Windows/Linux library tests passed the real Big Boo, Haunter and Community
  archives (eight content entries), reimport deduplication, cancellation, worker
  failure, restart persistence and review-only integrity checks. These remain
  import/controller tests, **not gameplay or visual certification**.
- The accepted gameplay/HUD/online Patch Pipeline policies and payload source
  selections are untouched. Only UI/build/packaging glue is integrated so far.
- Packages include worker startup/hash/invalid-input self-tests and third-party
  notices. See `docs/history/LEGACY-MOD-IMPORT-PREVIEW.md` for the honest playtester scope.

Still required before calling the approved modding milestone complete: actual
in-game mounting/switching qualification; additive Track Select/menu/records;
cross-revision materialization; content-bound online readiness and consented P2P
sharing. No import review is a runtime or online compatibility certificate.
