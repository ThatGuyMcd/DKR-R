# Water-heavy level performance investigation — 26 September 2026

## Scope and status

Investigation only. No game, renderer, patch-policy, settings, generated-code or
dependency changes were made for this investigation. No new build was produced.
The separate Android optimisation programme is deferred.

The reported problem is longstanding and affects Windows, Linux and Android,
with original textures and replacement packs, across settings. The primary
targets are Sherbet Island hub, Crescent Island, Pirate Lagoon, Whale Bay,
Treasure Caves, and both Bubbler races. Include Darkwater Beach as a related
water-heavy minigame. The working tree is dirty; this report concerns the local
source on top of HEAD `ab141cb0c031a8f2986d1fa15dadc98b076e8bcc`, not an assertion
that HEAD alone represents the shipped build.

**Conclusion:** there is confirmed avoidable vertex-copy work in the shared
F3DDKR-to-RT64 path, plus repeated bounded water selection/mesh work. The affected
levels also share a dual-texture water material. These are evidence-backed
targets, but no controlled trace from the reported slow locations was captured
in this pass. Their relative contribution to the severe frame drops is not yet
measured. It would be inaccurate to claim one proven dominant cause or promise
a particular FPS increase from static analysis.

## Evidence collected

- Traced the retail water update, visibility, mesh, physics and water-effect
  paths in the decomp, and confirmed generated recomp functions still call
  those water routines. Generated sources were inspected, not edited.
- Read the current project interpolation bridge, water identity hooks,
  Camera & Scenery policies, renderer diagnostics and staged-patch setup.
- Read RT64's vertex modification, frame matching and full-sync implementation.
- Read locally extracted US v1.0 level headers and object maps. Inflated the
  seven Sherbet geometry binaries **in memory**, checked decoded length against
  their headers, and counted segments/batches using the existing geometry
  format definitions. No asset files were written or redistributed.
- Replayed the retail water triangle index sequence in an in-memory counting
  experiment to verify the number of copy-on-write vertex operations.
- Checked previous performance qualification and recent saved profiling logs.

### Evidence limits

The asset counts below are US v1.0 data, not a separate v1.1 asset audit. Both
revision patch policies contain the water hooks; numerical/visual qualification
must still cover both ROMs. Static geometry totals are not per-frame visible
draw counts. Operation counts are not timings. Previous intro/title runs do not
substitute for sustained gameplay measurements in these locations.

## 1. What these levels actually contain

| Scene | Map ID | Level segments | Segments with water batches | Authored wave subdivisions | HQ window | Double density |
|---|---:|---:|---:|---:|---:|---|
| Crescent Island | 10 | 24 | 19 | 3 | 5 × 5 | No |
| Pirate Lagoon | 4 | 117 | 106 | 2 | 3 × 3 | No |
| Whale Bay | 8 | 49 | 49 | 4 | 5 × 5 | No |
| Treasure Caves | 30 | 25 | 21 | 3 | 5 × 5 | No |
| Sherbet Island hub | 14 | 8 | 6 | 6 | 5 × 5 | Yes |
| Bubbler 1 | 40 | 30 | 30 | 6 | 3 × 3 | Yes |
| Bubbler 2 | 53 | 30 | 30 | 6 | 3 × 3 | Yes |
| Darkwater Beach | 26 | 18 | 16 | 6 | 3 × 3 | No |

Bubbler 1 and 2 share the same geometry asset but have different object maps.
Water-batch classification is flag `0x2000`, not a guess based on texture names.
These are whole-level totals, including segments that may be invisible.

Sources: `extern/dkr-decomp/assets/.vanilla/us.v77/levels/headers/*.json`,
`asset_level_headers.meta.json`, `levels/models/sherbet_island/*.bin`, and
`src/textures_sprites.h:59`. Geometry layout is also documented by
`runtime-recomp/src/game/mods/legacy_mod_geometry.cpp:41`.

