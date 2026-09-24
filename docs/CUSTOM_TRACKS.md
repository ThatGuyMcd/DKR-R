# Custom tracks

Status: **in progress.** The asset-table layer is implemented; the Patch
Pipeline hooks and the UI are not yet wired.

## Why this needs no instruction patching

DKR resolves every level through one data-driven indirection
(`level_global_init` and `load_level_game` in the matching decomp):

```c
gTempAssetTable = (s32 *) asset_table_load(ASSET_LEVEL_HEADERS_TABLE);
for (i = 0; gTempAssetTable[i] != -1; i++) {}
i--;
if (levelId >= i) { /* out of range -> fall back to the hub */ }
offset = gTempAssetTable[levelId];
size   = gTempAssetTable[levelId + 1] - offset;
asset_load(ASSET_LEVEL_HEADERS, (u32) gCurrentLevelHeader, offset, size);
```

Three retail behaviours follow from that, and all three work in our favour:

- The level count is counted to the `-1` terminator, not compiled in.
- The range check derives its bound from the same count, so a longer table
  widens the accepted `levelId` range automatically.
- `gNumberOfWorlds` is a running maximum of each header's `world` field, so a
  header declaring a world beyond the retail maximum creates that world.

Publishing a longer table is therefore sufficient to add both tracks and
worlds. No bounds check is patched and no retail instruction is replaced.

## The offset rule

`size` comes from the difference between consecutive entries, so custom
offsets cannot be arbitrary tokens. Overwriting the retail end offset would
corrupt the size of the **last retail entry**.

Custom offsets therefore begin exactly *at* the retail end offset and advance
by exact entry sizes, so a payload behaves as though appended to the section:

```text
retail : [o0, o1, ..., o(n-1), oEnd, -1]                    -> n entries
result : [o0, o1, ..., o(n-1), oEnd, oEnd+s0, ..., E, -1]   -> n+K entries
```

Retail arithmetic is untouched, custom entry `j` lands at level id `n + j`,
and `offset >= oEnd` is the discriminator that routes a load to a mod payload
instead of the ROM.

## The `.dkrmap` format

A track is a directory named `*.dkrmap` under `custom-tracks/`, or a zip archive of the
same layout that the importer unpacks. The directory form is what an author
edits in place, so a reload picks up an editor's save without a repack.

```text
ancient-lake-remix.dkrmap/
  manifest.json
  header.bin
  objects.bin
  name.bin
  model.bin
  textures/
    0.bin
    1.bin
  minimap/
    texture.bin
    sprite.bin
```

```json
{
  "schemaVersion": 1,
  "id": "ancient-lake-remix",
  "name": "Ancient Lake Remix",
  "author": "example",
  "adds": [
    { "section": "LEVEL_HEADERS",     "file": "header.bin" },
    { "section": "LEVEL_OBJECT_MAPS", "file": "objects.bin" },
    { "section": "LEVEL_NAMES",       "file": "name.bin" },
    { "section": "LEVEL_MODELS",      "file": "model.bin" },
    { "section": "TEXTURES_3D",       "file": "textures/0.bin" },
    { "section": "TEXTURES_3D",       "file": "textures/1.bin" },
    { "section": "TEXTURES_2D",       "file": "minimap/texture.bin" },
    { "section": "SPRITES",           "file": "minimap/sprite.bin" }
  ]
}
```

`TEXTURES_3D` is the one section a track adds **many** entries to, and their
order in `adds` is their identity - see "A track's own artwork" below.
`TEXTURES_2D` and `SPRITES` are the track's own minimap - see "A track's own
minimap".

Payload paths are confined to the track directory: absolute paths and `..`
are rejected, so a manifest can never name an arbitrary file on the machine.

Section payloads are produced by the matching decomp's asset tool
(`tools/dkr_assets_tool_src`), which is what the `Hint((...))` annotations in
`include/level_object_entries.h` exist to drive. DKR-R never parses a level
format; it serves bytes.

## Hub doors

A hub gate is two separate objects in a level's object map, not one:

| Object | Field | Meaning |
|---|---|---|
| `LevelObjectEntry_Exit` | `destinationMapId` | target level id (`ASSET_LEVEL_HEADERS` index) |
| | `overworldSpawnIndex` | where the player appears in the overworld |
| | `returnSpawnIndex` | where the player returns to in the hub |
| | `radius` | activation radius |
| `LevelObjectEntry_Door` | `balloonCount` / `localBalloons` | balloons required to open |
| | `keyID` | key requirement |
| | `modelIndex`, `textID`, `scale` | appearance |

