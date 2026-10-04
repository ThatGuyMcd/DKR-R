# Additive legacy Custom Characters — implementation proposal

Date: 2026-09-09. Status: **approved; implementation and qualification in progress**.

The user additionally supplied `yooka in dkr.xdelta` as a second test fixture.
Its SHA-256 is `d3eff3bed957afdb2fb56e585e24e45650a737f7df08be14b5f46d34b6557ed8`.
Read-only reconstruction against US v1.0 succeeds. This exact image relocates
the asset directory from `0xecb60` to `0xe1240`; a hash-pinned assets-only reader
is being qualified. Rebuilt executable bytes are never applied. Analysis finds
Conker-derived car, hovercraft and plane models, portrait 126, and six appended
3D textures. Recognition is not a claim of runtime compatibility.

Pre-character source and Windows private-candidate rollback:
`G:/DiddykongWorkFolder/rollback-pre-custom-characters-20260909`.
The release qualification and Linux packaging gates below remain in force.

## 1. Requested result and boundaries

Import `.xdelta` files or ZIPs containing one or more patches, identify their
character changes, and add separately selectable characters without replacing
any original racer. Multiple characters derived from the same original must
coexist, including in the same race. The installed library must be pageable,
not limited to one replacement for each of the ten retail character IDs.

Preserve the accepted HUD, shadows, interpolation, single-window launcher,
controller ownership, boss-race topology, saves and online pacing. No Lua or
arbitrary mod code execution is included. No direct edits to submodules,
RecompiledFuncs or RecompiledPatches: runtime integration must be generated
from checked, project-owned Patch Pipeline metadata and adapters.

The latest private track candidate is not a completed public mod runtime. The
normal launcher still exposes a review-only importer; private recipes admit
the test courses. This plan must finish character activation through the real
library, not leave it dependent on another developer recipe. The broader
release gates in `LEGACY-ACTIVATION-WORK.md` remain applicable.

## 2. Evidence from this checkout and supplied Haunter patches

| Finding | Evidence | Consequence |
| --- | --- | --- |
| Character import currently detects a label, not a playable character. | [legacy_mod_format.hpp](C:/DKRPort/runtime-recomp/src/game/mods/legacy_mod_format.hpp), `Analysis::characters`; [legacy_mod_format.cpp:265](C:/DKRPort/runtime-recomp/src/game/mods/legacy_mod_format.cpp:265). | Add a structured character definition and preparation path; changing the Coming Soon label is insufficient. |
| Preparation explicitly blocks character activation. | [legacy_track_prepare_job.cpp:109](C:/DKRPort/runtime-recomp/src/game/mods/legacy_track_prepare_job.cpp:109), reason: character content requires the additive racer adapter. | Keep that gate until the character-specific adapter passes its tests. |
| Character selection uses fixed retail tables and a separate mapping to gameplay character IDs. | [menu.c:826](C:/DKRPort/extern/dkr-decomp/src/menu.c:826), unlock-dependent directional tables; [menu.c:7138](C:/DKRPort/extern/dkr-decomp/src/menu.c:7138), player mapping. | A new menu index cannot simply be written into a native character field. |
| Retail character IDs index gameplay assets and properties. | [structs.h:234](C:/DKRPort/extern/dkr-decomp/include/structs.h:234), signed-byte character; [objects.c:1274](C:/DKRPort/extern/dkr-decomp/src/objects.c:1274), character plus vehicle times NUM_CHARACTERS; [structs.h:1140](C:/DKRPort/extern/dkr-decomp/include/structs.h:1140), runtime character ID. | Use a stable logical mod identity alongside a valid retail base ID. Do not expand stock arrays or store library indexes in them. |
| Native model caching compares the original model ID alone. | [object_models.c:98](C:/DKRPort/extern/dkr-decomp/src/object_models.c:98), lookup; [object_models.c:127](C:/DKRPort/extern/dkr-decomp/src/object_models.c:127), bounds/table lookup; [object_models.c:892](C:/DKRPort/extern/dkr-decomp/src/object_models.c:892), animation loading. | A global replacement would make Haunter and T.T. share the wrong model or animations. Per-racer content and cache ownership are essential. |
| Native roster initialization must remain in the launch path. | [menu.c:7049](C:/DKRPort/extern/dkr-decomp/src/menu.c:7049), AI assignment; [thread3_main.c:1126](C:/DKRPort/extern/dkr-decomp/src/thread3_main.c:1126), racer-header initialization. | Preserve player and AI initialization. The previous all-Krunch private-test shortcut demonstrated the consequence of bypassing it. |
| Portraits are also selected through retail character identities. | [menu.c:10430](C:/DKRPort/extern/dkr-decomp/src/menu.c:10430), `menu_racer_portraits`. | Selection, race HUD and results need the same logical-racer identity, without modifying their established positions/scales. |
| Online catch-up guards the native roster. | [authoritative_state.cpp:1134](C:/DKRPort/runtime-recomp/src/game/netplay/authoritative_state.cpp:1134). | Do not relax this guard to allow mods. Introduce a separately validated logical roster and agree on it before launch. |
| The current UI explicitly promises additive characters. | [runtime_ui.cpp:8819](C:/DKRPort/runtime-recomp/src/game/runtime_ui.cpp:8819). | Replace the placeholder only when importing, enabling and selecting actually work. |

