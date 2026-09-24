"""A track's own minimap: the game's arithmetic, the picture and its payloads.

The minimap is two things drawn over each other that never consult one another:
a small greyscale **picture** of the road, and a **dot** per racer placed by
``minimap_marker_pos`` (``game_ui.c``) from nine numbers in the level model's
header and the model's bounds. A custom track that inherits another track's
nine numbers inherits its picture too, and its dots land wherever those numbers
send them - which is why every remix of Ancient Lake showed Ancient Lake's map.

This module is the whole of the answer that does not need Blender:

* :func:`project` and :func:`texel` are ``minimap_marker_pos`` and
  ``render_ortho_triangle_image`` solved for each other: where on the picture a
  world position's dot lands. ``tests/test_minimap.py`` holds them to all
  retail levels that ship a minimap - the computer racers' own line, run
  through the retail numbers, lands on the retail pictures' road.
* :func:`fit` chooses the nine numbers for a picture this module draws, and
  :func:`rasterize` draws it through the *same* mapping, so the dots line up
  by construction rather than by tuning.
* :func:`texture_payload` and :func:`sprite_payload` are the two assets the
  runtime appends to ``ASSET_TEXTURES_2D`` and ``ASSET_SPRITES`` for it.

The picture is the road and nothing else - retail's recipe: a white band with a
soft edge, the checkered finish line on the start grid, no scenery. The road is
what lies between the author's edges under the even-odd rule, so an outline
and an inner edge make a ring, and a shortcut is one more edge.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import math
import os
import json
import struct
import zlib
from typing import Iterable, List, Optional, Sequence, Tuple

from . import textures as texture_module

# ---------------------------------------------------------------------------
# What the game does
# ---------------------------------------------------------------------------

#: ``minimap_marker_pos``: at scale 1.0 the track's whole depth becomes this
#: many pixels. Both axes divide by the depth, so X and Z share one scale.
DOT_SPAN = 60.0

#: Where a dot's centre lands on the picture, in texels, beyond what the
#: offsets say. The quad a sprite is drawn on is one texel narrower than its
#: texture, the dot sprite's own centre is half a texel off its anchor, and the
#: marker position is truncated to a whole vertex coordinate. The sum was
#: derived from ``sprite_init_frame`` and measured against retail: with these,
#: 96% of the computer racers' line over all retail levels with a minimap lands
#: on the drawn road (``tests/test_minimap.py``).
TEXEL_SHIFT_X = 1.5
TEXEL_SHIFT_Y = 1.25

#: Mirrored (Adventure 2) tracks flip the picture about its anchor and draw the
#: dots from ``minimapOffsetXAdv2``. Retail's hand-set values sit at
#: ``anchor.x - offsetX + 2`` to within two pixels on every level that has one.
MIRROR_EXTRA = 2

#: ``TextureHeader`` ``posX``/``posY`` - ``sprite-x``/``sprite-y`` in the asset
#: tool. Retail minimaps use 15..17 and 2..7; the anchor is then placed at the
#: picture's bottom-right corner, which is the corner the one-player HUD pins
#: to the screen.
SPRITE_X = 15
SPRITE_Y = 2

#: The picture's longest side, in pixels. Retail's run from 60 to 80.
MIN_SIZE = 48
MAX_SIZE = 80
DEFAULT_SIZE = 64

#: IA8 is a byte a texel and a sprite frame is loaded in one block, so the
#: texture memory bounds the area. Retail's largest, Central Area, is 80x50.
MAX_TEXELS = 4096
#: One TMEM line is eight bytes, so an IA8 row is a multiple of eight texels -
#: every retail minimap is 32, 40, 48, 56, 64, 72 or 80 wide.
WIDTH_STEP = 8
#: Transparent texels left around the road.
MARGIN = 2
#: Samples per pixel side when filling. Four gives seventeen levels of edge,
#: the sixteen IA8 can store and then some - retail's soft edge.
SUPERSAMPLE = 4

#: The finish flag: retail's is a 5x5 checkerboard just behind the grid.
FLAG_SIZE = 5
FLAG_BEHIND = 2.0

#: Retail's tints. Snowflake Mountain draws its maps red, Crescent Island teal.
COLOUR_PRESETS = (
    ("WHITE", "White", 0xFFFFFF),
    ("SNOW", "Snowflake Red", 0xFF2828),
    ("TEAL", "Crescent Teal", 0x3C8080),
)
DEFAULT_COLOUR = 0xFFFFFF

#: ``gHudMinimapColours`` by character id - what the preview paints the dots.
MARKER_COLOURS = (
    (255, 160, 0), (255, 255, 0), (0, 128, 255), (255, 255, 255), (0, 175, 0),
    (0, 255, 128), (255, 0, 0), (255, 128, 190), (128, 128, 128), (0, 0, 255),
)
#: The player's character in the preview (Diddy) and the seven CPUs after him.
PREVIEW_PLAYER = 9
PREVIEW_CPUS = (0, 1, 2, 3, 4, 5, 6)

# ---------------------------------------------------------------------------
# What the runtime rewrites
# ---------------------------------------------------------------------------

#: What a model's ``minimapSpriteIndex`` holds for the track's own minimap.
#: The real index is the ROM's sprite count plus an ordinal, and the count
#: belongs to the player's cartridge, so DKR-R substitutes it as it serves the
#: model - the same arrangement as :data:`.textures.CUSTOM_ID_BASE`. 193 is the
#: retail count; 0x7000 is far past it and inside the s16 a sprite id travels
#: in. Kept identical in ``runtime-recomp/src/game/custom_tracks.hpp``.
CUSTOM_SPRITE_ID_BASE = 0x7000
#: What the sprite's ``baseTextureId`` holds for the track's own 2D texture.
CUSTOM_TEXTURE_ID_BASE = 0x7000

#: Level header byte 0xBC, bit 0: "this level has no minimap". Hubs and
#: cutscenes set it; ``hud_render_general`` returns before the minimap for it.
NO_MINIMAP_POINTER = "/unknown/unkBC"
NO_MINIMAP_BIT = 0x01

#: ``TextureHeader.flags`` the retail minimaps carry: cutout, both axes
#: clamped, and ``RENDER_LINE_SWAP`` - odd rows pre-swapped so ``material_init``
#: loads the texture with ``gDPLoadTextureBlockS`` like every retail one.
FLAG_CUTOUT = 0x10
FLAG_CLAMP_S = 0x40
FLAG_CLAMP_T = 0x80
FLAG_LINE_SWAP = 0x400
TEXTURE_FLAGS = FLAG_CUTOUT | FLAG_CLAMP_S | FLAG_CLAMP_T | FLAG_LINE_SWAP
IA8 = texture_module.FORMAT_CODES["IA8"]

#: ``SpriteHeader`` in the asset tool: base texture, frames, anchor x, anchor y
#: and a word the game fills, then one byte per frame boundary.
SPRITE_HEADER = ">hhhhi"
SPRITE_SIZE = 16

#: A stroke whose ends are this close, as a fraction of the track's size, is
#: closed without asking.
CLOSE_FRACTION = 0.03

#: Start From AI Path: the road is taken as this much wider than the span of
#: the four lanes, and never narrower than this fraction of the lap's size on
#: each side - about four texels across once fitted, as retail's roads are.
LANE_WIDEN = 1.8
MIN_HALF_FRACTION = 0.032
#: How many samples ahead a turn's inner edge is searched for a place where it
#: crosses itself - offsetting a tight corner inward folds the edge over.
LOOP_WINDOW = 24

Point2 = Tuple[float, float]


class MinimapError(Exception):
    """The minimap cannot be made, with the reason to act on."""


# ---------------------------------------------------------------------------
# The numbers
# ---------------------------------------------------------------------------

def f32_bits(value: float) -> int:
    """A float as the raw word the model header stores."""
    return struct.unpack(">I", struct.pack(">f", float(value)))[0]


def f32_value(bits: int) -> float:
    return struct.unpack(">f", struct.pack(">I", int(bits) & 0xFFFFFFFF))[0]


def binary_angle(degrees: int) -> int:
    """``(minimapRotation * 0xFFFF) / 360`` as the game computes it."""
    return ((int(degrees) & 0xFFFF) * 0xFFFF) // 360


def angle_sincos(degrees: int) -> Tuple[float, float]:
    radians = (binary_angle(degrees) & 0xFFFF) * (2.0 * math.pi / 65536.0)
    return math.sin(radians), math.cos(radians)


class Placement:
    """The nine header numbers and the picture they were chosen for.

    ``bounds`` is the model's ``(lowerX, upperX, lowerY, upperY, lowerZ,
    upperZ)``, which the dot formula reads as much as the header fields.
    """

    __slots__ = ("bounds", "rotation", "x_scale", "y_scale", "offset_x",
                 "offset_y", "offset_x2", "offset_y2", "colour", "width",
                 "height", "sprite_x", "sprite_y", "anchor_x", "anchor_y")

    def __init__(self, bounds, rotation=0, x_scale=1.0, y_scale=1.0,
                 offset_x=0, offset_y=0, offset_x2=None, offset_y2=None,
                 colour=DEFAULT_COLOUR, width=0, height=0, sprite_x=SPRITE_X,
                 sprite_y=SPRITE_Y, anchor_x=None, anchor_y=None):
        self.bounds = tuple(int(v) for v in bounds)
        self.rotation = int(rotation) % 360
        self.x_scale = float(x_scale)
        self.y_scale = float(y_scale)
        self.offset_x = int(offset_x)
        self.offset_y = int(offset_y)
        self.width = int(width)
        self.height = int(height)
        self.sprite_x = int(sprite_x)
        self.sprite_y = int(sprite_y)
        self.anchor_x = int(self.sprite_x + self.width - 2 if anchor_x is None
                            else anchor_x)
        self.anchor_y = int(self.sprite_y + self.height if anchor_y is None
                            else anchor_y)
        mirrored = mirror_offsets(self.offset_x, self.offset_y, self.anchor_x)
        self.offset_x2 = int(mirrored[0] if offset_x2 is None else offset_x2)
        self.offset_y2 = int(mirrored[1] if offset_y2 is None else offset_y2)
        self.colour = int(colour) & 0xFFFFFF

    def header_fields(self) -> dict:
        """What :func:`apply_to_model` writes, by ``HEADER_FIELDS`` name."""
        return {
            "minimap_sprite_index": CUSTOM_SPRITE_ID_BASE,
            "minimap_rotation": self.rotation,
            "minimap_x_scale": f32_bits(self.x_scale),
            "minimap_y_scale": f32_bits(self.y_scale),
            "minimap_offset_x_adv1": self.offset_x,
            "minimap_offset_y_adv1": self.offset_y,
            "minimap_offset_x_adv2": self.offset_x2,
            "minimap_offset_y_adv2": self.offset_y2,
            "minimap_colour": self.colour,
        }

    def __repr__(self):
        return ("Placement(%dx%d, rot %d, scale %.2f/%.2f, adv1 %d,%d, adv2 %d,%d)"
                % (self.width, self.height, self.rotation, self.x_scale,
                   self.y_scale, self.offset_x, self.offset_y, self.offset_x2,
                   self.offset_y2))


def mirror_offsets(offset_x: int, offset_y: int, anchor_x: int) -> Tuple[int, int]:
    """The Adventure 2 offsets that put a mirrored track's dots on its road."""
    return int(anchor_x) - int(offset_x) + MIRROR_EXTRA, int(offset_y)


