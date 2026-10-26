# Repository scripts

## v1.0.11 release contract

The release steps are `scripts/release/` ([Building](../docs/BUILDING.md#release-packages)). Packaging uses explicit runtime/legal allowlists; no offline Player Guide or old playtest handoff notes. Both US revisions, all owned payloads and exact staged inputs are required. Run only the named synthetic fixtures and ROM-free helper/updater checks; gameplay acceptance remains separate.

Run these commands from the repository root with Python 3.11+ (`python` on
Windows). Commands with options describe them with `--help`.

| Task | Command | Inputs |
|---|---|---|
| Fast contributor checks | `python3 scripts/check.py` | Git and Python; no ROM or downloads |
| Build-tool checks | `python3 scripts/check.py --suite host-tools` | Also CMake, Ninja and pre-commit; may download hook environments |
| List checks or choose suites | `python3 scripts/check.py --list` / `--suite guard,tooling` | No C++ build |
| Refuse new patch layers | `python3 scripts/pattern_guard.py --tree` (`--explain all` lists rules) | Git; checks `architecture/pattern-guard-baseline.txt` |
| Prepare pinned dependencies | `python3 scripts/bootstrap_dependencies.py` | Git; network for missing revisions |
| Apply reviewed patches | `python3 scripts/apply_dependency_patches.py` | Prepared dependencies |
| Inspect patch readiness | `python3 scripts/apply_dependency_patches.py --check` | Read-only checkout validation |
| Prepare/verify/sign updates | `python scripts/release/update_assets.py --help` | Finished packages, committed source, original Ed25519 private key for signing only |
| Check release docs | `python scripts/release/check_docs.py --help` | The previous release tag |
| Validate release metadata | `python3 scripts/validate_release_metadata.py` | Repository files |
| Stage the source code-mod SDK | `python3 scripts/stage_magic_code_sdk.py --output <fresh-directory>` | Authored SDK, guide and address-only kits; optional sample NRMs |
| Render the modder's book | `python3 scripts/render_magic_code_guide.py --output <fresh-guide.html>` | `modding/MODDERS-GUIDE.txt` and exact public headers; no game execution |
| Scan source assets | `python3 scripts/scan_for_game_assets.py` | Prunes ignored build/dependency directories |
| Track Lab Python suites | `python3 scripts/check.py --suite track-lab` | Private fixtures optional; absence is SKIP |
| Native host build/tests | [CMake presets](../docs/DEVELOPMENT.md) | C++20, CMake and pinned dependencies |
| Regression and performance reports | `python3 scripts/regression.py --output build/regression/baseline` | Built host tests; see [harness guide](../docs/REGRESSION.md) for private replay and comparisons |
| Sanitized native host tests | `cmake --preset host-sanitizers` in `runtime-recomp` | Linux/macOS; [sanitizer commands](../docs/NIGHTLY-CHECKS.md#asan-and-ubsan) |
| Seeded session/network stress | `python3 scripts/stress_tests.py --build-dir build/host --rounds 10 --parallel 2 --output build/stress/local` | Built host tests, CTest 4.1+; [reproduction guide](../docs/NIGHTLY-CHECKS.md#repeated-randomized-sessionnetwork-tests) |
| Summarize CI results | `python3 scripts/ci_summary.py --junit build/host/junit.xml --regression build/regression/ci/report.json` | Existing JUnit and harness reports; optional `--cache-stats` and `--output` |
| Capture an actual-ROM replay fixture | `python3 scripts/capture_replay_fixture.py --help` | Qualification runtime and canonical US ROM; [capture workflow](../docs/REGRESSION.md#prepare-actual-rom-fixtures) |
| Runtime build | `Build-Windows.cmd`, `./Build-Linux.sh`, `./Build-macOS.sh` | [Platform prerequisites and prepared payloads](../docs/BUILDING.md) |

Linux packaging is opt-in with `./Build-Linux.sh --package`. Windows wrappers
honour `DKRPORT_NO_PAUSE=1`; `Run-Windows.cmd` then waits for the runtime and
returns its exit status. Runtime arguments are forwarded in both modes.

For 1.0.11, rebuild every platform from the same source and compare both owned
engine IDs from the final packages. Publish fresh SHA-256 records only after
packaging. See [Updates](../docs/UPDATES.md) for signed update metadata; a GitHub
release without that metadata is not an automatic installation offer.

Use these names for new scripts:

- Shell: lowercase kebab-case, such as `macos-host-tools.sh`.
- Python: lowercase snake_case, such as `package_macos.py` (also usable as module names).
- PowerShell: retain the existing Verb-Noun command style, such as `Prepare-Game-Source.ps1`.

Older public commands such as `Build-macOS.sh`, `Setup-macOS.sh`, and
`Prepare-DKR-Runtime-macOS.sh` retain their names so existing documentation,
automation, and user commands keep working. Historical names are mixed; they
are not the convention for new shell or Python scripts.

Shared shell helpers live in `scripts/lib/` and use the `dkr_` function prefix.
Sourcing a helper should only define functions: do not change shell options,
working directory, environment, or install packages at source time. Keep helpers
compatible with the Bash 3.2 shipped with macOS and return failures to the caller.
Build entrypoints enable `set -euo pipefail` themselves.

`lib/apple-build.sh` provides shared macOS and Xcode checks, required-tool checks,
and positive-integer parallel job selection. Apple build entrypoints and the
macOS host prerequisite helper share these functions. Job selection prefers an
explicit argument, then `CMAKE_BUILD_PARALLEL_LEVEL`, then the logical CPU count;
if CPU detection fails it uses one job. Platform-specific SDK and packaging
checks remain with the build that needs them.

Run `shfmt -w -i 2` on edited shell scripts.
