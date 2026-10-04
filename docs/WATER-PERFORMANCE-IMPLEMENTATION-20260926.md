# Water performance pass — implementation and qualification

Status: implementation and three-platform packaging complete. Full raced-route
and physical-device qualification remains pending; Linux ARM packaging is blocked.

## Rollback

The pre-change delivery (Windows, Linux x64, Linux ARM64, Android, hashes and
validation notes) and dirty source snapshot were copied to
`G:/DiddykongWorkFolder/water-performance-20260926/baseline` before editing.
HEAD at capture: `ab141cb0c031a8f2986d1fa15dadc98b076e8bcc`. HEAD alone is
not the previous delivered source: the snapshot also retains uncommitted work.

## Changes so far

- Explicit procedural-water provenance follows the existing submitted matrix
  binding. It does not change interpolation IDs or camera continuity. Nested
  shadow/scenery scopes do not inherit that permission; aspect-only scopes do.
- `water_uv_rt64.hpp` compares the current RSP vertex streams before redundant
  copy-on-write. It skips only used vertices with exactly equal signed UVs and
  already-disabled texture generation. Reloads and changed coordinates take the
  old path. There is no persistent vertex-slot cache to become stale.
- The same water-only path retains the first triangle of every authored batch
  as a draw boundary, but does not dirty an already-equal culling state for
  subsequent triangles. Changes of culling direction still take the original
  path. It does not merge across batches, matrices, materials or viewports.
- Opt-in `DKR_WATER_PROFILE=1` provides bounded water bridge counts/times and
  guest water-region inclusive timings. `DKR_WATER_UV_BASELINE=1` restores the
  old UV path for A/B tests; `DKR_WATER_DRAW_BASELINE=1` restores per-triangle
  cull invalidation. Use both for the pre-optimisation bridge. No gameplay
  setting is changed.
- Renderer timer histories are published by their owning thread and copied
  through a separate bounded, nonblocking try-lock snapshot. The decoder no
  longer depends on winning the renderer's long-held mutex to obtain timings.
  Enable `DKR_TRACK_PROFILE=1`; histories can include the preceding scene.
- `--water-profile` in the patch configuration generator composes timing hooks
  after checking full function-body SHA-256 and sole return coverage for both
  retail revisions. Existing hooks at shared entry points are composed, not
  overwritten. No guest instructions, registers or wave physics are changed
  by production timing hooks.
- `DKR_WATER_QUALIFICATION=ON` is a **private-only** offline scene-preview
  probe. Packaging scripts reject it. It uses the regular loader and an
  isolated profile, and is absent from production builds. Preview measurements
  are not full laps, live online sessions or hardware GPU qualification.

## Verification so far

The real pinned `RSP::modifyVertex` passes differential tests for subdivisions
1, 2, 3, 4 and 6; signed UV wrapping; seams; texture generation; reused slots;
and all attributes that its copy-on-write path clones. The two-row retail
submission (not an illegal 49-vertex load) produces these counts:

| Subdivisions | Triangles | Previous records | Equal-UV records |
|---:|---:|---:|---:|
| 2 | 8 | 24 | 12 |
| 3 | 18 | 54 | 24 |
| 4 | 32 | 96 | 40 |
| 6 | 72 | 216 | 84 |

These are mesh conversion counts, **not FPS or whole-renderer percentages**.
The actual pinned `GameFrame::matchTransform` also produces identical expanded
position and texture-coordinate velocities across successive animated frames,
including UV seams/wrap. The real `RSP::drawIndexedTri -> checkDrawState -> flush`
recorder verifies 8 -> 2, 18 -> 3, 32 -> 4 and 72 -> 6 calls for those grids.
Expanded triangle attributes and recorded culling/render mode remain equal.
Changed and mirrored cull states are tested separately; unrelated mode bits
must remain intact. This is not a claim of pixel-perfect GPU capture coverage.

## Dominant measured cause

The project bridge previously cleared the cull bits **for every triangle**,
then set them back. RT64's `RSP::clearGeometryMode` marks the geometry mode dirty
even when the final bits are identical. `State::checkDrawState` flushes on that
flag without comparing the final mode. Water rows were consequently fragmented
into many single-triangle game calls. Tile interpolation expands matching call
hash ranges into candidate pairs (`rt64_game_frame.cpp`, tileCheckSet path).
Repeated calls inside the same water matrix/material identity make that search
expensive despite the existing correct stable matrix identities.

Keeping an unchanged water row in one call removes this unnecessary candidate
explosion. It does not disable tile, vertex, camera or UV interpolation.

The first automated preview initially hid the defect: title mode deliberately
disables tile interpolation through the pre-existing policy. Its ~0.59 ms
matching result was not representative of gameplay. The PRIVATE probe now
exercises the gameplay tile policy without changing the guest menu/save state.
The same Sherbet scene then measured ~18 ms before row coalescing and ~0.41 ms
after it. This private policy override is compiled out of every deliverable.