`destinationMapId` is a `u8`. Retail uses 34 level ids, so roughly 220 remain.

## Progression

The retail EEPROM is 512 bytes, bit-packed with per-slot checksums, and fixed
at `kWorldCount = 6` / `kCourseCount = 34` (`dkr_save_codec.hpp`). A seventh
world does not fit, and it is not a matter of effort - there are no spare
bytes.

Custom progression therefore belongs in a DKR-R-owned sidecar beside the
virtual EEPROM, never inside the retail image. This also means installing a
custom world can never corrupt a real Adventure save.

## Hook design

Two interceptions are needed, and both follow the pattern already used by
`rev_a_asset_mutex.cpp`: an `instructionPatches` entry blanks the retail call,
and a `functionHooks` entry at the same VRAM performs the work natively,
delegating to the retail routine whenever the fast path does not apply.

### 1. Publish the extended table

`asset_table_load(assetIndex)` returns a freshly allocated copy of one asset
table. The hook runs at its epilogue, and when the request was
`ASSET_LEVEL_HEADERS_TABLE` it allocates a longer buffer through
`mempool_alloc_safe`, writes `build_extended_table()` into it, and replaces the
return value in `context->r2`.

The retail allocation is released through `mempool_free` once the extended
copy has replaced it, since callers free only the pointer they were given.

### 2. Route custom offsets away from the ROM

`asset_load(assetIndex, address, assetOffset, size)` DMAs from the cartridge.
A custom offset is deliberately beyond the section, so the DMA must never run:
the hook copies `payload_for()` into `address` and reports success instead.
When `payload_for()` returns null the retail loader is invoked unchanged.

Hooking the two call sites in `level_global_init` and `load_level_game` is
preferred over hooking inside `asset_load`, because a hook cannot skip a
function body but a blanked `jal` can be replaced wholesale.

### Symbols

```text
level_global_init   = 0x8006A6B0     asset_table_load   = 0x80076C58
asset_load          = 0x80076E68     mempool_alloc_safe = 0x80070C9C
gTempAssetTable     = 0x80121160     gNumberOfLevelHeaders = 0x80121170
```

The two `jal asset_load` instruction addresses still have to be read from the
generated disassembly; every other address above comes from the matching
decomp symbol file. The v1.1 policy is produced by
`scripts/generate_revision_policy.py` and is never hand-authored.

## Alongside legacy mods

DKR-R also imports legacy mods from `.xdelta` patches: extra courses and up to
two custom characters (`src/game/mods/`, `docs/LEGACY-MODS-BETA.md`). They
answer the same asset calls, so the two systems are composed rather than
stacked.

**One entry hook, legacy first.** `scripts/compose_legacy_mod_policy.py` puts
the legacy asset bus at the entry of `asset_table_load` and `asset_load`. The
`.dkrmap` recorder is already there, so the composer merges the two into one
hook instead of refusing the address: the legacy call returns early when a
mounted bank served the request, and otherwise the `.dkrmap` recorder runs and
the retail path continues to its epilogue as before. A mounted request never
reaches that epilogue, so `dkr_legacy_asset_api` does the `.dkrmap` work itself:

- a level table (sections 20, 22, 24, 26) is extended after the mount copied it,
  and the mount's allocation is released like the retail one;
- a read at a `.dkrmap` offset is served before the mount sees it
  (`dkr_custom_tracks_asset_load_override`), because a mounted section refuses
  any read past its end. The header fixups still run.

**One texture namespace.** A mod session appends custom characters' textures to
the 3D texture table at boot, and the table's allocation is fixed from then on:
every scene rewrites it in place. `.dkrmap` artwork therefore cannot be added
by the table hook in a mod session. `prepare_mod_launch` instead builds the
`.dkrmap` texture table against the character-augmented boot table
(`publish_dkrmap_artwork`) and appends the textures to the boot bank and to
every scene bank with `AssetBank::append_textures`. They get the IDs
`build_extended_table` wrote into the level models: characters first, then
`.dkrmap` artwork in scan order. Section 3 and section 2 skip the `.dkrmap`
hooks in a mounted session, since the bank already holds them.
`legacy_dkrmap_artwork_tests.cpp` checks this against an owned ROM. Without
legacy mods nothing changes: no session exists and the table hook publishes the
artwork as before.

The artwork is not part of the mod session's identity. That identity names the
separate modded save folder, and re-exporting a track must not strand the
player's modded Adventure progress.

