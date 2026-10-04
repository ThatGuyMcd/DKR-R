"""Water that moves: what a level model has to hold for the wave simulation.

Waves in DKR are not a material. ``waves.c`` runs a height-field simulation and
draws its own meshes, one per *tile*, over the water the model marks; the
marked water itself is only drawn when a tile is too far away for waves. This
module is everything the addon needs to know about that, measured against the
22 retail models that use it and read out of the decomp.

**What turns it on** (``init_track`` and ``func_800BBE08``/``func_800BBF78``):

* A segment with ``hasWaves`` set (retail writes -1) is a **wave tile**. With
  none, the simulation never starts. It only starts in single player:
  ``init_track`` skips the count when two or more people play, and split screen
  shows the marked water flat.
* A batch flagged ``RENDER_WATER | RENDER_WAVES`` (bits 13 and 22) is a tile's
  water. The Y of its first vertex is the tile's water height - the last such
  batch in the segment wins - and each tile can have its own. Retail's is one
  flat quad, two triangles, as large as the tile.
* The first batch flagged ``RENDER_WAVE_REFERENCE | RENDER_WATER`` (bit 24) and
  not hidden is the **reference**. Its segment's bounding box is the size of
  *every* tile, and its texture is the one the waves are drawn with. Without
  one the game reads a texture through a null batch: the track does not load.
* The header's bytes ``0x56``-``0x71`` shape the waves; :data:`HEADER_FIELDS`.

**The grid is over the whole model, not over the water.** Every segment -
water or not - is placed on the grid by the ``(x1, z1)`` corner of its box,
nudged by 8 and rounded down. When the camera is near a wave tile, *every*
segment placed on that tile is added to the list of wave meshes to draw, and
the height of each comes from its own water batch. So a dry segment that lands
on a wave tile draws a second wave mesh there, at height zero, with vertices
left over from another tile. Retail avoids that the only way the format
allows: its wave tracks are cut into a grid of equal squares, one segment per
square, and it holds across 21 of the 22 models - one segment per wave tile,
no other segment on one, and the wave segment first. ``ocean_track.bin``,
which no header loads, is the exception. :func:`problems` checks all of it and
:func:`level_model_layout.resegment` builds it.

**Limits.** The tile mask ``D_8012A0E8`` is 64 rows of ``s32``, so wave tiles
must sit in columns 0-31 and rows 0-63 of the grid, counted from the model's
low corner. The model is still limited to 127 segments, and the wave tiles are
segments. In translucent mode (``wavesXlu``, every retail track but Hot Top
Volcano) ``wave_load_material`` loads both the reference's texture and the
header's detail texture as square 16 or 32 texel RGBA blocks, and anything else
leaves its mask unset.

**Calm water is simpler.** A batch flagged ``RENDER_WATER`` without bit 22 gives
its segment a flat water plane at the height of its first vertex
(``func_8002C71C`` and ``get_level_segment_waves``); a texture whose surface type
is ``SURFACE_WATER_CALM`` does too, even hidden. Retail's is ``0x12205``.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

RENDER_ANTI_ALIASING = 1 << 0
RENDER_SEMI_TRANSPARENT = 1 << 2
RENDER_HIDDEN = 1 << 8
RENDER_NO_COLLISION = 1 << 9
RENDER_WATER = 1 << 13
RENDER_TEX_ANIM = 1 << 16
#: ``RENDER_UNK_0400000``: the batch is a wave tile's water.
RENDER_WAVES = 1 << 22
#: ``RENDER_UNK_1000000``: the batch whose segment sizes every tile.
RENDER_WAVE_REFERENCE = 1 << 24

#: Retail's wave water, less the animation bit, which follows the texture:
#: anti-aliased, blended, not solid, water, a wave tile. ``0x412205`` on 313
#: batches with an animated texture and ``0x402205`` on the trophy race's flag.
WAVY_FLAGS = (RENDER_ANTI_ALIASING | RENDER_SEMI_TRANSPARENT
              | RENDER_NO_COLLISION | RENDER_WATER | RENDER_WAVES)
#: Retail's still water, less the animation bit: ``0x12205`` on Ancient Lake.
CALM_FLAGS = (RENDER_ANTI_ALIASING | RENDER_SEMI_TRANSPARENT
              | RENDER_NO_COLLISION | RENDER_WATER)

#: What retail writes into ``hasWaves``; ``init_track`` only asks for non-zero.
HAS_WAVES = -1

#: ``D_8012A0E8[64]`` of ``s32``: one bit a column, one word a row.
MAX_COLUMNS = 32
MAX_ROWS = 64

#: The slack ``func_800BBF78`` gives a box corner before rounding it down.
CORNER_NUDGE = 8

#: Retail's water, white and fully lit.
WATER_COLOUR = (255, 255, 255, 255)

#: The textures ``wave_load_material`` can load, in translucent mode.
WAVE_TEXTURE_SIDES = (16, 32)
WAVE_TEXTURE_FORMATS = (0, 1)  # RGBA32, RGBA16

#: The ROM textures retail draws its water with, by asset id.
DEFAULT_WAVY_TEXTURE = "ASSET_TEX3D_WATER_WATER4"
DEFAULT_CALM_TEXTURE = "ASSET_TEX3D_WATER_WATER5"
#: The header's detail texture, a 2D asset. Every retail header but the
#: trophy race's names this one.
DETAIL_TEXTURE = "ASSET_TEX2D_WATER_DETAIL"

#: Tile sizes offered, in map units. Retail's run from 828 (Pirate Lagoon) to
#: 2560 (Wizpig), with 1920 the commonest.
TILE_SIZES = (1024, 1280, 1600, 1920, 2048, 2560, 3200, 4096, 5120, 6400)
DEFAULT_TILE = 1920

#: Segments kept free when a tile size is chosen, for the ones that do not fit
#: a tile's count exactly - the slivers :func:`level_model_layout.resegment`
#: folds into their neighbours.
TILE_HEADROOM = 12


class WaterError(Exception):
    pass


def is_water(flags) -> bool:
    return bool(int(flags) & RENDER_WATER)


def is_wavy(flags) -> bool:
    flags = int(flags)
    return bool(flags & RENDER_WATER and flags & RENDER_WAVES)


def is_reference(flags) -> bool:
    flags = int(flags)
    return (flags & (RENDER_WAVE_REFERENCE | RENDER_WATER | RENDER_HIDDEN)
            == (RENDER_WAVE_REFERENCE | RENDER_WATER))


def water_flags(wavy: bool, animated: bool) -> int:
    flags = WAVY_FLAGS if wavy else CALM_FLAGS
    return flags | (RENDER_TEX_ANIM if animated else 0)


def wave_texture_problem(width, height, texture_format) -> Optional[str]:
    """Why a texture cannot draw translucent waves, or ``None``."""
    if int(texture_format) & 0xF not in WAVE_TEXTURE_FORMATS:
        return ("the waves load it as an RGBA block, so it has to be RGBA16 "
                "or RGBA32")
    if int(width) != int(height) or int(width) not in WAVE_TEXTURE_SIDES:
        return ("the waves load it as a square of 16 or 32 texels, and it is "
                "%dx%d" % (int(width), int(height)))
    return None


# ---------------------------------------------------------------------------
# The game's grid
# ---------------------------------------------------------------------------

class WaveGrid:
    """What ``func_800BBE08`` and ``func_800BBF78`` compute from a model."""

    __slots__ = ("reference", "tile_w", "tile_h", "origin_x", "origin_z",
                 "columns", "rows", "tiles", "wavy", "heights")

    def __init__(self):
        #: The reference segment, or ``None`` if the model has none.
        self.reference: Optional[int] = None
        self.tile_w = 0
        self.tile_h = 0
        #: ``gWaveBlockPosX``/``Z``: the grid line at or below the model's
        #: lowest box corner.
        self.origin_x = 0
        self.origin_z = 0
        self.columns = 0
        self.rows = 0
        #: ``(column, row)`` per segment, where the game places it.
        self.tiles: List[Tuple[int, int]] = []
        #: ``hasWaves`` per segment.
        self.wavy: List[bool] = []
        #: The water height the game reads per segment, or ``None``.
        self.heights: List[Optional[int]] = []

    @property
    def valid(self) -> bool:
        return self.tile_w > 0 and self.tile_h > 0

    def wave_tiles(self) -> List[Tuple[int, int]]:
        return [tile for tile, wavy in zip(self.tiles, self.wavy) if wavy]


def reference_segment(model) -> Optional[int]:
    """The segment ``func_800BBE08`` takes the tile size from, or ``None``."""
    for index, segment in enumerate(model.segments):
        for batch in segment.batches:
            if is_reference(batch.flags):
                return index
    return None


def simulate(model) -> Optional[WaveGrid]:
    """The grid as the game would build it, or ``None`` for a model with no boxes.

    Integer arithmetic throughout, as the game's: every quotient here is of a
    non-negative number, so C's truncation and Python's floor agree.
    """
    boxes = list(model.bounding_boxes)
    if not boxes:
        return None
    grid = WaveGrid()
    grid.reference = reference_segment(model)
    box = boxes[grid.reference if grid.reference is not None else 0]
    grid.tile_w = int(box[3]) - int(box[0])
    grid.tile_h = int(box[5]) - int(box[2])
    grid.wavy = [bool(segment.has_waves) for segment in model.segments]
    grid.heights = []
    for segment in model.segments:
        height = None
        for batch in segment.batches:
            if is_wavy(batch.flags) and 0 <= batch.vertex_offset < len(segment.vertices):
                height = segment.vertices[batch.vertex_offset][1]
        grid.heights.append(height)
    if not grid.valid:
        return grid

    low_x = min(int(b[0]) for b in boxes)
    high_x = max(int(b[3]) for b in boxes)
    low_z = min(int(b[2]) for b in boxes)
    high_z = max(int(b[5]) for b in boxes)
    grid.origin_x = int(box[0])
    while low_x < grid.origin_x:
        grid.origin_x -= grid.tile_w
    grid.origin_z = int(box[2])
    while low_z < grid.origin_z:
        grid.origin_z -= grid.tile_h
    grid.columns = (high_x - grid.origin_x) // grid.tile_w + 1
    grid.rows = (high_z - grid.origin_z) // grid.tile_h + 1
    grid.tiles = [
        ((int(b[0]) - grid.origin_x + CORNER_NUDGE) // grid.tile_w,
         (int(b[2]) - grid.origin_z + CORNER_NUDGE) // grid.tile_h)
        for b in boxes
    ]
    return grid


def has_waves(model) -> bool:
    """Whether the model holds any wave water, flagged or switched on."""
    return any(segment.has_waves for segment in model.segments) or any(
        is_wavy(batch.flags)
        for segment in model.segments for batch in segment.batches)


def problems(model) -> List[str]:
    """What stops the waves working in this model, worst first. Empty if nothing.

    Only what cutting the track into the grid again repairs, and only what no
    retail track ships with - an export re-cuts a track this finds anything
    in, so a retail remix must come back empty. :func:`notes` has the rest.
    """
    if not has_waves(model):
        return []
    found = []
    tiles = [index for index, segment in enumerate(model.segments)
             if segment.has_waves]
    if not tiles:
        found.append(
            "the model has wave water but no segment has hasWaves set, so the "
            "wave simulation never starts; re-segment the track")
        return found

    grid = simulate(model)
    if grid is None or grid.reference is None:
        found.append(
            "no wave water is marked as the reference (flag bit 24), and the "
            "game reads the wave texture through it - the track would not "
            "load. Re-segment the track to pick one")
        return found
    if not grid.valid:
        found.append(
            "the reference segment %d has a box %dx%d wide, so every wave tile "
            "would be that size" % (grid.reference, grid.tile_w, grid.tile_h))
        return found

    placed: Dict[Tuple[int, int], List[int]] = {}
    for index, tile in enumerate(grid.tiles):
        placed.setdefault(tile, []).append(index)
    for index in tiles:
        tile = grid.tiles[index]
        column, row = tile
        if not 0 <= column < MAX_COLUMNS or not 0 <= row < MAX_ROWS:
            found.append(
                "wave tile %d sits in column %d, row %d of the grid, and the "
                "game can only mark columns 0-%d and rows 0-%d; use larger "
                "tiles" % (index, column, row, MAX_COLUMNS - 1, MAX_ROWS - 1))
            break
    for tile, owners in sorted(placed.items()):
        wavy = [index for index in owners if grid.wavy[index]]
        if not wavy:
            continue
        if len(owners) > 1:
            found.append(
                "segments %s all sit on wave tile %s; the game draws a wave "
                "mesh for each of them there, and all but one float at the "
                "wrong height. Re-segment the track"
                % (", ".join(str(i) for i in owners[:6]), tile))
            break

    return found


#: How far past its tile a wave segment's box may reach before it is worth
#: saying. Retail's widest is Boulder Canyon's, ten units over.
SPILL_SLACK = 16


def notes(model, reference_texture=None, translucent=True) -> List[str]:
    """What is odd about the waves but does not stop them. Empty if nothing.

    ``reference_texture`` is ``(width, height, format)`` of the texture the
    reference batch draws, when the caller can see it; ``translucent`` is the
    header's ``wavesXlu``, which is when that texture's shape matters.
    """
    if not has_waves(model):
        return []
    found = []
    grid = simulate(model)
    if grid is None or not grid.valid:
        return found
    # A wave tile with no wave water of its own draws its waves at height 0.
    # Retail ships four (Pirate Lagoon's 15, 75 and 78, and one in the Bubbler
    # cutscene), under ground, so it is not said here; the export switches
    # waves off on a tile whose water the author deleted.
    for index, segment in enumerate(model.segments):
        if not segment.has_waves:
            continue
        box = model.bounding_boxes[index]
        if (box[3] - box[0] > grid.tile_w + SPILL_SLACK
                or box[5] - box[2] > grid.tile_h + SPILL_SLACK):
            found.append(
                "wave tile %d is %dx%d and the tiles are %dx%d; its waves cover "
                "only its own tile" % (index, box[3] - box[0], box[5] - box[2],
                                       grid.tile_w, grid.tile_h))
            break
    if reference_texture is not None and translucent:
        problem = wave_texture_problem(*reference_texture)
        if problem:
            found.append("the waves are drawn with the reference water's "
                         "texture, and %s; the game would load it with a "
                         "mask it never set" % problem)
    return found


def reference_extent(model, index) -> Optional[Tuple[int, int, int, int]]:
    """``(x1, z1, width, depth)`` of a segment's wave water, or ``None``."""
    segment = model.segments[index]
    xs, zs = [], []
    for batch in segment.batches:
        if not is_wavy(batch.flags):
            continue
        for vertex in segment.vertices[batch.vertex_offset:
                                       batch.vertex_offset + batch.vertex_count]:
            xs.append(vertex[0])
            zs.append(vertex[2])
    if not xs:
        return None
    return min(xs), min(zs), max(xs) - min(xs), max(zs) - min(zs)


