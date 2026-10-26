# Fold open items

## For the maintainer

1. **`DKR_EXPERIMENTAL_FULL_SCENES` on the launcher.** The base CMake adds
   it to `runtime_ui.cpp` when the owned payload has full scenes. v1.0.11
   compiled the launcher without it: a release layer relocated
   `runtime_ui.cpp` and did not carry that per-source define over. The three
   `#if defined(DKR_EXPERIMENTAL_FULL_SCENES)` blocks in `runtime_ui.cpp`
   were therefore off in the shipped build, and the fold keeps them off
   (`runtime-recomp/CMakeLists.txt` says so where the define used to be set).
   Was dropping it a bug, or intended? Turning it back on is a behaviour
   change for after the fold.
2. **Launcher copy.** 71 reviewed strings are superseded by v1.0.11; see
   [launcher-copy-review.md](launcher-copy-review.md).

## For B6 (native macOS)

- `netplay/online_directory.cpp` (online-directory layer) holds a
  `std::atomic<std::shared_ptr<const View>>`. libstdc++ supports that C++20
  specialization; Apple's libc++ does not (`std::atomic<T>` requires a
  trivially copyable `T`), so the macOS host build stops there. v1.0.11 was
  Linux and Windows only, so no shipped build hit it. It needs an
  `__APPLE__`-guarded alternative (a mutex-guarded `shared_ptr`, or
  `std::atomic_load`/`std::atomic_store` on a plain one).
- The Mod Store links CURL only off Windows, Android and Apple, as the
  release did; plan B2 lists CURL on Apple for B6.

## Dependency stage patches (B3)

- The 11 dependency transforms are six registered stage patches
  (`patches/rt64/staged/`, `patches/n64-modern-runtime/staged/`), derived by
  `scripts/fold/dependency_stages.py` from the Linux chain output and applied
  after that chain. The gate checks them on Linux. Windows takes the same
  CMake branches for these files, so its chain input should be the same, but
  nobody has configured it. Android stages five of the files further
  (`android-*` stages), and the shipped layers applied on top of whatever the
  chain produced, so the Android `git apply` is unverified until the
  `android` job runs.
- `0001-render-stability` and `0003-present-queue-pacer` are diffs against
  the rollback stages of `rt64_state`, `rt64_workload_queue` and
  `rt64_present_queue`. v1.0.11 only shipped that chain
  (`DKR_EXPERIMENTAL_RACE_TEST=ON`). A configure without those stages
  (`host-full-nogen`, or any RT64 build with the option off) doesn't apply the
  two patches and keeps the earlier renderer behaviour. They add no symbols
  that first-party code calls, so such builds still link.
- The shipped layers kept the replaced source's include directories; the
  stage calls pass `KEEP_INCLUDE_DIRECTORIES` to do the same. The other 54
  calls are unchanged.

## Guest hooks and symbols (B4)

- `runtime-recomp/guest-hooks/hooks.toml` replaces the guest-code rewrites of
  six layers. `scripts/guest_hooks.py` runs at configure time from
  `cmake/GuestHooks.cmake`, like the code-mod dispatch staging. The plan had an
  `add_custom_command`, but at configure time the gate's header scan and
  preprocessing see the hooked files without a build step.
- The Camera++ shadow kernel is first-party (`src/game/camera_shadow_kernel.c`),
  and `Camera.cmake` derives its hook stubs from the prepared kernel, so
  `DKR_CAMERA_SHADOW_KERNEL_V*` is gone. `DKR_CAMERA_KERNEL_V*` is still a
  prepared input from `patches/camera-plus/scene_kernel_pipeline.py`, so moving
  that pipeline out of `patches/` is B8's call.
- `scripts/gen_guest_symbols.py` generates the four pure symbol headers
  (achievement, polish, vehicle, render heap) into `<build>/generated/symbols`.
  `title_text_rt64.hpp` and `credits_sky_rt64.hpp` keep their tables inline,
  among hand-written code; `gen_guest_symbols_tests.py` checks the tables
  against the pinned decomp table.
- The polish header reads `gRareCopyrightString`, which only the decomp symbol
  table has, so a `host-full-nogen` configure needs
  `extern/dkr-decomp/ver/symbols`. That's the sparse "symbols" dependency
  profile the plan describes for the `linux` job.

## `patches/` content the build still reads (before B8)

Everything else under `patches/` that the base CMake reads is a dependency
`*.patch` (fold step B3 registers them).

| Directory | Read by | What it is | Plan |
|---|---|---|---|
| `enhanced-water/native/` | `cmake/EnhancedWater.cmake` (copies the HLSL and headers into RT64's staged tree), `DKREnhancedWaterPolicyTests` include path, `scripts/compose-water-*.py` (write it) | first-party RT64 additions: water and lava shaders, `WaterRayScene.cpp`, policy headers | B3 or B8: either first-party source under `runtime-recomp/` staged into RT64 the same way, or new files inside a registered RT64 patch. Not moved yet: the compose scripts still write into this directory |
| `startup-memory/` | `RuntimeMemory.cmake` was included by `cmake/PerformancePatches.cmake` | one `dkr_stage_patch` call plus `runtime-memory.patch` | call folded into `PerformancePatches.cmake`; the patch is registered in B3, the directory's other files go in B8 |
| `custom-magic-codes/` | `cmake/PerformancePatches.cmake` | `runtime-code-heap.patch` only | B3 registers it |
| `save-layout/` | `scripts/save_layout_pipeline.py` (`guest_save_layout.h`) | guest header for the owned-payload pipeline | B4, with the guest hooks |
| `character-portraits/` | `scripts/character_portrait_pipeline.py` (`*.inc`) | guest inputs for the portrait payload | B4, with the guest hooks |

`camera-plus/` is also still read, by `scripts/camera_plus_pipeline.py` and
the scene-kernel pipeline that produce the Camera++ kernels; B4 replaces
them with the generator.