Important field mapping: the asset extractor calls offsets 0x70/0x71 `unk70`
and `unk71`. In the runtime they are respectively **wavesXlu** and
**waveDoubleDensity**, as shown by `extern/dkr-decomp/include/structs.h:488`.
All eight target scenes have wavesXlu enabled. An intermediate investigation
note initially transposed these fields; this table and report use the verified
mapping. Whether the material uses the special translucent render mode also
depends on texture format and viewport layout, not solely on wavesXlu.

The difference is important: Pirate Lagoon has many small segments but a small
HQ mesh window; the hub has very few segments but a denser, double-density mesh.
A segment-search optimisation alone cannot convincingly explain or solve both.

## 2. Confirmed avoidable work: repeated UV writes duplicate water vertices

The bridge calls `rsp.modifyVertex(..., G_MWO_POINT_ST, ...)` for **all three
corners of every triangle**, even when the same source vertex keeps the same
texture coordinates:

- `runtime-recomp/src/game/f3ddkr_rt64.cpp:2848`–2880.
- `extern/rt64/src/hle/rt64_rsp.cpp:743`–807: if that vertex was already used,
  RT64 copies its position, velocity, colour, UVs, matrix indices and other
  metadata into a new vertex record **before** applying the UV assignment.
- `extern/dkr-decomp/src/waves.c:329` and `:816`: adjacent water triangles share
  grid vertices with equal UVs. The shared-coordinate case genuinely exists.

Tracing one complete HQ cell through the retail row-by-row submission gives:

| Subdivisions | Triangles | Loaded row vertices | Extra copies now | Total records now | Records with safe equal-UV no-op elimination |
|---:|---:|---:|---:|---:|---:|
| 2 | 8 | 12 | 12 | 24 | 12 |
| 3 | 18 | 24 | 30 | 54 | 24 |
| 4 | 32 | 40 | 56 | 96 | 40 |
| 6 | 72 | 84 | 132 | 216 | 84 |

Formula: `2s²` triangles, `2s(s+1)` row-loaded vertices, `4s²-2s` redundant
copies, giving `6s²` records on this path. The counting experiment follows the
actual two-triangle index order and the renderer's used-vertex flag lifecycle.
It is not a benchmark of elapsed runtime.

For the six-subdivision case this is about 61% fewer vertex records for that
specific mesh conversion if equal writes are elided safely. **It does not mean
61% less total rendering work or 61% more FPS.** Row-to-row duplicate loads remain;
the full six-subdivision grid has 49 unique positions, but the retail row path
loads 84. The distinction matters.

Why it matters beyond the copy itself: these records occupy host vectors and
upload buffers, and the interpolation matcher hashes/processes position and UV
streams (`extern/rt64/src/hle/rt64_game_frame.cpp:700`–793).

Proposed first optimisation candidate, after timing: a water-scoped bridge
fast path that skips only proven no-op UV modifications. It must preserve all
other side effects, including cleared look-at state; invalidate cached knowledge
on vertex reloads, slot changes, task/scope changes and relevant state changes.
Different UVs at real seams must continue to make distinct records. Do not alter
the canonical vehicle/Taj shadow path or HUD billboards. Do not modify RT64's
checkout to implement this.

## 3. Confirmed repeated water selection and mesh work

The shared authored path does the following:

1. `tracks.c:308`: wave phase/UV update once in the scene rendering pass, before
   the per-viewport rendering loop—not once per interpolated presentation.
2. `tracks.c:1689`: per-viewport visibility reset and selection.
3. `waves.c:476` and `:533`: a linear search of level segments for each selected
   water cell. Grid-to-segment relationships were established at load time.
4. `waves.c:556`: another linear pointer-to-segment search when testing a
   segment for HQ water. `tracks.c:1905` calls this in the non-opaque pass only;
   this investigation did **not** find it being registered in both passes.
5. `waves.c:878`: per-viewport fade bookkeeping over all level segments.
6. `waves.c:569` and `:665`: each visible block scans the selected-cell list,
   then computes its matching grid's heights/colours/alpha.