**Scenes and selection.** A legacy course is a scene published at `level_load`
for the carrier level it borrows. A `.dkrmap` level is never a carrier
(`custom_tracks::owns_level_id`), so loading one always publishes the original
scene, even with a legacy request pending. Confirming a course in Track Select
(`trackmenu_assets(TRACKMENU_TYPE_LOAD_LEVEL)`) disarms Track Lab, with or
without legacy courses installed, so the player's choice is what loads. Auto
boot and the L+Z restart never pass through that confirmation and keep
reloading the armed track.

Track Lab is a section of **MODS / HACKS**. It lists the tracks in the
author's working folder; installed tracks appear beside the legacy courses in
**My mods**, marked **DKR**.

**Test-race setup.** Auto boot prepares a single-player Tracks race as Diddy,
fills the opponents through the native unlock-dependent character selection,
and commits all eight racer headers before entering the track. Skipping the
menus must not skip that initialization: zeroed racer characters are Krunch.
At the existing `level_load` scene-reset hook, gameplay on the armed track
uses the addon's exported default vehicle and synchronizes the player vehicle
selections read by the AI. This applies again on L+Z and track changes, without
rerolling the roster on restart. Menu previews and disarmed Track Select keep
their native choices. Special debug vehicles remain load arguments; the
three-entry player selection arrays use an allowed normal vehicle. Authored
setup-point overrides and boss-specific spawning still run in the native code.

**Memory.** Retail DKR sizes its main pool (`mmInit`) to the 4 MB console,
about 2.96 MB of allocations. A `.dkrmap` race can need more: a 75-texture
track (each picture a 64x32 RGBA16, ~4 KB) with eight different hovercraft
racers peaks at 3.01 MB, and the next allocation returned NULL and crashed in
`init_triangle_particle_model`. The runtime maps 8 MB, and nothing else uses
the upper 4 MB. So the same `level_load` hook grows the main pool's tail slot
to `0x80800000` the first time a `.dkrmap` level loads. The pool never
shrinks, and the allocator code is unchanged. Retail and legacy levels never
trigger the growth; the log shows
`main memory pool grown into expansion RAM` when it happens. `load_level_game`'s
entry hook does the same growth first, because it runs before the display-list
heap is allocated.

**The level model heap.** A bigger pool is not a bigger *level model*. That is a
separate reservation: `generate_track` asks for `LEVEL_MODEL_MAX_SIZE`
(`0x82A00`, 535,040 bytes) in one allocation and builds the whole model inside
it — the inflated blob, then per segment two bytes a triangle, sixteen bytes per
collision plane and two per wave batch. Collision dominates. Bluey, retail's
largest, lands near 80% of it with a fraction of the triangles an exported track
carries, and a `.dkrmap` race can exceed it honestly.

Retail does not refuse when it does. It compares the total, reports it through
`rmonPrintf` — stubbed in this build, so nothing is printed — and writes past
the heap anyway, straight over `gCollisionCandidates`, `gCollisionSurfaces` and
the pool's slot list behind them. The symptom is a wild pointer or a rejected
display-list opcode some frames later, never the overflow itself.

So the arena is measured before the load and the reservation sized to fit.
`custom_tracks::measure_level_model_arena` walks the payload exactly as
`track_init_collision` does, including the edge planes a neighbouring pair
shares, and the `level_load` entry hook leaves the result for a hook inside
`generate_track`. It has to be `level_load`: Track Select previews load through
`load_level_for_menu`, never `load_level_game`, and a preview that kept the
retail heap overflowed it and crashed in `obj_loop_texscroll` on a wild
texture pointer. That hook writes `s5`, the register holding the constant, at
the one instruction where it is complete and before anything has read it:

```
8002c0f4  lui   s5, 0x0008
8002c0f8  ori   s5, s5, 0x2a00    <- the hook runs after this
8002c104  jal   mempool_alloc_safe
8002c108  or    a0, s5, zero      <- delay slot: the size asked for
8002c1c0  addu  t6, s0, s5        <- where the compressed blob is landed
```

One write therefore enlarges the reservation *and* keeps the compressed payload
at the tail of the larger heap, which is what guarantees the inflate cannot
overrun its own source. US Rev A's prologue is instruction-for-instruction the
same, forty-eight bytes further on. The hook refuses to write unless `s5`
already holds the retail constant, so a revision that differs keeps the retail
heap rather than having a misread register overwritten, and every retail level
keeps it byte for byte. The log shows `builds ... over the retail ...` when it measures and `track heap raised from ... to ... bytes` when the hook applies it.

