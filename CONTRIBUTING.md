# Contributing to DKR-R

## The pattern

First-party code lives in `runtime-recomp/`, `modding/` and `scripts/` and is
edited in place. Third-party code is changed only through `patches/manifest.json`
patch sets against `extern/`. Nothing stages, copies, string-replaces or swaps
first-party sources at configure time. Guest hooks go in
`guest-hooks/hooks.toml` (the save-layout and portrait stages that feed the
owned payload are the only scripts that write guest code), symbol tables in
`symbols/*.toml`, and online
identity lives in `netplay/legacy_identity.hpp` and changes only by deliberate
edit.

The pattern guard (`scripts/pattern_guard.py`), the local hooks and the
required CI checks all enforce this, so a change that breaks it fails before it
can merge.

Through v1.0.11 the release was built from first-party layers under
`patches/`; the fold replaced them with source, and the guard's baseline
(`architecture/pattern-guard-baseline.txt`) is empty. Server-side code is in
`services/`.

## Local setup

You need Git and Python 3.10 or newer. `Setup-macOS.sh`, `Setup-Linux.sh` and
`Setup-Windows.cmd` install the git hooks after the prerequisite checks (pass
`--skip-hooks` to opt out), or run `python3 scripts/bootstrap_hooks.py`
directly. With a global `core.hooksPath`, git won't run the repository's hooks,
and the bootstrap prints how to opt this checkout in. Then:

```sh
python3 scripts/check.py               # pattern guard, tooling, metadata and asset checks
python3 scripts/check.py --suite all   # everything CI runs that needs no ROM
```

On Windows use `python` in place of `python3`. See `docs/DEVELOPMENT.md` for
host builds and tests.

## Lint and format

| Area | Tool | Config |
|---|---|---|
| Python | ruff (`E`, `F`, `W`, `I`) | `pyproject.toml` |
| Python types | pyright, strict on new tooling scripts | `pyproject.toml` |
| C/C++ format | clang-format 18 | `.clang-format` |
| C/C++ lint | clang-tidy 18 | `.clang-tidy` |
| C/C++ warnings | `dkr_enable_warnings(<target>)` | `runtime-recomp/cmake/Warnings.cmake` |
| CMake | gersemi | `.gersemirc` |
| Shell | shfmt, shellcheck | `.editorconfig` |
| Workflows | actionlint, zizmor | `.github/workflows/` |

clang-format, clang-tidy and the sanitizer build all use LLVM 18. Bump them
together or not at all, since two majors format the same file differently.

Existing code is grandfathered, not exempt. `pyproject.toml` carries a
generated per-file baseline of the ruff rules each existing file still trips.
New files get no entry, and the baseline only shrinks. Don't hand-edit it to
make a new finding go away; fix the code. clang-tidy runs on the lines you
changed, so untouched legacy code doesn't block you.

## Adding a feature

1. Put the code in a module directory under `runtime-recomp/src/game/`, as a
   CMake library.
2. Add the include edges it needs to `architecture/deps.toml`. Anything not
   listed is forbidden.
3. Register settings, launcher pages and owned services in their registries
   instead of patching literals.
4. Add tests under `runtime-recomp/tests/`.
5. Fill in the PR template, including the platforms you built.

## Fixing a dependency

1. Edit the checkout under `extern/<dependency>/`.
2. `git diff` the change into a `*.patch` file under `patches/`.
3. Register it in `patches/manifest.json` with its sha256.
4. Run `python3 scripts/apply_dependency_patches.py --check`.

## Changing online behaviour

Anything that changes simulation, the wire format or an owned service changes
online compatibility. Bump the identity in `netplay/legacy_identity.hpp` in the
same PR. Both owners review that file (see `.github/CODEOWNERS`).

## What happened to `patches/<feature>`

Through v1.0.11, first-party features were shipped as patch layers that the
build composed at configure time. The fold wrote the shipped bytes back into
the source tree and deleted the layers; git history keeps them, and the ADRs in
`docs/adr/` record the decisions that were only written down in layer notes.

## Review and merging

`main` and `release/*` are covered by a repository ruleset; the intended
settings are in `.github/ruleset-main.json`:

- Changes go through a pull request. The repository admin can bypass this for a
  hotfix push; the ruleset log records it.
- The required checks are `lint`, `linux`, `macos (macos-15)`,
  `macos (macos-15-intel)`, `windows`, `asset-scan`, `dependency-lock`,
  `tooling` and `workflow-lint`. Skipping local hooks with `--no-verify`
  changes nothing about mergeability.
- No approval is required for ordinary code. Files listed in
  `.github/CODEOWNERS` (the rule files) need a review from the other owner.
- Force-pushes and branch deletion are blocked.

`tidy` is advisory for its first four weeks: it annotates findings on changed
lines and doesn't block merging.

## Release

Releases are built by the `release` workflow from a tag; see
`docs/RELEASING.md`.