7. `waves.c:1034`: matrix setup, vertex loads and triangle submission per block;
   subdivided meshes are submitted as a pair of rows at a time.

The selector is bounded to a 3 × 3 or 5 × 5 window. Double-density blocks split
into four subcells, but do **not** grant four independent full 5 × 5 pools. Avoid
the false diagnosis that every ocean segment is tessellated to full detail.

Possible lossless improvements if the measured timings justify them:

- Build a scene-lifetime lookup for the existing grid/segment relationships;
  preserve retail first-match behaviour where multiple segments share a cell.
- Resolve a segment pointer by a validated index rather than repeated search.
- Index selected cells by block while retaining their existing processing order.
- Cache only identical render computations with complete scene/tick/viewport,
  selection, phase, fade, magnitude and buffer ownership keys.

Caching is not automatically safe: the mesh generators also advance per-entry
bookkeeping, and viewport fades and alternating vertex buffers are distinct.
Do not memoise solely by address, skip fades, reuse prior-frame data, or change
wave phase advancement.

## 4. Water effects and physics must be measured separately

Water is more than the visible surface:

- `tracks.c:3535` generates projected geometry for water effects as well as
  shadows. Water effects enter `func_8002EEEC` / `waves.c:1844`, sampling a
  bounded patch of the water mesh beneath an object.
- `waves.c:1102` supplies height/normal queries used by gameplay.
- `waves.c:2217` supplies floating-object heights.
- `waves.c:2247` evaluates extra swell generators, with distance checks and,
  when applicable, square root and trig operations. It uses an existing spatial
  shortlist of up to eight generators; it is not an unconditional scan of every
  generator for every vertex.

The inspected primary Bubbler object maps each contain 18 buoys and three logs.
This is a reason to measure floating-object/water-effect work, **not** proof that
every object has the same cost or that those counts are all active runtime
objects. The primary target object maps do not list a WaveGenerator object;
Boulder Canyon's does. Runtime creation/other map banks still need counters.
Also, the base sine height table is precomputed at level initialisation
(`waves.c:268`), not rebuilt with trigonometry for every vertex every frame.

Therefore an indiscriminate sine approximation, reduced physics rate, or
lower wave subdivision setting is not a justified fix. Preserve hovercraft
handling, buoy/log motion, collisions, wakes, boss hazards and online state.

## 5. GPU rendering and synchronous work remain serious candidates

All target headers select the two-texture wave material in `waves.c:992`.
It loads two texture inputs and uses a two-cycle combine path. The special
`G_RM_AA_ZB_XLU_INTER2` branch additionally requires RGBA32 detail texture and
the single-viewport condition; otherwise that branch selects an opaque surface
mode. The actual path needs capture/counters for each test configuration.

Large water coverage can increase fragment/material work even when the mesh
count is modest. Test that, rather than assuming modern planar reflections:
no separate reflection-camera pass was found in this authored water renderer.

RT64 also defaults to render-to-RDRAM (`rt64_emulator_configuration.cpp:13`).
`rt64_state.cpp:1457`–1479 converts render targets, submits, waits and copies
native colour/depth back. This is shared, not Android-specific. More expensive
water drawing may lengthen a synchronous dependency and make a GPU stall look
like a game-thread slowdown. This remains a hypothesis until correlated timings
are taken in the affected scenes.

Do not turn readback off globally. Any proposed removal/deferment requires
proof of which game operations consume colour/depth, with menu, preview,
transition and effect coverage. Fence waits and full-sync scopes overlap; do
not sum their wall times and call the result CPU usage.

## 6. Existing patches: preserve what already works