Two ceilings remain, and no larger heap lifts either. A collision plane index is
packed as `index | 0x8000` when a pair shares one, so a segment cannot build
more than 32,767 planes — roughly 11,000 collidable triangles. And `collision.c`
considers ten segments at a time whatever the track's size. The runtime reserves
at most 2 MB for a level model and says so in the log when a track needs more
than that; past there the only fix is fewer collidable triangles or less
geometry.

**Display lists.** `alloc_displaylist_heap` sizes each frame's list from
`gNumF3dCmdsPerPlayer` (4500 commands for one player), and the matrix heap
starts right after it. `render_level_segment` spends 3 to 10 commands on every
visible batch. A 68-segment export has 1214 batches and a PVS with every bit
set, so driving it reached 5613 commands. The list then ran into that frame's
matrices, F3DDKR rejected the garbage opcodes, and RT64 crashed in a `memmove`.
At `load_level_game`'s entry the runtime inflates the track's own model and
counts its batches. It then sets each table entry to retail +
`batches x 10 x viewports`, capped at 0x20000 commands. Any other level gets the
retail table back. When the table changes, the hook invalidates
`gPrevPlayerCount`, so the retail allocator rebuilds the heap in that same call.
The log shows
`level N draws up to B batches; display lists sized for C commands`. A track
that ships no model of its own keeps the retail budget.

Track Select previews draw into the menu's one-player list, and their path
cannot resize it: the preview often loads on thread30 while the menu is still
drawing. So the runtime sizes it at boot. `level_global_init` loads the header
table, which publishes each course's level ID, just before
`default_alloc_displaylist_heap` first allocates the lists. At that point the
runtime takes the largest batch count among the `.dkrmap` courses Track Select
offers. It grows the pool and raises every table entry to at least that
course's one-player budget. Later loads keep that floor, so the heap a race
leaves for the menu can still draw every preview. The log shows
`Track Select previews draw up to B batches`. A course enabled after boot that
needs more is named in the log at preview time; restart the game to size the
lists for it.

## Verified end to end

A smoke test installed one track whose payload is a byte copy of Ancient
Lake's retail level header, taken straight out of the ROM's asset sections.
Running the built `DKR-R.exe` against the US v1.0 ROM produced:

```text
[custom-tracks] loaded Ancient Lake Clone by smoke test
[custom-tracks] section 0: 65 retail + 1 added
```

The count matches the ROM independently: `ASSET_LEVEL_HEADERS_TABLE` holds 68
entries, terminated at index 66, which is the 65 levels `level_global_init`
counts, and the decomp extracts exactly 65 level header files. The second line
repeats once per `asset_table_load` call, which is once at level table init and
again for each level load.

The Blender addon defaults new tracks and remixes to `WORLD_CUSTOM_TRACKS`
(header world byte `6`). Enabled normal races in that category appear beside
legacy courses under **CUSTOM TRACKS** in the offline Track Select menu, even
when no legacy courses are enabled. Preview and race loads use each `.dkrmap`'s
appended level ID. The native menu supports IDs below 128; higher IDs remain
available through Track Lab. Hub, boss and special level types are not added
to the normal-race grid.

World `6` only files the course; the game never sees it. Retail indexes
five-world arrays with `header->world - 1`. The post-race mosaic read past
`gTracksMenuBgTextureIndices`, and `bgdraw_texture` then tiled a non-texture
until its display list ran out of memory. A sixth world would also grow
`gNumberOfWorlds`, and with it the save file's per-world fields. So when a
header with world `6` is served, the runtime writes Dino Domain (`1`) in its
place. That is the world whose background Track Select already draws for the
category. The catalogue reads the world from the package itself.

An explicit World selection in the addon overrides the default. Existing
packages retain their exported world: select `WORLD_CUSTOM_TRACKS` and export
again to put them in this category. Choosing another world does not itself
create a retail menu entry or hub door.

## Sections a track can replace or extend

The hooks resolve an asset section index to a payload slot, so all four level
aspects work through one mechanism (`AssetSectionsEnum` in the decomp's
`include/asset_enums.h`):

