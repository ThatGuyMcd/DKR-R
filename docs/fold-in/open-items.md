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

3. **`DKRQuickJoinLifecycle` is intermittent** on the Linux host presets
   (amd64 under emulation on the devops Mac). Alone it fails about half its
   runs, at `quick_join_lifecycle_tests.cpp:126`: after the rejected rekey
   the client never takes the host's Quick Join bootstrap, and the 20 s wait
   expires (passing runs take about 3 s). It predates the fold: built from
   `origin/main` (`1d46835`, no fold) it passed 10 of 20 back-to-back runs,
   and from the fold branch 8 of 20, in the same image. So it's a race in the
   Quick Join path or the test. `DKRExperimentalQuickJoin` aborted once under
   parallel load the same way, and it was the one failure before the fold
   too. Behaviour fixes belong after B10.

## PR-2 merge blockers (non-Linux coverage)

The gate is Linux x86_64 only. These close only when the `windows` and
`android` CI jobs build the folded tree and pass on the PR:

- MSVC compile flags of the shipped TUs (`/bigobj`, `/EHs /EHc-`,
  `/fp:strict`, `/FI` forced includes) and the Windows stage chain.
- The stage patches on the Android chain (five files take `android-*` stages
  first) and the `DKRPortGameCore` object library inside the Android
  `SHARED` `DKRPortGame`.

The jobs that close them are `windows-full-nogen` (`build-windows.yml`: the
`host-full-nogen` preset under MSVC) and `android-folded` (`build-android.yml`:
an NDK r29 arm64 configure and build of `runtime-recomp/` with RT64 on). Both
build RT64 with its stage chain and stage patches and every `DKRPortGameCore`
TU, without payloads. They need the sparse `extern/dkr-decomp/ver/symbols`
checkout (`prepare-dependencies` `decomp-symbols`) and, on Android, the new
`android-full` dependency profile. actionlint and zizmor are clean locally.
**Neither job has run yet**; they first run when PR 2 opens.

Known before they run:

- `android-folded` will fail until the libc++ gaps are fixed. A local run in
  the CI image (NDK r29, same flags) configures cleanly and applies every
  stage patch the Android chain takes. Everything else compiles, but 7
  first-party TUs don't:
  - `std::atomic<std::shared_ptr<T>>`, which libc++ lacks (the B6 Apple gap):
    `online_directory.cpp`, `vehicle_binding.cpp`, `achievements.cpp`,
    `mod_store_install.cpp`;
  - ambiguous `std::to_string` calls: `mod_store_names.cpp`,
    `mod_store_install.cpp`, `texture_dump.cpp` (`texture_gallery.inl`);
  - `curl/curl.h` in `mod_store.cpp` (the store links CURL only off Android,
    Apple and Windows, but still compiles there).
  v1.0.11 never compiled these for Android or Apple, so they fail closed.
  The fixes are platform-guarded source changes that leave the Linux level 2
  stream unchanged, which is B6/C12 work.
- Android doesn't configure `DKR_EXPERIMENTAL_RACE_TEST`, so it doesn't take
  `0001-render-stability` and `0003-present-queue-pacer` (they sit on the
  rollback chain). Android releases are discontinued, so that stays open.
- `windows-full-nogen` assumes the runner's `python` is 3.11 or newer; a
  first step checks it. If `windows-2022` defaults to an older Python, it
  needs a pinned `actions/setup-python`.
- Adding `decomp-symbols` to the source cache key invalidates the existing
  dependency caches once.

## Open questions

- **Android release transforms aren't covered.** The gate and the stage
  patches are checked on the Linux chain only; the Android-only stages and
  the release layers' Android transforms are not reproduced or verified.
  Android releases are discontinued, so this stays open rather than blocking.

## For C5 (tests)

- The layers' remaining check targets and the "Before" negative controls
  move to C5 with the other promoted fixtures; the fold doesn't port them.

## For B6 (native macOS)

- `apply_closure.py` respelled absolute staged includes inside `#if` branches
  no Linux TU takes (Apple, Windows, Android) by path alone. No configure has
  compiled them, so B6 and the Windows configure are their first check.

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
  against the pinned decomp table. Splitting those tables into generated
  `.inc` files is cleanup for after equivalence (it would cost a level 1
  delta now).
- The polish header reads `gRareCopyrightString`, which only the decomp symbol
  table has, so a `host-full-nogen` configure needs
  `extern/dkr-decomp/ver/symbols`. That's the sparse "symbols" dependency
  profile the plan describes for the `linux` job.

## Online identity (B7)

- The owned rollback contracts are frozen in
  `runtime-recomp/src/game/netplay/legacy_identity.hpp`
  (`docs/contracts/ONLINE_IDENTITY.md`). The gate compares identity defines
  exactly, and `online_identity_tests.py` pins the values.
- `cmake/OwnedContentIdentity.cmake` no longer feeds any identity; only
  `owned_content_identity_tests.py` still runs it. B8 deletes both.
- `DKR_LOBBY_STABILITY_CONTRACT` was already a committed literal
  (`src/game/build_contract.hpp`). Moving it into `legacy_identity.hpp`
  would cost a level 1 delta now, so that waits for Phase C.

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