The existing inspector was rerun against both supplied Haunter delta editions
using the verified US v1.0 source. Both identify the reviewed
`sixtyfour-haunter-2019` profile, and both contain the same asset-area digest:

`db3f3fdd59e9adb7e95e8255fcc952c12b444e314579f7abad17a5a6cae0eb34`

Their target digests differ:

- `92c016c905b08d4063ccf77a46fac229530d2267accda0dbbf14d87e54607102`
- `4fe181d105b69e527d7c175da565be146667308262ed1fff3ee304339649c20c`

The inspector reports 84 changed asset records for each complete patch and a
T.T.-derived Haunter with car, hovercraft and plane content. **84 is not the
character dependency count.** The patch also contains unrelated changes and
reviewed unlock conveniences. The current recognized-profile result is not
proof that additive Haunter works at runtime.

This gives us a useful first qualification fixture and a deduplication case:
the two editions should produce one equivalent character definition if their
character dependency graphs and inherited behaviour match. Their original
unlock/code edits must not be applied globally.

## 3. Import and preparation

### 3.1 Reuse the existing safe worker pipeline

1. The user chooses a delta or ZIP in Custom Characters. The shared importer
   may detect both tracks and characters in a mixed archive and route each to
   its proper library section.
2. Validate archive members, decompressed sizes, patch sizes and offsets using
   the existing bounded input rules. Never execute bundled programs/scripts or
   treat archive member names as output paths.
3. Identify the exact source ROM required by the patch among the user's
   imported Game Paks. Decode against a private immutable copy. Never modify
   the user's original ROM or try an arbitrary revision when validation fails.
4. Compare decoded assets and executable/global-data changes. Identify each
   candidate character, its retail base, and its dependency graph.
5. Build a review: name, portrait, base behaviour, available vehicles,
   animation/portrait/voice coverage, source revision and explicit blockers.
6. Prepare a content-addressed character artifact, validate it, and publish
   its catalogue entry atomically. Interrupted imports must not produce an
   enabled partial character. Import work, hashing and decompression stay off
   the launcher/render thread, with progress and cancellation.

### 3.2 A real character definition

Introduce a versioned `CharacterDefinition`, separate from the track `Root`:

- stable content ID and display name; patch provenance/credits when available;
- source revision, verified adapter version, dependency hashes;
- original behaviour ID, distinct from selector position and logical ID;
- per-vehicle object/model references, textures, animations and required LODs;
- portrait and selection-preview references;
- explicitly declared inherited or supported custom audio;
- capability flags and blocking reasons; prepared artifact version/hash.

Discover dependencies by following the verified object/model/animation/texture
tables. Do not interpret every changed record as belonging to the character.
Include unchanged base dependencies when the new character references them.
Resolve dependencies from the patched image; do not require every dependency
to have changed. Separate multiple characters in one delta into multiple
definitions while sharing immutable identical resources.

Deduplicate by normalized character content/behaviour, not ZIP name, import
order, donor character or whole-ROM digest. Keep the original delta/provenance
for reproducible preparation and later consented online sharing.

### 3.3 Compatibility policy

Haunter is the first verified profile, not a promise that every character hack
will immediately work. Generic asset-only layouts can use structural
validation; unknown executable changes, custom skeleton/animation formats,
changed physics or audio-engine changes require a reviewed adapter or a clear
unsupported explanation. Never silently omit changes that are essential to
the mod working.

Initial behaviour is inherited from the verified retail base, shown in the
review: Haunter uses the supported T.T. behaviour rather than an invented
eleventh stats-table index. Original voice/horn inheritance must be explicit
when the patch does not supply supported replacements.

Patch source revision and playable revision are separate questions. A delta
requiring v1.0 still needs an owned/imported v1.0 source. Playing its prepared
assets under v1.1 is allowed only after a verified dependency conversion and
revision-specific hooks pass tests. No unchecked v1.0 offsets in a v1.1 build.

## 4. Additive runtime architecture

### 4.1 Logical roster, stable native behaviour

Each occupied racer slot has a logical identity: a stock character or a custom
content ID. Native controller slots, racer count and retail behaviour IDs stay
valid. A project-owned sidecar maps scene/racer lifetime to that identity.
It is never inferred from the last mod loaded or from the current menu cursor.

