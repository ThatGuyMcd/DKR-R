# Modding architecture

Status: proposal. The DKR-R `.nrm` validation/API design below is not a shipped
feature. Its recorded baseline is 4 October 2026, not the current 1.0.5 source.
Legacy reviewed behaviour adapters are implemented separately; see
[the current mod guide](LEGACY-MODS-BETA.md) and [architecture](ARCHITECTURE.md).

This document defines the official way DKR-R runs third-party code: the
community-standard N64Recomp `.nrm` format, loaded by the vendored librecomp,
behind a DKR-R validation layer. DKR-R's existing content formats are not
replaced:

| Format | Purpose | Status |
|---|---|---|
| `.nrm` | Code mods (this document) | New |
| `.dkrmap` | Custom tracks from Track Lab (`docs/CUSTOM_TRACKS.md`) | Unchanged |
| `.xdelta` | Legacy ROM-hack import, assets only (`docs/LEGACY-MODS-BETA.md`) | Unchanged |
| Texture packs | `docs/TEXTURE_PACKS.md` | Unchanged |

RT64 `.rtz` texture-pack containers, which other recomps register as a mod
content type, are not adopted: DKR-R already has its own texture pack flow.

## Recorded baseline

Facts recorded from `d971908`, which the proposal builds on. Version numbers,
hook counts and missing safeguards in this section describe that snapshot.

- The game registers with librecomp as `mod_game_id = "dkr"` for **both** US
  revisions (`runtime-recomp/src/game/game_registration.cpp`).
- `recomp::start` always calls `initialize_mods()` and `scan_mods()`, which
  open every `.nrm` in `<config>/mods`. Enabled state persists in
  `<config>/mods.json`. `load_mods` then runs on every `start_game`. Nothing in
  DKR-R filters this set.
- DKR's code is not compressed, so librecomp treats hooks as available and
  implements `RECOMP_HOOK` by **recompiling the hooked base function again
  from ROM bytes** (`ModContext::regenerate_with_hooks`).
- DKR-R's own behaviour is injected into the generated C by the recomp policy:
  v1.0 has 146 function hooks and 20 instruction patches across 96 functions,
  and v1.1 has 160 + 34 across 101. librecomp cannot see any of them: there is
  no `RecompiledPatches` section, so they are not "base patched" functions.
- `project_version` is hard-coded to `1.0.0` (`game_main.cpp`). The release
  version (`VERSION`, currently `1.0.5-beta.10`) goes only into the suffix, so
  a mod's `minimum_recomp_version` cannot tell DKR-R releases apart.
- No DKR base exports or events are registered and no reference symbol files
  are published. The only exports mods can import are librecomp's own:
  `recomp_alloc/free`, config getters, mod file paths, dependency checks,
  hook return values and `recomp_change_save_file`.

### Holes this leaves open today

1. **Silent loss of DKR behaviour.** A mod that hooks or replaces a function
   the policy touches gets a copy without DKR's hooks. See
   **Protected functions** for concrete cases.
2. **Wrong revision.** A `.nrm` built against v1.0 symbols also loads on v1.1,
   because both share `"dkr"`. Its references resolve to v1.1 addresses that
   hold different code.
