# DKR level model format

Reference notes for anyone writing a track geometry encoder. Every field below
was read out of a retail asset and cross-checked against the matching decomp's
structures; the layout arithmetic is self-consistent and is shown so it can be
re-verified.

Worked example throughout: Ancient Lake (`levels/models/dino_domain/`).

## Compressed container

Level assets are stored compressed. The container is five bytes followed by a
raw DEFLATE stream:

```text
bytes 0..3   decompressed size, u32 LITTLE endian
byte  4      0x09
bytes 5..    raw DEFLATE (no zlib or gzip wrapper)
```

Verified across every level model in the US v1.0 asset set: the declared size
matched the inflated size in all cases. Ancient Lake is 24,912 bytes
compressed and 49,644 inflated.

## LevelModel header

Pointer fields are byte offsets into the decompressed blob; the game fixes them
up after loading.

```text
0x00 textures            0x18 numberOfTextures        s16
0x04 segments            0x1A numberOfSegments        s16
0x08 boundingBoxes       0x1E numberOfAnimatedTextures s16
0x0C unkC                0x20 minimapSpriteIndex      s32
0x10 segmentsBitfields   0x28 minimapXScale/YScale    f32
0x14 segmentsBspTree     0x3C lowerXBounds .. bounds  s16
                         0x48 modelSize               s32
```

The header is 0x4C bytes. `modelSize` is the whole inflated length and the
loader treats it as the start of its scratch arena, so it is not optional; see
"What the file owns" below.

Ancient Lake: 25 textures, **24 segments**, 7 animated textures, bounds
X -5918..-23, Y -56..885, Z -12559..-2048.

The per-segment arrays confirm their own element sizes:

```text
boundingBoxes 0x9B60..0x9C80 = 288 bytes / 24 = 12  -> LevelModelSegmentBoundingBox
bspTree       0x9C80..0x9D40 = 192 bytes / 24 =  8  -> BspTreeNode
```

## Segments and the BSP tree

A `LevelModelSegment` is 0x44 bytes and points at its own vertex, triangle and
batch arrays. Bounding boxes tile the world, and the BSP splits on those
boundaries.

**There are no separate leaf nodes.** The earlier note here — that a leaf is a
node with `leftNode == rightNode == -1` — reads the tree as leaves holding
segments under internal nodes holding splits, and that is wrong. The array holds
**one node per segment**: `segmentIndex` names the segment the node *is*,
`splitType` and `splitValue` partition space at it, and `leftNode` / `rightNode`
are child node indices with `-1` meaning no subtree on that side. A node with
both children `-1` is simply a segment that subdivides nothing further.

Ancient Lake's root, and the two shapes:

```text
node  0: left=1  right=12  axis=X  segment=12  split=-2623   seg12 X -2623..-239
node 19: left=20 right=-1  axis=Z  segment=23  split=-3864   seg23 Z -3864..-2128
node  4: left=-1 right=-1  axis=X  segment=1   split=-4764   seg1  X -4764..-2623
```

Two things follow, both verified across every extracted model:

- **`segmentIndex` never repeats among the nodes reachable from node 0.** True
  in all 110 models. The array is sized `numberOfSegments`, so a tree that uses
  fewer leaves the rest as junk — only 10 of the 110 models reach every slot.
  Reading unreachable slots is what produces `splitType` values like 223 and 255.
- **`splitValue` is usually the node's own segment's lower box edge on the split
  axis** — 1854 of 2192 reachable nodes — but not always, so it is a
  construction habit rather than an invariant an encoder must reproduce.

### How the game walks it, and the one rule a builder must keep

`traverse_segments_bsp_tree` (`tracks.c`) never draws a node's own segment. It
is called on the root with the index run `[0, numberOfSegments - 1]`, and each
node splits the run it was handed at its `segmentIndex`: `[lo, seg - 1]` goes to
the left child, `[seg, hi]` to the right, and a side with no child adds `lo` or
`hi` - the one segment its run holds. The camera only decides which side is
walked first; both always are, so the set of segments drawn is the same from
every viewpoint.