| Existing layer | Current role | Treatment in this pass |
|---|---|---|
| v77/v80 policy `waves_init` hook → `dkr_maximise_persistent_water_detail` | Promote the authored window to 5 × 5 at highest Modern water detail | Keep allocation/selector limits; measure authored and max detail separately |
| `initialise_player_viewport_vars` → `dkr_anchor_persistent_water_to_camera` | Camera-centred selection at Modern detail ≥ 2 | Preserve camera ownership; do not shift selection as a performance shortcut |
| `waves_visibility` → `dkr_stabilise_persistent_water_transition` | Suppress selected-cell fade holes at detail ≥ 2 | Preserve transition behaviour; include time in diagnostics |
| `waves_render` begin/end/block/selection and `mtx_cam_push` identity hooks | Stable scene/viewport/block/topology identity | Keep; any batch optimisation must maintain deterministic identity and topology |
| F3DDKR typed sidecars and bounded marker handling | Prevent water variants being mistaken for HUD/aspect commands | Keep all safety fixes |
| `SelectInterpolationGroup` | Linear explicit matrix ordering; tile interpolation enabled only for identified groups | Already improved; do not claim changing AUTO→LINEAR is a new fix |
| Performance staged patches | Snapshot lifetime, waits, resource budgets and diagnostics | Preserve; extend profiling only through checked patch composition if necessary |

Key sources: `runtime_enhancements.cpp:474`–564,
`modern_camera_policy.hpp:130`–158, both `dkr.us.*.recomp-policy.json` files,
`presentation_identity.cpp:1226`–1341, `f3ddkr_rt64.cpp:204`–267.

Water detail defaults to 1. At detail 5, a scene authored for 3 × 3 can select
up to 25 rather than nine HQ cells. This can amplify workload, but cannot be
the sole explanation for a problem reported at default settings since early
builds. Similarly, retaining more scenery can increase visibility without
being the original defect.

RT64 still expands equal-hash draw-call ranges for groups needing tile
interpolation (`rt64_game_frame.cpp:520`–583). Stable water identities constrain
that set, but repeated calls inside an identity can still create combinations.
Count this before changing it. The existing no-op early-out does not apply to
water groups that genuinely interpolate tiles. Do not sacrifice water scrolling
or reintroduce the old floating/flickering scenery to save matching work.

## 7. Why previous performance validation did not settle this

`docs/TRACK-PERFORMANCE.md` describes a real earlier marker-collision fix but
explicitly says Crescent/Sherbet/Bubbler FPS improvement was not measured. It
also says water tessellation, selection and simulation were unchanged.

Recent retained logs under
`G:/DiddykongWorkFolder/task-lifetime-20260925/tests/production-v80-normal/logs/runtime.log`
include map 21/menu 1 and map 23 intro/title scenes, not the required water
routes. Their track-profile records show `renderer-history-available=0` in the
inspected windows. That is unavailable data, **not zero renderer cost**.

Current `DKR_TRACK_PROFILE` measures synchronous display-list processing and
tries to copy renderer histories without blocking (`rt64_renderer.cpp:640`–704).
`DKR_POWER_PROFILE` has aggregate full-sync/fence/copy counters and successful
presentation-return intervals. Neither currently attributes time to individual
water phases. The older document's lack of presented-interval capture has since
partially improved, but physical display pacing and per-water-stage attribution
still require explicit qualification.

## Proposed implementation and verification plan — approval required

### Phase A — capture the exact failure without changing game behaviour

1. Preserve the current four packages, hashes and dirty source baseline before
   an implementation build. Keep saves/settings and imported mods untouched.
2. Add opt-in, bounded timings/counters through project-owned policy hooks:
   phase/UV update; visibility/lookups; water mesh generation; water effects;
   floating-object/physics queries; bridge vertex/triangle/UV-copy counts;
   matching; native render/full-sync/readback; enhanced render; presentation.
3. Record map, ROM revision, viewport count, effective settings, selected/visible
   cells, water batches, vertices, generator activity and queue backlog with
   each timing window. Publish completed renderer timings from its owning thread
   to a bounded snapshot so a contended try-lock cannot erase all attribution.
   No extra GPU waits, hot-loop console output or blocking diagnostic locks.
4. Use identical routes: hub shoreline circuit and camera pan; full laps of the
   four races; both Bubbler races including hazards; Darkwater Beach. Separate
   first-load spikes from warmed repeated laps. Use Ancient Lake, a dry section
   and Hot Top Volcano as controls, not only the title screen.