| Table / data | Section | Manifest name |
|---|---|---|
| 3 / 2 | 3D textures | `TEXTURES_3D` |
| 5 / 4 | 2D textures (the minimap picture) | `TEXTURES_2D` |
| 13 / 12 | sprites (the minimap sprite) | `SPRITES` |
| 20 / 21 | object maps | `LEVEL_OBJECT_MAPS` |
| 22 / 23 | headers | `LEVEL_HEADERS` |
| 24 / 25 | names | `LEVEL_NAMES` |
| 26 / 27 | models | `LEVEL_MODELS` |

The texture pairs and the sprite pair are the same numbering read from the
other end of the enum, and note their order is data-then-table, the reverse of
the four level pairs.

## A track's own artwork

A `.dkrmap` can add textures the ROM does not hold, and the mechanism is the
one above rather than a new one. `textures_sprites.c` reaches the 3D texture
list exactly the way `level_global_init` reaches the level list:

```c
gTextureAssetTable[TEX_TABLE_3D] = asset_table_load(ASSET_TEXTURES_3D_TABLE);
for (i = 0; table[i] != -1; i++) {}          // count, then i--
if (assetIndex >= gTextureTableSize[..]) { } // range check, from the table
assetOffset = table[assetIndex];
assetSize   = table[assetIndex + 1] - assetOffset;   // size BY DIFFERENCE
asset_load(ASSET_TEXTURES_3D, dest, assetOffset, assetSize);
```

So publishing a longer table grows the texture count and the range check
together, and an appended payload loads. Nothing else changes.

### What a texture payload is

The bytes `dkr_assets_tool`'s `BuildTexture::build` writes for an uncompressed
texture: a 32-byte `TextureHeader` and then the image, row-major and
unswizzled. The Blender addon writes them itself, so no C++ toolchain stands
between an author and a track; `tools/blender/tests/test_custom_textures.py`
holds every field and every texel conversion to the decomp's own.

Two limits are the hardware's, not the format's, and both are refused at import
rather than discovered as a corrupt road in game:

- The RDP has **4 KiB of texture memory** and `material_init` loads a level
  texture as one block, so a 16-bit format stops at 2048 texels - **64x32**,
  not 64x64. The eight-bit formats reach 64x64 and the four-bit ones are capped
  by the wrap limit instead.
- `material_init`'s mask loop only walks the powers of two **up to 64**, so a
  side larger than that gets `G_TX_CLAMP` and `G_TX_NOMASK` however the flags
  are set: the texture stretches once across each face instead of tiling.

Colour-indexed formats are excluded. Their palettes are loaded from
`ASSET_EMPTY_14` by a byte offset into that section, and a track cannot add
one.

The reduction those limits force is undone outside the track, not inside it.
The Blender addon's export also writes `<track>-hd.zip` beside the package: a
Rice texture pack of the author's originals, each named by the identity RT64
computes for the payload the package ships. The track neither needs nor
references it, and without it draws exactly what it always did. See
`docs/TEXTURE_PACKS.md`.

A payload is also refused if it is shorter than 40 bytes or not a multiple of
16. `load_texture` reads `sizeof(TempTexHeader)` - 40 bytes - before it knows
how large the texture is, and it puts the display list it builds at
`align16(tex + assetSize)` inside an allocation of exactly that size plus the
lists.

### What the header decides, and what the installer checks

`material_init` builds each texture's display list from its own header, and
three of its bytes decide how the game draws it:

| Header | Meaning |
|---|---|
| `format & 0x0F` | the texel format |
| `format >> 4` | the render mode: `TRANSPARENT` (0), `OPAQUE` (1), `TRANSPARENT_2` (2), `OPAQUE_2` (3) |
| `numOfTextures >> 8` | how many frames the texture animates through |

RGBA32, RGBA16 and CI4 are see-through when the render mode is a `TRANSPARENT`
one; IA16, IA8 and IA4 always are; I8 and I4 never are. `render_level_segment`
draws a see-through texture only in its second pass - over batches
`[numberofOpaqueBatches, numberOfBatches)` - so a model has to put the batches
drawing one past that split, or they are never drawn. The Blender exporter
does, and `docs/LEVEL_MODEL_FORMAT.md` has the rule. A batch flagged
`RENDER_CUTOUT` is alpha-tested instead of blended, and RT64 draws it the same
way with an HD replacement, cutting where the replacement's alpha is below an
eighth; the exporter hardens a cut-out's HD original at half so both cut in the
same place.

`inspect_texture_payload` reads those headers when a track is scanned, walking
every frame by its own `textureSize` as `load_texture` does, and refuses a
payload the loader would misread: a colour-indexed format, a render mode past
3, no frames, a zero-sized frame, a `textureSize` smaller than its header and
texels, or a frame that runs past the payload. A compressed payload's frames
are packed, so only its first header is read. Track Lab shows what each track
brings: *3 textures, 1 see-through, 1 animated*.

