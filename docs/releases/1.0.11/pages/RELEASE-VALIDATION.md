# v1.0.11 Release Build record — 25 October 2026

This release uses the complete vehicle playtest 1 repair chain, including the
guarded game heap, render stability, scheduler handoff and retained v1.0.10
Online/spectator fixes. Windows x64 and Linux x86_64/ARM64 are included. Steam
Deck uses the exact Linux x86_64 payload. Android, macOS and iOS are skipped.

The maintainer approved preparing this release after reviewing the playtest
handoff. That approval is not a recorded test on every device, mod or network.

## Source and automated checks

The release overlay changes the stable version and packaging, and disables
fused-operation contraction only for ARM64 vehicle preparation and its fixtures
so the prepared data matches x64 exactly. It retains the
native fixes, launcher design and canonical memory-playtest peer identities.
The memory repair changed those identities from v1.0.10; use matching complete
v1.0.11 packages. Online architecture/content compatibility checks remain.

Audits recompose final production units, verify compiler bindings and linked
guards, and traverse the complete retained source chain. Document changes have
before/after hashes. Submodules, RecompiledFuncs and RecompiledPatches are not
edited. Existing authored native layers remain unchanged.

Named synthetic fixtures cover both ROM revisions' actual allocator bodies,
fragmentation, upper guard, owned entry, render/cache policy, scheduler handoff,
spectator admission, race release, offline retention and mod transfer. Negative
controls reproduce the previous failures. Vehicle data tests cover Brute v2,
nine unchanged legacy samples, pixel/geometry preparation and rejected formats.
Native Linux vehicle tests include ASan/UBSan. ARM64 CPU/data checks use QEMU;
they do not establish physical ARM64 GPU, controller or audio compatibility.

Archive read-back verifies every delivered file. Both Linux ELF trees receive
architecture and dependency inspection. Packaged worker capability probes load
no ROM or mod. Exact counts, hashes and build results are in the external
release folder's Evidence directory and VALIDATION.txt.

## Limits of this evidence

The exact previously logged malformed triangle task has not been reproduced
with game assets. The scheduler ownership gap is established and repaired;
malformed-input guards and provenance logging remain. Do not equate that code
repair with proof that every reported rendering incident has been eliminated.

No game, user ROM, RSP workload, GPU workload or live lobby is executed during
release preparation. ROM-free updater/helper checks, where recorded, run in
isolated temporary profiles. Compilation and fixtures are not gameplay results.

## Updates and publication

The exact final packages are signed through DKR-R-update.json and
DKR-R-update.sig using the existing trusted Ed25519 key. The stable tag is
Version1.0.11 and names the committed release source. Steam Deck uses the
linux-x86_64 updater entry. No Android or Apple entry is created.

The maintainer uploads and publishes the release. Signing and pushing source
do not publish a GitHub release. Do not modify signed packages afterwards.

Before public rollout, check HUD digits, track previews, repeated restarts,
water families, Brute textures/seats/wheels, modded joining, fresh-cache
spectators, custom music, race finish and track changes. Check both ROM
revisions and Steam Deck where available. Verify the v1.0.10 update prompt,
install and restart preserve saves, mods and settings.

The [v1.0.10 record](docs/history/RELEASE-VALIDATION-1.0.10.md) is preserved.
