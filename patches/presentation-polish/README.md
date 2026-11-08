# Intro, fade and original-water polish

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

Patch Pipeline layer on the accepted credits-backdrop beta 10 stage. All
authored files live here. Existing stages, checked-out runtime, dependencies,
RecompiledFuncs and RecompiledPatches remain unchanged. No target application,
gameplay tests, captures or replays are launched by this pipeline.

## Changes and boundaries

* Credits: a two-native-pixel horizontal overlap is applied to both the exact
  admitted black fade and its signed scissor. Vertical coordinates stay at
  0..native height. Colour/depth aggregate readback bounds are intersected with
  the native 320x240/288 window independently of widescreen draw geometry,
  immediately after the fade and again before fullSync. Per-draw signed
  clipping and original scene proportions remain unchanged.
* Intro: identify the actual MENU_LOGOS white SMALLFONT copyright run by its
  full text, original font metrics, atlas/load commands, colours and location.
  Add a compact rounded black plaque and the centred second line
  `RECOMPILED BY THATGUYMCD!`. Reuse the live game atlas, including texture-pack
  replacements. Match the original text opacity, retain the original line,
  validate all texture commands before use, and restore material/aspect/atlas.
  This is not an OS-font overlay or a new full-width bottom bar.
  Follow-up: one continuous rounded fan mesh, all four arcs at the same radius
  and tessellation, replaces separately raster-rounded strips. Black opacity
  is 68% of the copyright opacity. The screen-space X correction matches the
  original-font rectangles so corners remain circular on wide viewports.
* Intro sky: capture only the actual skydome's current validated vertex range.
  Extend the lower portion using tan(effective FOV/2), with a bounded overlap.
  Preserve upper sky and world/logo projection. A hemisphere whose last ring
  ends at/above Y=0 gets a downward extension of that last ring only. Changes
  are to decoder-local vertices, not the guest mesh or camera.
* Original water: animated liquid frames each have a distinct replacement
  identity. The existing streaming policy requests each at first visibility,
  which can alternate original and HD artwork through the first animation
  cycle. Capture only validated liquid families from the scene registry and
  wave reference. Warm their matching Rice replacement paths in one background
  request batch instead of waiting for each frame to become visible. Reuse the
  existing upload worker and streaming threads, including their deduplication;
  do not mix direct loads with in-flight streams of the same image. Normal
  pack precedence, author load policy and memory budget remain.
  Bounded: 64 frames/family, 128 requested paths/scene/generation, 512 frame
  hashes per scan. No main-thread file I/O, GPU waits or dummy textures.
  Static images, indexed/interlaced textures, non-stream policies and native-only identities retain
  their existing loading path; no replacement identity is guessed for them.
* Native UV history now resets on a replacement-set generation change, and
  duplicate-view samples require the same image. Ordinary animation frame
  changes still retain the accepted phase/velocity matching. Clearing an old
  enhanced material seals pending calls first. Water Off schedules no optics.

## Source evidence

Read-only decomp `menu.c:menu_logo_screen_loop`, `font.c:render_text_string`,
`tracks.c:skydome_render`, `waves.c:waves_update`, and `structs.h` provide the
font, model and animated texture layouts. Resolve addresses independently from
the US v77/v80 symbol maps. The existing RT64 Rice resolver uses the same
word-swapped CRC and domain-separated identity; this layer mirrors that
identity only for supported complete texture frames, with no dependency edit.

## Rebuild

1. `python -B patches/presentation-polish/pipeline.py stage --base <accepted
   credits stage> --renderer-base <frozen effective renderer/render folder>
   --output <NEW build stage>`.
2. Configure `CMAKE_PROJECT_INCLUDE` to this folder's `NativeOverlay.cmake` and
   set `DKR_PRESENTATION_POLISH_STAGE`. Retain all prior pipeline stage settings.
3. Compile Windows and Linux x86_64. Package Windows ZIP and Linux AppImage,
   retaining previous assets/workers, version, rollback IDs and receipts.

Compile and static receipt/package checks are not visual acceptance. The owner
checks cold/warm loads with HD packs and Water Off/On; windowed/fullscreen;
4:3/16:9/16:10/21:9; high FOV; the complete credits fade; both spinning logos;
plaque fading/skip transitions; and the accepted river/waterfall smoothness.

## Owner-reported credits crash

The 20261019-182050 dump faults in D3D12Buffer::map, called by DKR's optional
GeneratedMips::uploadRGBA32 on a texture streaming thread. The upload is a
320x320 PNG (9 mip levels; 0x91000-byte staging buffer), not credits text or fade
geometry. The C++ buffer wrapper is non-null but its native D3D12 resource is
null. The exact driver allocation failure reason was not captured in the log.

Compose the existing DKR-owned generated-mip hook over a new frozen build copy
of the effective texture-cache translation unit. Validate actual D3D12/Vulkan
resources before mapping/recording, including optional native-texture mip
scratch. Attempt a guarded base-level upload if mips are unavailable; if that
also fails, retain the normal original-texture path and log a bounded warning.
No unchecked base uploader follows a failed optional allocation. A first-party
renderer setup adapter caps the cache at half dedicated VRAM, 512..3072 MiB,
to reserve headroom for scene targets, streaming and mips. This does not prove
that the original failure was OOM; it is preventive memory budgeting.

No checked-out dependency, public renderer interface, guest simulation or
previous generated input is edited. Only the new Patch Pipeline build copy
and first-party renderer adapter are compiled. The source receipt records the
exact effective renderer inputs, rather than assuming an upstream checkout
matches the owner-tested staged renderer.

## Second credits crash: confirmed negative-row readback

The exact matching EXE/PDB for 20261019-184638 resolves the fault to
Framebuffer::copyNativeToRAM. Its dstRowStart is 0xFFFFFFFE (-2), width 320,
dstRowEnd 242. The previous two-pixel vertical fade overlap introduced that
negative row into drawColorRect/drawDepthRect; State::fullSync converts the
signed aggregate top into unsigned row indices. The resulting destination is
far outside guest memory. GPU resource-creation failures also occur in the log;
the allocation guard alone could not repair this producer-side bounds error.

Remove vertical overscan, retain the horizontal edge overlap, and constrain
both aggregate write rectangles and aggregate scissor to the known native
window. Keep each saved widescreen draw call and its scissor untouched. Do not
patch the dependency consumer or suppress the exception. The guard runs on
the producer-owned workload before submission, not after handoff to a worker.
