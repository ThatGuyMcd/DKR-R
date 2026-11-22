# Standalone launcher performance — 1.0.4

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


This is a project-owned launcher performance pass, not a renderer migration.
The active, visible launcher targets **60 frames per second**, including when
no controls are being moved (or the reported display rate if below 60 Hz).
An unfocused launcher without recent controller input targets 10 Hz; a hidden
or minimised launcher does not build or submit UI frames. Services and input
continue to be pumped while drawing is throttled.

## What was measured

The reported problem was approximately 10 FPS throughout the Linux launcher,
while the overlay during gameplay was fast. An unmodified staged Linux build
reproduced approximately 11 FPS in WSLg, using SDL's software renderer. This
was an extracted executable test, so AppImage extraction was not the cause of
its sustained low frame rate. The test used a clean profile without texture
packs. Slow texture-pack scanning was therefore not necessary to reproduce it.

Local measurements used Ubuntu 24.04/WSLg on a Ryzen 3900X Windows host, with
SDL 2.30.0 on Linux. These are **not measurements from a physical Steam Deck**.
Do not interpret them as a Deck battery-life or power-consumption result.

| Checkpoint | Viewport / workload | Measured result |
| --- | --- | --- |
| Previous packaged Linux launcher | 1440 × 900, clean Play page | 210 frames / 18.53 s; UI construction 0.199 ms; drawing plus present 87.936 ms |
| Clip and cache cloud tiles | 1440 × 900 | Drawing plus present about 62.7 ms; insufficient alone |
| Solid-colour geometry path | 1440 × 900 | Drawing plus present about 29.5 ms; insufficient for 60 FPS |
| Remove the root fill hidden by opaque clouds | 1440 × 900 | Drawing plus present about 26.9 ms |
| Add bounded static-panel caching | 1440 × 900, clean Play page | Drawing 12.87 ms and present 0.80 ms in the initial five-second sample, which includes cache warm-up |
| Final controlled Linux check | 1280 × 800, ROM selected, stationary Play page, warmed cache | Successive samples: 16.667 / 16.668 ms average frame interval; drawing 13.379 / 13.476 ms; present 0.566 / 0.567 ms |
| Native Windows check | 1440 × 900, active launcher | 300 frames per five seconds; approximately 16.667 ms average frame interval |

The intermediate rows are diagnostic checkpoints, not a controlled hardware
benchmark across every tab. Page contents, warm-up and viewport size matter.
At 1440 × 900 with a ROM selected the final Linux run was closer to 59 FPS;
larger windows can remain CPU-limited. The new 60 Hz ceiling is not a guarantee
that every machine, DPI scale, tab or resolution can sustain 60 FPS.

### Why the cost was misleading

The Linux launcher intentionally uses SDL2 software rendering, whereas the
in-game overlay uses the game's renderer. Those are very different pixel
workloads. SDL software drawing is queued, and much of it runs inside
`SDL_RenderPresent`. The old aggregate labelled `avg-present` includes draw
submission and execution, not merely time waiting for a display refresh.

The opt-in phase profiler showed small service, event and UI-construction
costs compared with software pixel drawing in this reproduction. Two large
contributors were repeatedly transforming cloud tiles (including invisible
parts and mirrored copies), and sampling the font texture to draw large
solid-colour ImGui panels. The fallback gradient and root background were
also being drawn underneath a fully opaque cloud image.

This establishes a reproducible rendering bottleneck. It does not establish
that unrelated slow storage, drivers, imports or network calls can never
cause a separate hitch.

## Changes and boundaries

All production changes for this pass live in `runtime-recomp/src/game/`:

1. **Cloud background.** Clip geometry and UVs before submission; preserve
   scrolling speed and mirrored tile continuity. Linux retains a decoded
   source surface and caches scaled normal/mirrored textures per size. Failed
   allocations use the existing texture path and are not retried every frame.
   The missing-image gradient is now drawn only when the image is unavailable.
2. **Solid UI geometry.** A launcher-only adapter recognises triangles whose
   three UVs are exactly the font atlas's white texel. Those triangles use SDL's
   untextured geometry path with the original vertex colours. Glyphs, images,
   textured antialiased lines, callbacks, ordering and clip rectangles retain
   their normal handling. The dependency's ImGui SDL backend is not edited.
3. **Static panel cache.** Repeated large solid meshes can be rasterised once
   and copied on subsequent frames. Exact vertex/colour data form the key;
   transient meshes must recur before allocating. The eight-entry cache has
   a 16-megapixel aggregate texture limit and a four-megapixel per-entry limit.
   Colour gradients, invalid bounds, unsupported scaling and failed allocations
   fall back to normal drawing. In-use entries cannot be evicted before their
   pending draw callbacks execute. Renderer resources are released before the
   launcher renderer is destroyed. This is a surface-only software operation;
   it creates no second window or GPU context.