### Controlled Windows v1.0 preview comparisons

Ryzen 9 3900X; Windows Vulkan; original textures; same isolated saved settings;
60 FPS target; 1440x900 window. Settings store resolution enum 2; the renderer
reports scale 4.0 (do not label it a measured 2x render). No compile jobs ran
during these comparisons. Each run lasts 24 seconds; values below are the last
complete rolling 120-sample matching window, not total frame time. Earlier
startup/history windows are excluded. Timings include enabled diagnostics.

| Scene | Baseline matching p50 / p95 / p99 ms | Candidate p50 / p95 / p99 ms |
|---|---:|---:|
| Sherbet hub 14 | 17.025 / 18.005 / 19.855 | 0.414 / 0.547 / 0.620 |
| Bubbler 1, 40 | 7.937 / 9.134 / 12.035 | 0.462 / 0.590 / 0.919 |
| Bubbler 2, 53 | 8.201 / 8.693 / 9.061 | 0.449 / 0.537 / 0.579 |
| Whale Bay 8 | 1.172 / 1.567 / 1.793 | 0.315 / 0.439 / 0.476 |
| Crescent Island 10 | 0.727 / 0.874 / 1.012 | 0.456 / 0.556 / 0.607 |
| Pirate Lagoon 4 | 0.329 / 0.446 / 0.551 | 0.266 / 0.364 / 0.428 |
| Treasure Caves 30 | 0.337 / 0.715 / 0.764 | 0.329 / 0.702 / 0.749 |
| Darkwater Beach 26 | 3.094 / 3.704 / 4.555 | 0.362 / 0.442 / 0.465 |

This is a large reduction in the dense scenes' matching cost, **not a universal
FPS improvement of the same percentage**. Treasure Caves' sampled preview is
essentially unchanged; other routes/cameras can expose different amounts of
water. GPU render cost in these runs was around half a millisecond, not the
dominant Sherbet bottleneck. Baseline and candidate triangle totals agree in
matched complete windows. Sherbet's 119-water-task window keeps all 137,088
triangles while avoiding 251,328 redundant UV copies and 125,664 cull flushes.

The Sherbet guest trace measured 720 block lookups in ~0.035 ms per 120 renders,
and total water rendering ~20 ms **across all 120 renders**. These inclusive
figures cannot be added to parent durations. They do not justify rewriting
physics, introducing mesh caches or replacing selection loops in this pass.
Phase C therefore closes with **no speculative gameplay change**. Phase D is
the measured row-coalescing correction, not a readback or GPU material rewrite.

### Private-probe limitation discovered

Central hub map 0 is not a safe substitute for the title's scripted reload:
one baseline run (both optimisations disabled) crashed after the private
override was called again. That run is retained as a failed qualification,
not counted as a successful control or attributed to the water optimisation.
Map 0 was removed from the test probe allowlist. Ancient Lake (5) and Hot Top
Volcano (7) are used as normal track-preview controls. Production contains no
scene override. This does not assert that general central-hub gameplay is
broken or that the private crash constitutes a repaired gameplay bug.

### v1.1 and additional checks

The same eight-scene comparison passed on v1.1. Matching p50/p95/p99 in ms:

| Scene | Baseline | Candidate |
|---|---:|---:|
| Sherbet 14 | 17.382 / 18.485 / 19.645 | 0.475 / 0.710 / 0.797 |
| Bubbler 40 | 8.010 / 8.323 / 8.714 | 0.514 / 0.635 / 0.758 |
| Bubbler 53 | 7.943 / 8.224 / 8.523 | 0.533 / 0.808 / 0.972 |
| Whale Bay 8 | 1.157 / 1.642 / 1.676 | 0.310 / 0.480 / 0.584 |
| Crescent 10 | 0.770 / 0.950 / 1.065 | 0.481 / 0.589 / 0.642 |
| Pirate Lagoon 4 | 0.334 / 0.452 / 0.651 | 0.303 / 0.398 / 0.440 |
| Treasure Caves 30 | 0.365 / 0.839 / 1.036 | 0.380 / 0.865 / 1.043 |
| Darkwater Beach 26 | 3.182 / 3.810 / 4.487 | 0.389 / 0.475 / 0.518 |

Ancient Lake and Hot Top Volcano controls completed on both bridge paths,
with matching around 0.3 ms. Treasure Caves remains essentially unchanged
in this view; do not present timing noise as a gain.

Final v1.0 checks included Sherbet at 120 FPS (matching p50 17.578 -> 0.541 ms;
workload p50 24.079 -> 6.297 ms), DX12 (matching 17.444 -> 0.510 ms), and Bubbler
with saved water detail 5 (8.482 -> 0.457 ms). The latter produced the same
visible triangle count as its default preview, so it is not evidence of a
larger visible mesh. With guest water timers disabled, Sherbet matching p50
was 0.525 ms. None of these results establishes handheld or full-race FPS.

