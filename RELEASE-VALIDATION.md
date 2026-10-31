# v1.0.12 Release Build record — 26 October 2026

This release promotes the accepted online/profile-header build and corrected
live FOV slider. It keeps the memory/HUD, water, custom-car, offline mod copy,
race-start and spectator fixes. Windows x64 and Linux x86_64/ARM64 are included;
Steam Deck uses the identical x86_64 AppImage. Android, macOS and iOS are skipped.

## Source and checks

The existing Patch Pipeline release recipe includes character-library-portraits
and online-performance above the complete earlier chain. The stable version
comes from VERSION. ARM64 vehicle preparation and its fixtures retain the local
-ffp-contract=off policy so prepared data matches x64. Shared simulation settings,
the launcher design and current online fixes are preserved.

The release work keeps a recoverable source backup and a declared change ledger.
Audits verify the final compositions, compiler bindings, linked guards and peer
identities. Submodules, RecompiledFuncs and RecompiledPatches are not edited.
The pattern guard, pinned-byte checks, tooling and host tests remain mandatory.

Named CPU/data fixtures cover the retained allocator/render/water/handoff,
spectator admission, race release, offline retention, transfer and vehicle paths.
The current online menu, setup/cache/logging, directory lifecycle, portraits and
profile checks are included too. Negative controls must fail as expected. Native
Linux vehicle checks include ASan/UBSan. ARM64 fixtures use QEMU.

Archives are read back against their full file receipts. Linux ELF dependency
and ABI inspection, packaged worker capability probes and isolated Windows
updater checks are recorded with the exact final artifact hashes. The handoff's
Evidence and VALIDATION.txt contain the actual results; this source document
describes the required coverage rather than inventing future test results.

## Gameplay and publication

Automated checks do not run a game, user ROM, RSP/GPU workload, live Discord
flow or multiplayer match. Physical Steam Deck/ARM64, controller, audio and
network acceptance remain separate. The maintainer has confirmed the corrected
live FOV behaviour; that does not establish every other device or network.

Before rollout, check modded joins with a fresh cache and a reused cache,
character/vehicle selection, race finish and track changes, spectators, immediate
lobby closure and profile name/picture changes. Check remembered sign-in in both
Steam Deck modes and a v1.0.11 update preserving saves, mods and settings.

The signed DKR-R-update.json and DKR-R-update.sig must name Version1.0.12 and
the exact committed release source. Steam Deck uses linux-x86_64. The maintainer
uploads and publishes the stable GitHub release. Do not alter signed packages.

The [v1.0.11 validation record](docs/history/RELEASE-VALIDATION-1.0.11.md) is preserved.
