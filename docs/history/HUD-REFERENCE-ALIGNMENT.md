# Beta 5 HUD reference alignment — 2026-09-08

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


## Evidence and correction

1. **Wrong / unmoved groups.** The active `BeginWidget` path formerly used
   `groups::bounds` and `native_anchor` to infer an edge. For example, the
   ordinary lap and banana groups could remain centred. Golden Balloon uses
   an explicit 59-by-5 mode/element table: laps RIGHT except time trial LEFT;
   bananas RIGHT except boss LEFT and challenge CENTER. DKR-R now uses that
   exact classification, not a second set of guessed boxes. The dormant editor
   data remains stored but does not influence either supported preset.
2. **Mixed renderer proportions.** `SelectInterpolationGroup` assigns a MODEL
   matrix ID (`proj=false`). RT64's `ProjectionProcessor::processScene` reads
   aspect from `viewProjTransformGroups`, not `worldTransformGroups`. Assigning
   ADJUST to the model therefore cannot guarantee HUD proportions. The new
   one/two-player pass pushes an explicit ADJUST PROJECTION group and uses
   ADJUST for RDP rectangles, then restores both. The negative test proves a
   model-only ADJUST still produces projection scale 1 instead of 1/cover
   under the old overscan conditions.
3. **Overscan mistaken for framebuffer size.** The old HUD pass rebased its
   right clip origin and enlarged the aggregate scissor. RT64's framebuffer
   renderer checks that aggregate's aspect before preserving rectangle shape.
   One/two-player HUD clips now use signed overscan, and only their tagged
   framebuffer aggregates are normalized to native width before full-sync
   submission. Per-draw clip coverage remains expanded. Existing quadrant
   tracking and its world-projection switch remain separate.
4. **Special draw paths.** Minimap markers are CENTER in the upstream table
   because upstream already moves their shared `gMinimapScreenX`. DKR-R does
   not mutate that variable: it applies RIGHT to background AND markers.
   Generated race/lap timer glyphs use their existing verified caller hooks;
   finish times/text remain centred. Stopwatch text uses the left group.
5. **Slide / mode mismatch.** Fit's draw-only slide term now matches the
   upstream whole-canvas scale. It does not scale the bounce or mutate game
   state. The retail no-slide check is by asset 40, not by the whole
   speedometer group. Taj challenges use the normal race classification;
   the actual boss type selects the relocated banana group. Time trial takes
   precedence, and the alternate hub type is recognized.

## Scope and preservation

- Only Modern one/two-player HUD presentation is opted into the new pass.
- No enabled editor, custom placement, size slider, or custom input capture.
- No manual changes to RT64, N64ModernRuntime, N64Recomp, the decomp or
  generated C files. Existing verified recomp hook placements are reused.
- Three/four-player HUD policy, shadows, world interpolation, online timing,
  saves, controls and launcher lifecycle are not modified in this pass.
- Original selected mode remains centred at native size; Fit translates
  groups into the expanded viewport. Neither deliberately stretches a group.
- The accepted Beta 3 and preceding Beta 5 packages remain separate rollback
  artifacts; the new package does not overwrite them.

## Verification / user playtest

`hud_reference_source_tests.py` compares the full upstream table with the
runtime policy. `DKRHudGroupsTests` includes the real RT64 processor, projection
stack restoration, rectangle dimensions/UV derivatives, all element modes,
shared two-player challenge anchors, and the existing affine/config tests.
`hud_pipeline_tests.py` checks both ROM revisions and unchanged protected
world/online/quadrant files against the accepted pre-HUD source snapshot.

Native visual checks are explicitly still required (the user handles them):
switch 4:3 / Fit during ordinary race, hub, boss, time trial, each challenge
type, and two-player play. Check all members of laps, bananas, race timers,
banked lap rows, stopwatch, item/count, placement/ordinal, speedometer, balloon
count, minimap and score strips. Include animated ranks, race-entry slides,
finish messages, mirror tracks, pause/Taj dialogues, and resizing. Verify
the accepted three/four-player views remain unchanged. Test both ROM revisions
and DX12/Vulkan, with and without texture replacements, plus Linux/Steam Deck.

Upstream: https://github.com/akratch/goldenballoon/tree/83a847ccbd3e6334c9cc9c697122d5b3cddb91c1
Provenance: THIRD_PARTY.md and packaging/licenses/GOLDEN-BALLOON-NOTICE.txt.

## Minimap-only follow-up (2026-09-08)

The user accepted the reference-aligned HUD, but reported that the minimap
background did not line up with its correctly positioned racer markers.
The background's begin/end hook uses `revision_addresses::HudDisplayList`;
the marker hooks instead receive the actual display-list holder as an argument.
The address table incorrectly named the neighbouring matrix holder `gHudMtx`
as the display-list holder, so the background's presentation markers were
recorded against matrix allocations rather than its draw commands.

The retail ELF symbol tables and generated call arguments confirm:

| ROM | Correct `gHudDL` | Incorrect previous value (`gHudMtx`) |
| --- | --- | --- |
| US v1.0 / v77 | `0x80126CFC` | `0x80126D00` |
| US v1.1 / v80 | `0x801272BC` | `0x801272C0` |

The production correction changes only these two address-table entries.
No anchor, coordinate, scale, projection, marker placement, draw algorithm or
hook placement changes. The existing one/two-player Modern HUD eligibility
guard still excludes three/four-player sessions. Original 4:3 remains an
identity transform, and Fit now records the background's transform at the
same draw-command stream used by the dots.

The pipeline regression test now compares the actual address-table field to
`gHudDL` read directly from each retail ELF, and rejects `gHudMtx`. This catches
the original mistake independently of the C++ tests' address expectations.
The previous `hud-reference-20260908` package is preserved as the rollback.
Visual confirmation of alignment is left to the user, including mirror tracks
and the shared two-player minimap.

## Course-direction indicators (2026-09-08)

Following acceptance of the minimap correction, the user requested moving the
two course-direction indicators outward in Fit to Window. This is an explicit
DKR-R layout preference beyond the reference table (Golden Balloon keeps
`HUD_COURSE_ARROWS` centred).

Retail `hud_course_arrows` draws the same slot (33) at X = -120 and +120,
negating and restoring its X between draws. Both copies use the same course
direction, so sprite ID or rotation cannot identify the side. The existing
`hud_element_render` entry hook runs before retail adds slide/bounce; the hook
now passes this slot's authored X to the presentation-only anchor resolver.
Only slot 33 uses its sign to select left/right. No persistent coordinates,
direction, rotation, alpha, size or animation state are modified.

Fit translates each copy outward by one horizontal gutter, retaining the
authored 40-unit edge inset. At 16:9 this adds 53.33 authored units outward
per copy; at 16:10, 32 units. Other aspect ratios use the same viewport-derived
formula. Original 4:3 is still an identity transform; the unchanged session
guard excludes three/four-player HUDs. The base 59-by-5 upstream table remains
unchanged, as do all other groups and the accepted minimap fix.

Regression tests check both sides across seven scenarios, five aspect ratios,
one/two-player presentation and positive/negative entry slides. They verify
constant edge inset, original dimensions and Y, no effect on any other slot,
and identity transforms in Original mode. The previous minimap package remains
the rollback point. User visual checks should include ordinary and mirrored
turns, sharp/U-turn and warning variants, and window resizing.
