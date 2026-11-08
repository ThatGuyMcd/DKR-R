# Full-frame credits

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

Patch Pipeline-only addition on the owner-accepted corrected title-text stage.
Dependencies, RecompiledFuncs, RecompiledPatches and checked-out runtime sources
remain untouched. No guest memory, simulation, audio, credits timings, ordering,
save state or skip controls change. No application/gameplay tests are run.

## Source evidence

menu_credits_init configures the same inset as the old rotating-logo intro:
0,40..320,196 NTSC / 0,38..320,224 PAL, with a full-size custom 3D viewport whose
inclusive bounds give the same 160.5 X centre. Reuse the accepted intro viewport
algorithm in a separately admitted MENU_CREDITS (25) instance. The original
logo helper stays byte-identical. Extend its reviewed scissor/fill rectangles to
the complete native height, centre the 3D viewport, retain both viewport scales,
and let RT64 supply the host aspect. credits_fade uses the same inset rectangle,
so it too covers the complete viewport without changing opacity/timing.

Credits use postrace_render / draw_menu_elements, not the title-name table.
gCreditsMenuElements contains nine 0x20-byte slots. Headings use FUNFONT (0),
names BIGFONT (2), with ASCII text, horizontal centre alignment and transparent
backgrounds. Their paths enter at X=480, hold at160 and leave at-160. Addresses
for both supported US revisions come from the individual decomp symbol maps.

Extend the accepted font-pass parser to try those two fonts only in MENU_CREDITS.
Each complete run must match an actual active credit row, its font, colours,
opacity, alignment, atlas, metrics and clipping. Unrecognised shapes retain the
original path. Spaces have separate alignment/cursor kerning rules in retail;
preserve both. Restore signed right-edge positions beyond512 by comparing the
packed coordinate domain. Use at most96 glyphs/1023 scanned words. Centre-path
translation preserves letter sizes and centred layouts on ultrawide screens.
The original title-name matching method remains unchanged and separately gated.

Only admitted text draws get extended horizontal scissors. Restore each prior
scissor/origin/aspect immediately afterwards. Before fullSync, normalise only
the producer-owned affected framebuffer aggregate to native320 width. This
retains the earlier protection against transient framebuffer/text stretching.
No shaders, rays, extra textures, render targets or GPU waits are introduced.

## Fade/lower-sky follow-up

Owner acceptance confirmed the first credits framing/text update, but reported
4:3 fades and lower-sky side bars. The first fill widened its geometry without
also widening the inherited draw scissor. It now uses signed window coordinates
for BOTH, with exact restore and native framebuffer-reference normalisation.
Full 319x239/287 2D/transition scissor resets are normalised to complete native
bounds; partial UI scissors remain untouched.

The existing dkr_widen_void_primitive hook uses split_sky_horizontal_cover_scale,
which explicitly returns1 for the credits' single-camera layout0. Capture only
the live credits void mesh range from its active ping-pong buffer, final batch
cursor and count. void_generate_primitive flushes at24 vertices, so the range
must cover earlier batches too. Validate its <=2000-byte allocation and the
camera lateral basis. Apply the same camera-horizontal expansion to local
decoded vertices only, preserving Y, forward depth, colours and guest memory.
Disable this path for split layouts to avoid applying their existing fix twice.

## Build and owner checks

pipeline.py stage --base <accepted corrected-title stage> --output <NEW stage>.
Configure this NativeOverlay.cmake and DKR_CREDITS_VIEWPORT_STAGE, retaining all
previous title/viewport/water/save/Store/SDK stages. Build Windows and Linux,
including its AppImage. Package inspection verifies both credits markers and
every accepted title/water/intro feature. Compile/static inspection is not proof
of visual acceptance; the owner performs that check.

Check every scene/fade and the rotating character portraits; heading/name
entry/hold/exit; continuation headings; long/spaced/punctuated names; best times
and final/cheat text; 4:3 Fit,16:9,16:10,21:9 and wider; fullscreen/windowed and
resize; original-aspect/Accurate framing; clean transitions back to the logo
intro/title; retained title names and river/waterfall interpolation.