### The id a level model stores, and why it is a placeholder

A `TextureInfo` in a level model stores an index into the global list, and
`tracks.c` resolves it with `load_texture(id | 0x8000)`. A shipped texture's
index is **the ROM's retail texture count plus its ordinal**, and that count is a
property of the cartridge: 1401 in the US v1.0 extraction and 1416 in Rev A,
both counted from the asset tables. So the exporter cannot know it. It writes
`0x7000 + ordinal` instead, and DKR-R substitutes the real index as the model
is served, exactly as it already patches a header's model and object-map
fields.

**Order in the manifest is the ordinal, and therefore the identity.**
Reordering the `TEXTURES_3D` entries repaints the track with no error anywhere.

An id that cannot be resolved - a track added by a rescan after boot, or a
model naming more textures than its package ships - is rewritten to texture 0
rather than left alone. Leaving it would send `load_texture` past the end of the
table: it range-checks such an index, sets `id = 0`, and then indexes with the
unclamped value anyway.

### How the id is reachable inside a compressed model

A level model arrives compressed - `track_init_level_model` does `asset_load`
and then `gzip_inflate` - and a four-byte field inside a Huffman-coded DEFLATE
block has no byte offset to patch.

The answer is DEFLATE's own. Block type `00` is *stored*: byte-aligned and
verbatim, and `gzip_inflate_block` dispatches to `gzip_inflate_stored` for it
exactly as it does to the Huffman decoders for the other two. So the exporter
writes the model's header and texture table as one stored block and compresses
the rest. The stream stays a legal DEFLATE stream, the game inflates it with the
code it always used, and the ids sit at a fixed offset:

```text
0..4    container: uncompressed size (LE u32), then the tag 0x09
5       the stored block's BFINAL/BTYPE byte, whose remaining five bits the
        reader discards - which is what "stored is byte-aligned" means
6..7    LEN, little endian        8..9  NLEN, LEN's complement
10..    the model's own first LEN bytes, uncompressed
```

The prefix is about two kilobytes at the largest possible texture table,
against models of a hundred to five hundred. Every packed model pays it,
including remixes with no artwork of their own, because one format that is
always patchable is worth more than two that differ in a way nothing downstream
can see.

### The texture table is published once

`tex_init_textures` runs once at boot, from `thread3_main`. The level tables are
rebuilt at every level load, so a rescan renumbers them; this one cannot be
renumbered afterwards. A track installed mid-session therefore has no textures
in the published table at all, and its model's ids are reset to texture 0.
DKR-R has to be relaunched after installing a track that ships artwork - HD
pack or the 64x32 in the `.dkrmap`, it makes no difference.

This is the one step no amount of UI removes. Track Lab makes it *one* click,
not zero: when a track ships an HD pack that the current session cannot load
yet, its row shows **HD textures: restart to load** and a single **Restart &
play in HD** button arms the track, switches to Modern, turns on auto-boot, and
relaunches straight into it. `custom_tracks::track_textures_published()` is what
tells the UI which state the track is in. Every launch after that is zero
clicks: the armed track and auto-boot persist in `custom-tracks-state.txt`.

### Making one

```sh
blender --background --factory-startup \
    --python tools/blender/make_texture_demo_track.py -- \
    --image path/to/picture.jpg --out build/my-track.dkrmap
```

That script drives the same operators the sidebar does, in the same order, and
prints the package back from its own bytes. In Blender, the Textures panel's
"This track's own artwork" section is the same four steps by hand.

## A track's own minimap

The minimap is two things drawn over each other that never consult one another:
a small greyscale **picture** of the road - a 2D texture inside a sprite - and a
**dot** per racer, which `minimap_marker_pos` (`game_ui.c`) places from nine
numbers in the `LevelModel` header (`0x20`-`0x3B`) and the model's bounds. A
remix that inherits another track's numbers inherits its picture, and its dots
land wherever those numbers send them. `docs/CUSTOM_MINIMAP_PLAN.md` has the
investigation; this is what shipped.

**Two more tables, the same mechanism.** `tex_init_textures` loads
`ASSET_TEXTURES_2D_TABLE`, then the 3D table, then `ASSET_SPRITES_TABLE`, each
counted to its terminator, and `load_texture`, `tex_load_sprite` and
`load_sprite_info` read an entry by table difference. So a picture appended to
the 2D table and a sprite appended to the sprite table load exactly as a
`TEXTURES_3D` payload does, and like it they are **published once, at boot**.

