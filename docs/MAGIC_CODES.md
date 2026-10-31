# Magic Codes and Custom Magic Codes

The original game's Magic Codes and imported **Custom Magic Codes** are two
different things. Original codes keep their normal unlock/gameplay rules.
Custom Magic Codes are executable `.nrm`/`.dkrcode` packages with their own
settings and reviewed game-service access.

Import a package through **Mods / Hacks > Custom Magic Codes** or the Mod Store.
Open Details, review and approve the exact code edition, save its settings and
enable it for the next game. Only approve code from creators you trust. A data
import does not automatically approve code. Revoking permission disables it.

## SDK 2.0

API 2 adds typed racer/object handles, supported live edits and batches, events,
collision queries, checkpoint/route tools, debug drawing, bounded documents,
controller-friendly tool panels and opted-in live settings. API 1 remains
supported; installed API 1 mods do not need a forced conversion.

The SDK is not unrestricted native code or arbitrary ROM execution. Lifecycle,
memory, permissions, budgets and online authority remain enforced. Unsupported
operations need dedicated adapters, not guessed writes into engine internals.
The [Modding Bible](../modding/MODDERS-GUIDE.txt) includes the exact headers.

## Supplied examples

- **Custom Trophy Races:** create cups below Custom Tracks, choose each round's
  track/laps/vehicle, and use lineup or shuffled order. Cups, Lineup and Share
  screens support controller navigation and on-screen text entry. Opening
  Details refreshes the track list. Import/export shares cup definitions,
  not other creators' track assets. Missing tracks still need installing.
- **AI & Boss Difficulty:** tune the supported AI/boss settings. Avoid enabling
  conflicting difficulty mods. The live-settings example is a separate tool.
- **Dev Menu:** public SDK 2 inspection, watches, supported edits/teleport,
  scene tools, pause/step, routes/checkpoints, debug overlays and development
  documents. It uses isolated development progress, not your normal Adventure.
- **ModernCamera++:** optional local presentation presets and live camera tuning.
  VR is unfinished and is not a supported release feature.

Online Custom Magic Codes require the v1.0.12 Rollback host mode and the host's
admitted code/settings. Development tools additionally need explicit lobby
consent and host authority. A local setting cannot secretly turn an ordinary
match into a developer session. See [Online](ONLINE_MULTIPLAYER.md).