So the tree has to cover **contiguous runs of segment indices**: every subtree
owns a run, and a run of one segment is a side with no child. `n` segments take
`n - 1` nodes, which is why 100 of the 110 retail models leave one slot
unreachable. Walked this way, every retail tree draws each of its segments
exactly once.

A tree that merely looks like retail - one node per segment, each naming itself
- does not survive the walk. It draws some segments twice and others never, and
a run that goes below zero adds `-1`: `add_segment_to_order` guards with a
signed `index < numberOfSegments`, the `u8 segmentIds[]` array stores 255, and
`render_level_segment` reads segment 255's struct out of the vertex data. That
is the crash a re-segmented track made before the addon's `build_bsp` was
rewritten; `level_model_layout.draw_order` is the walk, and the export rebuilds
any tree that fails it.

The same function bounds the segment count: it keeps the segments it draws in
`u8 segmentIds[128]` and clears `objectsVisible[1..numberOfSegments]` in an array
of 128, so a model holds at most **127** segments.

### The BSP does not contain its segments

It reads like a containment tree and it is not one. Walking every model and
checking each node's box against the half-spaces on its path from the root:
**199 of 3911 constraints are violated, in 34 of the 55 models**, by a median of
11 units and as much as 5440. It partitions the camera, not the geometry, so a
segment poking out of its own half-space is ordinary. Any check that a segment
stays inside its half-space would fire on most retail tracks; and a BSP built by
recursive median split would be no worse than the data the game ships.

## Batches, triangles, vertices

```text
DkrBatch     12 bytes   textureIndex (0xFF = none), verticesOffset,
                        trianglesOffset, lightSource, flags u32
DkrTriangle  16 bytes   flags (0x40 = draw backface), vi0..vi2, 3 x uv (s16,s16)
DkrVertex    10 bytes   x, y, z (s16) + r, g, b, a
```

Two rules an encoder must respect:

- **The batch list carries a terminator.** The entry after the last real batch
  holds the end offsets, so each batch's span is `batch[i+1] - batch[i]`. The
  same size-by-difference convention governs the asset tables.
- **Triangle vertex indices are batch-local, not segment-local.** Triangle 3 of
  Ancient Lake's segment 0 indexes `(0,1,2)` inside a batch whose window starts
  at vertex 5. Because the index is a `u8`, the format allows a batch to address
  256 vertices — but no retail batch comes near that. Across all 110 extracted
  models the widest is **24** and the mean is about 9, which reads like the RSP
  vertex buffer rather than the field width. An encoder should aim at retail's
  ceiling, not the format's.

Vertex colour is the baked lighting; level geometry carries no normals.

Each `DkrTriangle` also carries three UV pairs, `s16` fixed point with five
fractional bits and measured in **texels**, so a normalised coordinate is
`raw / 32 / texture_size`. Track surfaces tile heavily, so values well outside
0..1 are normal. `DkrBatch.textureIndex` selects from the model's own texture
table, an array of 8-byte `DkrTextureInfo` whose `id` indexes the global
`ASSET_TEXTURES_3D` list - the same indirection object models use.

Decoding all of that is what lets the Blender addon show a track as it looks
rather than as a grey shell; see `tools/blender/dkr_track_editor/level_model.py`.

## The texture table

```text
TextureInfo  8 bytes   id (s32), width, height, format, surfaceType (u8)
```

**A track is not scoped to the textures it shipped with.** `id` indexes the
global `ASSET_TEXTURES_3D` list, and `tracks.c` resolves the whole table at load
with `load_texture(id | 0x8000)` — the same call and the same list every object
model uses. Nothing anywhere ties an id to the level that references it, so a
custom track can name any of the ROM's textures.

**And it can add one.** A `.dkrmap` carries a `TEXTURES_3D` section, so a track
can ship artwork the ROM does not hold. Such an id is written as a *placeholder*
- `0x7000` plus the texture's ordinal in the package - because the real index is
the ROM's retail texture count plus that ordinal and the count belongs to the
player's cartridge. DKR-R substitutes it as the model is served, which is why
`level_model_encoder.pack` writes the header and this table as a **stored**
DEFLATE block: a field inside a compressed block has no byte offset to patch.
See `docs/CUSTOM_TRACKS.md`.