def _spans(bounds) -> Tuple[int, int]:
    lower_x, upper_x, _ly, _uy, lower_z, upper_z = bounds
    across = int(upper_x) - int(lower_x)
    deep = int(upper_z) - int(lower_z)
    if across <= 0 or deep <= 0:
        raise MinimapError(
            "the track's bounds are %d wide and %d deep; the game divides by "
            "both, so a minimap needs geometry that spans some ground"
            % (across, deep))
    return across, deep


def project(bounds, rotation, x_scale, y_scale, x, z) -> Point2:
    """``minimap_marker_pos`` before its offsets: the rotated, scaled position.

    The aspect ratio is multiplied in and the width divided out exactly as the
    game does it, rather than simplified away, so a float the game rounds is
    rounded here too.
    """
    across, deep = _spans(bounds)
    aspect = float(across) / float(deep)
    scaled_x = (DOT_SPAN * aspect * x_scale * (x - bounds[0])) / across
    scaled_y = (y_scale * -DOT_SPAN * (z - bounds[4])) / deep
    sin, cos = angle_sincos(rotation)
    return (scaled_x * cos + scaled_y * sin,
            scaled_x * sin - scaled_y * cos)


def texel(placement: Placement, x, z, mirrored=False) -> Point2:
    """Where on the picture the dot for world ``(x, z)`` is centred.

    In texels, continuous: texel ``(i, j)`` covers ``[i, i+1) x [j, j+1)`` with
    the picture's top row at ``j = 0``. Mirrored play flips the picture about
    its anchor and the dot's X term together, so a correct Adventure 2 offset
    gives the same texel back.
    """
    rx, ry = project(placement.bounds, placement.rotation, placement.x_scale,
                     placement.y_scale, x, z)
    if mirrored:
        u = (rx - placement.offset_x2 + placement.anchor_x - placement.sprite_x
             - TEXEL_SHIFT_X + MIRROR_EXTRA)
    else:
        u = rx + placement.offset_x - placement.sprite_x - TEXEL_SHIFT_X
    v = ry - placement.offset_y - placement.sprite_y - TEXEL_SHIFT_Y
    return u, v