## Additional user-reported scrolling reset

After the visible previews, the user confirmed the water looked correct
apart from a longstanding **scroll position reset**, and requested smoothing.

The retail `waves_update` in `extern/dkr-decomp/src/waves.c` wraps each base UV
with `(texture dimension * 32) - 1`. Vertex UVs reach RT64 in texels, divided
by 32. Pinned `GameFrame::matchTransform` instead uses the workload-wide
wrap distance, whose state default is **8092 texels**. A 16/32-texel reset is
far too small to trigger that fallback, so interpolation traverses almost an
entire tile backwards. The visible Sherbet probe confirms an authored 16x16
period. Texture frame selection is separate and remains unchanged.

The correction captures the two verified UV-mask globals in the existing
water matrix sidecar, on the guest submission thread. Addresses are checked
against both retail ELFs. The native bridge carries the corresponding powers
of two in a private high-bit extension of the CPU-only transform group's
texture-coordinate interpolation byte. This is never encoded into a packed
guest GBI command. All existing RT64 consumers were inspected: native
`matrixId` preserves the byte, matching treats it as forced interpolation,
and the debugger only prints its numeric value.

`patches/performance/water-scroll-interpolation.patch` is applied to a checked
build-local copy of `rt64_game_frame.cpp`. It uses the captured per-axis period
only when both matched groups carry the same valid water tag. No protected
checkout or generated guest source is edited. No global wrap distance, vertex
endpoint, texture sampler, physics, simulation rate or online state is changed.
Ordinary textures and unsupported/non-power-of-two periods keep the old path.
Tags follow queued transform ownership, not mutable live guest state or a
cross-thread global registry. Nested non-water scopes cannot inherit them.

Tests exercise real RT64 matching for 16/32/64/128-wide, 16/32/64-high textures,
independent positive/negative wraps, unchanged authored UV endpoints, and
untagged fallback. For example, 31.75 -> 0.25 yields +0.5 texels of velocity
instead of -31.5. The native matrix API's preservation of the tag is tested.
`DKR_WATER_SCROLL_BASELINE=1` restores the pre-correction interpolation for
diagnosis without disabling the performance changes. The separate visible
scroll probes run at 120 FPS; concurrent build jobs mean their timings are
not controlled A/B performance measurements.

## Final verification and deliverables

- Windows production Release: 83/83 DKR project tests passed, 122.40 seconds.
- Linux x64 Release under Ubuntu 24.04/WSL: 83/83 passed, 39.22 seconds.
- Both ELF timing-hook composition and UV-mask address checks passed.
- Production Windows Vulkan startup, visible, 50 seconds per ROM revision:
  v1.0 completed 1,472 graphics tasks; v1.1 completed 1,476; both stopped cleanly.
  These are normal startup tests, without the private scene substitution.
- The additional visible scrolling probes completed on Sherbet v1.0, Bubbler 1
  v1.1 and Pirate Lagoon v1.0, with captured 16x16 periods and clean shutdowns.
  The user accepted the earlier performance previews; explicit visual acceptance
  of the newly corrected scrolling loop is still pending.
- Windows ZIP packaged with its runtime/helper checks. Linux x64 AppImage passed
  its actual packaged Pak and SDL2/SDL3 live-switch self-tests.
- Android ARM64 APK rebuilt; signature verified, versionCode 1050021,
  versionName 1.0.5-beta.12, min SDK 28, target SDK 35. It retains the existing
  preview application ID and debug signing identity. No Android device was tested.
- Package guards verified private test macros/marker strings are absent.
  Package content scans reject ROM/save files. Previous delivery is preserved.

Artifacts and SHA256SUMS are in
`G:/DiddykongWorkFolder/water-performance-20260926/delivery`.
Logs and the generated per-preview JSON summary are retained outside packages
under the adjacent `tests` directory. Initial unfiltered CTest runs also picked
up long-running upstream zstd benchmarks; those runs were stopped, then the
complete **DKR project** suites were rerun with `-R ^DKR`. They are not claimed
as completed dependency stress tests.

Linux ARM64 is **not** a new build in this delivery. Its environment requires
Docker recovery, which the user explicitly deferred. Docker was left alone;
the prior ARM artifact remains in the rollback directory, not relabelled as fixed.

Remaining manual qualification: full laps/hazards and hub shoreline routes,
live online/split-screen transitions, all water-detail/aspect configurations,
representative replacement packs, Steam Deck and physical Android/ARM devices.
The static previews and unit tests are not substitutes for that matrix. No
universal FPS or device-temperature guarantee is made. Broad Android work
remains deferred. No physics/cache/readback/network redesign was justified.