**Only two of the four trailing bytes are ever read, and they are the same
one.** Grepping the decomp for reads of `gCurrentLevelModel->textures[...]`
finds `surfaceType` in `tracks.c` and in `collision.c`, and nothing else.
`width`, `height` and `format` have **no reader**: the renderer takes all three
from the `TextureHeader` the texture asset carries (`texHeader->width` in
`tracks.c`), so the copies here are descriptive. They should still be written
correctly, and both follow from the asset:

- **Size** is the PNG's own, matching the table in 1358 of 1360 retail entries.
  The two that differ — a wall in Darkmoon Caverns, a fog texture in Wizpig 2 —
  declare a size larger than the image.
- **Format**'s low nibble is `TextureHeader.format`: 0 RGBA32, 1 RGBA16, 2 I8,
  3 I4, 4 IA16, 5 IA8, 6 IA4, 7 CI4, 8 CI8. It agrees with the extracted
  sidecar's `format` in all 1360. The high nibble takes 0x00, 0x10, 0x20 or
  0x30 and correlates with nothing in the asset — not the wrap flags, not the
  render mode, not the frame count — and has no reader either. (In the texture
  *asset*'s own header the same nibble is the render mode, `TEXTURE_RENDER_MODES`
  in the asset tool, and `material_init` does read it there. It is only this
  copy that means nothing.)

The largest retail table is Spaceport Alpha's 63 entries; the median track has
23. The ceiling is 255, since `textureIndex` is a `u8` and `0xFF` means none.

### `RENDER_TEX_ANIM` is a fact about the artwork

`RENDER_TEX_ANIM` (`1 << 16`) on a batch is set **exactly** when the texture it
draws has more than one frame. Across all 10,389 batches of the 55 level models
it is set on the 619 whose texture is animated and on none of the 9,770 whose
texture is not — no exceptions in either direction. `track_tex_anim` walks the
batches looking for the bit, so a batch given an animated texture without it
renders frozen on one frame. An encoder should derive it rather than carry it.

### `numberOfAnimatedTextures` is a gate, not a count

It is tested once, in `tracks.c`: `> 0` decides whether `track_tex_anim` is
called at all, and nothing indexes by it. It is **not** the number of animated
textures in the table — that reading fails on 35 of the 55 models, and Pirate
Lagoon declares 106 against a table of 26. So the value a track carries cannot
be reconstructed and should be left alone; a track being *given* its first
animated texture only needs the gate raised above zero.

## Segment visibility: `segmentsBitfields` is a PVS

`segmentsBitfields` holds one bitmask per segment saying which segments are
visible from it — a potentially visible set. Its size follows from the segment
count alone:

```text
numberOfSegments * ceil(numberOfSegments / 8) bytes
```

Verified against every extracted level model as the distance from
`segmentsBitfields` to `unkC`, which is exactly that figure up to the alignment
slack that follows it: Windmill Plains 288 for 48 segments, Wizpig 2 464 for 58,
Snowball Valley 504 for 63, Pirate Lagoon 1760 for 117 (rule: 1755).

It is read in `render_level_segments` (`tracks.c`) to skip segments the camera
cannot see, and written by the same file when a track opens or closes a route.
An encoder that keeps a model's segmentation keeps this array unchanged; one
that re-segments has to recompute it, and "every bit set" is the conservative
fallback, correct but paid for in draw calls.

## Collision is derived at load, into space the asset reserves

The earlier claim here — that `collisionFacets` and `collisionPlanes` are both
NULL in the asset — is **wrong for `collisionFacets`**, and the difference
matters to an encoder.

`collisionFacets` is a real offset that the loader fixes up like any other, in
the pointer-fixup loop after `gzip_inflate` in `tracks.c`:

```c
LOCAL_OFFSET_TO_RAM_ADDRESS(CollisionFacetPlanes *, gCurrentLevelModel->segments[k].collisionFacets);
```