# ---------------------------------------------------------------------------
# Laying out tiles
# ---------------------------------------------------------------------------

def grid_origin(value: float, tile: int) -> int:
    """Where a new grid starts: on the track's own low edge.

    Anchoring there rather than at zero lays whole squares over the track, so
    a track as wide as three tiles takes three, not four with a part-tile
    hanging off each side. The game does not care where the grid starts - it
    measures from the reference - so the tile size is all that is shared.
    """
    del tile
    return int(math.floor(float(value)))


def cell_of(value: float, origin: int, tile: int) -> int:
    return int(math.floor((float(value) - origin) / tile))


def cells_covering(low: float, high: float, origin: int, tile: int) -> range:
    """Every cell index a span ``[low, high]`` touches."""
    first = cell_of(low, origin, tile)
    last = cell_of(high, origin, tile)
    if last > first and origin + last * tile == high:
        last -= 1  # ends exactly on a line: the cell beyond is not touched
    return range(first, last + 1)


def tile_quad(column: int, row: int, tile_w: int, tile_h: int,
              origin_x: int, origin_z: int, height: int):
    """``(corners, triangles)`` for one tile's water, in map space.

    Two triangles, wound so that they face up (+Y), which is what retail's
    are and what the collision planes the game derives expect.
    """
    x1 = origin_x + column * tile_w
    z1 = origin_z + row * tile_h
    x2, z2 = x1 + tile_w, z1 + tile_h
    corners = [(x1, height, z1), (x2, height, z1), (x2, height, z2),
               (x1, height, z2)]
    # Map space is Y-up with +Z towards the viewer of a top-down map, so
    # (x1,z1) -> (x1,z2) -> (x2,z2) turns counter-clockwise seen from above.
    triangles = [(0, 3, 2), (0, 2, 1)]
    return corners, triangles