Preserve native ready/cancel and racer-header initialization. Adapt only the
conversion from selected logical character to per-racer visual resources and
verified base behaviour. Native duplicate-selection and AI-fill assumptions
must use logical uniqueness where necessary: Haunter and T.T. are different
characters even though they share a base behaviour ID.

Keep stock AI choices by default; this phase does not introduce an
unrequested custom-AI population setting. An AI T.T. may coexist with human
Haunter. Tests must also cover several custom human racers derived from T.T.

### 4.2 Per-racer resources, not global replacements

Prepare separate immutable asset contexts for selected characters. Resolve
the racer object/model, animation and textures in that actor's context. Reuse
the current asset-bus and content-identity foundations, but do not apply the
track runtime's single scene-wide bank as a character replacement system.

Cache keys must include kind, normalized dependency identity, legacy record
ID and owning content generation. A model with unchanged bytes but different
referenced textures or animations must not share the wrong cached resource.
Preserve native live references/free semantics and keep resources pinned while
any actor, preview or deferred renderer work can still use them.

Resolve supported custom-character resources ahead of global course overlays
for racer-owned assets. Stock racers use stock character assets unless a
separately supported course feature explicitly requires otherwise. Ambiguous
cross-mod dependencies must fail preparation, not depend on load order.

The implementation must prove loader/reentrancy ownership before using scoped
routes. No process-global "current character" surviving a scheduler yield.
No mid-frame cache-ID swapping as a workaround. Keep existing interpolation
identity tied to each actor's lifetime, and preserve the accepted shadow/HUD
algorithms.

### 4.3 Every appearance of the racer

Audit selection preview, car/hovercraft/plane, hub vehicle changes, track
preview racers, 2P Adventure's active racer, boss transitions, results,
podiums, portraits, minimap markers, ghosts and replay reconstruction.
Use the existing HUD geometry and layout hooks; substitute resources only.

Missing a required vehicle is a visible capability restriction, not a reason
to silently become T.T. or disappear. Adventure requires its full vehicle
set unless an explicit, approved fallback policy is introduced later.

Persist logical selection and custom ghost metadata in a separate mod-aware
sidecar, not additional native EEPROM character IDs. Custom content must not
overwrite stock records or the user's single-player/online save separation.
If content is unavailable later, present a clear choice to use a stock racer;
do not silently substitute during an online session or replay.

### 4.4 Large libraries without unbounded memory

Index installed definitions on disk, page the UI, and prepare/pin the active
race roster plus a small bounded preview working set. Do not preload every
installed character. Existing guest cache/address limits still apply; perform
budget checks before launch and report limits cleanly. "As many as desired"
means a scalable installed library, not an impossible guarantee of unlimited
RAM or that every imported character is resident at once.

## 5. User interface and in-game design

### Launcher / overlay

Keep Custom Characters in Mods/Hacks using the same disclosure-button style.
Replace Coming Soon only when activation works. Inside:

- full-width Import Character / ZIP, then controller-friendly search and
  compatibility/enabled filters using the established on-screen keyboard;
- responsive portrait cards with the full wrapped character name, inherited
  base and vehicle support; consistent row sizing, no clipped controls;
- Enable and Manage actions; the details modal holds technical import facts,
  credits, unsupported reasons and removal/re-import actions;
- import review and progress modal; clear success/blocked states; no frozen UI;
- enabling/disabling applies at a safe menu/session boundary, never halfway
  through a race, active preview draw or online session.

### In-game Character Select

Do not cram extra portraits into the original ten positions or shrink the
original roster. Keep the original character page and unlock behaviour.
Add a clearly labelled Custom Characters view reached through visible
left/right category controls with controller shoulder-button prompts.

Proposed custom view: eight comfortably sized cards per page (four columns,
two rows), with name, portrait, vehicle icons and a page indicator. Previous
and next pages allow a growing catalogue. Keep the design readable at 4:3,
1280x800 and wider ratios; no horizontal portrait stretching. Use native
selection audio and the game's visual language. Long names wrap in the
details/selected-character area without covering portraits or player badges.

A selects, B backs out, and native ready/start behaviour remains. Opening
Character Select initially shows the original page. Do not auto-jump everyone
into custom content when the library changes.

Multiplayer uses independent logical cursors. Browsing another page must not
clear another player's selection. Show persistent P1-P4 selected-character
badges and ready states even when a portrait is on another page. The view
must make page ownership explicit and serialize simultaneous page-change
requests rather than let competing inputs flicker the shared screen. Keep
the established stock duplicate policy for an identical logical character;
do not reject different characters merely because they share a retail base.

Preview uses the prepared character's selection asset when present. Where a
legacy mod supplies only racer models, define and label a supported vehicle
preview; do not quietly show the original donor's model. Tests decide whether
the in-game renderer can support the chosen paged presentation without new
global HUD/window behaviour. That proof precedes full UI wiring.