So the asset **reserves the storage** — `numberOfTriangles * 8` bytes per
segment, laid out consecutively from `unkC` to the end of the blob, which is
where roughly a fifth of a level model goes. Measured across every extracted
model, the region from `unkC` to EOF is the sum of `numberOfTriangles * 8` plus
a little slack: Jungle Falls 20,740 against 20,736, Ancient Lake 9,316 against
9,248.

The *contents* are **authored, not uninitialised** — an earlier version of this
section said otherwise, and an encoder that believed it shipped tracks a racer
falls through. Each 8-byte `CollisionFacetPlanes` holds a triangle's
`basePlaneIndex` and, for each of its three edges, the plane index of the
triangle across that edge — its own index for an edge with no neighbour. In
retail, Ancient Lake's first triangle is `(0, 5, 0, 2)`.

`track_init_collision` (`tracks.c:3064-3223`) derives one plane per triangle
(skipping `TRI_FLAG_80`) into `collisionPlanes`, which genuinely is
runtime-only — assigned from the scratch arena past `modelSize`, never from the
file. Then it **reads** each facet: for every edge it takes the neighbour's
plane and builds the plane bounding that edge — the bisector of the two, or,
for an edge naming its own triangle, a wall straight up from the edge — and
rewrites the edge entry with the plane it made (marking the neighbour's
matching entry `| 0x8000` to share it). Zeroed facets point every triangle at
the first one's plane.

The mistake came from **object models**, whose loader does generate facets
itself when the pointer is NULL (`object_models.c:409`):

```c
if (model->collisionFacets != NULL) return;
/* ... count collidable faces, allocate facets and 16 floats each ... */
model->collisionFacets[s4].basePlaneIndex = s4;   /* then func_80060910 */
```

Its neighbour search (`func_80060AC8` / `func_80060C58`) is also the best
account of the rule the level model tool used: the first triangle in a
collidable batch with an edge on the same vertices or on corners within 3
units per axis, either way round. Applied to the 55 retail level models it
reproduces 99.6% of their 90,617 facets.

`level_model_layout.collision_facets` used to implement it and no longer does,
because the loader handles two kinds of pair badly:

- **A neighbour that runs the edge the same way round**, which means a flipped
  face. The loader hands it the first triangle's edge plane negated (the
  `| 0x8000`), and that puts it on the wrong side of its own edge. It stops
  colliding where it is and collides past the edge instead.
- **A neighbour folded back onto the triangle**, which means a double-sided
  face. The edge plane leans by the sum of the two normals, which is zero
  here. A zero plane passes every point, so the triangle collides over its
  whole plane. On a custom track, a double-sided sign by the road became an
  invisible wall across it.

The addon only pairs an edge with a triangle that runs it the other way round
and whose normal is no further than a dot of -0.95 from its own. Otherwise the
edge names its own triangle, which gives a wall straight up from the edge.
Under a transcription of `track_init_collision` and `resolve_collisions`, this
rule leaves no triangle of the 55 US models that fails to collide inside
itself, and no edge that collides 60 units past itself. Retail's own facets
have 113 of the first and 195 of the second. The new rule still agrees with
96.7% of retail's facets
(`tools/blender/tests/test_level_model_layout.py`).

**An encoder therefore has to write the facet array** — adjacency, not
collision planes — and correct offsets to it, and decide which batches are
solid. Sixteen floats per facet at runtime is four planes of `(A, B, C, D)`: the
triangle's own plane plus three edge planes. A batch opts out with
`RENDER_NO_COLLISION = 1 << 9` (`textures_sprites.h`), which shares bit 9 with
coverage because level geometry ignores coverage.

## What the file owns, and what the loader overwrites

The fixup loop in `tracks.c` settles this field by field, and it is the list an
encoder works from. Anything the loader assigns is scratch: the asset carries
whatever was in the build machine's memory, which is why `unk8` holds the same
value in every segment of every model.