3. **Online desync.** Code mods load in online sessions. Custom assets are
   already refused there ("Offline custom assets cannot enter an online
   runtime"), but `.nrm` code is not.
4. **Saves.** Code mods run against the normal EEPROM, and any mod can call
   `recomp_change_save_file`. That bypasses the online and legacy save
   routing.
5. **Unreviewed native code.** librecomp accepts `native_libraries` (DLLs),
   `.offline.nrm` DLL mods and `patch.bps` ROM patches.

Phase 1 below closes all five before any public SDK exists.

## Principles

1. **Standard format, standard tooling.** A DKR-R code mod is a plain `.nrm`
   built by the pinned RecompModTool from a `mod.toml`, exactly as in other
   N64Recomp projects. DKR-R adds rules, not a new container.
2. **One code loader: librecomp's.** DKR-R does not write a second loader. It
   validates what librecomp opened and disables what breaks the rules before
   the game starts.
3. **The stable contract is the DKR API, not decomp symbols.** Exports and
   events with a versioned API are supported long-term. Raw hooks and
   replacements of decomp functions are allowed, but they are an unstable tier
   that may break with any DKR-R update.
4. **DKR-R behaviour is protected.** A mod cannot hook or replace a function the
   policy touches. Calling it is fine: the call runs DKR-R's hooks normally.
5. **Offline by default.** Until Phase 4, any enabled code mod keeps online play
   from starting.
6. **Packages never contain ROM-derived data.** `docs/ASSET_POLICY.md` applies
   to mods exactly as it applies to the repository.

## Mod tiers

| Tier | What the code uses | Revisions | Stability |
|---|---|---|---|
| **API** | Only DKR/librecomp exports, events and its own memory | Both, one `.nrm` | Stable within an API major version |
| **Patch** | `RECOMP_HOOK`/`RECOMP_PATCH` on decomp functions or direct base symbol references | See **Revisions** | Unstable, may break on any update |

The validator assigns the tier from the parsed `mod_syms.bin`, not from a
self-declared field. A mod with no reference to base symbols is API tier. Any
base reference, hook or replacement makes it Patch tier.

## Package format

A mod is one `.nrm`: a ZIP generated by RecompModTool containing `mod.json`,
`mod_binary.bin`, `mod_syms.bin` and whatever `additional_files` the author lists
(for example `thumb.png`, shown by the launcher).

DKR-R-specific metadata travels as a `dkr.json` at the root of the
`.nrm`, added through `additional_files`. RecompModTool only writes the
manifest fields it knows, so custom keys in `mod.toml` would be dropped.

```json
{
  "schema": 1,
  "api": 1,
  "online": "offline-only",
  "revisions": ["us.v77", "us.v80"]
}
```

- `schema`: the metadata schema, always `1`. `api`: the DKR API major version
  the mod is built against (`1` or `2`).
- `online`: `"offline-only"` (the default) or `"deterministic"`.
- `revisions`: the supported ROM revisions, required.
- `requires_tracks`: see **Mods that need a track**.

The full field list, as the package inspector in
`runtime-recomp/src/game/mods/magic_code_package.cpp` enforces it, is the JSON
Schema at `modding/schema/dkr.schema.json`.

Code mods (Custom Magic Codes) must ship `dkr.json`: the inspector refuses a
package without it.

Data the mod needs (animations, tables, sounds) is compiled into
`mod_binary.bin` as C arrays, the same way the community embeds assets today.
Mods can also locate their own `.nrm` through `recomp_get_mod_file_path`, but
there is no API for reading files from inside it yet (see **Limits**).

## Mods that need a track

`.dkrmap` stays the track format. A mod uses one of two cases.

**The track belongs to the mod** (for example a private arena). It ships inside
the `.nrm` through `additional_files`, unchanged, and is declared in
`dkr.json`:

```json
{
  "schema": 1,
  "api": 1,
  "revisions": ["us.v77", "us.v80"],
  "tracks": [{ "file": "arena.dkrmap", "track_select": false }]
}
```

- When the mod is enabled, DKR-R extracts the track to
  `<config>/mod-cache/<mod id>/<version>/` and adds it to the custom-tracks
  registry as an extra root owned by the mod. Disabling the mod removes it.
- The track id is prefixed with the mod id (`fulano_taj_brawl/arena`), so it
  cannot collide with an installed standalone track.
- `track_select: false` keeps it out of Track Select. Only the mod loads it,
  through `dkr_track_level_id` and the track selection export.
- A `<track>-hd.zip` can ship alongside it and goes through the existing HD
  pack import.
- Registration happens in the validation step, before `start_game`. That is
  before the first asset table load, which is when the registry must be complete.

**The track is published on its own** and is raceable normally. It stays a
standalone `.dkrmap` in `custom-tracks/`. The mod declares it:

```json
{
  "schema": 1,
  "api": 1,
  "revisions": ["us.v77", "us.v80"],
  "requires_tracks": ["fulano_coliseum"]
}
```

The validator checks the registry. If the track is missing or disabled, the
mod is disabled with "requires the track …". This is a DKR-R dependency, not a
librecomp one: librecomp dependencies only name mod ids.

In both cases the track follows the same rules as a standalone `.dkrmap`,
including `docs/ASSET_POLICY.md`: a track embedding ROM-extracted textures
cannot be distributed, inside a mod or not.

## Revisions

`.nrm` has one `game_id` and mod ids must be unique in the folder
(`DuplicateMod`), so one mod cannot ship two revision builds side by side.
Patch-tier references are addresses in a specific revision. Three ways out:

1. **Patch tier targets v1.0 only.** The SDK publishes only the v1.0 reference
   symbols. On a v1.1 ROM, the validator disables Patch-tier mods with "this mod
   requires the v1.0 Game Pak". API-tier mods work on both. No librecomp
   change is needed.
2. **The runtime translates v1.0 → v1.1 at load.** One `.nrm` works on both. The
   project already maps functions by name across revisions
   (`scripts/generate_revision_policy.py`), but translating hooks, replacements
   and data relocations requires a substantial librecomp patch.
3. **Two `.nrm` with different ids**, each depending on a marker mod registered
   by the runtime (`dkr_us_v10` / `dkr_us_v11`). This works with stock librecomp
   but is awkward for authors and users.

### Measured difference between revisions

Comparing the generated function tables (`build/composed-recomp/v77` vs `v80`):

- All 1851 v1.0 functions exist in v1.1, and v1.1 adds 2 (`dmacopy_v1`,
  `rumble_enable`).
- 1818 (98%) have the same size. Only 250 keep the same address: the rest are
  the same code, shifted.
- 33 changed size. They are mostly asset loading, save/Controller Pak menus,
  Magic Codes, time-trial HUD, credits and subtitles. Several of these are
  already protected.
- Both revisions have one code section, `.main`, at ROM `0x1000` /
  RAM `0x80000400`.

Every reference a mod makes to the base game lives in `mod_syms.bin` as
section + address: relocations, hooks and replacements. None is baked into
`mod_binary.bin`. Translating a v1.0 mod to v1.1 therefore means rewriting
addresses in `mod_syms.bin` through a by-name symbol map built from the two
pinned decomp ELFs. N64Recomp already has the parser and the serializer for
this file.

### Recommendation: option 2, done as a symbol rewrite

- The SDK publishes v1.0 reference symbols only. Authors build one `.nrm`.
- On a v1.1 ROM, DKR-R rewrites the mod's symbol data before librecomp
  parses it. The hook point is a small dependency patch: a host callback in
  `init_mod_code` that receives the `mod_syms.bin` bytes. The mod keeps its
  id, so there is no `DuplicateMod`.
- On the 33 changed functions, calls translate normally. Entry/return hooks
  translate with a warning: the signature is unchanged, but the body differs.
  Replacements are refused, because they would run v1.0 logic inside v1.1.
  Data references into symbols whose size or layout changed are refused too.
  A refused mod is v1.0-only, with the reason shown.
- Same size does not prove identical code. The SDK build compares the two
  revisions' function bytes with relocated fields masked, and publishes the
  list of truly identical functions that the translator trusts.

### When a mod is refused on v1.1

In order of preference, the author can:

1. **Use an API event instead of the replacement.** A mod that needs to
   replace a whole function usually points to a missing API extension point,
   and the API is revision-neutral.
2. **Ship v1.0-only.** v1.1 users see the reason on the mod's card.
3. **Ship two mods.** They need different ids (for example `fulano_menu` and
   `fulano_menu_v11`), because two `.nrm` cannot share one. Each declares a
   dependency on a revision marker mod that DKR-R registers as an embedded mod
   (`dkr_rev_us_v10` or `dkr_rev_us_v11`). The validator silently skips the
   variant for the other revision instead of reporting a missing dependency.

Option 1 of the previous list (Patch tier on v1.0 only) stays as the fallback
if the rewrite turns out harder than measured.

## Validation layer

librecomp opens `<config>/mods` at `recomp::start`. Code loads later, at
`start_game`. DKR-R runs its validator in between, which keeps the
community-standard workflow: users drop `.nrm` files in the `mods` folder.

For each opened code mod the validator:

1. parses `mod_syms.bin` with `N64Recomp::parse_mod_symbols` (DKR-R already
   links N64Recomp) and derives the tier;
2. refuses hooks and replacements on protected functions for the selected
   revision, naming the function and the policy reason;
3. applies the revision rule above;
4. refuses `native_libraries`, `.offline.nrm` (outside a developer setting) and
   `patch.bps`;
5. refuses imports of `recomp_change_save_file` (DKR-R owns save routing);
6. checks `dkr.json`: the API major version must exist, and `online` must be valid;
7. disables every enabled code mod for an online session.

A refused mod is disabled for that session only, and My mods shows the reason
on its card. The user's saved choice in `mods.json` is left as is. Whether
librecomp's public `enable_mod` can disable without persisting needs
checking. If not, the validator keeps its own session override.

## Protected functions

The set is generated, not hand-maintained: every function named in
`functionHooks` or `instructionPatches` of the revision's recomp policy, plus
an explicit `protectedFunctions` list in the same policy for host boundaries
that matter without a hook.

Why it is needed, with mods people would naturally write:

| Mod idea | Natural hook | DKR-R behaviour silently lost |
|---|---|---|
| "Run my code every frame" | `main_game_loop` | Interpolation frame reset, launcher Magic Codes, netplay commit |
| Custom HUD | `hud_render_player` | Expanded HUD clip scope (HUD falls back to 4:3 positions) |
| Texture tweaks | `load_texture` | 9× command reservation, so CI palette commands overflow the texture heap |
| Random track mode | `level_load` | Scene/audio reset between loads, Modern gameplay FOV |

Each of these is refused, and the error points to the API alternative
(`dkr_on_frame`, HUD draw export, track selection export, texture packs).

Enforcement has two layers: the validator (above), plus copies of the
generated `FuncEntry` tables with `rom_size = 0` for protected functions when
the payload registers sections. librecomp then refuses hooks on them with
`CannotBeHooked`. Replacements are only caught by the validator.

A mod that legitimately needs behaviour inside a protected function gets a
new API event or export at that point, not an exception to the rule.

## DKR API

Base exports (`recomp::overlays::register_base_export`) are imported with
`RECOMP_IMPORT("*", …)`. Base events (`register_base_events`) are received
with `RECOMP_CALLBACK("*", …)`. DKR-R binds both to the running revision, so
API-tier mods are revision-neutral.

- `dkr_api_version()` returns `major.minor`. Exports are only added within a
  major version, never changed or removed. Removal requires a major bump after
  at least one release of deprecation.
- Game state is reached through accessor exports, not decomp struct offsets.

First version, driven by the worked examples below (names are illustrative):

| Area | Exports / events |
|---|---|
| Lifecycle | `dkr_on_boot`, `dkr_on_frame`, `dkr_on_race_start`, `dkr_on_race_end`, `dkr_on_level_load` |
| Input | `dkr_input_read`, `dkr_input_consume` (`input_update` is protected) |
| Camera | `dkr_camera_override(pos, rot, fov)`, `dkr_camera_cut` (tells interpolation a cut happened) |
| Race state | racer count, positions, lap, timers, current track |
| HUD | text/rectangle drawing through the existing HUD pipeline |
| Tracks | `dkr_track_level_id(id)` for bundled or required tracks, choose the next track to load |

The list is a starting point for review, not a commitment.

## Limits of `.nrm` in DKR-R

These follow from the format and the vendored librecomp. Each needs either an
API answer or acceptance:

| Limit | Consequence | Answer |
|---|---|---|
| Hooks only at function entry (`RECOMP_HOOK`) or return (`RECOMP_HOOK_RETURN`) | Mods cannot insert code mid-function, unlike DKR-R's own policy (`beforeVram`) | API events at the points that matter |
| Natively reimplemented functions have `rom_size = 0` | libultra-level functions cannot be hooked | Acceptable |
| Code mods are not runtime-toggleable | Changing the code mod set requires starting a new game session | Matches the launcher's prepare-then-launch flow |
| One `game_id`, unique mod ids | Revision handling | See **Revisions** |
| Manifest fields are fixed by RecompModTool | DKR metadata cannot live in `mod.toml` | `dkr.json` via `additional_files` |
| Config options are Enum, Number or String | Mod settings UI is limited to those | Rendered in My mods, D-pad operable |
| Mod code and `recomp_alloc` memory live at `0x81000000`+ | Several DKR-R guards accept only `0x80000000`–`0x807FFFFF` (audio voice/bus guards in `runtime_stubs.cpp`, header checks in `runtime_netplay.cpp` and `runtime_enhancements.cpp`, `custom_tracks_hooks.cpp`). Game-facing structures a mod allocates in its own memory are rejected there. | Phase 1 audit: each guard decides whether mod memory is legal; otherwise mods allocate game-facing data from the game's own heaps through an export |
| No API to read files packed in the `.nrm` | Large data is duplicated into the binary | Add `dkr_mod_read_file` when a real mod needs it |
| librecomp dependencies only name mod ids | A mod cannot declare a `.dkrmap` or a legacy import as a librecomp dependency | `.dkrmap`: bundled or `requires_tracks` (see **Mods that need a track**). Legacy imports: not supported |

## Saves and online

- Any enabled code mod switches to an isolated EEPROM keyed by the enabled mod
  set, reusing the legacy `save_subfolder` route. Mods cannot change the save
  file themselves.
- Online play does not load code mods (Phase 1).
- Phase 4: the match descriptor carries the sorted `id@version#sha256` list of
  enabled mods marked `netplay-safe`. A peer without the exact set cannot join.
  Determinism is the author's responsibility, and the project can revoke a
  `netplay-safe` declaration.

## SDK

- A template repository (separate, mirrored like Track Lab), following the
  community's mod template layout: `mod.toml`, Makefile, clang MIPS setup,
  RecompModTool from the **pinned** N64Recomp, the reference symbol files, and
  the DKR API headers. The pin matters: the vendored RecompModTool already
  writes fields (`custom_gamemode`) that the vendored librecomp does not read,
  so tool and runtime versions must come from the same N64Recomp revision.
- Reference symbol files come from the same pinned decomp ELFs the payload
  build uses, published with each release.
- Two example mods: a freecam (API tier) and a small gameplay tweak in an
  unprotected function (Patch tier).
- `.offline.nrm` (DLL instead of live recompilation) is for debugging only,
  behind a developer setting.

## Worked examples

### Freecam (API tier, both revisions)

The game computes its camera normally. Then a `RECOMP_HOOK_RETURN` on
`update_player_camera` (not protected) overwrites the result through
`dkr_camera_override`. Interpolation keeps working, because the protected
matrix functions (`mtx_perspective`, `mtx_world_origin`) run afterwards on the
already-modified camera. Input comes from `dkr_input_read` /
`dkr_input_consume`, so the kart does not also steer. `dkr_camera_cut` on
toggle keeps interpolation from smearing the jump.

Unverified side effect: DKR uses distance to the active camera for draw
range, object fades and spatial audio. A far camera may hide or freeze
objects near the player.

### Taj Brawl (Patch tier)

A Smash-style fight against Taj in the hub: the player leaves the kart and
fights on foot.

| Piece | How | Protected? |
|---|---|---|
| Trigger near Taj | Hook `obj_loop_parkwarden` | No |
| Leave the kart | Hook `update_player_racer`, take over while fighting | No |
| On-foot physics, damage %, knockback, Taj AI | The mod's own code | No |
| Ground and walls | Call `collision_get_y` and existing collision queries | Calls only |
| Side camera | Hook `update_player_camera` | No |
| Input, HUD | DKR API | Via API |
| Hit effects | Call `spawn_object` | Calls only |

The hard part is content, not architecture. DKR has no on-foot walk, jump
or attack animations, so the author creates them. How the decomp separates
driver and vehicle models still needs checking. Object rendering
(`render_object`) is protected, so the mod changes *what* is drawn (position,
animation, model) but not *how*.

Source layout:

```text
taj-brawl/
├── mod.toml
├── thumb.png
├── dkr.json
├── src/
│   ├── brawl.c          hooks: Taj, racer, camera
│   ├── fighter.c        physics, damage, knockback
│   └── taj_ai.c
├── assets/anims/        converted to C arrays at build time
└── Makefile
```

`mod.toml`:

```toml
[manifest]
id = "fulano_taj_brawl"
version = "0.1.0"
display_name = "Taj Brawl"
description = "Leave your kart in the hub and fight Taj, Smash-style."
short_description = "Fight Taj on foot."
authors = ["Fulano"]
game_id = "dkr"
minimum_recomp_version = "1.1.0"
dependencies = []

[[manifest.config_options]]
id = "exit_button"
name = "Leave-kart button"
description = "Button that leaves the kart near Taj."
type = "Enum"
options = ["L", "Z", "D-pad down"]
default = "L"

[[manifest.config_options]]
id = "taj_difficulty"
name = "Taj difficulty"
description = "How aggressive Taj fights."
type = "Enum"
options = ["Easy", "Normal", "Hard"]
default = "Normal"

[inputs]
elf_path = "build/mod.elf"
mod_filename = "fulano_taj_brawl"
func_reference_syms_file = "dkr-sdk/dkr.us.v77.func-syms.toml"
data_reference_syms_files = ["dkr-sdk/dkr.us.v77.data-syms.toml"]
additional_files = ["thumb.png", "dkr.json"]
```

The output is a single `fulano_taj_brawl.nrm`. On v1.1 it is translated at
load. None of its hooks (`obj_loop_parkwarden`, `update_player_racer`,
`update_player_camera`) is among the 33 changed functions. It plays offline,
with an isolated save.

## Implementation phases

1. **Close the holes.** Validator between scan and `start_game`, online
   exclusion, isolated saves, protected functions on both layers,
   `project_version` taken from `VERSION`, RDRAM guard audit. No public SDK.
2. **Library UI.** Code mods in My mods: tier, revision availability, refusal
   reasons, per-mod config, load order.
3. **API v1 and SDK.** First export/event set, template repository, example
   mods, reference symbol files published per release.
4. **Online.** `netplay-safe` and the mod set in the match descriptor.

## Open questions

- Revision handling: confirm the symbol-rewrite approach (option 2) once the
  data-symbol comparison between the two ELFs is done. Only functions have
  been measured so far.
- Should Patch tier be public in the first SDK release? Without it, mods like
  Taj Brawl are impossible.
- Which `protectedFunctions` beyond the policy hooks are needed (netplay frame
  boundary, save routing, asset bus)?
- Is an isolated EEPROM the right default for API-tier mods that only draw HUD?
