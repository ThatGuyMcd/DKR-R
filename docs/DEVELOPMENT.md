# DKR-R development guide

## v1.0.11 release contract

The release steps and prepared-input contract are in [Building](BUILDING.md#release-packages). The SDK 2 Bible/public headers describe supported tools and API 1 compatibility. Gameplay/hardware acceptance is owner-led; compilation and static inspection are not runtime acceptance.

Edit source and documentation in place; nothing is layered over the tree.

Keep the changes focused and reproducible. Project-owned runtime code lives in
`runtime-recomp/src/game`, tests in `runtime-recomp/tests`, release assets in
`assets`/`packaging`, automation in `scripts` and Track Lab in
`tools/blender`.

## First contribution: tests without a ROM

Start with Git and Python 3.11 or newer. From the repository root:

```sh
python3 scripts/check.py
python3 scripts/check.py --list
python3 scripts/check.py --suite tooling
```

The default command runs the pattern guard, synthetic tooling tests, release
metadata checks and the source asset scan. `--suite` repeats and takes comma
lists (`--suite guard,tooling`). It stops at the first failure and never downloads
anything or changes dependency checkouts. `--list` shows the selected commands.
On Windows, use `python` in place of `python3`; POSIX-only tests report skips.
To run the Track Lab Python suites, use `--suite track-lab`. Missing private
fixtures are reported as SKIP, not as successful retail-content tests.

For a single regression, invoke its test file directly, for example:

```sh
python3 runtime-recomp/tests/build_tooling_tests.py PatchPipelineTests
python3 runtime-recomp/tests/host_task_policy_tests.py
```

C++ host tests also need CMake 3.24+, a C++20 compiler and Ninja (or Visual
Studio 2022 on Windows). Prepare the pinned source dependencies once:

```sh
python3 scripts/bootstrap_dependencies.py --only n64-modern-runtime --only rt64 --only gekkonet --only monocypher --only libdatachannel --only mbedtls --only sdl3 --only dkr-decomp
python3 scripts/apply_dependency_patches.py
cd runtime-recomp
cmake --preset host
cmake --build --preset host
ctest --preset host
```

CI uses the Ninja `host` preset on every desktop platform. Windows CI
initializes the x64 Visual Studio developer environment and explicitly uses
MSVC with bounded Ninja parallelism. For local Windows builds, `host-windows`
uses Visual Studio 2022 without requiring a developer shell. CI runs two host
tests at a time, then collects performance reports in a separate step so concurrent tests do not distort those measurements.
JUnit results are retained on successful and failed runs. Workflows run for
pull requests and pushes to `main`, avoiding duplicate branch/PR builds. Use
**Run workflow** for a branch without a pull request.

Pinned dependency sources use shallow checkouts of the exact locked revisions,
including submodules, to keep CI archives small. They are cached by operating
system, architecture, dependency profile,
lockfile, reviewed patches, and preparation scripts. Every cache hit still
passes checkout and patch validation. The cache is saved before configuration
and excludes build outputs, ROMs, and generated retail payloads.

macOS and Linux CI cache C/C++ compiler results with ccache; Windows CI uses
pinned sccache with a local disk cache. GitHub Actions stores these caches separately
for each runner architecture and compiler/SDK version. A new commit can reuse
unchanged compilations; both caches still check source, headers, and compiler
options. Builds
remain fresh CMake configurations, and every test still runs. Cache statistics
and toolchain details are retained with the test-results artifact. The first
run for a toolchain is a cold build. The miniz target embeds MSVC debug
information per object to avoid parallel shared-PDB writes. Its few C files
bypass the compiler cache because upstream also supplies a literal `/Zi` flag;
its pinned dependency sources stay unchanged.

Every desktop job writes a summary directly to the GitHub Actions run page,
including test totals, failed-test output, the five slowest tests, regression
scenario results, checkpoint timing, and available compiler-cache statistics.
Missing or malformed reports are called out explicitly, including when an
earlier build step failed. The summary runs after failures; full logs and
HTML/JSON/JUnit artifacts remain available for deeper inspection.
To render the same summary locally:

```sh
python3 scripts/ci_summary.py --junit build/host/junit.xml \
  --regression build/regression/ci/report.json
```

`host-debug` and `host-windows-debug` provide separate Debug
build directories; Release also retains assertions in project test programs.
The Ninja presets write `compile_commands.json` in their build directory for
clangd and other editors (for example `build/host/compile_commands.json`).

Use `ctest --preset host -R <test-name>` for a focused test, `-L tooling` for
Python/CMake tooling tests, and `-N` to list tests without running them.
`--output-junit junit.xml` writes the report in the preset build directory
(`build/host/junit.xml`, or `build/host-windows/junit.xml` for Visual Studio).
Failure details are in `build/host/Testing/Temporary/LastTest.log`.
These checks do not compile the complete game or validate gameplay; follow
[Building](BUILDING.md) for that path. The separate [iOS compilation check](IOS-CI.md)
compiles production platform adapters and patched SDL2 against the arm64
iPhoneOS SDK without ROMs; it does not link or run the complete app. The
[Android compilation check](ANDROID-CI.md) compiles production platform sources
with the Android NDK; it does not package or run the complete app.

For ASan/UBSan builds and seeded session/network repetitions, follow
[Nightly checks](NIGHTLY-CHECKS.md). The same commands run locally and in the
nightly workflows, with seeds and first-failure evidence retained.

For repeated regression runs and performance comparisons, use the
[regression harness](REGRESSION.md). From the repository root,
`python3 scripts/regression.py --output build/regression/baseline` produces
HTML, JSON and JUnit reports with logs, deterministic fingerprints and native
checkpoint timing. A separate private suite covers actual retail CPU replay.
Host CI retains a one-pass harness report without enforcing cross-host timing
baselines; performance gates belong on a consistent machine and configuration.

For actual-ROM checks, follow the [capture and replay workflow](REGRESSION.md#prepare-actual-rom-fixtures)
once, then keep the local executable, ROM and fixture paths in
`build/retail/replay-paths.json`. Subsequent runs start with a quick preflight:

```sh
python3 scripts/regression.py --suite regression/replay.json \
  --build-dir build/retail/capture \
  --vars-file build/retail/replay-paths.json --check-inputs
python3 scripts/regression.py --suite regression/replay.json \
  --build-dir build/retail/capture \
  --vars-file build/retail/replay-paths.json --repeat 1 --warmup 0 \
  --output build/regression/retail-check
```

Use a fresh output directory each time. Add `--scenario 'v77-owned-race'` for a
focused correction check; omit it for both revisions and all scene routes.
These checks compare actual CPU state and audio samples, without validating
rendered frames or physical audio output.

## Worktrees and incremental builds

First-party sources, policies and tests always come from the configured
worktree. CMake defaults dependencies to that worktree's `extern/`; an explicit
`-DDKR_DEPENDENCIES_DIR=/absolute/path/to/extern` can reuse already-prepared
checkouts read-only. Never prepare or patch a shared dependency tree while
another build is using it. Each worktree needs its own build directory and
any generated game payloads it will regenerate.

Older caches that override `DKRPORT_ROOT` to another checkout must migrate:

```sh
cmake --preset host -U DKRPORT_ROOT -DDKR_DEPENDENCIES_DIR=/absolute/path/to/extern
```

Unchanged staged patches and revision symbol maps retain their timestamps, so
reconfiguring does not force recompilation of identical generated sources.
Changes to either the patch inputs or revision definitions still regenerate
those outputs. Never edit these generated files to repair a build.

## Where a change belongs

| Change | Owner | Verification |
|---|---|---|
| Game-independent rules and state transitions | `runtime-recomp/src/game/*_policy.hpp` and focused tests | ROM-free host preset |
| Runtime, UI and renderer integration | `runtime-recomp/src/game` and platform adapters | Focused tests, full target build, relevant runtime checks |
| Dependency behaviour | `patches/<dependency>` and `patches/manifest.json` | Patch tooling tests and affected target build |
| Guest game code | Revision policies and composed fragments | Regenerate both revisions and run guest/runtime tests |
| Setup and packaging | `scripts`, platform CMake modules, `packaging` | Hermetic tooling tests and a real target package |

Keep platform APIs behind their adapter boundary. Extract shared behaviour
when multiple callers need it; a file split alone does not reduce coupling.
Generated files and dependency checkouts are outputs of their owners above.

## Protected inputs

Never hand-edit submodules/dependency checkouts, `RecompiledFuncs` or
`RecompiledPatches`. Game changes go through versioned policies and composed
fragments; dependency changes go through checksum-pinned patches.
Use fresh generated output directories, not manual repairs to generated C.

Revision policies map named functions and checked offsets. Both normal and
owned outputs must contain the reviewed hooks. A policy change can require
regenerating all four CPU payloads, not simply relinking the executable.
[Building](BUILDING.md) explains the current flow.

## Behaviour and ownership

Accurate is the presentation reference. Modern must not accidentally speed up
simulation/audio or change race outcomes through interpolation.
Explicit reviewed mod behaviour is bound to the custom identity and compatibility
manifest; it must not leak into stock characters/courses.

Replay-safe state belongs to the checkpoint owner. Renderer resources, device
handles, audible output, filesystem changes and external effects need separate
lifetimes/confirmation. Do not add an untracked mutable sidecar to rollback.
Local camera/scenery/audio settings may require a presentation implementation,
not a private mutation of shared simulation.

## Checks before a release

- Add focused policy/ownership tests for changed behaviour.
- Run the complete configured DKR CTest suite, revision dispatch and required
  packaged worker/runtime self-tests.
- Validate dependency patch hashes and release metadata.
- Scan source and staged packages for game assets, saves, secrets and local data.
- Package Windows and Linux AppImage; preserve earlier artifacts.
- Record real hardware, gameplay and multiplayer checks separately from headless
  results. A passing unit suite is not a WAN/GPU certification.

Do not weaken a failing gate just to produce a package.
Respect the owner's testing preferences; when visual/gameplay testing is
user-led, record that outstanding acceptance rather than claiming it was run.

## Documentation belongs with the change

Before handing over a feature or fix:

1. Update the relevant current guide and actual menu names/defaults.
2. Update the changelog and any packaged readme affected.
3. State platform gates, limits and unfinished parts next to the feature.
4. Retain historical investigations with a clear historical/proposal status.
5. Check local links, example arguments and release metadata references.
6. Keep contributor attribution and licence texts intact.

Use direct, practical British English. Explain the user action first, then
technical detail where it helps. Don't describe experimental features as
flawless, “infinite”, or qualified on hardware nobody tested.

The [documentation index](README.md) is the starting point; technical formats
remain the reference for encoders, not casual prose to simplify at the expense
of field accuracy.
