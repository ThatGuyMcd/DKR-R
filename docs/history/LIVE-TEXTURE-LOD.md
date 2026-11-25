# Live texture LOD bias — v1.0.5-beta.1 candidate

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


## Diagnosis

Two different issues made the slider appear ineffective:

1. The runtime published the selected bias only while constructing the renderer.
   The shader library then captured that value for its lifetime. Moving the
   in-game slider changed the saved preference, but neither rendering path
   received the new value.
2. Bias selects among existing texture mip levels. It does not generate them,
   change model detail, or apply a blur/sharpen post-process. The current normal
   texture and PNG upload paths allocate one level. DDS replacements can carry
   multiple levels. Therefore original textures and single-level PNG packs
   remain visually unchanged even after repairing live updates.

The user's isolated test imported a Rice pack containing 1,596 PNG files and no
DDS files. Its lack of a visible LOD-bias response is consistent with issue 2;
it is not evidence of mipmap support in that pack.

## Narrow implementation

- `runtime-recomp/src/game/runtime_ui.cpp` publishes the effective bias on each
  UI draw, before all visibility early returns. Closing the overlay does not
  prevent subsequent updates. Accurate mode continues to use zero; Modern
  preserves the saved -2.00 to +2.00 preference, defaulting to zero.
- Revised the existing project-owned
  `patches/rt64/0015-configurable-default-mip-lod-bias.patch`, updating its
  manifest hash and applying it through `Apply-Dependency-Patches.ps1`.
  No new overlapping dependency patch was added, and no dependency source was
  hand-edited.
- The raster worker snapshots the atomic bias once per raster pass into the
  existing 32-byte push-constant structure. Its size and member offsets are
  unchanged from the accepted build.
- Immutable sampler bias remains zero. Native sampling multiplies both UV
  gradients by `exp2(bias)` before `SampleGrad`, changing mip selection without
  rebuilding resources. Scaling both gradients equally preserves their ratio.
  The manual sampling path uses the same live bias in its mip-level calculation.
- Both texture stages receive the value. A slider change requires neither a
  renderer restart nor new GPU textures, samplers or pipelines.
- The UI help explicitly explains live application and the mipmap requirement.

No mipmap generation, texture conversion changes, filtering-mode changes,
interpolation changes, networking changes, save changes or window-system changes
are included. A visible effect on original/PNG textures needs separately
approved mipmap work.

## Verification

`runtime-recomp/tests/live_lod_tests.cpp` checks atomic updates after constructing
a shader library, clamping, gradient equivalence and the push-constant ABI.
`live_lod_pipeline_tests.py` checks the live UI/raster wiring, neutral samplers,
both texture stages, both sampling paths and single-patch ownership.

The optional Windows GPU probe compiles the actual patched
`TextureSampler.hlsli`, not a duplicate sampling implementation. It uploads a
seven-level texture with a different solid colour in each level plus a
single-level control, creates resources once per anisotropy configuration, then
changes only the bias parameter for successive dispatches. It reads pixels back
from the GPU and checks them against the expected mip colours and alpha.

The DX12 and Vulkan probes passed 480 sampled-output checks in total:

- Biases -2.00, 0.00, +2.00, -1.25, +0.75 and reset to 0.00.
- Manual sampling and all nine native wrap/mirror/clamp combinations.
- Anisotropic filtering at 1x and 16x.
- Mipmapped response, unchanged single-level output and unchanged alpha.

Probe results are in `build/live-lod-validation-20260907/gpu-dx12.log` and
`gpu-vulkan.log`. The test texture is generated in memory; no game textures or
ROM data are distributed with the probe.

The rebuilt Windows and Linux candidates each passed all 66 DKR-specific CTest
tests (141.75 s and 32.19 s respectively). The original unfiltered CTest runs
also entered bundled Zstandard benchmarks/fuzzers; those unrelated long-running
jobs were stopped, then the complete DKR suites were rerun with `-R ^DKR`.
No Zstandard fuzzer pass is claimed. All 43 dependency patches passed the final
pipeline check. Windows ZIP and Linux AppImage packaging also passed their
runtime pak, input-switch and private SDL3-host checks.

Example Windows probe shader commands (quote the SPIR-V environment option in
PowerShell):

```text
dxc -T cs_6_3 -E CSMain -I extern/rt64/src -I extern/rt64/src/shaders -Fo probe.dxil runtime-recomp/tests/live_lod_probe.hlsl
dxc -T cs_6_3 -E CSMain -spirv "-fspv-target-env=vulkan1.0" -fvk-use-dx-layout -I extern/rt64/src -I extern/rt64/src/shaders -Fo probe.spv runtime-recomp/tests/live_lod_probe.hlsl
DKRLiveLodTests dx12 probe.dxil
DKRLiveLodTests vulkan probe.spv
```

The in-game smoke check exercised the Windows game/overlay. During user testing,
the user reported a pre-existing None-to-4x MSAA freeze and subsequently reported
it resolved. The next isolated run logged successful 0->2, 2->3, 3->1, 1->0,
0->2 and 2->3 AA enum transitions, including None-to-4x. No anti-aliasing patch
was added in this pass; this is not a claim to have diagnosed or eliminated an
intermittent AA issue.

## Rollback

The accepted playtester package remains in
`dist/playtest-v1.0.5-beta.1-20260907` and the verified rollback remains in
`build/rollback-split-screen-working-20260907`. Neither is overwritten by this
candidate. Production changes are limited to the existing LOD patch, its
manifest hash and the runtime UI publication/help text. Additional source edits
only add tests, test registration, documentation and inclusion of the test HLSL
file in source archives.
