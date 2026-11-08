# v1.0.10 Release Build record — 25 October 2026

The release uses the accepted playtest.4 Patch Pipeline. It includes Windows
x64, Linux x86_64 and Linux ARM64 builds. Steam Deck uses the exact Linux
x86_64 AppImage payload. Android, macOS and iOS are skipped.

The owner reported that playtest.4 worked well after the spectator fix.
That feedback covers the tested setup. It does not establish performance or
compatibility on every device, mod collection or network.

## Source and build checks

The release layer includes spectator-admission-repair and its complete retained
chain: offline mod retention, race starts, lobby stability, Track Select music,
directory, Scoreboard and earlier runtime repairs. The launcher layout remains
the accepted playtest layout. The release changes version metadata and packaging;
it does not introduce new gameplay or checkpoint formats.

The source audit recomposes the final production units, checks actual compiler
bindings and linked feature markers, and runs the retained source/security
checks. Source snapshots and document change receipts account for the release
metadata changes. Submodules, RecompiledFuncs and RecompiledPatches are not edited.

Both ROM revisions retain the accepted playtest.4 peer identities. Online still
enforces build/content/architecture compatibility. Match the complete v1.0.10
packages on all test machines.

## Automated regression checks

Windows and Linux x86_64 run the production-code synthetic spectator admission
and handler fixtures, race-start regression fixtures and offline-retention
fixtures. The old resident-music and race-release implementations are negative
controls: they must still reproduce their original failures. These use synthetic
WAVs, files and owner hooks, without running a game, user ROM or RSP.

Release tooling checks version metadata, updater signing and package ownership.
The final Windows ZIP is checked for CRC and its complete updater file table.
Both Linux architectures receive ELF/dependency/ABI inspection and AppImage
SquashFS checks. Packaged worker capability checks are ROM-free. ARM64 execution
under emulation, where recorded, is not physical ARM64 GPU acceptance.

Exact results, hashes, package sizes and test receipts are in the external
release folder's VALIDATION.txt and Evidence directory. A compile-only result
must not be reported as a passed gameplay test.

## Update files and publication

DKR-R-update.json and DKR-R-update.sig use the existing trusted Ed25519 key.
The stable manifest names the exact Windows/Linux packages and committed
release source, under tag Version1.0.10. Steam Deck uses linux-x86_64. There
are no Android or Apple entries. The private key stays outside source/packages.

Signing and verification happen after the package bytes are final. A release
handoff is not a published GitHub release. The signed-pair verification receipt
and SHA256SUMS.txt accompany the downloads. The maintainer uploads the files
and checks the release before publishing it.

## Final device checks

Before public rollout, check the final packages with a Windows/Linux modded
lobby, a fresh spectator cache, custom music, race finish, track changes,
rejoining and offline copies left enabled. Include Koopa Troopa Beach, a stock
track, both supported ROM revisions and Steam Deck where available. Verify
the v1.0.9 update prompt/install/restart preserves user data. Those final-package
checks remain separate from the accepted playtest and automated fixtures.

The [v1.0.9 record](docs/history/RELEASE-VALIDATION-1.0.9.md) is preserved.