4. **Frame pacing.** Replace an additive sleep with interruptible SDL event
   waits and a monotonic, phase-preserving deadline. Whole-millisecond rounding
   no longer turns a nominal 60 Hz ceiling into roughly 57–58 Hz. Slow drawing
   does not incur another full sleep or trigger an unbounded catch-up burst.
   Close, launch and modal requests remain live between draw deadlines. Quiet
   focused frames use 60 Hz, not the previous 30 Hz cap.
5. **Repeated UI-only work.** Cache ROM-catalog canonical keys for display
   comparisons, without bypassing selection or launch validation. Cache the
   texture-library's maximum wrapped name height until its data, filters,
   query, width, font or font generation changes. Full names and uniform card
   heights are preserved.
6. **Diagnostics.** `DKR_LAUNCHER_PROFILE=1` enables bounded five-second
   phase summaries in the existing runtime log. It separates services, events,
   UI construction, drawing, presentation and frame intervals; Linux adds
   draw-list/background/panel detail. Extra render flushes exist only when
   profiling is enabled. No per-frame logging or telemetry upload was added.

SDL explicitly supports a null texture with per-vertex colour in
[SDL_RenderGeometryRaw](https://wiki.libsdl.org/SDL2/SDL_RenderGeometryRaw).
The software backend was inspected at its
[SDL 2.30.0 source](https://github.com/libsdl-org/SDL/blob/release-2.30.0/src/render/software/SDL_render_sw.c).
No SDL source or other dependency source was modified.

The existing Linux seam-protection underlay is retained, including its
overlapping translucent rectangles. The root fill removed here is the one
fully covered by the opaque backdrop, not that underlay.

### Explicitly untouched

- SDL window creation, renderer selection and launcher-to-game ownership.
- RT64, N64Recomp, N64ModernRuntime, SDL and all other submodule source.
- Generated `RecompiledFuncs` and `RecompiledPatches` source.
- Gameplay, interpolation, shadows, online synchronisation and save handling.
- SDL2/SDL3 input-host ownership, controller mappings and gyro implementation.
- The in-game overlay's rendering backend and pacing.

Existing Magic Codes work in the dirty checkout predates this pass and is
preserved; it is not a new launcher-performance change.

## Verification and limitations

`DKRLauncherRenderPolicy` checks tile coverage/UV clipping, large-clock mirror
continuity, malformed extents, active/idle/background/hidden pacing, controller
focus exceptions, slow frames and deadline rounding.

`DKRLauncherSoftwarePixels` renders without a window and compares the original
white-texture path, solid geometry and eligible cached meshes. It covers
clipping, fractional coordinates, alpha edges, overlapping translucent layers
and gradient fallback, plus cache reuse/invalidation and size guards. Displayed
RGB differences are bounded at three levels out of 255 in these cases.
Destination surface alpha is not used by the opaque launcher window and is
not compared: SDL 2.26 on Windows and SDL 2.30 on Linux accumulate it differently.

The full project tests must pass on both platforms before packaging. Package
checks exercise controller-pak behaviour and SDL2/SDL3 live switching and scan
for prohibited ROM/save data. The companion build validation records actual
build/test and launch results, rather than treating compilation as proof of
interactive correctness.

Remaining device acceptance checks:

- Steam Deck Gaming Mode and Desktop Mode at 1280 × 800, with no external FPS
  limiter set below 60. Check every tab, menus/modals and controller-only idle.
- Resize, minimise/restore, fullscreen and fractional-DPI behaviour; the static
  mesh cache deliberately falls back outside its supported 1:1 coordinate path.
- Large texture libraries, importing/filtering/searching and long pack names.
- Repeated launch/return cycles using both ROM revisions, then normal online
  play with a real second machine. Existing unit tests do not replace these.
- Observe CPU use, temperature and battery drain on the actual device. Higher
  delivered frame rate does not by itself prove lower total power consumption.

Initial cache warm-up and window resizing can produce a longer frame. Large
software-rendered desktop windows may still miss 60 FPS. Those are explicitly
not hidden by reducing UI image quality or changing the game renderer.

## Local rollback

The previous Windows ZIP, Linux AppImage and source ZIP were copied and their
SHA-256 hashes verified before this pass. They remain under
`build/rollback-pre-launcher-performance-20260906/`; the original distribution
under `dist/magic-codes-v1.0.4-20260906/` is untouched. New packages go into
`dist/launcher-performance-v1.0.4-20260906/`. Nothing is published to GitHub by
this operation.
