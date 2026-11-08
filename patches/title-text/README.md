# Full-width title character names

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

Presentation-only Patch Pipeline overlay on the accepted native-water / intro
stage. Nothing in dependencies, RecompiledFuncs, RecompiledPatches or checked-out
runtime sources is edited. No guest RAM, timing, audio, save or online simulation
contract is changed. This layer replaces only the first-party F3DDKR decoder.

## Evidence and scope

The owner's 20261019-1616-51 recording shows Pipsy and Banjo's names and colour
trails entering/leaving inside the widened title cinematic. The decomp's
`func_80083098` moves centred names from X=-80 to 160 to 400. The text renderer
`render_text_string` clips a partially negative glyph to X=0 and advances its
atlas S coordinate before emitting an unsigned 12-bit texture rectangle. Simply
widening the RDP scissor cannot recover that missing portion; interpreting the
remaining negative coordinates as unsigned also wraps them to the other side.

Admission requires Modern + Fit to window, an aspect wider than 4:3, MENU_TITLE
and TitleRevealTimer=0. Both supported US revisions get addresses generated from
their individual read-only decomp symbol maps. Other menus and logo scenes do
not qualify. The font pass must begin at the exact dDialogueBoxBegin display list.

The adapter scans at most 255 immutable command words, with at most 32 glyphs.
It accepts only the reviewed font pass shape: pipe syncs, font draw-mode and
atlas lists, expected primary/trail colours, 1:1 non-flipped glyph rectangles,
and the exact render-mode-reset terminator. Each complete string must match one
of the ten actual title-name strings and its BIGFONT atlas, per-letter metrics,
kerning, original geometry, and CPU clipping calculation. Unrecognised or edited
command shapes retain the untouched original path. There is no broad negative-
rectangle reinterpretation or renderer-wide scissor override.

The first owner run did not activate the correction: its continuation check had
incorrectly used familiar F3DEX B4/B3 IDs instead of DKR's B3/B2. This was verified
against render_text_string's generated instructions at 800C4C84 and 800C4CB8
(read-only inspection). The pipeline now derives both tags from DKR's own gbi.h,
records them in its receipt and emits bounded skip diagnostics for unrecognised
font-pass shapes. Compilation alone does not prove the path was admitted.

For recognised runs, restore each glyph's signed bounds and original UVs. Move
the whole string centre by (centre-160)*(windowCover-1), preserving glyph size,
spacing, colours, trail order, timing and the centred dwell position. This makes
the arrival/departure path cover ultrawide windows without scaling the font.
Only those individual draws use the expanded horizontal scissor. Restore the
previous scissor, edge origins and rectangle aspect immediately afterwards.

Remember only affected native 320-wide framebuffer identities. Normalise their
producer-owned aggregate scissor to 0..320 immediately BEFORE fullSync transfers
the workload to workers. Never normalise individual wide text draw scissors or
touch worker-owned workloads. This avoids the earlier transient aspect/stretch
regression. No extra texture upload, render target, shader, GPU wait or ray work
is added. The one-time runtime marker proves admission, not visual acceptance.

## Build and owner acceptance

Create a NEW immutable stage with pipeline.py stage --base <accepted viewport-
flow stage> --output <new build stage>. Configure this NativeOverlay.cmake and
DKR_TITLE_TEXT_STAGE while retaining all accepted viewport/water/save/Store/SDK
stages. Compile Windows and Linux, and package the Linux AppImage too.

Compilation and static package inspection only; do not launch the game or run
replays/tests. The owner checks every title name/trail entering and leaving at
16:9, 16:10, 21:9 and wider, window/fullscreen and resize; unchanged letter size
and centred names; clean title-logo/start/options transitions; original 4:3 and
Accurate framing; and retention of the accepted water/intro behaviour.
