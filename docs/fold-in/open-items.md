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