| | Written by the encoder | Overwritten at load |
|---|---|---|
| `LevelModel` | `textures`, `segments`, `segmentsBoundingBoxes`, `unkC`, `segmentsBitfields`, `segmentsBspTree`, `modelSize` (0x48) | — |
| `LevelModelSegment` | `vertices`, `triangles`, `batches`, `collisionFacets`, the three counts, `numberofOpaqueBatches` | `unk10`, `collisionPlanes`, `unk30`, `unk32`, `unk34` |

`modelSize` is not decoration: the loader allocates its scratch arena starting
at `gCurrentLevelModel + modelSize`, so a wrong value has the game write
collision planes over the model it just loaded.

## Memory budget

`LEVEL_MODEL_MAX_SIZE` is `0x82A00` — 535,040 bytes — and it covers the inflated
blob *plus* everything the loader allocates past it. The dominant term is
collision: `collisionPlanes` is 16 floats per collidable facet, so each triangle
costs 8 bytes in the file and 64 bytes at runtime. Bluey, the largest retail
model, lands around 80% of the budget; the median track is near 34%. Triangle
count, not file size, is what runs a track out of memory, and
`track_load_model` only reports it as `ERROR!! TrackMem overflow`.

That is retail's ceiling. For a `.dkrmap` track DKR-R measures the same total
before the level loads and enlarges the reservation to match — see "The level
model heap" in `docs/CUSTOM_TRACKS.md`. The format ceilings behind it do not
move: a segment still cannot build more than 32,767 collision planes, because a
shared one is written as `index | 0x8000`.

## What an encoder actually has to do

| Task | Notes |
|---|---|
| Container | DEFLATE plus the five byte header |
| Header and texture table | direct field writes, `modelSize` included |
| Segment the mesh spatially | the author's choice of partition |
| Build the BSP over segments | standard axis/split-value tree |
| Batch triangles | group by texture and flags |
| Write collision facets | `numberOfTriangles * 8` per segment: each triangle's plane index and its edge neighbours (see above) |
| Recompute the PVS | only when segmentation changes |
| Collision planes | nothing to do; runtime derives them |

Two limits bound a batch. The vertex index is `u8` and **batch-local**, so the
format allows 256 vertices; but the widest batch in any of the 110 extracted
models is **24**, and the mean is around 9. The ceiling that matters in practice
is the retail one, not the field width.

The triangle count has a separate **16-triangle draw limit**. `gSPPolygon`
(`include/f3ddkr.h`) packs `count - 1` into four bits; the runtime reads
`((w0 >> 20) & 0xF) + 1`. A batch of 24 triangles therefore draws only eight,
even if its vertices fit. Every retail batch respects this limit. The builder
splits on both vertex and triangle counts, and the encoder refuses an oversized
draw batch. Exporting a mesh based on an older oversized model rebuilds its
batches instead of patching that layout in place.

`tools/blender/dkr_track_editor/level_model_encoder.py` implements the
layout-preserving half of this table, and
`tools/blender/tests/test_level_model_roundtrip.py` holds every extracted model
to byte equality through it.
`level_model_layout.py` is the other half — it lays a model out afresh when the
counts change — gated by `tests/test_level_model_layout.py`.

## Layout rules, for a builder that generates offsets

The section order is the same in all 55 models, and within a segment so is the
order of its three arrays:

```text
header < textures < segments < bbox < bsp < segmentsBitfields < unkC < facets
                    per segment:  batches < triangles < vertices
```

Whether the per-segment arrays are interleaved or grouped is *not* consistent —
31 models interleave and 19 group — so a builder is free to choose. Padding
between arrays is not derivable either: it runs 0, 4, 8, 10, 12 and on to 770
bytes with no rule, which is why an encoder can reproduce retail's layout only
by preserving it, never by regenerating it.

Three invariants a builder has to maintain, each measured across every extracted
model:

- **`modelSize` at 0x48 equals the inflated length.** All 55 models.
- **The batch terminator holds `(numberOfVertices, numberOfTriangles)`.** All
  1146 segments.
- **Batch windows tile a segment exactly.** Zero gaps and zero overlaps in all
  2292 segments, on both the vertex and the face windows. So no vertex is shared
  between batches, and a re-batcher must duplicate rather than share — which the
  `u8` batch-local index makes mandatory anyway.

