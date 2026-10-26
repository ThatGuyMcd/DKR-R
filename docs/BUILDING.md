# Building DKR-R v1.0.11

Use the [v1.0.11 release Patch Pipeline](../patches/release-prep/v1.0.11/BUILDING.txt)
and its `ReleaseOverlay.cmake`. This includes the memory, render, scheduler
handoff and updated vehicle repairs, above the retained playtest 4 fixes
for mod joining, offline copies, race starts and spectator music, plus the Online directory,
Scoreboard, party repairs, spectators, vehicles, achievements and texture tools,
plus the previous SDK/Store/save/presentation layers. Building the base target
alone omits those additions. `VERSION` and the compiled release value are `1.0.11`.

The recipe requires the verified frozen 1.0.7 source capsule and existing normal,
owned, camera, shadow, visual-model and RSP inputs. These are read-only build
inputs, not files to edit or rebaseline to pass a check. A fresh contributor
checkout needs legitimate prepared ROM inputs before it can reproduce them.
Windows/Linux/ARM64 must share the accepted source ledger. Never relabel
an earlier binary as a new release.

The v1.0.11 build set and exact static checks are in
[release validation](../RELEASE-VALIDATION.md). Source build commands for Apple
remain below; no new Apple package was built in this pass.

For contributor checks that need no ROM or game build, start with
`python3 scripts/check.py` and the [host presets](DEVELOPMENT.md#first-contribution-tests-without-a-rom).
The instructions below cover runtime and package builds.

Build from the pinned sources and authored Patch Pipeline. Don't patch RT64,
N64Recomp, N64ModernRuntime or generated functions by hand to make a build pass.

Windows and Linux releases go together, and Linux must include an AppImage.
The [platform guide](PLATFORMS.md) lists package versions, ABI requirements and actual qualification.

## Tools and preparation

Windows needs Visual Studio 2022 Desktop development with C++, Git, Python 3,
PowerShell and WSL2/Ubuntu for the preparation workflow.
Linux needs CMake 3.24+, Ninja, a compatible C++ toolchain, Vulkan/display
development libraries and the AppImage packaging tools.
Use the locked dependency revisions, not arbitrary installed substitutes.

Linux updater checks use libcurl 7.85+ with HTTPS certificate validation; install
`libcurl4-openssl-dev` before configuring. Windows uses system WinHTTP. Android
and Apple builds use their platform HTTPS APIs. The private desktop updater is
built separately from SDL/RT64 and is included by the package scripts.
See [Updates](UPDATES.md) before preparing signed release assets.

Supply your own clean US v1.0/v77 and US Rev A/v1.1/v80 ROMs locally.
A complete release needs both native CPU payloads and the matching Rare RSP
output. Ordinary players only need the ROM they want to play.

## Prepare (Windows and macOS)

One prepare script per platform does the whole job, the same way on both:

```text
Prepare-DKR-Runtime.cmd -RomV77 "<US 1.0 ROM>" -RomV80 "<US Rev A ROM>"
bash scripts/Prepare-DKR-Runtime-macOS.sh --rom-v77 "<US 1.0 ROM>" --rom-v80 "<US Rev A ROM>"
```

It checks both ROMs against their retail SHA-1, builds one decomp ELF per
revision (`build/decomp/v<rev>`; under WSL on Windows), builds N64Recomp, and
runs `scripts/compose_payloads.py` for each revision. That composes the
production policy (the versioned base policy plus the legacy mod, track menu,
character and character menu fragments, with the scene runtime) and runs
N64Recomp into `build/generated/v<rev>`. Last, `scripts/prepare_payloads.py`
derives everything else the Public Build compiles into `build/prepared`: each
payload with its save-layout and in-world portrait stages, the isolated
full-scene owned rollback engines, the Camera++ scene kernels and the local
model kernels. `Build-Windows.cmd` and `Build-macOS.sh` build from there, and a
plain configure finds the whole set by default
(`runtime-recomp/cmake/PreparedInputs.cmake`). A configure that names its own
`DKR_GENERATED_SOURCE_V77` gets none of it, since the owned engines and kernels
belong to the payload they were prepared from.

Each stage checks pinned function hashes, native callback signatures, header
hashes and service ownership, and writes new output only; it never edits its
input. Despite the historical "probe/test" names, the owned outputs supply the
integrated backend. Don't loosen a failed check or edit generated output to
admit a new closure. Revision translation uses named functions and checked
offsets, not a global address delta.

`scripts/generate_legacy_menu_qualification.py` builds menu qualification
payloads; it isn't part of a release build. To rerun only N64Recomp for one
revision, run `scripts/compose_payloads.py payload` (`--help` lists its
inputs). Keep ROMs, generated output and
local profiles out of source control.

For an already prepared platform cache, configure through the current overlay:

```text
python -B patches/release-prep/v1.0.11/release.py configure --build <platform-build> --capsule <verified-capsule> --log <fresh-evidence>/configure.log
cmake --build <platform-build> --config RelWithDebInfo --target DKRPortGame --parallel
```

The displayed configuration reuses the cache's reviewed compiler/toolchain,
normal/owned/RSP inputs and platform dependencies. It is not a universal
fresh-machine bootstrap command. Select the correct Windows/Linux/ARM64
cache, never reuse compiled objects across architectures, and retain both visual
model inputs. Older ARM caches can supply them with
`--define DKR_VISUAL_MODEL_GENERATED_V77:PATH=<model-v77>` and its V80 equivalent.
Use the actual platform's build configuration (Ninja uses its configured build
type); build the required desktop helpers before packaging.

