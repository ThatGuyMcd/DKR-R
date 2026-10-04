# Optional generated texture mipmaps — approved plan and implementation record

Status: implemented for **v1.0.5 Beta 2**. The original approved plan is retained
below for traceability. Actual implementation, measurements and qualification
limits are recorded in [Generated mipmap validation](GENERATED-MIPMAP-VALIDATION.md).
The user approved implementation and subsequently requested a loading modal.
The previous live-LOD-only repair did not add mip levels.

## Evidence and the changes it requires

| Current source | Finding | Planned change |
| --- | --- | --- |
| `extern/rt64/src/render/rt64_texture_cache.cpp`, `TextureCache::setRGBA32` | PNG upload sets `mipmaps = 1` and allocates one level. | When opted in, generate and upload a complete lower-resolution chain on the existing texture-loading worker. Keep the original base level unchanged. |
| Same file, native upload/decode path around `Texture Cache RGBA32` | Original textures decode on the GPU into a one-level RGBA texture. | Generate from decoded colour, after the decode has completed, rather than averaging packed TMEM bytes or palette indices. |
| Same file, `TextureMap::add` / `TextureMap::use` | Original texture dimensions hard-code a depth of 1; `hasMipmaps` is only set for replacements. | Report the actual mip count for eligible decoded original textures too. Keep original/replacement identity and high-resolution flags independent of mip availability. |
| Same file, `setDDS` | DDS imports already respect their authored mip count. | Preserve authored chains; do not regenerate or overwrite them. Keep this existing behaviour when generated mipmaps are off. |
| `extern/rt64/src/render/rt64_framebuffer_renderer.cpp`, `updateTextureCache` | Framebuffer copies are explicitly single-level; raw TMEM and replacement flags are separate. | Preserve these exclusions. A raw TMEM fallback or framebuffer copy must not be advertised as a generated mip chain. |
| `extern/rt64/src/shaders/TextureSampler.hlsli` | Mip selection is conditional on the tile's `hasMipmaps` flag. | Use the repaired live bias only when the texture really has valid, eligible levels. Distinguish generated chains from authored replacement chains. |
| `extern/rt64/src/contrib/plume/plume_render_interface_types.h` | Texture views and copies expose mip slices, but the existing barrier abstraction operates at whole-texture level. | Prototype using separate scratch resources and the existing copy/compute interfaces; do not assume simultaneous read/write access to different mips is safe on both APIs. Do not expand the rendering abstraction unless evidence shows it is unavoidable. |

The user's isolated Rice-pack test contained 1,596 PNGs and no DDS textures.
The live bias probe already passed 480 GPU readbacks on DX12/Vulkan, including
manual and native sampling at 1x/16x anisotropy. Those tests establish the
selection mechanism, not the correctness of a future generation mechanism.

## 1. User-facing behaviour

- Add **Generate texture mipmaps (optional)** beside Texture LOD bias in the
  Graphics page, available in the launcher and overlay.
- Default off, including for existing profiles. Retain the current LOD range
  (-2.00 to +2.00) and default zero.
- Recommended first implementation: changing generation takes effect on the
  **next game launch**, not a full application restart. Show that explicitly in
  the overlay. Once generated chains are available, bias changes stay live.
- Track requested versus active generation state so a checked-but-pending box
  cannot misleadingly imply the current game already has generated mipmaps.
- Modern mode enables the optional generated sampling policy. Accurate mode
  retains its prior behaviour; toggling presentation profiles must not leak the
  generated sampling policy into Accurate. Existing authored DDS behaviour is
  separate and remains unchanged.
- Add a diagnostic count of generated, authored and single-level textures, so
  support can establish whether the bias has anything to act on.

## 2. Generation and resource safety

### PNG/Rice replacements

Use the existing asynchronous load/decode path. Keep level 0 byte-identical;
create lower levels from the decoded RGBA image before publishing the finished
texture. Do not rewrite user packs, repurpose their files, or synchronously
convert the entire library on the UI thread. Preserve alpha and colour handling
explicitly rather than depending on backend-specific implicit filtering.

### Original textures

The original decode is GPU-based. Prototype a small compute downsample pass
after that decode, retaining its copy-to-direct semaphore ordering. Use separate
scratch textures where needed so a resource is not simultaneously an SRV and
UAV under an unsupported whole-resource state. Copy the finished levels into
the final mip-chain texture, then expose it for sampling only after completion.

This prototype must pass GPU readback and barrier validation before integration.
If the existing API cannot support it safely, stop and reassess instead of
editing Plume or creating an unreviewed backend rewrite.

### Both paths