**What the payloads are.** `TEXTURES_2D` is a texture payload like any other,
checked by the same `inspect_texture_payload`; the addon writes the retail
minimap recipe - IA8, render mode `TRANSPARENT`, `posX`/`posY` 15/2, cut-out,
clamped both ways and `RENDER_LINE_SWAP` with its odd rows pre-swapped so
`material_init` loads it with `gDPLoadTextureBlockS` as it loads every retail
minimap. `SPRITES` is what `BuildSprite` writes: a 12-byte `SpriteHeader`
(`s16 baseTextureId, s16 numberOfFrames, s16 anchor x, s16 anchor y, s32 0`),
the frame boundaries, padded to 16. `inspect_sprite_payload` refuses one with
no frames, frame boundaries that do not start at 0 and climb, or more than the
512 bytes `gCurrentSpriteAsset` holds.

**Two placeholders.** The real indices are the ROM's counts plus an ordinal -
193 sprites and 906 2D textures in US v1.0 - so, as for textures, the exporter
writes placeholders and DKR-R substitutes them as the tables are built:

| Where | Placeholder | Becomes |
|---|---|---|
| model `minimapSpriteIndex` (`0x20`, inside the stored prefix) | `0x7000 + n` | the sprite table index of the track's `n`th sprite |
| sprite `baseTextureId` | `0x7000 + n` | the 2D table index of the track's `n`th 2D texture |

The sprite is patched in the sprite blob when that table is built, which is
after the 2D table at boot; the model is patched with its textures when the
level model table is built. A scan refuses a package whose model names a sprite
it does not ship, or whose sprite names a 2D texture past its own - the HUD
would otherwise draw another track's picture.

**Installed after boot.** Like its textures, a track's minimap only exists in a
session that booted with it. A track added by a rescan has its model's
placeholder reset to sprite 0, and its header is served with the **"no
minimap" bit** - byte `0xBC`, bit 0, the one hubs and cutscenes set, which
makes `hud_render_general` return before the minimap. The HUD shows no minimap
rather than a stray one until DKR-R is relaunched (`minimap_hidden`).

**Tracks without one.** The addon sets that same bit itself for a track that
ships no minimap and has no retail one to keep: a reshaped remix with no road
edges drawn, or a track built from a mesh. An imported retail track whose
geometry was not reshaped keeps its own minimap - no bit, no payloads. Track
Lab lists *own minimap* beside a track's artwork.

## A level has two object maps, and they must stay separate

`init_track` spawns from both, with different roles:

```c
init_track(geometry, skybox, players, vehicle, entrance,
           header->collectables,   // 0x36 -> track_spawn_objects(.., 1)
           header->unkBA);         // 0xBA -> track_spawn_objects(.., 0)
```

Ancient Lake splits as:

| Header field | Map | Contents |
|---|---|---|
| `0xBA` | 5 | 98 objects: checkpoints, spawn points, cameras, scenery |
| `0x36` | 73 | 86 objects: coins, weapon balloons, fish |

A manifest entry for `LEVEL_OBJECT_MAPS` therefore must carry a `slot`:

```json
{ "section": "LEVEL_OBJECT_MAPS", "slot": "structure",    "file": "objects_structure.bin" },
{ "section": "LEVEL_OBJECT_MAPS", "slot": "collectables", "file": "objects_collectables.bin" }
```

An entry without one is refused rather than guessed at. Exporting the two maps
merged into a single payload is the failure this guards: the 184 combined
objects would all spawn with the collectables flag, checkpoints and spawn
points included, while the retail structure map kept spawning beside them.

## The header's object map is patched, not authored

`LevelHeader` names its object map by index:

```c
/* 0x34 */ s16 geometry;      // ASSET_LEVEL_MODELS
/* 0x36 */ s16 collectables;  // ASSET_LEVEL_OBJECT_MAPS
```

Those indices do not exist until the extended tables are built, so an author
cannot write them. The runtime rewrites them as the header is served, pointing
each at what the same track supplied (`sibling_index`):

| Offset | Field | Patched when the track ships |
|---|---|---|
| `0x34` | `geometry` | a `LEVEL_MODELS` payload |
| `0x36` | `collectables` | `LEVEL_OBJECT_MAPS` slot `collectables` |
| `0xBA` | `unkBA` | `LEVEL_OBJECT_MAPS` slot `structure` |

