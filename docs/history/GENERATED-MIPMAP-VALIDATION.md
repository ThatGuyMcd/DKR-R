# Optional mipmaps — v1.0.5 Beta 2

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


## Behaviour

Graphics now offers **Generate texture mipmaps (optional)**, off by default.
Enable it before starting a game. If changed during play, the overlay explicitly
shows the requested/active difference: stop and start the game to apply generation.
An application restart is not required. LOD bias (-2 to +2, default zero) continues
to change sampling live after the mip chain exists.

Original N64 textures are downsampled from decoded RGBA on the texture-upload GPU
worker. PNG/Rice textures are downsampled on the existing loading workers. The
base image is preserved byte-for-byte; authored DDS chains and pack files are not
rewritten. There is no new disk cache. Existing texture hashes/content invalidation
and eviction remain the owners of texture lifetime.

Generated sampling is deliberately conservative: Modern-mode perspective opaque
world draws, valid depth testing/writing, and whole-texture tiles only. HUD/menu
rectangles, raw TMEM, framebuffer copies, translucent/decal/shadow passes, alpha
cutouts and unsafe subtiles retain their previous sampling. Accurate mode disables
generated sampling, independently of authored DDS sampling. This is not a promise
that every texture or close-up view will change when LOD bias moves.

DKR's F3DDKR bridge leaves its separate projection matrix at identity and carries
the combined model/view/projection matrix in the model stack. Eligibility therefore
also checks the draw's actual model matrices for a finite projective W component.
It does not relabel projections or edit transforms. Affine/translated HUD matrices,
invalid ranges and mixed affine/projective ranges remain excluded. This specific
regression is covered by the CPU scope test; separate-projection-only eligibility
would incorrectly suppress DKR world mipmaps.

## Loading feedback

- Work lasting less than 250 ms does not flash a modal.
- Longer work shows **Generating texture mipmaps**, an animated spinner and
  processed/pending counts. The counts describe work processed, not a percentage
  of a whole pack that may only be discovered gradually during play.
- The modal dismisses when work completes. No dismiss button can abandon a
  partially initialized texture.
- PNG workers publish atomic activity only; they never call ImGui.
- For native upload waits, the existing condition-variable completion predicate
  is retained. After 250 ms a thread-local callback is offered every 33 ms with
  the upload mutex released. Default-off sessions retain the original wait path.
- The callback uses the existing window, presentation worker, swap-chain framebuffers
  and semaphores, under the present-thread mutex. It renders UI only; it does not
  replay a game workload, advance a VI, modify online barriers or change queue IDs.
- It requires a completed initial presentation and valid, non-resizing swap-chain
  resources. A failed device, minimized/invalid surface or GPU hang cannot be made
  responsive by a spinner. No watchdog timeout or unsafe presentation bypass was added.

## Ownership and bounded work

Dependency changes are solely in ordered patch
`patches/rt64/0016-optional-generated-texture-mips.patch`, applied by the verified
dependency pipeline. Patch 0015 remains the sole live-bias owner. No hand-edited
dependency sources, generated recompilation output, simulation, networking, save
routing, shadow geometry or controller/window lifecycle changes are part of this pass.

Each downsample reads and writes separate resources. The final chain stays a copy
destination until complete. Temporary textures and descriptors survive the existing
upload fence; no additional per-frame device-idle wait was added.

Generation is limited to 4,194,304 base texels per texture. PNG staging and native
batch scratch payload have a 32 MiB bound, with at most 512 retained native scratch
textures per batch. These are payload/resource-count limits, **not** a claim that
driver allocations, all workers together or total VRAM are capped at 32 MiB.
Unsupported/budget-exhausted optional generation keeps the base-level path. Full
chains are included in texture memory accounting. No global colour-space conversion
was introduced; lower levels use matching integer alpha-weighted RGB/mean-alpha
filtering on CPU and GPU. Binary alpha coverage is not guessed: cutout draws are excluded.

## Automated evidence (7 September 2026)

