# High Quality Water art direction

The DKR palette with full HD reflections is approved. The implementation keeps
the plan's reflection constraints; its details and verification are recorded below.

Keep the current HD water's strong RTX reflections. Bring the water's underlying
colour and surface highlights closer to Diddy Kong Racing's art style. The
reflection of a buoy, palm tree or shoreline should remain clear and recognisable
as the surface moves. The art direction must not depend on reducing reflection
strength or replacing reflective water with the original raster surface.

## Visual target

Use the supplied High Quality Water screenshot as the composition and reflection
reference. Use the original-water screenshot for its turquoise palette and broad
mint highlights. The images show different frames, so a runtime comparison will
need matched camera and scene states.

The recommended preview is RTX with a DKR palette: a brighter turquoise water
body, strong orange buoy and green shoreline reflections, and restrained mint
highlights. A second possible direction adds slightly broader painted crest
accents while keeping the same reflections. Do not put opaque painted shapes
over reflected scenery.

The generated preview is an art-direction illustration, not an in-game render.
It cannot prove ray tracing, exact reflection fidelity or performance. The
supplied screenshots also do not establish which reflection backend was active.

## What the implementation supports

- `runtime-recomp/src/game/enhanced_water_policy.hpp` provides depth and
  shallow-water colours, roughness, ripple and foam profiles. Stock courses and
  custom-track metadata can use different profiles.
- `runtime-recomp/src/game/enhanced_water_settings.hpp` separates reflection
  method, strength, detail and distance from several appearance controls. Keep
  those reflection settings intact while establishing the new colour direction.
- `runtime-recomp/src/game/enhanced_water_rt64.hpp` builds the material from
  those profiles. The track-colours option uses a material profile; it does not
  directly preserve the original rendered water texture.
- The composed `WaterPS.hlsl` computes the water body separately from reflected
  scenery. The full shader adds reflected radiance using the angle-dependent
  Fresnel response and the selected reflection strength. This is the path to
  retain, including the existing hardware reflection and fallback behaviour.
- Its `CLASSIC_STYLE` branch uses the captured original water colour and a
  reduced reflection weight of `reflection.a * fresnel * 0.32 * (1 - foam)`.
  That branch is not a suitable replacement for the requested full-reflection
  appearance. The existing pipeline compiles both paths.
- The material's vertical-surface flag already distinguishes waterfalls and
  similar surfaces. Do not reuse it as a horizontal-water style switch.

These findings come from the first-party material/settings code and read-only
inspection of the composed shader. Implementation must use the existing
approved Patch Pipeline and leave generated code and submodules untouched.

## Proposed work after approval

1. Establish a matched HD-water baseline. Record the active reflection backend,
   current material settings, camera and scene. Capture stills and moving footage
   of the title scene, a sunny course, dark water, snow, waterfalls and a custom
   track. Include reflections of scenery and moving racers.
2. Tune the water body first. Adjust shallow/deep colours and absorption so
   tropical water reads as cyan and turquoise without turning reflected orange
   objects cyan. Work before reflection composition rather than grading the
   entire final image. Honour stock/custom course profiles, fog and texture packs;
   do not give every course the same tropical colour.
3. Retain the full reflection pipeline. Keep the hardware path, ray budget,
   reflection resolution, distance, strength and angle-dependent response at the
   baseline settings. Preserve current SSR and authored-reflection handling where
   they are already used. Do not introduce new blurring, shorter rays, reduced
   reflection coverage or a switch to the reduced-reflection classic path.
4. Refine surface highlights carefully. Try a few broad, soft mint crest accents
   and less harsh non-reflection sparkle. Keep the existing reflection normals
   during the first pass: changing ripple normals also changes reflected detail.
   Preserve wave geometry, motion, depth/refraction cues, shore contact, wakes and
   spray. Any later normal change needs a separate moving comparison. Do not
   overlay flat colour or foam sheets across reflected objects.
5. Keep the choice small. Prototype through existing material controls first.
   If a style choice is needed, add it within the current water settings while
   retaining the present HD appearance as an option. Style must remain separate
   from reflection backend and quality. Do not silently reset reflection choices
   or introduce a new launcher layout.
6. Validate the actual shader result. Compare identical frames and motion with
   the baseline at the same reflection settings. Check buoy/shoreline reflection
   coverage, readability, distortion, fog, distance transitions and moving racers.
   Measure GPU frame time, memory and any added passes. Avoid a new full-frame
   colour capture unless measurements establish it is necessary and affordable.
7. Check the existing compatibility matrix before packaging: both supported ROM
   revisions, stock/custom tracks, texture packs, races, battles, splitscreen,
   online racers and spectators. Test Windows D3D12, Linux Vulkan, ray-capable
   hardware and Steam Deck's supported fallback. Preserve the residency, memory,
   GPU-fence and online presentation fixes. No physics or rollback changes are
   needed for this material-only proposal. Prepare matching Windows and Linux
   AppImage playtests once the approved appearance is ready.