## 6. Online content agreement

Finish offline coexistence before exposing custom characters online.

The host pins the session's track and character manifest, including required
definition/dependency hashes, inherited behaviour and adapter/protocol version.
Clients identify characters by these hashes, not local library ordering.
Character selection and readiness use stable logical IDs.

Missing content is offered through the approved consented P2P content flow.
Send permitted patch/manifest material, not ROMs, local saves or secrets.
The client must possess the required base ROM and successfully prepare and
validate the content before reporting Ready. Reject unsupported behaviour or
mismatched content with a useful reason before countdown.

Pin each racer's logical identity across load barriers, retries, catch-up and
Adventure transitions. Preserve native roster validation; add the logical
manifest check, rather than disabling safety checks. Do not serialize host
pointers/cache addresses into network state. Any sidecar state that can affect
simulation or rollback restoration must be deterministic and included in the
existing validated state/lifecycle contract.

No changes to rollback frame limits, input delay, pacing or boss player-count
rules are part of this feature. Offline-only remains the honest gate until
mixed-platform, long-session and transition tests pass.

## 7. Implementation ownership and sequence

1. **Freeze baseline.** Preserve the accepted rollback and snapshot the current
   private source/binaries before character work. Record hashes, ROM revisions,
   composed policy identities and test results. No release-package guard bypass.
2. **Character analysis/artifacts.** Add project-owned modules such as
   `legacy_character_definition`, `legacy_character_materialize` and
   `legacy_character_catalog`; integrate with the existing importer worker.
   Qualify both Haunter editions, dependencies, deduplication and failures.
3. **Offline coexistence proof.** Add a logical roster and per-racer asset
   adapter. Produce checked character fragments for each supported revision
   using verified function/data symbols and full-body/entry signatures.
   Compose with existing hook owners; reject conflicts. First proof: original
   T.T. and Haunter side by side, then multiple T.T.-derived customs.
4. **Selection and library UI.** Add the paged selector and launcher controls
   only after the runtime can safely represent those selections. Preserve the
   original character/AI/header path and all players' input ownership.
5. **Lifecycle and persistence.** Cover every vehicle, menu/results transition,
   preview, restart, portrait and mod-aware record/ghost path. Demonstrate stock
   resources and saves remain unchanged before/after custom play.
6. **Online integration.** Extend the content manifest/selection mapping through
   the existing session lifecycle without replacing the transport or pacing.
   Gate readiness on verified content and logical roster agreement.
7. **Package only after qualification.** Build Windows and Linux AppImage as
   v1.0.5 Beta 6 (the user's updated target) with matching content/adapter protocol identities, licenses,
   release notes and rollback artifacts. Production activation must work from
   the user's library with qualification mode OFF. Otherwise deliver a clearly
   labelled private test stage and report the remaining gates, not a false
   claim that the complete feature is ready.

Expected existing owners touched: importer/worker integration in `mods/`,
project runtime bridges/payload callbacks, checked recomp fragments and their
composer, `runtime_ui.cpp`, mod-aware session manifest/selection integration
and focused tests. Exact guest hook addresses are to be verified during adapter
authoring, not guessed from the decomp source or copied between revisions.

## 8. Acceptance criteria

- Import bare deltas, multiple deltas in ZIPs, mixed track/character ZIPs and
  multiple character definitions per patch. Duplicate editions do not add
  duplicate slots. Corrupt, oversized, path-traversal, unsupported and cancelled
  inputs leave the catalogue usable and the original ROM unchanged.
- Select original T.T. and Haunter together; select two different T.T.-derived
  customs together. Correct car, hovercraft, plane, animations, portrait and
  inherited audio/stats remain associated with each racer throughout.
- Stock character selection works unchanged with zero installed mods, with
  disabled mods, and with a large library. Custom page navigation does not lose
  P1-P4 choices. No all-Krunch fallback or changed controller assignments.
- Repeated stock/custom races, menu returns, restarts and preview scrolling do
  not leak memory, reuse another character's cache entries or hit a growing
  virtual-bank limit. Preparation reports insufficient budgets before launch.
- Confirm HUD sizing/placement, both shadow systems, interpolation, 3/4-player
  quadrants, boss topology and performance remain at the accepted baseline.
- Both supported US revisions, Windows and Linux; 1-4-player offline and then
  2/3/4-player online; stock and custom tracks; mixed selections; content
  missing/mismatch, disconnect, reconnect, catch-up, race end and cutscene chains.
- Single-player and online saves retain their separation; stock records and
  ghosts are not overwritten by custom content.

Automated tests establish structural/lifecycle guarantees. The user remains
the final visual/audio/gameplay tester. Do not represent compilation or an
inspector's recognized-profile result as proof of appearance or full gameplay.