def world_of(placement: Placement, u, v) -> Point2:
    """:func:`texel` backwards: the world ``(x, z)`` a picture position shows.

    The mapping is a rotation, a scale and a shift, so the picture's corners
    come back as the corners of the ground it covers - which is how the
    viewport overlay lays the picture over the track.
    """
    across, deep = _spans(placement.bounds)
    rx = u - placement.offset_x + placement.sprite_x + TEXEL_SHIFT_X
    ry = v + placement.offset_y + placement.sprite_y + TEXEL_SHIFT_Y
    sin, cos = angle_sincos(placement.rotation)
    scaled_x = rx * cos + ry * sin
    scaled_y = rx * sin - ry * cos
    aspect = float(across) / float(deep)
    x = placement.bounds[0] + scaled_x * across / (DOT_SPAN * aspect * placement.x_scale)
    z = placement.bounds[4] + scaled_y * deep / (-DOT_SPAN * placement.y_scale)
    return x, z


def marker_position(placement: Placement, x, z, screen=(135, -98),
                    mirrored=False) -> Point2:
    """The dot's HUD position exactly as ``minimap_marker_pos`` writes it."""
    rx, ry = project(placement.bounds, placement.rotation, placement.x_scale,
                     placement.y_scale, x, z)
    if mirrored:
        return (screen[0] - rx + placement.offset_x2 - placement.anchor_x,
                placement.offset_y2 + screen[1] - ry + placement.anchor_y)
    return (screen[0] + rx + placement.offset_x - placement.anchor_x,
            placement.offset_y + screen[1] - ry + placement.anchor_y)


# ---------------------------------------------------------------------------
# Choosing the numbers for a picture
# ---------------------------------------------------------------------------

def _edge_points(edge):
    return edge.points if isinstance(edge, Edge) else edge


def _points(edges) -> List[Point2]:
    return [(float(p[0]), float(p[-1]))
            for edge in edges for p in _edge_points(edge)]