**A track must not try to compute these**: whatever they contain is
overwritten. Everything else in the header, `skybox` included, is the author's
to write. A Phase 1 remix ships no model, so its authored `geometry` survives
and keeps pointing at the retail track it was built on.

Known ordering caveat: `objects.c` loads the object-map table *after* the
header. If a header is served before that table has been built even once,
`sibling_index` returns -1 and the field is left as authored rather than
filled with a guess.

## Installing

**MODS / HACKS → Import mods → DKR-R tracks** installs a copy. **Choose
.dkrmap folder** takes the `.dkrmap`, the track's own folder, or the folder that
holds both the `.dkrmap` and its `<track>-hd.zip`; **Choose track ZIP** takes a
`.zip` of the `.dkrmap` (optionally wrapping the pack too). Both run the system
picker off the graphics thread. The manifest is validated before anything is
copied, and the installed track is shown in **My mods**.

If the track declares an `hdTexturePack` and the matching `<track>-hd.zip` is
found beside it, DKR-R imports that pack in the same gesture -
`texture_packs::import_archive` with a `TrackPackOwner`, so the pack is born
enabled, filed against the track (`Origin::TrackPack`), and kept out of the
texture-pack browser's default list. A pack whose stamped `textureDigest` does
not match the manifest's is left out with a note; the track still plays, in
64x32.

Track Lab and the importer work in the Accurate profile as well. The list, the
import and arming all work there; what stays impossible in Accurate is a custom track
actually *loading*, because importing, arming or playing one switches the
profile to Modern first (with a note saying so). Copying a folder into
`custom-tracks/` by hand still works.

## Two traps worth knowing

**Policy edits need a recompile, not just a rebuild.** Patch Pipeline hooks are
injected while N64Recomp translates the ELF. Adding hooks to
`dkr.us.v77.recomp-policy.json` and rebuilding does nothing until N64Recomp
runs again for that revision - the generated `RecompiledFuncs` still carry the
old code. The first smoke test failed exactly this way: v80 had been
regenerated after the policy edit and v77 had not, and the run used v77.

**The versioned policy is not the whole policy.** The legacy mod hooks live in
`runtime-recomp/legacy-*.recomp-fragment.json` and are composed into a build
copy of each policy before N64Recomp runs; CMake refuses a payload that lacks
them. Editing a `.dkrmap` hook in the versioned policy therefore means
regenerating through the composer, which also checks that the legacy entry
hook and the `.dkrmap` recorder still share `asset_table_load` and `asset_load`
exactly as reviewed:

```text
python scripts/generate_legacy_menu_qualification.py --characters --character-menu
    --v77-build <folder with dkr.us.v77.elf and .z64>
    --v80-build <folder with dkr.us.v80.elf and .z64>
    --recompiler <N64Recomp.exe> --output build/legacy-generated
```

Point `DKR_GENERATED_SOURCE_V77` / `_V80` at `generated-v77` / `generated-v80`
in that folder. The presentation step accepts only the reviewed ELF hashes.

**`mods/` belongs to librecomp.** N64ModernRuntime ships its own mod system,
it is live in DKR-R, and it scans `mods/` for its `.nrm` format. A `.dkrmap`
placed there is reported as `Mod is missing a mod.json` to the user. Custom
tracks therefore live in `custom-tracks/` beside it.

**Regenerating the v80 policy loses v80-only entries.**
`scripts/generate_revision_policy.py` translates the v1.0 policy forward, so
anything that exists only in the v1.1 policy - the fourteen Rev A asset mutex
hooks and their instruction patches - is dropped. Merge them back after
regenerating, or Rev A silently loses its uncontended DMA fast path.

## Remaining work

1. **Removing an installed track.** Track Lab can enable/disable but not delete.
   When it can, it has to call `texture_packs::forget_track_pack(track_id)`
   alongside, or a track's HD pack is left enabled and ownerless. The runtime
   side of that call already exists.
2. **Detection also at arm time.** The HD pack sibling is resolved when a track
   is installed or rescanned, not when it is armed, so a track and its pack
   arriving separately (track first, pack later) is not picked up until the
   next rescan. Cheap to add.
3. **Online and Accurate policy.** Map identity is already part of the online
   contract (`dkr_netplay_gameplay_level_begin` seeds from the selected map),
   so a custom track must either enter the session handshake or be refused
   online. Custom tracks belong to Modern, as texture packs do.