## Acceptance target

The result should still look unmistakably like the current reflective HD water,
with the buoy and shoreline reflections present at the same settings. Its body
colour and restrained highlights should fit the surrounding DKR scenery better.
A still preview is only a direction: moving hardware captures must establish
reflection stability, appearance and rendering cost before release.

## Implementation

Graphics > Ray tracing & water has a Water style choice: DKR colours and
Current HD. The same control appears in the launcher and in-game overlay. The
choice is saved independently of quality and reflection settings. Reset water
appearance selects DKR colours; quality presets and quality reset keep the
selected style. Original water keeps its existing rendering path.

The appearance policy lives in normal first-party source under
`runtime-recomp/src/game/water_style/`. Stock tropical water gets brighter cyan
and turquoise body colours; snow keeps a colder palette and dark courses keep
their previous colours. Custom track profile colours remain intact. The material
adapter leaves absorption, roughness, reflection settings, normals, depth and
wave controls alone. Waterfalls, fountains and lava bypass it.

The existing enhanced-water shader patch adds broad, derivative-filtered accents
using the current wave lighting and reduces the sharpest direct sun glints.
These changes happen before reflection composition. The reflection sampling,
Fresnel response and reflected-radiance composition are unchanged. No extra
normal map, ray, texture capture, render pass, GPU fence or resource is added.
The material and structured-buffer layouts retain their existing sizes, including
the 2048-byte draw stride. An exact float-representable flag uses the debug
field's unused high bits; the low eight bits retain their debug meaning.
The spray and wireframe consumers mask those debug bits, so selecting an art
style cannot suppress the existing spray or change the debug rendering mode.

A profiling run exposed an access violation at the native view-matrix conversion
in the enhanced-water setup. DKR already supplies a captured camera, and the
existing projection helper overwrites that provisional camera. The DKR path now
skips the unused native view lookup. Native-camera water still requires its view
table. Both paths check the raster-projection and viewport bounds before reading
them; an incomplete submission retains the existing original draw. The normal
captured-camera projection, ray setup and reflection shader are unchanged.

The transition pipeline binds the shared UI, persistence and material hook. The
hook runs before the existing single material comparison and flush. The reviewed
upstream files and historical capsule's pinned source headers retain their exact
bytes. The residency and decoder paths resolve the same adapted material header.
No submodule, RecompiledFuncs or RecompiledPatches source is edited.

These are isolated matching Windows and Linux AppImage test builds. The existing
compatibility ledger conservatively includes changed integration sources, so
players should use the matching test pair together. Gameplay, physics, rollback
and transport logic are unchanged. Both-owner review remains required before
merging any compatibility change; this work does not merge or publish a release.

## Verification

The full Windows host suite passed all 150 tests. Linux passed 150 of 151 on
the full run; its regression harness exceeded subprocess timeouts while other
build and packaging work was running. That same harness passed when rerun in
isolation. Both original results and the retry are retained. The water policy
target passed again on both platforms after the camera guard was added,
including captured-camera and missing-table cases. Guard/tooling and the
changed-file pre-commit checks passed without changing pinned source bytes.

Both production renderers and their shader variants compiled. Readback checks
confirmed the final ZIP and AppImage contents, worker probes and matching
compatibility IDs. The only payload changed from the preceding Mod Store test
package is each platform's runtime executable. Source comparisons confirm that
the reflection sampling and composition block is identical to the prior shader,
and the appearance hook runs before the existing single material flush.

The final Windows package ran DKR colours with ROM revision 77 for 90 seconds:
1,835 profiled water frames carried the style flag, enhanced water was active,
and shutdown was clean. Current HD ran with revision 80 for 65 seconds:
1,331 water frames had no style flag, enhanced water was active, and shutdown
was clean. Windows reported ray-query support. The actual Linux AppImage ran
under Xvfb and software Vulkan for 65 seconds with revision 77: 302 water frames
carried the style flag and shutdown was clean. These exercise the draw paths;
they do not establish matched visual quality or hardware performance.

A final 90-second local host/client run reached the race on both peers, with
dynamic delay negotiated, zero minimum race lead and no replay-safe failure.
Both peers presented more than 1,500 race frames and closed cleanly. The
existing Mod Store overlay, cover loading and safe GPU eviction checks also
passed. This same-PC fixture has no artificial network impairment and does not
establish behaviour on a real long-distance connection.

Moving visual acceptance is still a playtest task. No matched before/after
hardware footage, Steam Deck hardware run, complete course/spectator matrix or
controlled GPU-cost comparison is claimed. Test the supplied pair together and
compare both styles at the same camera and reflection settings before release.