## Opacity follows the texture's render mode

`numberofOpaqueBatches` is a split point: `render_level_segment` draws `[0, k)`
then `[k, n)`, and in each run it draws a batch only if the batch belongs to
that pass:

```text
first pass  <=>  (texture not RENDER_SEMI_TRANSPARENT and batch not RENDER_WATER)
                 or batch is RENDER_DECAL
```

The texture's `RENDER_SEMI_TRANSPARENT` comes from `material_init`: RGBA32,
RGBA16 and CI4 get it when the high nibble of `TextureHeader.format` - the render
mode - is `TRANSPARENT` or `TRANSPARENT_2`; IA16, IA8 and IA4 always get it; I8,
I4 and CI8 never do. So a batch on the wrong side is **never drawn**: its own
pass skips it and the other pass never reaches it.

Neither a flag bit nor the texture's *format* separates the two sides - that is
what an earlier version of this note measured, and why it called the side
authored. The *render mode* does. Held to every textured batch in both
extracted revisions (10,388 in US v1.0, hidden ones included), the rule puts
each on the side retail wrote. The one batch it disagrees with is untextured
and unflagged, in `volcano_track`, which the game never draws either, so an
encoder derives the side for textured batches and carries it for untextured
ones. `RENDER_CUTOUT` (bit 4) is independent of the side: it makes the batch
alpha-tested (`G_RM_AA_ZB_TEX_EDGE`), and retail sets it on 302 batches, all
over see-through textures.

Vertex alpha does not move a batch either, and it only works in the second
pass. `track_init_level_model` reads a vertex coloured `(1, 1, b)` as alpha `b`
and flags its batch `RENDER_VTX_ALPHA` (bit 27), and `material_set` then draws
it with `dRenderSettingsVtxAlpha`, whose entries blend **and write depth**. The
no-write half is chosen by `RENDER_Z_UPDATE`, bit 8, which on a batch is
`RENDER_HIDDEN`. The first pass walks the segments front to back, so a faded
batch there is drawn before what stands behind it, blends with the sky and then
hides the rest from the depth buffer. Retail leaves 195 faded batches in the
first pass, and in both revisions every one of them fades to alpha 0: an edge
vanishing into nothing, never a see-through surface. The second pass runs back
to front, after the solid track and the objects. The addon's material opacity
therefore writes a faded texture of the track's own as `TRANSPARENT` and flags
faded calm water `RENDER_WATER`, which is what retail's own water carries.

## Waves

A segment with `hasWaves` non-zero (retail writes -1) is a wave tile, and the
simulation in `waves.c` runs only if one exists and only in single player. A
batch flagged `RENDER_WATER | 0x400000` is the tile's water, and the Y of its
first vertex is the tile's water height. The first batch flagged
`0x1000000 | RENDER_WATER` and not hidden is the reference: its segment's
bounding box is the size of every tile and its texture is what the waves are
drawn with. `func_800BBF78` places **every** segment on that grid by
`((x1 - posX + 8) / w, (z1 - posZ + 8) / d)`, and a segment placed on a visible
wave tile draws a wave mesh there whether it has water or not - so retail cuts
its wave tracks into equal squares, one segment each, with the wave segment
first. That holds in 21 of the 22 retail models that have waves;
`ocean_track`, which no header loads, is the exception. The tile mask is
`s32 D_8012A0E8[64]`, so wave tiles sit in columns 0-31 and rows 0-63.
`tools/blender/dkr_track_editor/water.py` transcribes all of it.

## Two shapes Blender cannot round trip

Relevant only to the addon, not to the format:

- **Degenerate triangles.** Eight across the extracted set, four per revision —
  `smokey` segment 10, `darkmoon_caverns` segment 2, `bluey` segment 12,
  `temple_track` segment 22 — each naming one vertex twice. Blender's mesh
  structure has no room for one, so a model rebuilt from a Blender mesh loses
  them. A rebuild that stays in Python keeps them.
- **Duplicate faces.** 134 across the set, once batch-local indices are resolved
  to segment-local. These are fine: BMesh keeps them, and they survive an edit
  session unchanged.