def occupied_cells(points: Sequence[Sequence[Sequence[float]]],
                   tile: int, origin_x: int, origin_z: int) -> set:
    """Cells any triangle's footprint touches. ``points`` are triangles."""
    cells = set()
    for triangle in points:
        xs = [p[0] for p in triangle]
        zs = [p[2] for p in triangle]
        for column in cells_covering(min(xs), max(xs), origin_x, tile):
            for row in cells_covering(min(zs), max(zs), origin_z, tile):
                cells.add((column, row))
    return cells


def choose_tile(triangles, water_rect, limit: int,
                sizes: Sequence[int] = TILE_SIZES) -> Tuple[int, int]:
    """``(tile, cells)``: the smallest tile the whole track fits under.

    Every cell the track touches becomes a segment once the track is cut into
    the grid, so the count has to stay under ``limit`` - the segment ceiling
    less some headroom - and the water has to stay inside the 32 columns the
    game can mark. Smaller tiles give finer wave detail and cheaper segments,
    so the smallest that fits wins; ``(0, n)`` if none does.
    """
    low_x = min([p[0] for t in triangles for p in t] + [water_rect[0]])
    low_z = min([p[2] for t in triangles for p in t] + [water_rect[1]])
    for tile in sizes:
        origin_x = grid_origin(low_x, tile)
        origin_z = grid_origin(low_z, tile)
        cells = occupied_cells(triangles, tile, origin_x, origin_z)
        for column in cells_covering(water_rect[0], water_rect[2], origin_x, tile):
            for row in cells_covering(water_rect[1], water_rect[3], origin_z, tile):
                cells.add((column, row))
        columns = {column for column, _row in cells}
        rows = {row for _column, row in cells}
        if (len(cells) <= limit and max(columns) < MAX_COLUMNS
                and max(rows) < MAX_ROWS):
            return tile, len(cells)
    return 0, len(cells)