def _extent(points, bounds, rotation, scale):
    xs, ys = [], []
    for x, z in points:
        rx, ry = project(bounds, rotation, scale, scale, x, z)
        xs.append(rx)
        ys.append(ry)
    return min(xs), max(xs), min(ys), max(ys)


def auto_rotation(points: Sequence[Point2], bounds) -> int:
    """The rotation that stands the road upright in the smallest picture.

    Retail turns a long track so it fits the corner of the screen - Fossil
    Canyon by 245 degrees, Jungle Falls by 269. Every whole degree in half a
    turn is tried (the other half gives the same box upside down), the picture
    is kept taller than wide, and among the ones within 3% of the smallest the
    one nearest to no turn at all wins, so a track that is already upright is
    not turned for a sliver.
    """
    if not points:
        return 0
    candidates = []
    for degrees in range(0, 360):
        left, right, top, bottom = _extent(points, bounds, degrees, 1.0)
        wide, tall = right - left, bottom - top
        if tall + 1e-9 < wide:
            continue
        candidates.append((wide * tall, degrees))
    if not candidates:
        return 0
    smallest = min(area for area, _degrees in candidates)
    near = [degrees for area, degrees in candidates if area <= smallest * 1.03]
    return min(near, key=lambda d: (min(d, 360 - d), d))


