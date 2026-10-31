# DKR-R architecture

## v1.0.12 release contract

The folded source includes guarded game-heap headroom, immutable scheduler
handoff, private decode isolation, water residency, vehicle preparation,
Online directory, Scoreboard, party/spectator repairs, achievements, texture
tools, Camera++, SDK 2, Store, shared saves and presentation fixes. API 2 is
additive. Personal, confirmed-online and development save ownership are
separate. Dependency fixes are checksum-pinned patches registered in
`patches/manifest.json`.

DKR-R is a static recompilation, with the matching DKR decomp as the readable
reference for symbols, structures and algorithms. Enhancements live in first-party
source under `runtime-recomp/`. The fold replaced the first-party patch layers
with the source they compiled; [the migration map](fold-in/MIGRATION.md)
identifies the current owners. Edit those files directly.

The final release adds the accepted online-performance and character/profile
integration layers. Profile and live-FOV logic lives in first-party modules
under `runtime-recomp/src/game`. Account/profile APIs handle identity and picture
sharing; gameplay remains on the admitted peer transport and Rollback path.
The full source fold is still separate from the guardrail setup in PRs #76–#78.

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
RT64, N64Recomp and N64ModernRuntime. Generated guest hooks are declared in
`runtime-recomp/guest-hooks/hooks.toml`; symbol tables live in
`runtime-recomp/symbols/`. The two retained owned-engine input stages are
documented in `AGENTS.md` and [the open items](fold-in/open-items.md#prepared-inputs-b8a).
Dependency changes belong in checksum-pinned patches. Prepared ordinary and
owned payloads are generated into separate directories. See [Building](BUILDING.md) and [Development](DEVELOPMENT.md).

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
but online and development save owners remain separate from personal
progress. Old mod-set saves are recovery inputs, not current per-mod-list ownership. See [ROM setup](ROM_SETUP.md) and [Saves](SAVES.md).

## Public Rollback and party ownership

v1.0.12 public hosting admits the owned Rollback engine (the existing internal
`ExperimentalRollback` value). The old Auto/Host Prediction/Strict Input Sync
selector is removed. Historical implementations/wire values are not renamed
or offered as an automatic fallback.

Isolated full-scene owned CPU closures for both ROM revisions capture guest
state plus audited native sidecars, predict bounded remote input and restore/
replay on correction. Scene construction, start/return, lifetime cuts and
finish transitions retain explicit ownership/confirmation gates. The directory
never advances gameplay or replaces checkpoint/save admission.

Racer and spectator identities are distinct from four controller ports.
Spectators consume confirmed viewing checkpoints; approved racer swaps use an
agreed handover boundary. Required racer departure safely returns the party;
spectator departure does not remove a racing input owner.

The owned revision registry keeps capsules and native callback dispatch
separate. Unknown imports, stale object/asset generations, out-of-range guest
addresses and exhausted budgets fail closed. Restoring a checkpoint must not
leave renderer references pointing at retired simulation memory.

Rendering consumes owned snapshots rather than live replay memory.
Confirmed audio and persistence are separate output consumers: replay does not
repeat audible events or persist speculative EEPROM/Pak state.
See [renderer ownership](RENDER_SNAPSHOT_ARCHITECTURE.md).

## Network and mod admission

The authenticated website API supplies a private worldwide SQLite Durable
Object directory. It leases/deduplicates account presence, publishes public
room summaries and serializes bounded admissions/reservations. Account/role/
epoch/policy/handshake-key binding prevents public browsing from granting
native membership. Reconnection can expire unused capabilities while preserving
already approved content preparation. Existing native peers survive directory
outages independently. See [public lobbies](PUBLIC_LOBBIES.md).

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
