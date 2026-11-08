# v1.0.9 Release Build record — 24 October 2026

The release set contains five downloads from four native builds. Compilation,
linking and static source/binary/package inspection completed. No game,
launcher, mod, native test executable or AppImage was executed by the release
tools. The owner reported that the preceding Windows public-lobby pilot worked
as intended; that does not establish physical-device or multiplayer acceptance
for every newly rebuilt platform.

| Download | Bytes | SHA-256 |
|---|---:|---|
| DKR-R-1.0.9-Windows-x64.zip | 59387124 | `02e6b1071ff447f33ac8cc82ad9ff9d462bd6575f76be1da1a19f6eb12ff903c` |
| DKR-R-1.0.9-Linux-x86_64.AppImage | 68254200 | `b8c4e57451289d4847ac4b32ede90be33cc25293229c693ee4f795ad9dca1913` |
| DKR-R-1.0.9-Linux-aarch64.AppImage | 58915336 | `0ccf0736ab9ad776695b27d1f16e333ea2a84b7e7ab05faa73dafb5416538547` |
| DKR-R-1.0.9-SteamDeck-x86_64.AppImage | 68254200 | `b8c4e57451289d4847ac4b32ede90be33cc25293229c693ee4f795ad9dca1913` |
| DKR-R-1.0.9-Android-arm64.apk | 48167287 | `359a4904827fe5c71d57a45723e8b889b921a30ea6159424fc86aa2b86a77329` |

Steam Deck uses the exact Linux x86_64 payload. No new macOS/iOS package was
built in this pass; the published v1.0.8 Apple packages are separate releases.
No old Apple asset may be relabelled as v1.0.9 or inserted into its manifest.

## Source and native evidence

The [v1.0.9 Patch Pipeline](patches/release-prep/v1.0.9/BUILDING.txt) composes
over the owner-accepted directory, Scoreboard, checkpoint/start/departure,
spectator, vehicle, achievement and texture layers. The four source audits
verified 2,343 existing authored files against a recoverable pre-release
snapshot, with only three declared release metadata changes. Documentation
changes are recorded in a subsequent, separate documentation audit.
Submodule revisions and generated guest/dependency inputs were preserved.

All four native binaries contain the same revision-specific owned identities:

- v77: `e76a0d88cfabc0206896dbecfb66db0f3c0e2453348734270d08528c696c9048`
- v80: `a299a66dad59854bb88fb8b91ac23767758010c5f607ad949ac03685d16f774c`
- Directory contract: `a374fe19a1037bbff9dffb4190657458e42adafdbf0821c9017f41d65fa61f2e`
- Scoreboard contract: `4326fab2f6776953b0d3676daf417c5911b1d2a57d76279c8c484e0037ccf083`

Static ELF inspection covered 99 x86_64 and 67 ARM64 ELF files, with no unresolved
non-system dependencies in their packaged trees. Both require GLIBC 2.38 and
GLIBCXX_3.4.32; the AppImages do not supply the host graphics driver or glibc.

Android is a non-debuggable Release APK, versionName `1.0.9`, versionCode
`1090001`, application ID `io.github.thatguymcd.dkrr.preview`. Its certificate
SHA-256 is `b4e82ec59de3b8cdbe2ad1c7e84e0eba271cde79da64aea1811c81aa3b82e262`,
verified against the earlier distributed APKs. All four native libraries have
at least 16 KiB PT_LOAD alignment. APK/ZIP metadata and signer continuity passed.

Private build receipts, snapshots, symbol files and detailed dependency reports
are outside the public packages. Package checks reject ROMs, personal saves,
profiles, credentials, installed mod libraries and private diagnostics.

## Service and hardware scope

The directory's recorded local Cloudflare runtime suites cover 83 normal,
89 separately gated and 3,092 lease/load assertions, plus 21 existing-library
regression checks. These include synthetic concurrent capacity, account/room
leases, hibernation and preserved library API behaviour. They are local backend
results, not real-world geographic routing or native gameplay tests.

The owner deployed the API and reported the public-lobby pilot working. The
running-spectator service flag remains separately controlled; no assumption is
made that it is enabled in production. FTP upload behaviour is retained.

Physical ROM import, both-revision play, GPU/audio, controller/touch, native
TLS/keyring behaviour, suspend/resume, sustained performance, WAN connectivity,
swaps, checkpoints and updater installation still need acceptance on the
actual target hardware. Follow [platform support](docs/PLATFORMS.md),
[Online](docs/ONLINE_MULTIPLAYER.md) and [Updates](docs/UPDATES.md).

## Publishing and update assets

The manifest must name these exact qualified package bytes and a committed
release source tree. `DKR-R-update.sig` must be a valid Ed25519 signature using
the existing pinned release key. An old signature, public key or checksum is
not a substitute. A separate signing receipt records verification; this build
record does not imply a signature, GitHub publication, tag, FTP upload or API
deployment was performed by the build tools.

The [v1.0.8 build record](docs/history/RELEASE-VALIDATION-1.0.8.md) is retained
verbatim as historical evidence. Earlier stable records remain under
`patches/startup-memory/history` and `patches/release-prep/v1.0.7/history`.