def _round_up(value: int, step: int) -> int:
    return ((int(value) + step - 1) // step) * step


def fit(edges, bounds, size=DEFAULT_SIZE, rotation=None,
        colour=DEFAULT_COLOUR) -> Placement:
    """The nine numbers for a picture of ``edges``, and the picture's size.

    ``edges`` are the road's edges in map space, each a sequence of points
    whose first and last components are X and Z. ``rotation`` is whole degrees,
    or ``None`` for :func:`auto_rotation`. ``size`` caps the longest side;
    the width is rounded up to a whole TMEM line and the area kept inside
    texture memory, so the picture may come out a little smaller than asked.
    """
    points = _points(edges)
    if len(points) < 3:
        raise MinimapError("draw the road's edges first: there is nothing to "
                           "make a picture of")
    _spans(bounds)
    size = max(MIN_SIZE, min(MAX_SIZE, int(size)))
    if rotation is None:
        rotation = auto_rotation(points, bounds)
    rotation = int(rotation) % 360

    left, right, top, bottom = _extent(points, bounds, rotation, 1.0)
    wide, tall = max(right - left, 1e-6), max(bottom - top, 1e-6)
    usable = size - 2 * MARGIN - 1
    # Three decimals: retail writes two, but a very long track can need a
    # scale small enough that a hundredth is a tenth of the picture.
    scale = math.floor(usable / max(wide, tall) * 1000.0) / 1000.0
    widest = min(MAX_SIZE, _round_up(size, WIDTH_STEP))
    while scale >= 0.005:
        width = _round_up(int(math.ceil(wide * scale)) + 2 * MARGIN + 1, WIDTH_STEP)
        height = int(math.ceil(tall * scale)) + 2 * MARGIN + 1
        if (width <= widest and height <= size
                and width * height <= MAX_TEXELS):
            break
        scale = round(scale - 0.005, 3)
    else:
        raise MinimapError("the road is too long and thin to fit a minimap")

    left, right, top, bottom = _extent(points, bounds, rotation, scale)
    # Centred: the rounding left over goes to both sides alike.
    spare_x = (width - (right - left)) / 2.0
    spare_y = (height - (bottom - top)) / 2.0
    offset_x = int(round(spare_x + SPRITE_X + TEXEL_SHIFT_X - left))
    offset_y = int(round(top - SPRITE_Y - TEXEL_SHIFT_Y - spare_y))
    return Placement(bounds, rotation, scale, scale, offset_x, offset_y,
                     colour=colour, width=width, height=height)


def placement_from_model(model, anchor=(0, 0), sprite_pos=(0, 0),
                         size=(0, 0)) -> Placement:
    """The numbers a level model already carries, e.g. a retail track's."""
    return Placement(
        model.bounds, model.minimap_rotation,
        f32_value(model.minimap_x_scale), f32_value(model.minimap_y_scale),
        model.minimap_offset_x_adv1, model.minimap_offset_y_adv1,
        model.minimap_offset_x_adv2, model.minimap_offset_y_adv2,
        colour=model.minimap_colour, width=size[0], height=size[1],
        sprite_x=sprite_pos[0], sprite_y=sprite_pos[1],
        anchor_x=anchor[0], anchor_y=anchor[1],
    )


def apply_to_model(model, placement: Placement) -> None:
    """Point the model at the track's own minimap and give it the numbers."""
    for name, value in placement.header_fields().items():
        setattr(model, name, value)


def has_own_minimap(model) -> bool:
    return int(model.minimap_sprite_index) == CUSTOM_SPRITE_ID_BASE


# ---------------------------------------------------------------------------
# The road's edges
# ---------------------------------------------------------------------------

class Edge:
    """One stroke: its points in map space and whether it closes."""

    __slots__ = ("points", "cyclic")

    def __init__(self, points, cyclic=False):
        self.points = [tuple(float(c) for c in p) for p in points]
        self.cyclic = bool(cyclic)

    def gap(self) -> float:
        """How far apart the ends are on the ground, 0 for a closed edge."""
        if self.cyclic or len(self.points) < 2:
            return 0.0
        first, last = self.points[0], self.points[-1]
        return math.hypot(first[0] - last[0], first[-1] - last[-1])


def close_distance(bounds) -> float:
    across, deep = _spans(bounds)
    return CLOSE_FRACTION * max(across, deep)


def open_edges(edges: Iterable[Edge], bounds) -> List[Tuple[int, float]]:
    """``[(position, gap)]`` for the edges too open to close by themselves."""
    limit = close_distance(bounds)
    return [(index, edge.gap()) for index, edge in enumerate(edges)
            if len(edge.points) >= 2 and edge.gap() > limit]


def usable_edges(edges: Iterable[Edge]) -> List[Edge]:
    """The edges that enclose any ground at all."""
    return [edge for edge in edges if len(edge.points) >= 3]


def edges_from_route(route, samples: int = 4) -> List[List[Tuple[float, float, float]]]:
    """Two closed edges either side of the computer racers' line.

    The line is already a whole lap, so this is a picture of the road in two
    strokes the author then only adjusts. The road's width at each gate is the
    span of the four lanes, widened, because the lanes sit inside the road.
    """
    from . import race_ai  # noqa: PLC0415 - catalogue free, but not needed above

    first = race_ai.lane_line(route, 0, samples)
    last = race_ai.lane_line(route, race_ai.LANES - 1, samples)
    if len(first) < 4 or len(first) != len(last):
        raise MinimapError("the AI path needs at least three checkpoints in the "
                           "set the car racers load")
    first, last = first[:-1], last[:-1]
    count = len(first)
    spread = max(max(p[0] for p in first + last) - min(p[0] for p in first + last),
                 max(p[2] for p in first + last) - min(p[2] for p in first + last))
    least = MIN_HALF_FRACTION * spread
    centres = [tuple((a[axis] + b[axis]) / 2.0 for axis in range(3))
               for a, b in zip(first, last)]
    left, right = [], []
    for index in range(count):
        a, b = first[index], last[index]
        # Across the road is square to the line, not from lane 1 to lane 4: a
        # gate can list its lanes the other way round, and following them
        # would twist the two edges through each other.
        ahead, behind = centres[(index + 1) % count], centres[index - 1]
        tangent = (ahead[0] - behind[0], ahead[2] - behind[2])
        norm = math.hypot(*tangent) or 1.0
        across = (tangent[1] / norm, -tangent[0] / norm)
        half = max(math.hypot(b[0] - a[0], b[2] - a[2]) / 2.0 * LANE_WIDEN, least)
        centre = centres[index]
        left.append((centre[0] - across[0] * half, centre[1], centre[2] - across[1] * half))
        right.append((centre[0] + across[0] * half, centre[1], centre[2] + across[1] * half))
    return [remove_loops(left), remove_loops(right)]


def _crossing(p, q, r, s):
    """Where segment ``pq`` crosses ``rs`` on the ground, or ``None``."""
    d1 = (q[0] - p[0], q[2] - p[2])
    d2 = (s[0] - r[0], s[2] - r[2])
    denominator = d1[0] * d2[1] - d1[1] * d2[0]
    if abs(denominator) < 1e-12:
        return None
    t = ((r[0] - p[0]) * d2[1] - (r[2] - p[2]) * d2[0]) / denominator
    u = ((r[0] - p[0]) * d1[1] - (r[2] - p[2]) * d1[0]) / denominator
    if 0.0 < t < 1.0 and 0.0 < u < 1.0:
        return tuple(p[axis] + (q[axis] - p[axis]) * t for axis in range(3))
    return None


def remove_loops(points, window=LOOP_WINDOW):
    """A closed edge with the small loops it folds into at tight turns cut out.

    An edge offset to the inside of a corner sharper than its offset crosses
    itself, and under the even-odd rule the fold becomes a notch in the road.
    Only crossings within ``window`` samples are cut, so a track whose road
    really does cross itself keeps both halves.
    """
    points = list(points)
    index = 0
    while index < len(points) - 1 and len(points) > 3:
        p, q = points[index], points[index + 1]
        cut = None
        for ahead in range(index + 2, min(index + 2 + window, len(points) - 1)):
            if index == 0 and ahead == len(points) - 2:
                break
            found = _crossing(p, q, points[ahead], points[ahead + 1])
            if found is not None:
                cut = (ahead, found)
        if cut is not None:
            ahead, found = cut
            points[index + 1:ahead + 1] = [found]
        index += 1
    return points


# ---------------------------------------------------------------------------
# Drawing the picture
# ---------------------------------------------------------------------------

def _polygons(edges, placement):
    polygons = []
    for edge in edges:
        points = _edge_points(edge)
        if len(points) < 3:
            continue
        polygons.append([texel(placement, p[0], p[-1]) for p in points])
    return polygons


def rasterize(edges, placement: Placement, supersample=SUPERSAMPLE) -> List[float]:
    """Coverage of the road, 0 to 1, top row first.

    Every edge is a closed polygon - an open stroke is closed by the straight
    line between its ends - and the road is what an odd number of them enclose,
    so a ring needs no notion of inside and outside and a shortcut is simply
    another edge. Filled at ``supersample`` times the size and averaged down,
    which is where the soft edge retail pictures have comes from.
    """
    width, height = placement.width, placement.height
    step = int(supersample)
    rows = height * step
    polygons = _polygons(edges, placement)
    segments = []
    for polygon in polygons:
        for index, (x0, y0) in enumerate(polygon):
            x1, y1 = polygon[(index + 1) % len(polygon)]
            if y0 == y1:
                continue
            segments.append((x0 * step, y0 * step, x1 * step, y1 * step))

    # Each segment filed under the sample rows it can cross.
    buckets = [[] for _ in range(rows)]
    for segment in segments:
        low = min(segment[1], segment[3])
        high = max(segment[1], segment[3])
        first = max(0, int(math.floor(low - 0.5)))
        last = min(rows - 1, int(math.ceil(high - 0.5)))
        for row in range(first, last + 1):
            buckets[row].append(segment)

    counts = [0] * (width * height)
    columns = width * step
    for row in range(rows):
        y = row + 0.5
        crossings = []
        for x0, y0, x1, y1 in buckets[row]:
            if (y0 <= y < y1) or (y1 <= y < y0):
                crossings.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
        if len(crossings) < 2:
            continue
        crossings.sort()
        base = (row // step) * width
        for pair in range(0, len(crossings) - 1, 2):
            start = max(0, int(math.ceil(crossings[pair] - 0.5)))
            stop = min(columns - 1, int(math.ceil(crossings[pair + 1] - 0.5)) - 1)
            for column in range(start, stop + 1):
                counts[base + column // step] += 1
    whole = float(step * step)
    return [count / whole for count in counts]


def soften(coverage: List[float], width: int, height: int, level: int) -> List[float]:
    """The edge the Soft Edge setting asks for.

    0 is hard - a texel is road or it is not. 1 is the filled coverage as it
    is, retail's look. 2 blurs that once more with a 1-2-1 kernel.
    """
    level = int(level)
    if level <= 0:
        return [1.0 if value >= 0.5 else 0.0 for value in coverage]
    out = list(coverage)
    for _ in range(level - 1):
        blurred = [0.0] * len(out)
        for y in range(height):
            for x in range(width):
                total = 0.0
                weight = 0.0
                for dy, wy in ((-1, 1.0), (0, 2.0), (1, 1.0)):
                    for dx, wx in ((-1, 1.0), (0, 2.0), (1, 1.0)):
                        sx, sy = x + dx, y + dy
                        if 0 <= sx < width and 0 <= sy < height:
                            total += out[sy * width + sx] * wx * wy
                            weight += wx * wy
                blurred[y * width + x] = total / weight
        out = blurred
    return out


def flag_texels(placement: Placement, grid, forward) -> Tuple[int, int]:
    """The top-left texel of the 5x5 finish flag.

    ``grid`` is the start grid's centre on the ground and ``forward`` a point
    ahead of it; retail's flag sits about two texels behind the grid.
    """
    u, v = texel(placement, grid[0], grid[-1])
    ahead = texel(placement, forward[0], forward[-1])
    du, dv = ahead[0] - u, ahead[1] - v
    length = math.hypot(du, dv)
    if length > 1e-6:
        u -= du / length * FLAG_BEHIND
        v -= dv / length * FLAG_BEHIND
    return (int(math.floor(u - FLAG_SIZE / 2.0 + 0.5)),
            int(math.floor(v - FLAG_SIZE / 2.0 + 0.5)))


def picture(edges, placement: Placement, soft=1, flag=None) -> bytearray:
    """The minimap as eight-bit RGBA, top row first: white road, clear ground.

    ``flag`` is :func:`flag_texels`'s corner or ``None``. The flag is drawn only
    on road, alternating black and white a texel at a time the way retail's is.
    """
    width, height = placement.width, placement.height
    coverage = soften(rasterize(edges, placement), width, height, soft)
    rgba = bytearray(width * height * 4)
    for index, value in enumerate(coverage):
        alpha = int(round(max(0.0, min(1.0, value)) * 255))
        if alpha:
            rgba[index * 4:index * 4 + 4] = bytes((255, 255, 255, alpha))
    if flag is not None:
        left, top = flag
        for dy in range(FLAG_SIZE):
            for dx in range(FLAG_SIZE):
                x, y = left + dx, top + dy
                if not (0 <= x < width and 0 <= y < height):
                    continue
                if coverage[y * width + x] < 0.35:
                    continue
                grey = 0 if (dx + dy) % 2 == 0 else 255
                at = (y * width + x) * 4
                rgba[at:at + 4] = bytes((grey, grey, grey, 255))
    return rgba


def tinted(rgba, colour: int, opacity=160) -> bytearray:
    """The picture as the game draws it: grey times the tint, at most 160/255
    opaque - ``gDPSetPrimColor`` with ``minimapColor``."""
    red, green, blue = (colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF
    out = bytearray(len(rgba))
    for at in range(0, len(rgba), 4):
        grey = rgba[at]
        out[at] = grey * red // 255
        out[at + 1] = grey * green // 255
        out[at + 2] = grey * blue // 255
        out[at + 3] = rgba[at + 3] * opacity // 255
    return out


def _fill_disc(out, width, height, cx, cy, radius, colour):
    for y in range(max(0, int(cy - radius) - 1), min(height, int(cy + radius) + 2)):
        for x in range(max(0, int(cx - radius) - 1), min(width, int(cx + radius) + 2)):
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= radius * radius:
                at = (y * width + x) * 4
                out[at:at + 4] = bytes(colour + (255,))


def _fill_triangle(out, width, height, corners, colour):
    xs = [c[0] for c in corners]
    ys = [c[1] for c in corners]
    (ax, ay), (bx, by), (cx, cy) = corners

    def side(px, py, x0, y0, x1, y1):
        return (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1)

    for y in range(max(0, int(min(ys))), min(height, int(max(ys)) + 1)):
        for x in range(max(0, int(min(xs))), min(width, int(max(xs)) + 1)):
            px, py = x + 0.5, y + 0.5
            d1 = side(px, py, ax, ay, bx, by)
            d2 = side(px, py, bx, by, cx, cy)
            d3 = side(px, py, cx, cy, ax, ay)
            if not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0)):
                at = (y * width + x) * 4
                out[at:at + 4] = bytes(colour + (255,))


def preview(rgba, width, height, colour=DEFAULT_COLOUR, markers=(), scale=3,
            background=(38, 38, 42)):
    """The picture enlarged over a dark ground, as the HUD tints it, with the
    racers on it. ``markers`` are ``(u, v, character, heading)`` in texels;
    ``heading`` is a texel-space direction for the player's arrow, ``None`` for
    a computer racer's dot. Eight-bit RGBA, top row first."""
    scale = max(1, int(scale))
    big_w, big_h = width * scale, height * scale
    shown = tinted(rgba, colour)
    out = bytearray(big_w * big_h * 4)
    for y in range(big_h):
        for x in range(big_w):
            at = ((y // scale) * width + x // scale) * 4
            alpha = shown[at + 3] / 255.0
            pixel = (y * big_w + x) * 4
            for channel in range(3):
                out[pixel + channel] = int(round(background[channel] * (1 - alpha)
                                                 + shown[at + channel] * alpha))
            out[pixel + 3] = 255
    for u, v, character, heading in markers:
        tint = MARKER_COLOURS[character % len(MARKER_COLOURS)]
        cx, cy = u * scale, v * scale
        if heading is None:
            _fill_disc(out, big_w, big_h, cx, cy, 1.2 * scale, tint)
            continue
        length = math.hypot(*heading) or 1.0
        hx, hy = heading[0] / length, heading[1] / length
        size = 2.4 * scale
        tip = (cx + hx * size, cy + hy * size)
        left = (cx - hx * size * 0.6 - hy * size * 0.8, cy - hy * size * 0.6 + hx * size * 0.8)
        right = (cx - hx * size * 0.6 + hy * size * 0.8, cy - hy * size * 0.6 - hx * size * 0.8)
        _fill_triangle(out, big_w, big_h, (tip, left, right), tint)
    return out


# ---------------------------------------------------------------------------
# The two payloads
# ---------------------------------------------------------------------------

def swap_lines(texels: bytes, width: int, height: int) -> bytes:
    """``N64Image::interlace`` for an eight-bit image: on every odd row, each
    pair of 32-bit words trades places. ``gDPLoadTextureBlockS`` expects it."""
    out = bytearray(texels)
    stride = int(width)
    for row in range(1, int(height), 2):
        at = row * stride
        for pair in range(stride // 8):
            start = at + pair * 8
            out[start:start + 4], out[start + 4:start + 8] = (
                texels[start + 4:start + 8], texels[start:start + 4])
    return bytes(out)


def check_picture(width: int, height: int) -> None:
    if width <= 0 or height <= 0:
        raise MinimapError("a minimap cannot be %dx%d" % (width, height))
    if width % WIDTH_STEP:
        raise MinimapError(
            "%d is not a multiple of %d; an IA8 row has to fill whole texture "
            "memory lines" % (width, WIDTH_STEP))
    if width * height > MAX_TEXELS:
        raise MinimapError(
            "%dx%d is %d texels and a minimap is loaded into %d bytes of "
            "texture memory at once" % (width, height, width * height, MAX_TEXELS))
    if width > 255 or height > 255:
        raise MinimapError("%dx%d does not fit a TextureHeader" % (width, height))


def texture_payload(rgba, width: int, height: int, sprite_x=SPRITE_X,
                    sprite_y=SPRITE_Y) -> bytes:
    """The minimap as the bytes ``ASSET_TEXTURES_2D`` holds for one texture.

    What the asset tool writes for a retail minimap, uncompressed: IA8, render
    mode TRANSPARENT, ``sprite-x``/``sprite-y`` in ``posX``/``posY``, and the
    retail flags with the odd rows swapped to match.
    """
    check_picture(width, height)
    header = bytearray(texture_module.texture_header(
        width, height, IA8, "TRANSPARENT", clamp_s=True, clamp_t=True))
    header[0x03] = int(sprite_x) & 0xFF
    header[0x04] = int(sprite_y) & 0xFF
    struct.pack_into(">h", header, 0x06, TEXTURE_FLAGS)
    texels = texture_module.encode_texels(rgba, width, height, IA8)
    payload = bytearray(header) + swap_lines(texels, width, height)
    while len(payload) % 16:
        payload.append(0)
    return bytes(payload)


def sprite_payload(anchor_x: int, anchor_y: int, texture_ordinal: int = 0) -> bytes:
    """The sprite wrapping the picture: one frame of one texture, its anchor.

    ``baseTextureId`` holds :data:`CUSTOM_TEXTURE_ID_BASE` plus the texture's
    position among the track's own 2D textures; DKR-R writes the real index.
    """
    payload = bytearray(SPRITE_SIZE)
    struct.pack_into(SPRITE_HEADER, payload, 0,
                     CUSTOM_TEXTURE_ID_BASE + int(texture_ordinal), 1,
                     int(anchor_x), int(anchor_y), 0)
    payload[12] = 0
    payload[13] = 1
    return bytes(payload)


def read_sprite_payload(payload: bytes) -> dict:
    base, frames, anchor_x, anchor_y, _unused = struct.unpack_from(
        SPRITE_HEADER, payload, 0)
    return {"base": base, "frames": frames, "anchor": (anchor_x, anchor_y),
            "offsets": list(payload[12:12 + frames + 1])}


# ---------------------------------------------------------------------------
# PNG in and out
# ---------------------------------------------------------------------------

def png_bytes(width: int, height: int, rgba) -> bytes:
    """Eight-bit RGBA, top row first, as a PNG - for Save PNG and the tests."""
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    stride = int(width) * 4
    raw = bytearray()
    for row in range(int(height)):
        raw.append(0)
        raw += bytes(rgba[row * stride:(row + 1) * stride])
    header = struct.pack(">IIBBBBB", int(width), int(height), 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def write_png(path: str, width: int, height: int, rgba) -> None:
    with open(path, "wb") as handle:
        handle.write(png_bytes(width, height, rgba))


def read_own_png(path: str, width: int, height: int) -> bytearray:
    """An author's retouched picture, refused unless it is the same size.

    A picture of another size no longer lines up with the dots: the numbers
    were chosen for this one, texel for texel.
    """
    try:
        found_width, found_height, rgba = texture_module.read_png(path)
    except texture_module.TextureEncodeError as error:
        raise MinimapError(str(error))
    if (found_width, found_height) != (int(width), int(height)):
        raise MinimapError(
            "%s is %dx%d and the minimap is %dx%d. The dots are placed for "
            "that size exactly, so a resized picture would not line up - "
            "retouch it without resizing" % (os.path.basename(path),
                                             found_width, found_height,
                                             width, height))
    return bytearray(rgba)


# ---------------------------------------------------------------------------
# Retail's own minimaps
# ---------------------------------------------------------------------------

class RetailMinimap:
    """A retail minimap sprite, its texture and the files they come from."""

    __slots__ = ("index", "name", "anchor", "sprite_pos", "png")

    def __init__(self, index, name, anchor, sprite_pos, png):
        self.index = index
        self.name = name
        self.anchor = anchor
        self.sprite_pos = sprite_pos
        self.png = png

    @property
    def label(self) -> str:
        return self.name.replace("_", " ").title()


def retail_minimap(root: str, sprite_index: int) -> Optional[RetailMinimap]:
    """The retail sprite ``sprite_index`` names under an extracted asset tree,
    if it is a minimap - the first 34 sprites are."""
    if not root:
        return None
    try:
        with open(os.path.join(root, "asset_sprites.meta.json"), "r",
                  encoding="utf-8") as handle:
            order = json.load(handle)["files"]["order"]
    except (OSError, ValueError, KeyError):
        return None
    if not 0 <= int(sprite_index) < len(order):
        return None
    asset = order[int(sprite_index)]
    prefix = "ASSET_SPRITE_MINIMAP_"
    if not asset.startswith(prefix):
        return None
    wanted = asset[len(prefix):]
    folder = os.path.join(root, "sprites", "minimap")
    try:
        names = sorted(os.listdir(folder))
    except OSError:
        return None
    for name in names:
        stem, extension = os.path.splitext(name)
        if extension != ".json" or stem.replace("_", "").upper() != wanted:
            continue
        try:
            with open(os.path.join(folder, name), "r", encoding="utf-8") as handle:
                sprite = json.load(handle)
            texture_json = os.path.join(root, "textures", "2d", "minimap", name)
            with open(texture_json, "r", encoding="utf-8") as handle:
                texture = json.load(handle)
        except (OSError, ValueError):
            return None
        png = os.path.join(os.path.dirname(texture_json), texture["images"][0])
        return RetailMinimap(
            int(sprite_index), stem,
            (int(sprite.get("unk4", 0)), int(sprite.get("unk6", 0))),
            (int(texture.get("sprite-x", 0)), int(texture.get("sprite-y", 0))),
            png)
    return None


def shows_minimap(model) -> bool:
    """Whether a model's numbers describe a minimap at all.

    Tracks without one carry sprite 0, scale 1.0 and zero offsets - Fossil
    Canyon's sprite with nothing tuned for it.
    """
    return not (int(model.minimap_offset_x_adv1) == 0
                and int(model.minimap_offset_y_adv1) == 0
                and int(model.minimap_rotation) == 0
                and f32_value(model.minimap_x_scale) == 1.0)