| Check | Result |
| --- | --- |
| Windows DX12 generated pixel readback | 47,088 exact byte comparisons, including base level |
| Windows Vulkan generated pixel readback | 47,088 exact byte comparisons, including base level |
| Live generated sampling, each API | 4,800 channel comparisons: original/PNG match; all ten samplers; AF 1/16; fractional and -2/0/+2 bias; nine exclusion scenarios |
| Pixel fixtures | 1x1, 1x31, 31x1, 3x5, 7x9, 32x32, 64x16; opaque, graded alpha, binary alpha, transparent |
| CPU policy tests | Invalid/overflow sizes, odd final rows/columns, alpha weighting, Modern/Accurate/off gates, original/replacement identity, loading delay/counters/callback scopes |
| Shipping Vulkan specialization | Six production shader graphs, 24 specializations covering smooth/flat and MSAA/non-MSAA; added after the real-game launch exposed a sampler-loop incompatibility |
| Windows DX12 loading test | 77 UI frames during three-second genuine cache-publication wait; six real TMEM-generated levels; automatic dismissal |
| Windows Vulkan loading test | 79 UI frames; same checks |
| Linux Vulkan loading test (Ubuntu 24.04 / WSL) | 77 UI frames; same checks |
| Presentation safety test | All three loading tests assert unchanged present count, write/read/barrier cursors and present ID |
| Regression suites | All 67 passed on each platform; one later concurrent Windows run needed the unchanged DirectSession suite rerun separately (123.94 s, passed) |
| Actual game, Windows Vulkan | Generated PNG/Rice pack enabled: four-minute intro/attract run, 7,146 display-list tasks, clean timeout exit; no shader compilation failure after correction |
| Actual game, Windows DX12 | Generated original textures enabled: 90-second intro/attract run, 2,646 display-list tasks, clean exit |
| Actual game, Linux Vulkan | 90-second original-texture run and clean exit under WSL llvmpipe software Vulkan; functional smoke only, not a hardware performance result |

The loading harness delays only its own texture-cache publication lock. It uses
the actual cache worker, mipmap generator, modal component and blocked-wait presenter;
there is **no test delay in shipping code**. Harness initialization/cleanup issues
were corrected in the harness, not by changing Plume or application lifecycle.

A full-game Vulkan launch caught a loop in the new sampler that the dynamic GPU
fixtures could compile but the production re-spirv specialization graph could not.
The new production-graph test reproduced that failure. The sampler now uses two
explicit scalar coordinate calculations instead; the optimizer itself is unchanged.
The build must pass that regression and actual game launches before packaging.

The direct-session source/header and its test are byte-identical to the saved
pre-mipmap source archive. Its one timing-sensitive handoff assertion failure is
retained in `build/mipmap-tests-windows-final.log`; the isolated passing rerun is
in `build/mipmap-direct-session-recheck.log`. It was not disabled or modified.
The loading harness also waits for the worker's activity guard to unwind after
cache publication, rather than requiring both independent events in the same
instant; dismissal is verified within a bounded 500 ms observation window.

Evidence logs are in `build/mipmap-gpu-*.log`, `build/mipmap-loading-*.log`,
`build/mipmap-tests-*.log` and the final package directory. Tests are not shipped
as launcher executables. The source archive includes the tests for reproduction.

## Qualification limits and rollback

Automated fixtures do not establish pixel identity for every track, pack, cutscene
or split-screen arrangement. Full native Steam Deck thermal/load-time qualification,
all aspect ratios/player counts, every animated palette/atlas, and extended
multiplayer playtesting remain acceptance checks for this beta. WSL is not SteamOS.
Do not infer a universal FPS improvement or crash-free guarantee from these results.

The accepted Beta 1 remains at `dist/playtest-v1.0.5-beta.1-20260907`.
The live-LOD-only candidate remains at `dist/live-lod-v1.0.5-beta.1-20260907`.
The pre-mipmap source snapshot is in `build/rollback-pre-mipmap-beta2-20260907`.
Beta 2 is staged separately; neither rollback package is overwritten or promoted
solely on these tests.
