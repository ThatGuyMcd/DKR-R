# Releasing DKR-R

A release is an annotated, signed tag `Version<VERSION>` on `main` (for
example `Version1.0.11`). The tag is the only release input: the version,
package names, update manifest and notes all follow from `VERSION`. The
builds need your ROM-derived inputs, so they run on your machines; CI runs the
gates that don't (`.github/workflows/release.yml`, on every `Version*` tag).

**Until C5 lands, no release can be finalized from this tree.**
`scripts/release/finalize.py` requires every check in
`scripts/release/required_checks.toml`, and 15 of them (the v1.0.11 release
fixtures and the vehicle audit) are pending until C5 restores them.

## Before tagging

1. On a branch, set `VERSION`, add the `<release>` entry to
   `packaging/linux/dkr-port.appdata.xml`, and write
   `docs/releases/<VERSION>/RELEASE-NOTES.txt` and `UPDATER.txt` (copy the
   previous release's and edit them). Merge it once CI is green.
2. Tag the merge commit and push the tag:

       git tag -s -a Version<VERSION> -m "DKR-R <VERSION>"
       git push origin Version<VERSION>

   The release gates job checks the tag matches `VERSION`, runs the guard,
   tooling, closure and formatting checks, and fails while any required
   check is pending. **Until C5 lands it fails on that last step on every
   tag**, by design: the registry names the 15 pending checks. That is the
   only reason it isn't green.

## Build each platform

On each build machine (Windows x64, Linux x86_64, Linux ARM64; macOS is
built separately, below), from a clean checkout of the tag:

1. Prepare: `Prepare-DKR-Runtime.cmd -RomV77 <US 1.0> -RomV80 <US Rev A>` on
   Windows, `bash scripts/Prepare-DKR-Runtime-macOS.sh --rom-v77 ... --rom-v80 ...`
   elsewhere. It ends with `scripts/prepare_payloads.py`, which writes
   `build/prepared`.
2. Build and test:

       python scripts/release/build.py --platform windows|linux --record <work>/<windows|linux|arm64>

   It checks `build/prepared` against `runtime-recomp/build-inputs.lock.json`,
   configures `release-<platform>` (runtime-recomp/CMakePresets.json), builds
   and runs the tests into `<work>/<platform>/ctest.xml`. The configure
   verifies the inputs again (`DKR_VERIFY_BUILD_INPUTS`).
3. Package and inspect (`--help` lists each script's inputs):

       python scripts/release/package_release.py windows|linux --compiled build/release-<platform>/bin/Release ... --output <work>/packages/<windows|linux|arm64>
       python scripts/release/probe_worker.py --worker <packaged DKR-R-ModWorker> --output <work>/<platform>/packaged-worker-probe
       python scripts/release/inspect_elf.py --tree <AppDir> --arch x86_64|aarch64 --output <work>/<platform>/elf-inspection.json

4. Record the evidence:

       python scripts/release/evidence.py write --record <work>/<platform> --output <work>/<platform>/RELEASE-EVIDENCE.json

   `evidence.py compare A B` checks two builds of the same tag (yours and a
   second machine's) compiled the same sources with the same definitions,
   identity constants and inputs.

## Hand off, sign and seal

    python scripts/release/check_docs.py --since Version<previous> --evidence <work>/evidence
    python scripts/release/finalize.py --work <work> --output <handoff>
    python scripts/release/update_assets.py sign --packages <handoff> --output <handoff> --commit <tag commit> --private-key <key>
    python scripts/release/qualify_updates.py --previous <previous Windows zip> --previous-receipt <its BUILD-RECEIPT.json> -- --case normal ...
    python scripts/release/seal.py --work <work> --output <handoff>

Run `qualify_updates.py` for each case (`normal`, `corrupt-stage`,
`interrupted`, `migrate-layout`) with the updater test arguments, writing
`<work>/windows/updater-<case>.log`. Keep the private key outside the
repository and the release folders. Then follow `<handoff>/UPLOAD.txt`:
draft the GitHub release at the tag, upload, verify the hashes, publish.

## macOS

`./Build-macOS.sh --rom-v77 ... --rom-v80 ... --notarize` builds from
`build/prepared` and writes the notarised package; `build.py --platform
macos` is the same build with the release record. Integrated rollback is off
on Apple (see "Rollback on Apple" in docs/fold-in/open-items.md), and macOS
isn't in the update manifest yet.

## When the pins move

A new N64Recomp, decomp or generator change changes the prepared inputs.
Prepare from the pinned ROMs, check the change is the one you meant, then
relock in the same PR:

    python scripts/release/lock_inputs.py write

## Tools

The lint and format tools are pinned in `.github/lint-tools.txt`; formatting
is `python scripts/format_sources.py --write` (a pinned image, so it gives
the same output everywhere). Compilers: Visual Studio 2022 on Windows, the
distribution GCC (glibc 2.38 or newer) on Linux, Xcode on macOS.
