# DKR-R architecture

DKR-R is a static recompilation, with the matching DKR decomp as the readable
reference for symbols, structures and algorithms. We keep enhancements in the
authored integration layer and Patch Pipeline rather than hand-editing the
generated game or dependencies.

## Runtime and patch ownership

The ordinary execution path combines the user's validated ROM, matching ELF,
N64Recomp CPU output, statically recompiled Rare audio RSP output,
N64ModernRuntime services and the project
[F3DDKR graphics HLE decoder](F3DDKR.md), which submits RT64 workloads.
Graphics microcode is handled by that decoder, not by an RSP translation unit.
One SDL2 frontend owns launcher/game interaction.
Supported desktop builds use a separate headless SDL3 input worker; Android
uses in-process SDL2. SDL3 never replaces the frontend's window ABI.

Protected inputs include `RecompiledFuncs`, `RecompiledPatches`,
RT64, N64Recomp and N64ModernRuntime. Instruction/function changes belong in
the versioned policies and composed fragments; dependency changes belong in
checksum-pinned patches. Both ordinary and owned payloads are generated into
new directories. See [Building](BUILDING.md) and [Development](DEVELOPMENT.md).

The project layer owns ROM selection, controller routing/profiles/gyro,
save leases and atomic persistence, settings UI, presentation/HUD identities,
texture priorities, enhanced-water policy, custom asset namespaces and netplay.

## Revision selection

Canonical ROM hashes choose US v1.0/v77 or Rev A/v1.1/v80 before game
registration and renderer/audio startup. Both CPU payloads are linked into
the application. The older extracted Windows Rev A module/cache architecture
is no longer the current dispatch path. Players do not start a second engine
executable.

Byte-swapped inputs are converted into a verified private cache without
modifying the original file. Both revisions use the compatible EEPROM format,
but online and legacy mod-set save namespaces remain separate from offline
progress. See [ROM setup](ROM_SETUP.md) and [Saves](SAVES.md).

## Three online backends

All concrete backends use the same lobby, Quick Join/friend admission, virtual
port ownership and save/mod compatibility checks. Auto is a host preference
that selects Experimental Rollback, not a new wire backend.

**Host Prediction** retains the original host-authored immutable commit chain.
Prediction is bounded; delayed replicas use portable host state and explicit
recovery/finish barriers. Published commits are not rewritten by late inputs.

**Strict Input Sync** advances with the required authenticated input set.
Its input waits are intentional rather than a hidden rollback implementation.

**Experimental Rollback** uses isolated full-scene owned CPU closures for both
supported revisions. It captures guest state plus audited native sidecars,
predicts remote input within a bounded window and restores/replays on correction.
The launch follows the normal retail menu flow. Scene construction, lifetime
cuts and finish transitions stay behind explicit ownership/confirmation gates.

The owned revision registry keeps capsules and native callback dispatch
separate. Unknown imports, stale object/asset generations, out-of-range guest
addresses and exhausted budgets fail closed. Restoring a checkpoint must not
leave renderer references pointing at retired simulation memory.

Rendering consumes owned snapshots rather than live replay memory.
Confirmed audio and persistence are separate output consumers: replay does not
repeat audible events or persist speculative EEPROM/Pak state.
See [renderer ownership](RENDER_SNAPSHOT_ARCHITECTURE.md).

## Network and mod admission

PeerJS introduces Quick Join peers; authenticated WebRTC channels carry native
session traffic. There is no bundled gameplay relay. Per-peer key agreement,
authenticated encryption, replay rejection and bounded traffic lanes keep
unapproved/stale data out of gameplay.

Admission binds build/protocol, original ROM, simulation policy, architecture/
floating-point mode, gameplay settings/Magic Codes, save proof and the frozen
host mod manifest. Mod schema/behaviour contracts bind exact verified content,
not names or mod-provided claims.

Consented preparation reuses matching offline content or verified caches and
transfers only missing payloads. Each client reconstructs against its own
original ROM. Offline retention publishes inactive verified copies without
replacing personal activation or saves. Session identity and source-path
validation remain mandatory even on a cache hit.

## Presentation and settings

Accurate is the original presentation reference. Modern interpolation runs
between authored frames without changing the simulation/audio clock.
HUD group translation does not stretch artwork or modify race state.

Renderer-only settings remain local. Settings that affect checkpointed
construction/culling/audio cannot simply be applied privately during replay.
Experimental scenery has a local billboard cache; a general independent
actor/mesh builder and category output mixer remain deferred. Modern camera
is gated offline. See [Graphics](GRAPHICS.md) and [Sound](SOUND.md).

## Assets, saves and release safety

Legacy imports create isolated banks and reviewed native behaviour adapters,
not a mutable patched cartridge or unrestricted guest-code interpreter.
Track Lab uses the additive `.dkrmap` format with confined payload paths,
optional music and matching HD packs. Custom identity owns the relevant
model/portrait/voice resources; retail donors stay unchanged.

Every launch holds one immutable save route until all save producers retire.
Online faults cannot fall through into offline storage. Scoped backups,
validation and recoverable transactions protect save and Track Lab edits.

No ROM, extracted Nintendo/Rare assets, user save, local profile or secret
belongs in the repository or release. See [asset policy](ASSET_POLICY.md),
[mod formats](CUSTOM_TRACKS.md) and [release validation](../RELEASE-VALIDATION.md).