5. Start offline with stock assets, Modern/60 FPS, controlled resolution and
   AA. This is an experimental baseline, not a suggested user workaround.
   Then vary resolution, 30/60/120 presentation and packs **one at a time**;
   compare CPU busy time, simulation rate, render times and frame intervals.

Deliverable: a per-scene bottleneck table with p50/p95/p99, spike timestamps,
sample counts, CPU/GPU/wait attribution and diagnostic overhead. If the dominant
cost is not one of the candidates above, follow the trace rather than forcing
the expected answer.

### Phase B — remove confirmed redundant bridge work

Implement the water-only equal-UV fast path first if attribution supports it.
Preserve validation, true UV seams, look-at semantics, slot lifetimes, draw
order, source geometry and interpolation ownership. Test wrapped/negative UVs,
16/32-bit textures, animated frames, both density modes, viewport changes,
reloads and custom water assets. Verify decoded output equivalence per triangle,
not just that a test exits without crashing. Compare repeated runs to baseline.

Do not combine an entire 49-vertex grid into a retail 32-slot vertex load.
Further row/batch improvements require their own design and buffer proofs.

### Phase C — remove measured CPU lookup/mesh overhead

Use validated scene-local indexes in place of repeated searches, with original
first-match/order semantics and a safe fallback for unsupported custom layouts.
Cache only demonstrably identical render work, keeping mutable fades/selection
advancement separate. Differential-test old and new results for both revisions,
scene reloads, two viewports, cutscenes and rollback/resimulation invalidation.
Do not change floating-point evaluation order or gameplay state as an incidental
part of this optimisation.

### Phase D — address measured GPU/matching/synchronisation bottlenecks

If material/coverage dominates, investigate redundant state changes, redundant
texture uploads, safe draw coalescing and identical-work reuse. Preserve blend,
depth, fog, ordering and opacity. If candidate matching dominates, reduce
redundant combinations using verified semantic ownership, not a global
interpolation disable. If readback/fences dominate, isolate consumers before
proposing any scheduling or readback change; retain existing scheduler lifetime
and online barriers. Each is a separate, reversible change with its own evidence.

No broad RT64 rewrite, global readback disable, lower water quality, reduced
physics rate, fast-math, network retuning or shadow redesign in this pass.

### Phase E — regression and release gates

- All target routes plus dry/water controls, both ROM revisions, stock textures
  and representative replacement packs, original and Modern presentation.
- Water detail 1–5; authored and extended scenery; 4:3, 16:9 and 16:10;
  single player and 2/3/4-player layouts. Retail HQ waves are normally enabled
  only for one/two-player layouts (`tracks.c:218`); verify the live path rather
  than assume identical split-screen workloads.
- Race start/end, track previews, hub/boss transitions, consecutive cutscenes,
  online sessions and repeated level reloads. Preserve water/shadow alignment,
  UV scrolling, interpolation, seams and camera changes.
- Windows DX12/Vulkan, Linux x64/Steam Deck, Linux ARM and Android on physical
  hardware where available. Compilation, QEMU and desktop traces are not proof
  of mobile/ARM GPU performance.
- Compare warmed repeated baseline/candidate runs; accept demonstrated lower
  relevant cost without worse p95/p99, new backlog or gameplay differences.
  At a 60 FPS target evaluate 16.67 ms presentation intervals and sustained
  authored simulation cadence separately. Do not claim universal 60 FPS on
  unspecified hardware or impose a fabricated percentage speed-up target.
- After approval and successful qualification, package Windows, Linux x64
  AppImage, Linux ARM AppImage and Android with a recorded test matrix and
  checksums. Leave broad Android-specific work deferred.

All implementation must use the checked patch pipeline/project-owned adapters.
No edits to submodule checkouts, `RecompiledFuncs`, or `RecompiledPatches`. If a
renderer-source change is genuinely necessary, stage an isolated copy and apply
a checked patch through the existing build pipeline; never edit generated or
dependency output by hand. Retain the working build as rollback throughout.