Use `patches/release-prep/v1.0.11/audit.py` and `package.py` for the release source
graph and allowlisted package composition. Their `--help` describes explicit
paths. They retain earlier contract audits and read-only frozen inputs.
Keep evidence and new outputs outside the repository. Linux packaging must
produce both AppImages; Steam Deck copies the exact x86_64 payload.

Release gameplay acceptance is owner-led. Do not launch a game, user ROM,
RSP or AppImage during package preparation. The explicitly named synthetic
regression fixtures and ROM-free updater/worker checks are recorded separately
from gameplay and acceptance on physical hardware.
The general Linux packager's no-execution settings are both
`DKR_SKIP_RUNTIME_TESTS=1` and `DKR_DEFER_RUNTIME_TESTS_TO_USER=1`; Windows uses
`-SkipRuntimeTests -DeferRuntimeTestsToUser`. They defer execution without
disabling package ownership, content or architecture checks.

The Linux release toolchain uses glibc 2.38 or newer. AppImages
bundle application libraries, not the system glibc or GPU driver. Record the
ELF architecture and required symbol versions when using a different build host.

`Build-Windows.cmd` builds from `build\prepared` by default;
`-GeneratedSource`/`-Revision80GeneratedSource` select other payloads (which
then build without the prepared owned engines and kernels). `Build-Linux.sh`
takes `DKR_LINUX_GENERATED_SOURCE`/`DKR_LINUX_V80_GENERATED_SOURCE`.
`./Build-Linux.sh` builds and tests; add `--package` only when you want an
AppImage. `./Build-Linux.sh --help` lists the input and output overrides.
`Build-Linux.sh` doesn't configure the extra owned outputs automatically;
don't describe that basic build as reproduction of the full Public Build.

## macOS

Run `./Setup-macOS.sh` to install and check prerequisites, or
`./Setup-macOS.sh --skip-deps` to check without installing. Setup and Build's
preparation share the same checks: full Xcode with Metal, GNU Make 4+ (`gmake`),
CMake, Ninja, Python, SDL2, PCRE2, and a `sha1sum` supporting `-c --quiet`
(Homebrew `coreutils`). The verifier is checked before ROM extraction.

