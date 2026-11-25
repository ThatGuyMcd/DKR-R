# Track performance pass — v1.0.5 Beta 1

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


This pass corrects a rendering-metadata defect. It does not lower graphics
quality, change water physics or modify online synchronization.

## Correction

Water and billboard identities have a five-bit variant (0–31). Previously,
the renderer bridge interpreted values 26–31 as HUD/aspect/viewport commands
before checking the geometry mode. A legitimate water begin could therefore
close an unrelated scope or change viewport state instead of beginning water
interpolation. Its matching end could then pop its parent segment.

Current sidecar producers specify a separate command kind. All 32 geometry
variants remain available, with unchanged identity generation and interpolation
flags. HUD and aspect markers retain their existing calculations. The legacy
encoded entry point is handled explicitly; current producers use typed sidecars.

Rejected nested begins consume their own ends without popping accepted parent
scopes. Marker recording remains bounded at eight markers per command. An
invalid/overflowed recording is discarded as a whole at submission, using the
existing no-sidecar fallback for that malformed task only. Ordinary valid tasks
do not take that path. This is fault containment, not an optimization that
periodically disables interpolation.

Scope errors retain a first diagnostic and exact event counts, followed by
sparse summaries. Repeated errors no longer produce unbounded per-draw output.
Task-end validation checks HUD/aspect scopes before restoring their state.

## Diagnostics

Set `DKR_TRACK_PROFILE=1` in the environment before starting DKR-R to enable
opt-in timing output in its normal runtime log. Without it, no timing samples,
renderer-history copies or profiling log output are taken by this feature.

Each complete 120-task window reports scene, map, menu, task cadence and
display-list processing time (nearest-rank p50/p95/p99). Scene/map/menu changes
restart the local window. These timings include synchronous work within the
display-list processing call; they are not pure game simulation timings.

Existing renderer matching, render CPU, render GPU and workload histories are
copied under the renderer's own mutex using a non-blocking try-lock. Diagnostics
never wait for that lock or modify renderer timers. Formatting occurs after
releasing it. Unavailable windows are explicitly counted: busy periods must
not be interpreted as zero-cost samples. Renderer histories are rolling,
may overlap, and can include a preceding scene until old samples age out.
Zero/unfilled or non-finite entries are excluded; check the reported count.

These are **not presented-frame interval distributions**. External presentation
tracing is still required for actual display p95/p99 and sustained-drop claims.
Logging has some overhead; compare performance with profiling disabled too.

## Qualification

Automated tests exhaust geometry modes/variants, genuine control kinds,
nested segment/surface restoration, capacity/overflow handling, resets,
diagnostic budgets and timing statistics. They do not replace GPU visual tests.

The matched three-minute Modern intro/title smoke runs initially produced
14,004 aspect mismatches, 17,657 presentation underflows and 488 HUD underflows
in the previous build, and none in the first corrected build. Each completed
5,371 authored tasks and exited cleanly. This is evidence that the collision
was corrected in those runs, **not a measured FPS improvement in Crescent,
Sherbet Island hub or either Bubbler race**.

Target-location routes, repeated laps, Steam Deck frame-time capture, texture
pack comparisons and the complete multiplayer/visual regression matrix remain
qualification requirements. The package-specific validation record lists the
checks actually completed. Do not describe the entire performance plan as
complete without those measurements.

Water tessellation, selection distance, scenery retention, simulation cadence,
gameplay tile interpolation and shadow projection remain unchanged. Additional
optimization must target a measured bottleneck, preserving the same rendered
geometry and simulation results.
