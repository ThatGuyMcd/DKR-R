# v1.0.14 release preparation — 4 November 2026

This is the local preparation record for the accepted v1.0.14 integration.
The release is unpublished and awaiting the owner's document/release review.
The final handoff records exact package hashes, source identity and test results.
No pass is inferred merely from a prior beta or a successful compile.

## Release scope

The build retains the accepted online/input/pacing, UI parity, mod-library,
seasonal/localisation, analogue driving, feedback, lighting, display/session,
crash-report and startup-handover work since v1.0.12. Windows x64 and Linux
x86_64/ARM64 are the download targets; Steam Deck uses the exact x86_64 AppImage.
Apple and Android status remains as documented in [Platforms](docs/PLATFORMS.md).

Source is reviewed and recoverably backed up. Final source audits bind the
normal and owned payloads, retained compile policies and both ROM revisions.
Submodules, RecompiledFuncs, RecompiledPatches and frozen capsules remain unchanged.

## Verification record

The external handoff's VALIDATION.txt and Evidence directory hold the completed
results, failures/limitations, archive readbacks, ELF reports and helper probes.
Mandatory guard/tooling/metadata/asset and installed hook checks remain required.
Host checks, native Windows/Linux startup and same-PC online checks, ARM64
CPU/data emulation and update qualification are recorded separately.

The stable production builds completed for Windows x64, Linux x86_64 and Linux
ARM64. Archive readback, payload membership, helper execution and both ROM
identities were checked on the finished packages. The Steam Deck download is
byte-identical to the Linux x86_64 AppImage. Both Linux architectures require
glibc 2.38 and GLIBCXX_3.4.32; ELF inspection found no missing non-system
dependencies.

The Windows host suite passed 165/165. Linux passed 164/165 in the mounted
workspace: DKRRegressionHarness exceeded its storage timeout. Its same 25 cases
passed when rerun on native Linux storage. This rerun does not change the
original suite result. Required repository guards, tooling, metadata, assets
and installed format/lint hooks were run separately.

All three architectures passed the retained 14-group fixture checks and seven
online fixture groups. Coverage includes 7,642 vehicle assertions, nine legacy
cases and eight expected rejection controls. The Linux x86_64 vehicle sanitizer
check passed. ARM64 execution uses QEMU with the target sysroot and covers CPU,
data and helper behaviour, not graphics or controller hardware.

Native Windows and Linux package checks exercised both ROM revisions, manual
Play handoff, real custom-vehicle loading, cancellation, automatic launch and
failed-mod fallback. Native captures also exercised the shared initial-sync
presentation. The external handoff records same-PC online results and the
separate companion-mod and signed-updater checks against exact package hashes.

Two inherited fixture failures were repaired before the final packages: the
old-race negative control now receives its own header closure, and the directory
lifecycle test uses the same compiled input layout as its linked core. The
vehicle assertion total was updated to include the accepted lighting tests.
Negative controls remain active. The existing conservative peer ledger hashes
these build policies, so participants must use matching final v1.0.14 packages.

## Physical and external acceptance

Physical Steam Deck LCD (Gaming/Desktop), ARM GPU, DualSense USB/Bluetooth,
audio, suspend/resume, controller feel and real WAN multiplayer require actual
device/network checks. WSLg Mesa llvmpipe is a software renderer and does not
prove a Deck 60 FPS or temperature claim. Same-PC peer tests are not WAN tests.

New language packs remain draft translations. Direct Deck patterns and USB
waveform haptics remain experimental/capability gated. Hardware ray tracing is
experimental. Custom battle courses require valid mode objects and connected
AI paths; the supplied Block Fort investigation was not a complete compatibility
repair. The separate supplied-log crash investigation did not establish its
first corruption point without the matching original dump.

## Source, updates and publication

Current guides and notes are reviewed in [Documentation](docs/DOCUMENTATION-1.0.14.md).
The [v1.0.12 validation record](docs/history/RELEASE-VALIDATION-1.0.12.md) is preserved.
Final signatures must bind Version1.0.14 and the exact reviewed source commit.
No push, upload or publication is performed before the owner's approval.