# ---------------------------------------------------------------------------
# Laying water
# ---------------------------------------------------------------------------

#: How many times the texture repeats across one tile of water. Retail's wave
#: water carries about four repeats of its 16-texel texture a tile, and the
#: UVs of that are well inside what an s16 holds.
REPEATS = 4


def triangles_of(model) -> List[List[Tuple[int, int, int]]]:
    """Every triangle in the model, as three map-space corners."""
    found = []
    for segment in model.segments:
        for batch in segment.batches:
            base = batch.vertex_offset
            for face in range(batch.face_offset, batch.face_offset + batch.face_count):
                if face >= len(segment.triangles):
                    break
                _flags, a, b, c = segment.triangles[face][:4]
                try:
                    found.append([segment.vertices[base + i] for i in (a, b, c)])
                except IndexError:
                    continue
    return found


def wet_cells(model, layout) -> set:
    """Squares of the grid that already hold water of any kind."""
    width, depth, origin_x, origin_z = layout
    cells = set()
    for segment in model.segments:
        for batch in segment.batches:
            if not is_water(batch.flags):
                continue
            window = segment.vertices[batch.vertex_offset:
                                      batch.vertex_offset + batch.vertex_count]
            if not window:
                continue
            x = min(v[0] for v in window) + CORNER_NUDGE
            z = min(v[2] for v in window) + CORNER_NUDGE
            cells.add(((x - origin_x) // width, (z - origin_z) // depth))
    return cells


class Laid:
    """What :func:`lay_water` did, for a report."""

    __slots__ = ("tiles", "dry", "held", "layout", "segments", "chosen",
                 "texture_index")

    def __init__(self):
        self.tiles = 0
        self.dry = 0
        self.held = 0
        #: ``(tile width, tile depth, origin x, origin z)``.
        self.layout = None
        self.segments = 0
        #: Whether the tile size was picked rather than asked for.
        self.chosen = False
        self.texture_index = -1


def lay_water(model, wavy: bool, level, rect, texture, tile: int = 0,
              skip_dry: bool = True, reserved=()) -> Laid:
    """Lay water over ``rect`` at ``level``, and cut the model to fit. In place.

    ``rect`` is ``(x1, z1, x2, z2)`` and ``level`` a height, both in map space.
    ``texture`` is anything with ``index``, ``width``, ``height``, ``format`` and
    ``frames`` - a ROM texture or one of the track's own.

    One flat quad a square of the grid, white, with the texture projected four
    repeats a square and the flags retail gives its own water. With waves the
    grid is the model's own if it has one, and the whole model is cut into it
    afterwards; calm water rides whatever grid it is given and only needs the
    usual segmentation. A square that already holds water is left alone, and so,
    with ``skip_dry``, is one where the ground stands above the water all over.
    """
    from . import level_model_edit, level_model_layout, textures  # noqa: PLC0415

    laid = Laid()
    level = int(round(float(level)))
    x1, z1, x2, z2 = (float(v) for v in rect)
    if x2 <= x1 or z2 <= z1:
        raise WaterError("the area to cover has no size")

    layout = level_model_layout.wave_layout(model) if has_waves(model) else None
    if layout is None:
        corners = triangles_of(model)
        if tile <= 0:
            limit = level_model_layout.MAX_SEGMENTS - TILE_HEADROOM
            tile, _cells = choose_tile(corners, (x1, z1, x2, z2), limit)
            laid.chosen = True
            if tile <= 0:
                raise WaterError(
                    "no tile size up to %d keeps this track under the %d "
                    "segments a model holds; cover a smaller area"
                    % (TILE_SIZES[-1], level_model_layout.MAX_SEGMENTS))
        low_x = min([p[0] for t in corners for p in t] + [x1])
        low_z = min([p[2] for t in corners for p in t] + [z1])
        layout = (int(tile), int(tile), grid_origin(low_x, tile),
                  grid_origin(low_z, tile))
    elif tile > 0 and tile != layout[0]:
        raise WaterError(
            "this track already has waves on %dx%d tiles, and every tile has "
            "to be the same size; leave the tile size on Auto"
            % (layout[0], layout[1]))
    laid.layout = layout
    width, depth, origin_x, origin_z = layout

    ground = Ground(model) if skip_dry else None
    held = wet_cells(model, layout)
    table = list(model.textures)
    gate = model.animated_texture_count
    try:
        index = level_model_edit.add_texture(
            model, texture.index, texture.width, texture.height,
            texture.format, 0, reserved=reserved)
    except level_model_edit.EditError as error:
        raise WaterError(str(error))
    laid.texture_index = index
    animated = int(getattr(texture, "frames", 1) or 1) > 1
    if animated:
        level_model_edit.set_animation_gate(model, 1)
    key = level_model_layout.BatchKey(index, water_flags(wavy, animated),
                                      0, 0, 0, False, None)

    faces, positions, colours = [], [], []
    scale = float(width) / REPEATS
    for column in cells_covering(x1, x2, origin_x, width):
        for row in cells_covering(z1, z2, origin_z, depth):
            if (column, row) in held:
                laid.held += 1
                continue
            corners, triangles = tile_quad(column, row, width, depth,
                                           origin_x, origin_z, level)
            if ground is not None and ground.dry(
                    corners[0][0], corners[0][2], corners[2][0], corners[2][2],
                    level):
                laid.dry += 1
                continue
            base = len(positions)
            positions.extend(corners)
            colours.extend([WATER_COLOUR] * 4)
            uvs = textures.project_face(corners, texture.width, texture.height,
                                        scale)
            for a, b, c in triangles:
                faces.append(level_model_layout.Face(
                    key, (base + a, base + b, base + c), (uvs[a], uvs[b], uvs[c])))
            laid.tiles += 1

    if not faces:
        model.textures = table
        model.animated_texture_count = gate
        raise WaterError(
            "there is nowhere to put water: %d square(s) are dry at that "
            "height and %d already hold water. Raise the water, or turn off "
            "Skip Dry Ground" % (laid.dry, laid.held))

    level_model_layout.add_faces(model, faces, positions, colours)
    try:
        laid.segments = level_model_layout.resegment(
            model, wave_grid=layout if (wavy or has_waves(model)) else None)
    except level_model_layout.LayoutError as error:
        message = str(error)
        if laid.chosen or "segments" in message:
            message += "; try a larger tile, or cover less of the track at once"
        raise WaterError(message)
    return laid


# ---------------------------------------------------------------------------
# Where the ground is
# ---------------------------------------------------------------------------

class Ground:
    """The heights of a model's solid, drawn ground, for asking "is this dry?".

    Built from the batches a racer can stand on and an author can see - not
    hidden, not water, and colliding - and bucketed so a question about one
    point looks at a handful of triangles.
    """

    BUCKET = 512

    def __init__(self, model):
        self._buckets: Dict[Tuple[int, int], List] = {}
        for segment in model.segments:
            for batch in segment.batches:
                flags = int(batch.flags)
                if flags & (RENDER_HIDDEN | RENDER_NO_COLLISION | RENDER_WATER):
                    continue
                base = batch.vertex_offset
                for face in range(batch.face_offset,
                                  batch.face_offset + batch.face_count):
                    if face >= len(segment.triangles):
                        break
                    _flags, a, b, c = segment.triangles[face][:4]
                    try:
                        corners = [segment.vertices[base + i] for i in (a, b, c)]
                    except IndexError:
                        continue
                    self._add(corners)

    def _add(self, corners):
        xs = [p[0] for p in corners]
        zs = [p[2] for p in corners]
        for bx in range(int(min(xs)) // self.BUCKET, int(max(xs)) // self.BUCKET + 1):
            for bz in range(int(min(zs)) // self.BUCKET,
                            int(max(zs)) // self.BUCKET + 1):
                self._buckets.setdefault((bx, bz), []).append(corners)

    def height(self, x: float, z: float) -> Optional[float]:
        """The highest ground at ``(x, z)``, or ``None`` over nothing."""
        best = None
        for corners in self._buckets.get((int(x) // self.BUCKET,
                                          int(z) // self.BUCKET), ()):
            y = _height_in(corners, x, z)
            if y is not None and (best is None or y > best):
                best = y
        return best

    def dry(self, x1, z1, x2, z2, level, samples: int = 8) -> bool:
        """Whether ground stands above ``level`` everywhere in a rectangle."""
        for i in range(samples):
            for j in range(samples):
                x = x1 + (i + 0.5) * (x2 - x1) / samples
                z = z1 + (j + 0.5) * (z2 - z1) / samples
                y = self.height(x, z)
                if y is None or y <= level:
                    return False
        return True


def _height_in(corners, x, z) -> Optional[float]:
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = corners
    denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
    if denominator == 0:
        return None
    u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / denominator
    v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / denominator
    w = 1.0 - u - v
    if u < -1e-6 or v < -1e-6 or w < -1e-6:
        return None
    return u * ay + v * by + w * cy


# ---------------------------------------------------------------------------
# The header
# ---------------------------------------------------------------------------

class HeaderField:
    """One wave byte of the level header, described for a panel."""

    __slots__ = ("pointer", "name", "label", "help", "minimum", "maximum",
                 "toggle")

    def __init__(self, pointer, name, label, help_text, minimum, maximum,
                 toggle=False):
        self.pointer = pointer
        self.name = name
        self.label = label
        self.help = help_text
        self.minimum = minimum
        self.maximum = maximum
        self.toggle = toggle


#: The header's wave bytes an author can reasonably set, with what they do in
#: ``waves_init_header`` and ``waves_update``. The texture id is kept apart: it
#: names a 2D asset, which only an asset tree can turn into a number.
HEADER_FIELDS = (
    HeaderField("/waves/wave-power", "power", "Wave Height",
                "How tall the waves are, in 256ths: every height is multiplied "
                "by it. 256 in most of retail, 128 on Pirate Lagoon", 0, 2048),
    HeaderField("/waves/subdivisions", "subdivisions", "Detail",
                "How many squares a side each tile's wave mesh has. Retail uses "
                "2 to 8; split screen always uses 4", 1, 8),
    HeaderField("/waves/UV-Scroll-X", "scroll_x", "Flow X",
                "How fast the water texture slides along X, every frame. "
                "Negative flows the other way", -128, 127),
    HeaderField("/waves/UV-Scroll-Y", "scroll_y", "Flow Z",
                "How fast the water texture slides along Z, every frame", -128, 127),
    HeaderField("/waves/UV-Scale-X", "scale_x", "Repeats X",
                "How many times the water texture repeats across a tile", 1, 16),
    HeaderField("/waves/UV-Scale-Y", "scale_y", "Repeats Z",
                "How many times the water texture repeats down a tile", 1, 16),
    HeaderField("/waves/unk70", "translucent", "Translucent",
                "Blend the waves with the detail texture over what is beneath. "
                "Off draws them solid, as Hot Top Volcano's lava is", 0, 1,
                toggle=True),
    HeaderField("/waves/unk71", "double", "Double Density",
                "Split every tile in four for the wave mesh, for finer waves "
                "near the camera", 0, 1, toggle=True),
    HeaderField("/waves/view-distance", "view", "Near Tiles",
                "How many tiles across get real waves around the camera: 3, or "
                "5 for anything else. Further tiles draw their water flat", 3, 5),
    HeaderField("/waves/sine-height-0", "height0", "Swell Height",
                "The first wave's height, in 256ths", 0, 8192),
    HeaderField("/waves/sine-step-0", "step0", "Swell Frequency",
                "How many of the first wave fit the pattern", 1, 8),
    HeaderField("/waves/sine-height-1", "height1", "Chop Height",
                "The second wave's height, in 256ths", 0, 8192),
    HeaderField("/waves/sine-step-1", "step1", "Chop Frequency",
                "How many of the second wave fit the pattern", 1, 8),
    HeaderField("/waves/seed-size", "seed", "Pattern Length",
                "How many heights the pattern cycles through. Always even", 2, 512),
    HeaderField("/waves/unk57", "pattern", "Pattern Tiles",
                "How many tiles the random pattern spans before it repeats", 1, 32),
    HeaderField("/waves/unk64", "shore", "Shore Calm",
                "How much of a wave survives where it meets the shore, in "
                "256ths. 153 in retail", 0, 256),
    HeaderField("/waves/unk66", "crest", "Crest Light",
                "How much a crest lights up in translucent water, in 256ths", 0, 4096),
)

HEADER_POINTERS = tuple(field.pointer for field in HEADER_FIELDS)
DETAIL_POINTER = "/waves/texture-ID"

#: Retail's wave settings, by the track they come from. Read out of the
#: extracted headers; ``tests/test_water.py`` holds them to those files.
PRESETS = (
    ("LAKE", "Haunted Woods", "HauntedWoods",
     "Gentle. What most retail headers carry, waves or not"),
    ("WINDMILL", "Windmill Plains", "WindmillPlains", "A river, flowing"),
    ("WHALE", "Whale Bay", "WhaleBay", "Open sea, rolling and still"),
    ("PIRATE", "Pirate Lagoon", "PirateLagoon", "A choppy lagoon"),
    ("CRESCENT", "Crescent Island", "CrescentIsland", "High, slow swell"),
    ("DARKWATER", "Darkwater Beach", "DarkwaterBeach", "Surf"),
    ("BOULDER", "Boulder Canyon", "BoulderCanyon", "A fast river"),
    ("LAVA", "Hot Top Volcano", "HotTopVolcano",
     "Solid, heaving lava; pair it with a lava texture"),
)

PRESET_VALUES = {
    "LAKE": {"UV-Scale-X": 1, "UV-Scale-Y": 1, "UV-Scroll-X": 1,
             "UV-Scroll-Y": 1, "seed-size": 120, "sine-height-0": 1536,
             "sine-height-1": 768, "sine-step-0": 1, "sine-step-1": 2,
             "subdivisions": 4, "unk57": 16, "unk64": 153, "unk66": 1024,
             "unk70": 1, "unk71": 1, "view-distance": 5, "wave-power": 256},
    "WINDMILL": {"UV-Scale-X": 4, "UV-Scale-Y": 5, "UV-Scroll-X": 3,
                 "UV-Scroll-Y": 1, "seed-size": 120, "sine-height-0": 1536,
                 "sine-height-1": 768, "sine-step-0": 1, "sine-step-1": 2,
                 "subdivisions": 4, "unk57": 16, "unk64": 153, "unk66": 1024,
                 "unk70": 1, "unk71": 0, "view-distance": 3, "wave-power": 256},
    "WHALE": {"UV-Scale-X": 6, "UV-Scale-Y": 6, "UV-Scroll-X": 0,
              "UV-Scroll-Y": 0, "seed-size": 178, "sine-height-0": 1971,
              "sine-height-1": 2406, "sine-step-0": 2, "sine-step-1": 1,
              "subdivisions": 4, "unk57": 16, "unk64": 153, "unk66": 998,
              "unk70": 1, "unk71": 0, "view-distance": 5, "wave-power": 256},
    "PIRATE": {"UV-Scale-X": 5, "UV-Scale-Y": 3, "UV-Scroll-X": 4,
               "UV-Scroll-Y": 2, "seed-size": 157, "sine-height-0": 4016,
               "sine-height-1": 4963, "sine-step-0": 2, "sine-step-1": 1,
               "subdivisions": 2, "unk57": 16, "unk64": 153, "unk66": 908,
               "unk70": 1, "unk71": 0, "view-distance": 3, "wave-power": 128},
    "CRESCENT": {"UV-Scale-X": 4, "UV-Scale-Y": 3, "UV-Scroll-X": 1,
                 "UV-Scroll-Y": 0, "seed-size": 187, "sine-height-0": 3020,
                 "sine-height-1": 2227, "sine-step-0": 2, "sine-step-1": 1,
                 "subdivisions": 3, "unk57": 16, "unk64": 153, "unk66": 1036,
                 "unk70": 1, "unk71": 0, "view-distance": 5, "wave-power": 256},
    "DARKWATER": {"UV-Scale-X": 4, "UV-Scale-Y": 2, "UV-Scroll-X": 2,
                  "UV-Scroll-Y": 1, "seed-size": 120, "sine-height-0": 2483,
                  "sine-height-1": 2048, "sine-step-0": 1, "sine-step-1": 1,
                  "subdivisions": 6, "unk57": 16, "unk64": 153, "unk66": 2035,
                  "unk70": 1, "unk71": 0, "view-distance": 3, "wave-power": 256},
    "BOULDER": {"UV-Scale-X": 3, "UV-Scale-Y": 3, "UV-Scroll-X": 1,
                "UV-Scroll-Y": 2, "seed-size": 120, "sine-height-0": 1100,
                "sine-height-1": 614, "sine-step-0": 1, "sine-step-1": 2,
                "subdivisions": 3, "unk57": 20, "unk64": 153, "unk66": 2560,
                "unk70": 1, "unk71": 1, "view-distance": 5, "wave-power": 256},
    "LAVA": {"UV-Scale-X": 2, "UV-Scale-Y": 2, "UV-Scroll-X": 4,
             "UV-Scroll-Y": -2, "seed-size": 120, "sine-height-0": 4608,
             "sine-height-1": 4172, "sine-step-0": 4, "sine-step-1": 2,
             "subdivisions": 4, "unk57": 8, "unk64": 153, "unk66": 1024,
             "unk70": 0, "unk71": 0, "view-distance": 3, "wave-power": 207},
}


def preset_answers(key: str) -> Dict[str, int]:
    """A preset as header answers, keyed by pointer."""
    values = PRESET_VALUES.get(key)
    if values is None:
        raise WaterError("no wave preset called %r" % key)
    return {"/waves/" + name: value for name, value in values.items()}


def header_problems(values: Dict[str, int]) -> List[str]:
    """What in a header's wave block would break the simulation."""
    found = []
    seed = values.get("/waves/seed-size")
    if seed is not None and int(seed) < 2:
        found.append("the wave pattern length has to be at least 2")
    pattern = values.get("/waves/unk57")
    if pattern is not None and int(pattern) < 1:
        found.append("the wave pattern has to span at least one tile")
    subdivisions = values.get("/waves/subdivisions")
    if subdivisions is not None and not 1 <= int(subdivisions) <= 8:
        found.append("wave detail runs from 1 to 8; the game sizes its "
                     "buffers for no more")
    return found
