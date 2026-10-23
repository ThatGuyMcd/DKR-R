# Building DKR-R v1.0.7

The stable version is `1.0.7`. All delivered platforms must use the same
accepted source/feature ledger. Start with the
[release pipeline](../patches/release-prep/v1.0.7/BUILDING.txt) for the final
composition and explicit prepared-input contract. Do not substitute an older
package or rename a beta executable.

For contributor checks that need no ROM or game build, start with
`python3 scripts/check.py` and the [host presets](DEVELOPMENT.md#first-contribution-tests-without-a-rom).
The instructions below cover runtime and package builds.

Build from the pinned sources and authored Patch Pipeline. Don't patch RT64,
N64Recomp, N64ModernRuntime or generated functions by hand to make a build pass.

Windows and Linux releases go together, and Linux must include an AppImage.
The [platform guide](PLATFORMS.md) separates public packages from preview ports.

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

`Build-DKR-Runtime.cmd` invokes `scripts/Prepare-DKR-Runtime.ps1`.
It prepares the decomp ELF, pinned dependencies and generated/probe inputs.
It is not, by itself, the complete mod-enabled/owned-rollback Public Build.
`Diagnose-DKR-Recompile.cmd` is a diagnostic regeneration entry point; it
does not replace composing both production policies.

Use fresh build/output directories. Keep ROMs, generated output and local
profiles out of source control.

## Compose both normal payloads

The versioned policies are only part of the policy. Production legacy scene,
track/character menu, presentation and behaviour fragments must be composed
before N64Recomp runs:

```text
python scripts/generate_legacy_menu_qualification.py --characters --character-menu
    --v77-build <prepared-v77-ELF-and-ROM-folder>
    --v80-build <prepared-v80-ELF-and-ROM-folder>
    --recompiler <N64Recomp-executable> --output <fresh-pipeline-output>
```

The displayed arguments are one command; join the lines using your shell's
continuation syntax. Paths in angle brackets are placeholders.
Use the resulting `generated-v77` and `generated-v80` directories separately.
CMake checks required hooks and rejects v80 pointed at the protected v77 output.

Revision translation uses named functions and checked offsets, not a global
address delta. If translating the base v80 policy, retain and validate its
revision-specific entries; don't overwrite them with a blind translation.

## Generate the owned rollback payloads

The complete Windows/Linux/Android Public Build also needs two isolated full-scene owned
outputs. A normal payload or the old single-track probe is not sufficient.

Run `scripts/generate_replay_probe.py --help` for the input contract.
It reads composed generated CPU inputs and the reviewed `recomp.h`, then emits
new isolated output; it does not edit the input tree. For each revision use:

```text
python scripts/generate_replay_probe.py <normal-generated-directory>
    <pinned-recomp.h> <fresh-owned-output-directory> --revision <77-or-80>
    --scene-cuts --audio-services --input-services --authored-cpu
    --isolate-symbols --full-scenes --mod-services --inline-memory
```

Despite the historical “probe/test” names, these checked full-scene outputs
supply the integrated backend. Native callback signatures, header hashes,
service ownership and the manifest are mandatory. Don't loosen a failed check
or edit generated output to admit a new closure.

## Configure a complete desktop build

Beta 5's local Online maximum-detail renderer also needs two fresh model-only
outputs. These are separate from both the normal and owned CPU directories:

```text
python scripts/generate_visual_model_kernel.py --generated <normal-v77>
    --header <pinned-recomp.h> --elf <v77.elf> --revision 77
    --output <fresh-model-v77>
```

Repeat for revision 80 using its own ELF and normal input. The script checks
the five leaf routine hashes, scalar symbols and memory ABI. It emits only
the model closure, with checked reads, shadowed guest stores and an operation
budget; there is no game-world redraw or gameplay/native-import escape.
Supply both outputs using `DKR_VISUAL_MODEL_GENERATED_V77` and
`DKR_VISUAL_MODEL_GENERATED_V80`. Without them the local model feature is not
compiled and the live Online maximum-detail control remains unavailable.

Example Windows configuration, with placeholder paths replaced:

```powershell
cmake -S runtime-recomp -B build/public-windows -G "Visual Studio 17 2022" -A x64 `
  '-DDKR_RELEASE_VERSION=1.0.7' `
  -DDKR_RUNTIME_BUILD_RT64=ON -DDKR_RUNTIME_BUILD_SDL3_INPUT_HOST=ON `
  '-DDKR_GENERATED_SOURCE_V77=<normal-v77>' '-DDKR_GENERATED_SOURCE_V80=<normal-v80>' `
  '-DDKR_GENERATED_RSP_SOURCE=<prepared-RSP-output>' `
  -DDKR_EXPERIMENTAL_RACE_TEST=ON `
  '-DDKR_EXPERIMENTAL_OWNED_GENERATED=<owned-v77>' `
  '-DDKR_EXPERIMENTAL_OWNED_GENERATED_V80=<owned-v80>' `
  '-DDKR_VISUAL_MODEL_GENERATED_V77=<model-v77>' `
  '-DDKR_VISUAL_MODEL_GENERATED_V80=<model-v80>' `
  -DDKR_LEGACY_QUALIFICATION=OFF -DDKR_REPLAY_QUALIFICATION=OFF `
  -DDKR_EXPERIMENTAL_RENDER_QUALIFICATION=OFF
cmake --build build/public-windows --config Release --parallel
ctest --test-dir build/public-windows -C Release --output-on-failure -R '^DKR'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Package-Windows.ps1 -BuildDirectory build/public-windows
```

Quote whole `-D...` arguments when paths contain spaces. PowerShell arguments
containing dotted version values must also be quoted.

On Linux use Ninja and a fresh build directory, with the same reviewed payloads:

```bash
cmake -S runtime-recomp -B build/public-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release '-DDKR_RELEASE_VERSION=1.0.7' \
  -DDKR_RUNTIME_BUILD_RT64=ON -DDKR_RUNTIME_BUILD_SDL3_INPUT_HOST=ON \
  '-DDKR_GENERATED_SOURCE_V77=<normal-v77>' \
  '-DDKR_GENERATED_SOURCE_V80=<normal-v80>' \
  '-DDKR_GENERATED_RSP_SOURCE=<prepared-RSP-output>' \
  -DDKR_EXPERIMENTAL_RACE_TEST=ON \
  '-DDKR_EXPERIMENTAL_OWNED_GENERATED=<owned-v77>' \
  '-DDKR_EXPERIMENTAL_OWNED_GENERATED_V80=<owned-v80>' \
  '-DDKR_VISUAL_MODEL_GENERATED_V77=<model-v77>' \
  '-DDKR_VISUAL_MODEL_GENERATED_V80=<model-v80>' \
  -DDKR_LEGACY_QUALIFICATION=OFF -DDKR_REPLAY_QUALIFICATION=OFF \
  -DDKR_EXPERIMENTAL_RENDER_QUALIFICATION=OFF
cmake --build build/public-linux --parallel
ctest --test-dir build/public-linux --output-on-failure -R '^DKR'
DKR_LINUX_BUILD_DIR="$PWD/build/public-linux" ./scripts/Package-Linux-AppImage.sh
```

`VERSION` supplies the package version. Default outputs are
`dist/DKR-R-1.0.7-Windows-x64.zip` and
`dist/DKR-R-1.0.7-Linux-x86_64.AppImage`.
Keep prior outputs; the AppImage packager expects a fresh output/AppDir.
Runtime packaging checks are on by default. If the release maintainer is doing
all execution and acceptance on their own devices, package Linux without
launching DKR-R by explicitly setting both `DKR_SKIP_RUNTIME_TESTS=1` and
`DKR_DEFER_RUNTIME_TESTS_TO_USER=1`. This only defers execution; it does not
declare the package tested or turn off architecture/content packaging guards.

The Linux release toolchain uses glibc 2.38 or newer. AppImages
bundle application libraries, not the system glibc or GPU driver. Record the
ELF architecture and required symbol versions when using a different build host.

The simpler `Build-Windows.cmd`/`Build-Linux.sh` wrappers consume prepared
normal sources. Supply `-GeneratedSource`/`-Revision80GeneratedSource`
on the Windows script, or `DKR_LINUX_GENERATED_SOURCE`/
`DKR_LINUX_V80_GENERATED_SOURCE` on Linux.
`./Build-Linux.sh` builds and tests; add `--package` only when you want an
AppImage. `./Build-Linux.sh --help` lists the input and output overrides.
They do not configure the extra owned outputs automatically; don't describe
that basic build as reproduction of the full experimental Public Build.

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
The integrated experimental backend is gated out on Apple; use a supported
legacy backend rather than expecting Auto to provide an unavailable engine.

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

## Linux ARM64 and Android

Use the project-owned ARM/Android toolchains and packaging adapters, with
separate generated/build directories. The older [ARM recipes](ARM-PLATFORM-PREVIEW.md)
document the initial cross-build setup; their preview version and test counts
are historical, not current release certification.

Android needs the correct NDK/SDK, ARM64 SDL2 input, shared C++ runtime and
native alignment requirements. Don't reuse a desktop SDL3 worker as an Android
input backend. No APK or cross-compiled AppImage certifies a physical GPU.

For 1.0.7, compose both normal and owned payloads above before using
`runtime-recomp/cmake/AndroidArm64.cmake` or
`runtime-recomp/cmake/LinuxArm64.cmake`. Android builds the `DKRPortGame` and
`DKRLegacyModWorker` targets with the Android NDK. Linux ARM64 builds the
desktop runtime and separate input/import workers against its ARM64 sysroot.
Keep private qualification switches off in the packaged builds.

Package Android with `scripts/Package-Android-Preview.ps1 -ReleaseBuild`,
supplying the native build, SDL2 source, NDK, Gradle and fresh staging/output
directories. Despite the script's historical name, `-ReleaseBuild` produces
the non-debuggable `DKR-R-1.0.7+beta.4-Android-arm64.apk`. Build code 1070010 keeps
the existing application ID and signing identity so it can update the old
app. Verify the signature, ARM64 libraries, 16 KiB ELF/ZIP alignment and
bundled notices before distribution; do not replace the signing key casually.

For ARM64 AppImages, set `DKR_LINUX_ARCH=aarch64` and use matching ARM64
linuxdeploy/AppImage tools with `scripts/Package-Linux-AppImage.sh`. The
cross-host runner is `scripts/Run-Arm64-AppImage.sh`; QEMU self-tests still
do not replace device rendering/audio tests. Steam Deck uses the Linux x86_64
AppImage, not ARM64. Beta 3 also has an extracted portable tarball with the
same x86_64 application/libraries and `START-DKR-R.sh`; it is not another engine.

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
the maintainer. Build only the runtime/helper targets; do not invoke CTest or
the executables. Windows packaging supports `-SkipRuntimeTests
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

## v1.0.8 startup hotfix

Use [the startup hotfix pipeline](../patches/startup-memory/BUILDING.txt).
It composes the same frozen 1.0.7 inputs, adds only the isolated runtime-memory
patch and compiles the actual 1.0.8 version. Android version code is 1080001.