Build and either preparation script hold a checkout lock for their entire run.
A second invocation in the same checkout fails immediately; nested preparation
inherits the lock. Locks release automatically when processes exit, including
failed builds. Separate checkouts must also use separate `extern/`, generated
payloads, runtime tools, and build output directories; do not point overrides at
another active checkout's resources for preparation. CMake-only builds can use
already-prepared dependencies read-only via `DKR_DEPENDENCIES_DIR`, as described
in the [worktree guide](DEVELOPMENT.md#worktrees-and-incremental-builds).

macOS packages get increasing `CFBundleVersion` values, including repeated
packages within one minute. `CFBundleShortVersionString` remains major.minor.patch.
Set `DKR_MAC_BUNDLE_VERSION=N[.N[.N]]` for an explicit release/CI build number
(up to 4/2/2 digits); keep explicit values increasing for the same bundle ID.
Packaging checks the version metadata copied beside the executable after linking,
so a new package label cannot silently rename a build of a different version.

With Xcode setup and Homebrew installed:

```bash
./Build-macOS.sh --rom-v77 "/path/to/US 1.0.z64" --rom-v80 "/path/to/US Rev A.z64"
./Run-macOS.sh
```

The script prepares pinned checkouts and both normal payloads. Add `--package`
for an ad-hoc-signed app ZIP, not a notarised public release. Bundled Homebrew
libraries inherit the host's macOS deployment requirements.

For a public release, use `--notarize` instead. It signs with a Developer ID,
notarizes, staples, and writes `dist/DKR-R-<version>-macOS-<arch>-notarized.zip`:

```bash
xcrun notarytool store-credentials dkr-notary   # once
DKR_MAC_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
DKR_MAC_NOTARY_PROFILE=dkr-notary \
  ./Build-macOS.sh --rom-v77 "/path/to/US 1.0.z64" --rom-v80 "/path/to/US Rev A.z64" --notarize
```

Setup recovery checks staged Python/native tools, and preparation uses isolated
revision trees. Do not edit live dependencies or delete protected directories
to recover a stage. See [macOS kit notes](../packaging/MACOS-BUILD-README.md).
The current integrated Rollback backend remains gated out on Apple. The public
Rollback-only v1.0.11 layer does not promise a fallback to a removed backend.
Existing v1.0.8 Apple releases have their own feature/build scope; validate
current Apple integration before producing a v1.0.11 package.

## iOS sideload build

The iOS port builds an arm64 device app for iOS 18 or newer. Use a Mac with
full Xcode, the iPhoneOS SDK and Metal compiler, plus the prerequisites above.
Prepare both original ROM revisions before building the IPA:

```bash
bash scripts/Prepare-DKR-Runtime-macOS.sh --rom-v77 "/path/to/US 1.0.z64" --rom-v80 "/path/to/US Rev A.z64"
bash scripts/build-ios.sh
```

The build uses native macOS shader tools and a separate iOS runtime directory.
`DKR_IOS_V77_GENERATED_SOURCE` and `DKR_IOS_V80_GENERATED_SOURCE` can select
existing prepared payloads; `DKR_IOS_BUILD_DIR` selects the device build directory.
Use separate build directories for devices and simulators. The IPA packager
rejects simulator binaries.

The output is `dist/DKR-R-<version>-iOS-arm64.ipa`. Its ad-hoc signature must
be replaced by your sideloading tool's signing identity before installation.
The app requests no JIT or increased-memory entitlement. Import your ROM with
the in-app document picker or copy it into the app's Documents directory using
Files. Back up that directory before removing the app. Legacy mod import that
requires a separate worker process is unavailable on iOS.

Packaging validates the device binary, release metadata, bundle layout,
signature and bundled asset policy. A successful build does not establish
physical-device gameplay, audio interruption or background/resume behavior;
check those on an iPhone or iPad before distributing a release.

## Linux ARM64

Use the reviewed Linux ARM64 toolchain/sysroot and separate compiled
outputs. Compose both normal and owned payloads above, with matching
visual-model inputs, then configure through the v1.0.11 release overlay.
The older [ARM recipes](ARM-PLATFORM-PREVIEW.md) describe the initial
cross-build setup; their preview versions and results stay historical.

The release package.py uses --arch aarch64, matching ARM64 libraries and
an inspected ARM64 AppImage outer runtime. Linux ARM64 includes the
desktop runtime and its input/import/updater helpers. Steam Deck uses
Linux x86_64. QEMU checks do not replace physical GPU/audio/input tests.

## Android source work

Android releases are no longer supported and there is no v1.0.11 APK.
The new release overlay rejects Android. Historical source recipes and
the v1.0.9 Android adapter remain available for development, with their
original application identity, signing and version-code requirements.
Do not change those numbers to claim a new tested release.

## Custom Magic Codes SDK

The SDK's authored C, headers, examples and address-only v77/v80 reference kits
live in `modding/`. Start with the [Modders Guide](../modding/MODDERS-GUIDE.txt)
and [compact API reference](../modding/SDK.txt). Do not edit the protected
generated CPU trees or dependencies to build a mod.

Normal CPU wrapping is staged by `cmake/CustomMagicCodes.cmake` into new outputs.
Supply the complete `DKR_CODE_POLICY_V77` / `DKR_CODE_POLICY_V80` TOML inputs that
produced the normal payloads; defaults are their sibling `v77.toml` / `v80.toml`.
The private generator accepts `--code-policy-config`. These inputs preserve
port-fix protection; do not substitute an incomplete policy to bypass a refusal.

The metadata-only inspector target is `DKRMagicCodeInspect`. SDK staging uses
`python scripts/stage_magic_code_sdk.py --output <fresh-directory>` and produces
both the plain-text and navigable HTML book. Reference-kit receipts pin exact
bytes; the repository preserves those files without line-ending conversion.

## Release checks

For maintainer-requested builds, runtime/gameplay testing can be reserved for
the maintainer. Build the runtime/helpers and named ROM-free
regression targets. Run only the selected synthetic/updater checks; do not
run a whole game/ROM qualification suite as a packaging side effect. Windows packaging supports `-SkipRuntimeTests
-DeferRuntimeTestsToUser`; Linux packaging uses `DKR_SKIP_RUNTIME_TESTS=1` and
`DKR_DEFER_RUNTIME_TESTS_TO_USER=1`. These explicit overrides do not change the
normal packaging defaults. Keep previews in a fresh output directory and do
not replace accepted release artifacts.

Run metadata and asset checks:

```text
python scripts/validate_release_metadata.py
python scripts/scan_for_game_assets.py
```

Keep Controller Pak, SDL3/import-worker, online import-capability and owned
revision tests in the release process. Package scans must reject ROMs, extracted
assets, saves, logs, local config and signing material.
Ship the whole package and notices, not just the game executable.

Record source identity, configuration, package hashes, test results and
outstanding device/gameplay checks in [release validation](../RELEASE-VALIDATION.md).

## Retained v1.0.8 startup hotfix

The v1.0.8 startup correction (Android code 1080001) is the registered
dependency patch `patches/startup-memory/runtime-memory.patch`. Android
release metadata is not changed by this release.
