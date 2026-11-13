# v1.0.5 Beta 10 — model-load crash and missing HUD investigation

> Historical report or implementation plan. This retains the original build
> findings, counts and limits; it is not the current 1.0.5 user guide.
> Use the [documentation index](../README.md) for current instructions.


## Confirmed crash

The supplied 15 September dump is a US v1.0/v77 session with offline custom
characters enabled. Its exception is a read access violation at executable RVA
`0x17F18B`, in `model_instance_init+0x5B`. The call chain is
`object_model_init → spawn_object → skydome_spawn → init_track → level_load`.
The failing guest model pointer is null; the instruction reads its animation
count at offset `0x48`. The model-cache scan had matched a zero model ID in an
empty entry instead of loading that model.

The local diagnostic symbol link has a `.text` section **byte-for-byte identical**
to the distributed Beta 9 executable, not just similar source. No dump, ROM,
private profile, or extracted asset is included in the release package.

The v77 native `object_model_init` reserves a cache slot before loading. Five
failure routes (model allocation, texture, normals, animation, instance) return
without restoring that reservation. The native v80 source already restores it.
An executed v77 Beta 9 fault-injection test failed with “Failed load left a
phantom model-cache entry”; the corrected native loader passes the same sequence.

The dump does **not** contain enough guest heap memory to establish which first
load failed or prove heap exhaustion was its cause. The cache bug is confirmed;
the initiating resource failure is not reconstructed from the dump.

## Surgical changes

All guest changes are hash-pinned, instruction-checked project Patch Pipeline
hooks. Submodules and canonical generated sources are not edited.

| Hook | US v1.0 | US v1.1 | Effect |
| --- | --- | --- | --- |
| Model instance entry | Symbol-resolved | Symbol-resolved | Return null safely instead of dereferencing a null model |
| Model load entry/return | `8005F99C` / `8005FCB4` | Native fix retained | Restore cache/free-slot counts on a failed load only |
| Main heap bound | `80070B50` | `80070D90` | Offline admitted custom-mod session uses existing 8 MiB memory extent |
| HUD failed-load observation | `800AA7AC` | `800AAD08` | Bounded diagnostic; native retry and draw behavior unchanged |

The heap change uses the native allocator's existing expansion-memory design.
Retail's pool ends at `80400000`; an admitted offline mod session can end at
`80800000`, inside the renderer/RSP's existing 8 MiB range. No buffers are moved
after startup. Slot counts, cache capacity, frees, asset limits, stock-session
memory extent and online-session memory extent are unchanged. The existing
online admission gate rejects prepared offline custom assets before guest boot.

Every pre-existing Beta 9 function hook and instruction patch is identical in
the composed Beta 10 policies. Only five v77 hooks and three v80 hooks were added.
Track Lab remains in Mods/Hacks directly below Magic Codes; its Blender controls
and shared texture-management modal are retained. No networking, windowing,
shadow, character-facing or HUD-position rewrite is part of this pass.

## HUD evidence and limits

The reported elements are GET READY, GO and the race position number.
`hud_element_render` silently returns when its texture/sprite/model loader returns
null. Extra custom assets share the original small heap with those HUD assets.
The expanded offline heap addresses that resource-pressure path, but this is
**not proof that every reported missing or misplaced element had that cause**.

Native guest tests now draw the non-portrait HUD elements after custom portrait
calls, at unit and animated scale, and inject failed loads followed by successful
retries. They verify that drawings resume, cache entries recover, and authored
coordinates/caller stack are preserved. These tests do not replace visual
verification of complete races and renderer output.

If an asset still fails, `runtime.log` records `[hud][asset-load-failed]` with the
sprite and element address. Reporting is bounded to once per sprite per guest
thread; no per-frame logging or extra allocation retry loop is introduced.

## Qualification

- 29 standalone suites passed on Windows and Linux/WSL.
- Native model/cache tests cover both revisions, five failure routes, new and
  recycled cache slots, null instances, successful cache hits and caller ABI.
- Native heap tests: 524 checks per revision, including exhaustion, nonoverlap,
  data preservation, address bounds, freeing, coalescing and reload.
- Native portrait/voice/HUD tests: 725 checks per revision.
- 14 Python policy tests include ELF-hash rejection, altered instructions,
  conflict rejection and Track Lab/sidebar preservation.
- All 73 project application suites passed on Windows (122.71 seconds) and
  Linux/WSL (64.23 seconds).
- The seven supplied-mod corpus passed 39,546 compatibility assertions on each
  platform, including both ROM revisions, isolated imports, activation,
  resident assets, hiding/removal/reimport and source preservation.
- Both final packages passed staged importer, Controller Pak and live
  SDL2/SDL3-switch checks. The Windows staged executable matches the tested
  build SHA-256; both artifacts report `1.0.5-beta.10`.

Audit: `G:/DiddykongWorkFolder/beta10-regression-20260915/`.
Rollback: the existing Beta 9 `packages-track-lab-mods-hacks` package directory
under `G:/DiddykongWorkFolder/character-presentation-beta9-20260913/` is untouched.

Visual playtest still required: original and custom racers, both HUD modes,
1/2/3/4-player layouts, start messages, animated position changes, scene reloads
and race results. Native Linux/Steam Deck visual acceptance is also pending.

Final Windows ZIP SHA-256:
`b7392fdebae29b9bdf1a5f12155bdba644e813bfcfcb901783797893a5822aa9`.
Final Linux AppImage SHA-256:
`ee5cb73aabe751fea9c02e582e2c664fe3fa9a1bb9f2c25ef3c60f6a1fb637a3`.
