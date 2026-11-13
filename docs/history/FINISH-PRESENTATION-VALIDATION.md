# v1.0.5 Beta 3 — finish presentation rebuild

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


This is a presentation-only rebuild of Beta 3, not a change to its online
protocol, simulation, input, saves, shadows, mipmaps, or launcher/window lifecycle.
Gameplay protocol remains 47. Native visual acceptance is performed by the user.

## Diagnosis and implementation

### Premature 4:3

Retail `postrace_start` enables a custom full-screen viewport before the wood
contracts. `viewport_menu_set` clamps that viewport's scissor to 319x239 even
though the framebuffer is 320x240. RT64 AUTO tests full-width viewport/scissor
coverage; the clamped scissor can therefore select the fixed-aspect path before
the animation. The former `menu_stage > 0` predicate also did not prove that
assets were ready or a contracted wooden frame had been drawn.

- Removed that independent menu-stage predicate from `widescreen_policy.hpp`
  and its use in `runtime_stubs.cpp`.
- Added a producer-side `PostraceFrameGate`. It observes the actual
  `menu_element_render(4)` call in `postrace_viewport`, after retail's loading
  and visibility gates and viewport updates. A full-size frame does not arm it.
- A contracted draw becomes effective only when its task is submitted, not
  during rollback replay, an abandoned display list, or interpolated display
  frames. The next authored world frame is the second zoom-out frame.
- Before that point, a typed, one-shot `PostraceFullViewport` sidecar normalizes
  the renderer's world viewport and scissor to 320x240. It changes no RDRAM.
  Mirrored-course handedness and viewport depth are preserved.
- After that point, the one-shot correction stops and the existing scoped
  framed-results policy uses the same decision. Retail controls all subsequent
  shrink/skip/exit geometry. There is no second competing timer or menu latch.
- This correction requires Modern/expanded presentation, an in-game post-race
  view, one active player, the single-camera layout, and no trophy-world result.
  Existing 2/3/4-player quadrants and fixed UI are not reprojected by it.
- Gates reset on `postrace_start` and scene reset. Background-only stretching,
  Track Select terminal-scissor normalization, and preview lens-flare
  containment are retained unchanged.

### Finish interpolation

The previous policy explicitly rejected finish camera modes 5 and 7. A hook on
the repeated mode-7 store disabled interpolation, and a later viewport hook
updated a single task-wide permission. One finishing viewport could therefore
disable an entire multiplayer task; even continuous spectator shots stayed at
the authored rate.

- Removed both disabling hooks through the recompilation policy; they are not
  left as competing/no-op patches in the generated program.
- Task permission now follows the presentation profile, with the existing
  corrupt-sidecar/queue fail-closed protections retained.
- Added per-logical-camera finish-shot tracking at root-matrix recording,
  before world drawing. The new retail hook records the selected spectator
  node and racer owner without changing them.
- Entering/leaving a finish mode or changing spectator node/owner advances
  that camera's continuity epoch once. Repeated stores, continuous look-at
  rotation, and the smooth challenge orbit retain their identities.
- The existing object/world identity machinery consumes that camera epoch.
  Close-by spectator cuts are detected by node identity, not an arbitrary
  angle threshold. Unfinished players keep their own independent epochs.
- No changes were made to shadow topology, ground selection, height bias,
  renderer matching internals, online state, or camera simulation math.

## Patch Pipeline anchors

| Purpose | Retail function | US v1.0 (v77) | US v1.1 (v80) |
| --- | --- | --- | --- |
| Observe selected spectator node | `update_camera_finish_race` | `0x80058DC4` | `0x80058E04` |
| Observe actual wooden-frame draw | `postrace_viewport` | `0x800955F0` | `0x80095AF4` |
| Reset result presentation | Existing `postrace_start` hook | `0x80094688` | `0x80094B8C` |

The existing post-race network barrier call remains first and unchanged; the
presentation reset follows it. Existing pre-viewport/world-scope hooks are
reused. Generated v77/v80 functions were regenerated into a new build directory
using `scripts/generate_recomp_config.py` and N64Recomp; no generated functions
or dependency sources were hand-edited.

## Automated evidence

- Both original ELFs and generated programs pass
  `finish_presentation_pipeline_tests.py`: exact instruction opcodes, unique
  placements, wooden image 4, and absence of the obsolete disabling hooks.
- `DKRFinishPresentation` tests finish/asset waits, full-size wood, first and
  second zoom frames, discarded tasks, rollback-only submissions, scene/retry
  resets, and display rates from 30 to 500 FPS. It also exercises 1–4 independent
  finish cameras, repeated updates, multiple matrix roots, nearby cuts,
  owner changes, challenge fallback, and separate cutscene slots.
- `DKRSplitScreenRT64` uses the pinned renderer's real projection processor.
  It verifies the full-to-framed AUTO projection change at five aspect ratios,
  production viewport/scissor correction with mirrored X, depth preservation,
  and all existing 40 split-layout cases.
- The full Windows and Linux project-suite runs, final package self-tests,
  artifact hashes and source audit are recorded in the rebuild's manifest.
  Dependency benchmark/fuzzer targets are not project acceptance tests.
- Build/test logs: `build/finish-presentation-20260908`.

## Rollback and remaining visual acceptance

The preceding Beta 3 packages remain unchanged in
`dist/playtest-v1.0.5-beta.3-20260908`; a hash-checked package/source snapshot is
in `build/beta3-finish-presentation-baseline-20260908`.

No interactive game window was opened for this pass. Automated results do not
prove the visual placement of every wooden-frame animation on hardware. Please
check both ROM revisions at 60/120/180+ FPS and 4:3/16:10/16:9/ultrawide:

1. Tracks race and minigame finishes: wide before zoom; switch hidden by wood
   on the second authored contraction frame; no later re-expansion.
2. Adventure race/boss/reward and chained cutscene transitions, including exits
   that never show a wooden frame.
3. Staggered finishes with 2/3/4 racers: unfinished players remain smooth,
   finished views track smoothly, and genuine spectator cuts snap cleanly.
4. Skip/confirm results immediately, wait in results, retry, and change tracks.
5. Check the already accepted shadows, scenery, split HUD, and Track Select
   preview lens flare. Accurate/fixed-aspect behavior should remain unchanged.

Prior Beta 3 boss/input/WAN acceptance checks also remain applicable. Do not
promote this rebuild to a visually verified rollback point until playtesting.