- Handle 1x1, 1xN, odd and non-power-of-two sizes; clamp dimensions to at least 1
  and reject overflow/invalid allocations.
- Generate once per new texture content, not once per rendered frame. Palette
  changes and animated texture contents must follow the existing invalidation
  rules so lower levels cannot retain old colours or animation frames.
- Keep temporary buffers, views and descriptor sets alive until GPU completion.
  No new per-frame global GPU-idle waits.
- Include all levels in cache memory accounting and eviction. Square texture
  chains usually add about one-third in texels, but narrow textures and row
  alignment can cost more; measure actual allocations instead of assuming 33%.
- Bound scratch/worker memory and retain the proven single-level fallback if
  generation is unavailable or an optional allocation fails.
- Do not start by adding a new persistent cache or cache-migration system.

## 3. Protect existing visuals

Do not blindly filter every texture draw. A texture can be shared between world
geometry and UI, so filename-based exclusions alone are insufficient.

- Identify existing HUD/menu draw scopes and rectangle/copy draws; keep generated
  chains out of HUD numbers, text, menus and frame/copy effects unless explicitly
  validated. If the existing scope cannot classify a draw reliably, keep its
  current sampling and report the limitation rather than guessing.
- Preserve wrapping and atlas/subtile boundaries. Ordinary downsampling can mix
  adjacent images in an atlas; unsafe tile/atlas cases need a validated method
  or a documented single-level fallback.
- Test alpha-aware RGB filtering against dark/bright fringes. Test alpha coverage
  separately for fences, foliage and other cutouts so they do not disappear at
  distance. Translucent water and shadow artwork must not be processed as binary
  cutouts merely because they have alpha.
- Keep the vehicle/Taj shadow geometry, interpolation identities, depth offsets,
  camera settings and split-screen transforms untouched.
- Do not change source colour-space interpretation globally as part of this
  feature. Establish a matching CPU/GPU generation rule and compare outputs.

These are validation gates, not claims that all edge cases are solved already.

## 4. Keep the patch ownership clear

The completed `0015-configurable-default-mip-lod-bias.patch` remains responsible
for live bias selection. The proposed generation work gets one project-owned,
ordered patch, provisionally `0016-optional-generated-texture-mips.patch`, for
texture creation, generation, metadata and eligibility. Avoid duplicating the
bias setter, adding a second texture cache, or layering two owners for the same
decision. Modify dependency sources only by applying the checked patch pipeline.

Project code changes are restricted to enhancement settings, persistence/UI,
renderer startup policy publication, tests and documentation. Never hand-edit
submodules, RecompiledFuncs or RecompiledPatches. Networking, save routing,
simulation and controller/window lifecycle are out of scope.

## 5. Acceptance and packaging gates

1. Generated levels must match a CPU reference on DX12 and Vulkan: opaque,
   translucent, alpha-cutout, odd-sized, wrapped, atlas and palette-derived
   fixtures; no missing or uninitialised levels.
2. Readback tests must prove visible bias changes on the **newly generated**
   original/PNG paths, not just pre-authored test mips. Test -2/0/+2 and fractional
   values with both sampling paths and anisotropy options.
3. Off/default must retain the old pixels for representative scenes. Preserve
   mipmapped DDS packs unchanged, including their behaviour when generation is
   disabled. Check requested/active state, profile switching and persistence.
4. Visually compare real original textures and the user's PNG pack on angled
   roads/roofs, fences, foliage, water, both vehicle and carpet shadows, HUD
   animations, menus and 1/2/3/4-player views at 4:3, 16:9 and Steam Deck 16:10.
5. Measure cold/warm load times, texture-worker time, generation GPU time,
   frame-time tails and memory/eviction during track changes on Windows and
   Linux/Steam Deck. The launcher must not drop back to slow synchronous work.
6. Repeatedly change the live bias, switch packs, start/stop games and change AA.
   No GPU lifetime/barrier errors, freeze or stale texture descriptors.
7. Run all DKR regression suites and package Windows ZIP plus Linux AppImage.
   Keep the currently accepted playtester build as rollback until user acceptance.

Do not promise improved FPS everywhere or a visible change to every pixel.
Mipmaps target minification/shimmering; the available levels, draw eligibility,
viewing distance and filtering mode determine their visible effect.

## Primary background references

- Khronos: [Generating Mipmaps](https://docs.vulkan.org/tutorial/latest/09_Generating_Mipmaps.html)
  explains level allocation, generation and the ordering/barriers between levels.
- Microsoft: [SampleGrad](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplegrad)
  documents explicit-gradient texture sampling, used by the live-bias path.
