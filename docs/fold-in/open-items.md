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

Also blocking: a real Windows run of `Prepare-DKR-Runtime.ps1` (both ROMs,
WSL) followed by `Build-DKR-R-Windows.ps1`, by ThatGuyMcd or Steve. Windows is
the platform that ships; the scripts only parse-checked here.

Known before they run:

- `android-folded` was red before B6. A local run in the CI image (NDK r29,
  same flags) applied every stage patch the Android chain takes. Then 7
  first-party TUs failed on libc++ gaps v1.0.11 never hit: missing
  `std::atomic<std::shared_ptr>`, `std::to_string` of a 128-bit file-clock
  count, and `curl/curl.h`. B6 fixes them portably (`portability.json`), and
  the same local run now builds.
- Android doesn't configure `DKR_EXPERIMENTAL_RACE_TEST`, so it doesn't take
  `0001-render-stability` and `0003-present-queue-pacer` (they sit on the
  rollback chain). Android releases are discontinued, so that stays open.
- The new jobs install Python 3.12 with the pinned `actions/setup-python`;
  `windows-full-nogen` also checks for 3.11 or newer as a backstop.
- Adding `decomp-symbols` to the source cache key invalidates the existing
  dependency caches once.

## Removed or renamed entrypoints

For the PR-2 description. Each line is what a maintainer ran before and what
replaces it.

| Before | Now |
|---|---|
| `Diagnose-DKR-Recompile.cmd` / `scripts/Diagnose-DKR-Recompile.ps1` | `python scripts/compose_payloads.py payload --revision <rev> ...` reruns N64Recomp for one revision |
| `scripts/Prepare-SaveLayoutPayload.ps1`, `scripts/Prepare-CharacterPortraitPayload.ps1` | `scripts/prepare_payloads.py`, which `Prepare-DKR-Runtime.cmd` and `Prepare-DKR-Runtime-macOS.sh` run last |
| `generate_legacy_menu_qualification.py --characters --character-menu` for release payloads | `Prepare-DKR-Runtime.cmd` (`scripts/compose_payloads.py`); the qualification script stays for menu qualification only |
| `generate_replay_probe.py` / `generate_visual_model_kernel.py` run by hand for the owned engine and model kernels | `scripts/prepare_payloads.py` (writes `build/prepared`; CMake reads it by default) |
| `patches/camera-plus/scene_kernel_pipeline.py` | `scripts/camera_scene_kernel.py` (run by `prepare_payloads.py`) |
| `scripts/camera_plus_pipeline.py` | none: Camera++ is source in `runtime-recomp/src/game` and `cmake/Camera.cmake` |
| `scripts/compose-water-*.py`, `compose-lava*.py`, `compose-waterfall.py`, `adapt-water-reference.py`, `normalize-water-adapter.py` | edit `patches/enhanced-water/*.patch` (registered RT64 patches) and `runtime-recomp/src/water/` directly |
| `scripts/Refresh-Water-Shaders.cmake` | none: the configure restages and rebuilds the water shaders |
| `patches/enhanced-water/native/*`, `LICENSE-WAVE-RACE.txt` | `runtime-recomp/src/water/` |
| `patches/<layer>/pipeline.py`, `configure.py`, `audit.py`, `package*.py`, `package-*.sh/.ps1`, `ReleaseOverlay.cmake` | none: the fold is the source; build and package the tree normally |
| `patches/library-workflow/pipeline.py` (+ `transforms.py`) | `services/library-workflow/build.py stage` (base defaults to `services/library-workflow/baseline/`) |
| `patches/online-directory/backend_pipeline.py` | `services/online-directory/build.py --api-worker <worker>` |
| `patches/online-directory/delivery.py` | none: it finalized the 1.0.9 public-lobbies playtest delivery only |
| `patches/mod-store/pipeline.py` (backend), `frontend_pipeline.py` | `services/mod-store/build.py --worker <worker> [--frontend <portal>]`; the native half is in `runtime-recomp/src/game` |
| `-GeneratedSource` required by `Build-Windows.cmd` | optional; defaults to `build\prepared` |

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

- Five TUs held a `std::atomic<std::shared_ptr<const T>>`, which libc++
  (Apple, Android) doesn't implement. They use main's
  `dkr::runtime::AtomicSnapshot` (`atomic_snapshot.hpp`): on libstdc++ and
  MSVC it's an alias for that same type, so their Linux objects must be
  identical (`portability.json` `objects_identical`, level 3).
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
  `DKR_CAMERA_SHADOW_KERNEL_V*` is gone. `DKR_CAMERA_KERNEL_V*` is a
  prepared input from `scripts/camera_scene_kernel.py` (B8a).
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

## For Phase C

- `atomic_snapshot.hpp`'s libc++ branch uses the free `std::atomic_load`/
  `std::atomic_store` on `shared_ptr`, deprecated in C++20 and removed in
  C++26. Replace it with a mutex-guarded holder (a `shared_ptr` under a
  `std::mutex`, releasing the previous value outside the lock), and drop the
  alias once every target toolchain has libc++'s `atomic<shared_ptr>`.

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

## `patches/` after B8

`patches/` holds `manifest.json` and the registered dependency `*.patch` files
only (release-prep aside, see below). The enhanced-water RT64 additions are
first-party source in `runtime-recomp/src/water/`, staged into RT64 by
`cmake/EnhancedWater.cmake`; the service backends are in `services/`.

## Prepared inputs (B8a)

`scripts/prepare_payloads.py` replaces the release chain's prepared inputs:
the staged payload (save layout, then the in-world portrait), the owned
engine regenerated after each stage, and the Camera++ and model kernels.
Their authored guest inputs moved from `patches/save-layout/` and
`patches/character-portraits/` to `runtime-recomp/guest/`. The gate's
`prepared` level checks them against the reference
(`scripts/fold/prepared.py` names the allowed differences).

- One prepare path per platform: `Prepare-DKR-Runtime.ps1` and
  `Prepare-DKR-Runtime-macOS.sh` both check the ROMs and compose the payloads
  through `scripts/compose_payloads.py`, then run `prepare_payloads.py`; the
  Windows build defaults to `build\prepared`. **The PS1 is unverified**: no
  Windows host here, and no CI job runs it (it needs ROMs and WSL). It parses
  under pwsh 7.4; `windows-full-nogen` covers the CMake side only. The macOS
  script's payloads came out byte-identical to the previous ones.
- The save-layout and portrait stages still rewrite copies of
  `funcs_*.c`, which AGENTS.md reserves for `guest-hooks/hooks.toml`. They
  stay stages because the owned engine is generated from their output and
  has to match the shipped one byte for byte. Moving them into hooks.toml
  (payloads `funcs` and `owned`) changes the owned manifest, so it's a
  separate, deliberate step after the fold.
- The portrait stage's native half edited seven first-party mods sources in
  a staged copy. B1 folded those edits into `runtime-recomp/src`, so the
  receipt records the folded files' digests instead of a native stage.
- `generate_replay_probe.py`'s native audits pin the folded sources. The old
  pins were v1.0.11's base sources, before the release layers changed them;
  the folded tree holds what shipped.
