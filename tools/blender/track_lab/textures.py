"""Every texture a track can draw with: the ROM's 1401, and its own.

This module answers two questions that used to have much smaller answers.

**Which of the ROM's textures may a track use?** All of them. A track built
from a mesh borrowed a *donor* track's texture table and could draw with
nothing else - two or three dozen images - which was never a limit of the
format. The ROM holds **1401** in the US v1.0 extraction, a later revision a
few more, and a level model can name any of them.

**May a track bring artwork the ROM does not hold?** Now yes. A ``.dkrmap``
had no texture section, so for a while the honest answer was no and this
module's job stopped at browsing. It has one now, and the second half of the
file - from "Artwork the ROM does not hold" - is what fills it: an image
becomes a texture asset in the format ``load_texture`` reads, the runtime
publishes a longer ``ASSET_TEXTURES_3D`` table for it, and the id a level model
stores is rewritten as the model is served. The section comment there carries
the reasoning and the hardware limits.

That it can is not an assumption. ``TriangleBatchInfo.textureIndex`` selects
from the model's own table, and each 8-byte ``TextureInfo`` there is
``{id, width, height, format, surfaceType}`` where ``id`` indexes
``ASSET_TEXTURES_3D``. ``tracks.c`` resolves it with
``load_texture(id | 0x8000)`` at load time, from the same global list every
object model draws from - nothing scopes a texture to the track that shipped it.

**Three of those four bytes are never read by the game.** Grepping the decomp
for reads of a level model's texture table finds exactly two, both of
``surfaceType`` (``tracks.c`` and ``collision.c``); ``width``, ``height`` and
``format`` have no reader at all, because the renderer takes all three from the
``TextureHeader`` the asset itself carries. They are still written honestly
here, for two reasons: the addon's own UV maths divides by the size, and a file
that describes itself wrongly is a trap for the next person reading it.

So the values come from the asset, measured against what retail wrote:

* **Size** is the PNG's own. It matches the table in 1358 of the 1360 entries
  across all 55 retail level models. The two that differ - a wall in Darkmoon
  Caverns and a fog texture in Wizpig 2 - declare a size larger than the image,
  which stretches it; nothing here needs to reproduce that.
* **Format** is the sidecar's name through :data:`FORMAT_CODES`. The low nibble
  of retail's byte agrees with the sidecar's ``format`` in all 1360, over the
  five formats level geometry uses. The high nibble is 0x00, 0x10, 0x20 or 0x30
  and correlates with nothing in the asset - not the wrap flags, not the render
  mode, not the frame count - so it is left at zero rather than guessed at. It
  has no reader.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import json
import math
import os
import re
import struct
from typing import Dict, List, Optional

from . import transparency
from .assets import META_TEXTURES_3D, AssetTree

#: ``TextureHeader.format``'s low nibble, from ``include/structs.h``. Level
#: geometry uses five of the nine; the rest are here because the enum is the
#: enum, and a texture in an unused format is still pickable.
FORMAT_CODES = {
    "RGBA32": 0,
    "RGBA16": 1,
    "I8": 2,
    "I4": 3,
    "IA16": 4,
    "IA8": 5,
    "IA4": 6,
    "CI4": 7,
    "CI8": 8,
}

#: What a texture with no readable sidecar format is written as. RGBA16 is what
#: 1034 of retail's 1360 table entries hold, and being wrong here costs nothing
#: the game reads - see the module docstring.
DEFAULT_FORMAT = FORMAT_CODES["RGBA16"]

#: One repeat of a texture over this many map units. Retail's median, measured
#: over 257,035 textured triangle edges across all 55 level models: 268 units,
#: with the quartiles at 149 and 449. Rounded to a power of two so that halving
#: or doubling it stays on the same grid.
DEFAULT_PROJECTION_SCALE = 256.0


class Texture3D:
    """One entry of ``ASSET_TEXTURES_3D``, resolved to something drawable."""

    __slots__ = ("index", "asset_id", "name", "group", "png", "width", "height",
                 "format", "frames", "render_mode", "wrap_s", "wrap_t")

    def __init__(self, index, asset_id, name, group, png, width, height,
                 texture_format, frames, render_mode="OPAQUE", wrap_s="Wrap", wrap_t="Wrap"):
        #: What a level model's texture table stores. The whole point.
        self.index = index
        self.asset_id = asset_id
        self.name = name
        #: The folder it was extracted into - ``dino``, ``winter``, ``water``.
        self.group = group
        self.png = png
        self.width = width
        self.height = height
        self.format = texture_format
        #: How many images the sidecar lists. More than one is an animated
        #: texture, which the batch drawing it has to be flagged for; see
        #: ``RENDER_TEX_ANIM`` in the geometry operators.
        self.frames = frames
        #: The sidecar's ``render-mode``, the high nibble of the header's
        #: format byte. It decides whether the game draws the texture
        #: see-through, and so which pass a batch drawing it belongs to; see
        #: :mod:`.transparency`. Every one of the 1401 sidecars names one.
        self.render_mode = render_mode or "OPAQUE"
        self.wrap_s = wrap_s
        self.wrap_t = wrap_t

    @property
    def animated(self) -> bool:
        return self.frames > 1

    @property
    def translucent(self) -> bool:
        return transparency.translucent(self.format, self.render_mode)

    @property
    def transparency(self) -> str:
        """The look the texture has on a face that asks for nothing else."""
        return transparency.BLEND if self.translucent else transparency.OPAQUE

    @property
    def own(self) -> bool:
        return False

    @property
    def label(self) -> str:
        return "%s  %dx%d" % (self.name, self.width, self.height)

    def __repr__(self):
        return "Texture3D(%d, %r, %dx%d)" % (
            self.index, self.name, self.width, self.height
        )


def png_size(path: str):
    """``(width, height)`` from a PNG's IHDR, or ``None``.

    Read here rather than through Blender or Pillow because this module is the
    one the tests run without either, and because the answer is 24 bytes in.
    """
    try:
        with open(path, "rb") as handle:
            head = handle.read(24)
    except OSError:
        return None
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", head[16:24])
    return (int(width), int(height))


def readable(stem: str) -> str:
    """``white_wall_200496`` as ``White Wall 200496``."""
    return re.sub(r"[_\-]+", " ", stem).strip().title()


#: Keyed by asset-tree root, because an author can point the addon at a
#: different extraction and the indices are the extraction's, not the addon's.
_CACHE: Dict[str, List[Texture3D]] = {}

#: The same catalogues keyed by texture id, for the panel's per-redraw lookup.
_BY_INDEX: Dict[str, Dict[int, Texture3D]] = {}


def catalogue(tree: Optional[AssetTree]) -> List[Texture3D]:
    """Every 3D texture the tree holds, indexed by the id a model stores.

    The list is dense and in asset order, so ``catalogue(tree)[n].index == n``
    for every entry that resolved. Entries whose sidecar or image is missing are
    left out rather than faked: an author cannot be shown a texture that is not
    there, and a table entry pointing at one would draw nothing.

    Reading 1401 sidecars and 1401 PNG headers takes about half a second, so it
    is done once per tree and cached.
    """
    if tree is None:
        return []
    cached = _CACHE.get(tree.root)
    if cached is not None:
        return cached

    found = []
    for index, asset_id in enumerate(tree.order(META_TEXTURES_3D)):
        entry = _entry(tree, index, asset_id)
        if entry is not None:
            found.append(entry)
    _CACHE[tree.root] = found
    return found


def _entry(tree: AssetTree, index: int, asset_id: str) -> Optional[Texture3D]:
    sidecar = tree.path_for(META_TEXTURES_3D, asset_id)
    if not sidecar or not os.path.isfile(sidecar):
        return None
    try:
        with open(sidecar, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (ValueError, OSError):
        return None

    folder = os.path.dirname(sidecar)
    images = [name for name in (data.get("images") or [])]
    png = None
    for name in images:
        candidate = os.path.join(folder, name)
        if os.path.isfile(candidate):
            png = candidate
            break
    if png is None:
        return None

    size = png_size(png)
    if size is None:
        return None

    return Texture3D(
        index=index,
        asset_id=asset_id,
        name=readable(os.path.splitext(os.path.basename(sidecar))[0]),
        group=os.path.basename(folder),
        png=png,
        width=size[0],
        height=size[1],
        texture_format=FORMAT_CODES.get(data.get("format"), DEFAULT_FORMAT),
        frames=max(1, len(images)),
        render_mode=data.get("render-mode") or "OPAQUE",
        wrap_s=(data.get("flags") or {}).get("wrap-s", "Wrap"),
        wrap_t=(data.get("flags") or {}).get("wrap-t", "Wrap"),
    )


def by_index(tree: Optional[AssetTree], index: int) -> Optional[Texture3D]:
    """The texture a model's table entry names, or ``None``.

    Kept as a lookup rather than a walk because the panel asks on every redraw.
    """
    if tree is None or index is None or int(index) < 0:
        return None
    entries = catalogue(tree)
    lookup = _BY_INDEX.get(tree.root)
    if lookup is None or len(lookup) != len(entries):
        lookup = {entry.index: entry for entry in entries}
        _BY_INDEX[tree.root] = lookup
    return lookup.get(int(index))


def groups(entries: List[Texture3D]) -> List[str]:
    """The folders the textures are extracted into, in order of size.

    They are the closest thing the extraction has to a theme - ``dino``,
    ``winter``, ``water`` - and are what makes 1401 textures browsable at all.
    """
    counts: Dict[str, int] = {}
    for entry in entries:
        counts[entry.group] = counts.get(entry.group, 0) + 1
    return [name for name, _n in sorted(counts.items(),
                                        key=lambda kv: (-kv[1], kv[0]))]


def search(entries: List[Texture3D], query: str = "", group: str = "") -> List[Texture3D]:
    """Narrow the catalogue by folder and by words in the name.

    Every word has to appear somewhere in the name or the asset id, so "ice
    wall" finds the icy walls without also finding every wall.
    """
    words = [word for word in re.split(r"\s+", (query or "").lower()) if word]
    found = []
    for entry in entries:
        if group and group not in ("ALL", "") and entry.group != group:
            continue
        if words:
            haystack = ("%s %s" % (entry.name, entry.asset_id)).lower()
            if not all(word in haystack for word in words):
                continue
        found.append(entry)
    return found


def clear_cache() -> None:
    """Forget the catalogues; the configured asset tree changed."""
    _CACHE.clear()
    _BY_INDEX.clear()


# ---------------------------------------------------------------------------
# Mapping
# ---------------------------------------------------------------------------
#
# A UV in the file is fixed point with five fractional bits, measured in texels
# of the texture the batch draws - so the same picture on a texture of another
# size is a different number, and a face with no UVs at all shows one texel
# stretched over the whole of it. Changing which texture a face draws is
# therefore never only an index change, and these are the two answers to what
# else it is. They live here rather than beside the operator because they are
# arithmetic on the format and nothing to do with Blender, which is also what
# lets ``tests/test_textures.py`` hold them to the properties they claim.

#: One texel, in the fixed point the file stores. ``UV_FRACTIONAL_BITS``.
TEXEL = 32.0

#: What an s16 UV reaches, in texels: 32767 / 32. A face cannot span more.
MAX_TEXELS = 32767.0 / TEXEL

#: ``(across, down)`` map-space axes to project along, indexed by the axis a
#: face most faces. Map space is Y-up, so a road faces Y and takes ``(x, z)``,
#: the ground plane - which is what makes a projected road tile the way a track
#: does rather than smearing down its length.
PROJECTION_AXES = ((2, 1), (0, 2), (0, 1))


def fits_s16(uvs) -> bool:
    """Whether every component survives the ``s16`` the file stores it in."""
    return all(-32768 <= value <= 32767 for pair in uvs for value in pair)


def rescale_uvs(uvs, was_width, was_height, width, height):
    """Texel UVs through a change of texture size, keeping the picture put.

    The normalised coordinate is ``raw / 32 / size``, so holding it still across
    a size change means scaling the raw value by the ratio. This is what makes
    swapping a 32x32 for a 64x64 leave the image covering the same ground
    instead of tiling it four times.
    """
    across = float(width or 1) / float(was_width or 1)
    down = float(height or 1) / float(was_height or 1)
    return [(int(round(s * across)), int(round(t * down))) for s, t in uvs]


def normalise_pair(uvs, width, height):
    """Texel UVs as a UV editor shows them: normalised and V-flipped."""
    return [
        (s / TEXEL / float(width or 1), 1.0 - (t / TEXEL / float(height or 1)))
        for s, t in uvs
    ]


def face_normal(corners):
    """The normal of a face's first three corners, in map space."""
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = corners[:3]
    u = (bx - ax, by - ay, bz - az)
    v = (cx - ax, cy - ay, cz - az)
    return (u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0])


def project_face(corners, width, height, scale=DEFAULT_PROJECTION_SCALE):
    """Plant the texture on the world and return this face's texel UVs.

    ``corners`` are the face's map-space positions. The axis is whichever the
    face most faces, so a road takes the ground plane and a wall takes the
    vertical plane it stands in.

    **The offset subtracted is a whole number of repeats, and that is the whole
    trick.** A raw UV is absolute, so a face 5000 units from the origin would
    project to a value far past what an s16 holds; subtracting an arbitrary
    origin would fix that and break the tiling, because neighbouring faces would
    each start their own copy of the texture. Subtracting an exact repeat cannot
    be seen at all - a texture shifted by one repeat lands where it already was
    - so the values stay small and the tiling stays continuous across the join.
    """
    normal = face_normal(corners)
    axis = max(range(3), key=lambda at: abs(normal[at]))
    across, down = PROJECTION_AXES[axis]

    per_unit_s = float(width or 1) / float(scale)
    per_unit_t = float(height or 1) / float(scale)
    raw = [
        (corner[across] * per_unit_s * TEXEL, corner[down] * per_unit_t * TEXEL)
        for corner in corners
    ]

    repeat_s = float(width or 1) * TEXEL
    repeat_t = float(height or 1) * TEXEL
    offset_s = math.floor(min(s for s, _t in raw) / repeat_s) * repeat_s
    offset_t = math.floor(min(t for _s, t in raw) / repeat_t) * repeat_t
    return [(int(round(s - offset_s)), int(round(t - offset_t))) for s, t in raw]


# ---------------------------------------------------------------------------
# Artwork the ROM does not hold
# ---------------------------------------------------------------------------
#
# Everything above browses the 1401 pictures already in the cartridge. This is
# the part that adds one, and the module docstring's opening claim - that a
# ``.dkrmap`` cannot - is the thing it retires.
#
# **Why it was true, and why it stopped being true.** Nothing in the *format*
# ever forbade it. A 3D texture is reached exactly the way a level header is:
#
#     gTextureAssetTable[TEX_TABLE_3D] = asset_table_load(ASSET_TEXTURES_3D_TABLE);
#     for (i = 0; table[i] != -1; i++) {}  // count, then i--
#     assetOffset = table[assetIndex];
#     assetSize   = table[assetIndex + 1] - assetOffset;   // size BY DIFFERENCE
#     asset_load(ASSET_TEXTURES_3D, dest, assetOffset, assetSize);
#
# That is ``tex_init_textures`` and ``load_texture`` in ``textures_sprites.c``,
# and it is the same shape DKR-R's custom tracks already grow: publish a longer
# table whose extra offsets begin at the retail end offset, and the count, the
# range check and the loader all follow. The limit was only that the runtime's
# section list had four entries and none of them was textures. It now has five,
# so what a track ships is no longer only geometry.
#
# **What a texture id in a level model means once a track can add one.** The
# index the runtime assigns is ``retail_count + ordinal``, and the retail count
# is a property of the ROM the player owns - measured at 1401 in the US v1.0
# extraction and 1416 in Rev A. The addon cannot know it and must not guess it,
# so it writes a *sentinel*
# instead (:data:`CUSTOM_ID_BASE` plus the ordinal) and the runtime rewrites it
# as the model is served, exactly as it already rewrites a header's model and
# object-map fields. See ``docs/CUSTOM_TRACKS.md``.
#
# **What the hardware fixes, and it is not negotiable.** The RDP has 4 KiB of
# texture memory and ``material_init`` loads a whole texture as one block, so
# 2048 texels is the ceiling for a 16-bit format - 64x32, not 64x64. And its
# mask loop only walks the powers of two up to 64, so a texture wider or taller
# than that clamps instead of tiling however its flags are set. Both are
# checked here rather than discovered as a corrupt road in game.

#: The texture id a track writes for its own first added texture, with the rest
#: following it. Any value works that no retail id can reach and that survives
#: ``load_texture``'s ``id & 0x7FFF``; 0x7000 is an order of magnitude above the
#: largest retail table and half the 15-bit space below the mask.
CUSTOM_ID_BASE = 0x7000

#: How many a track may add. A model's texture table is indexed by a ``u8`` with
#: 0xFF spoken for, so 255 entries is the ceiling on the *table* - a track
#: cannot reference more artwork than that whatever its source.
CUSTOM_ID_COUNT = 255

#: ``sizeof(TextureHeader)``, from ``include/structs.h``.
TEXTURE_HEADER_SIZE = 32

#: ``sizeof(TempTexHeader)``. ``load_texture`` peeks this much before it knows
#: how large the texture is, so a payload shorter than this would have the
#: loader read past its end.
TEMP_HEADER_SIZE = 40

#: The RDP's texture memory. A level texture is loaded as a single block, so
#: this is a hard ceiling on the image and not a guideline.
TMEM_BYTES = 4096

#: ``material_init`` walks ``size`` over 1, 2, 4 ... 64 looking for an exact
#: match; a dimension it never lands on is given ``G_TX_CLAMP`` and
#: ``G_TX_NOMASK``. So 64 is the largest side that can tile, whatever the wrap
#: flags say.
MAX_WRAP_SIZE = 64

# **An invariant the high-resolution texture packs depend on.** The identity
# RT64 replaces a texture by (:mod:`.rice_identity`) is hashed over a rectangle
# the renderer derives from the tile ``material_init`` sets up - its mask, its
# clamp bits and the DXT of the load - and not from the image's own size. The
# two agree for every texture :func:`check_size` accepts: a power of two no
# larger than MAX_WRAP_SIZE makes the mask exactly the side, and
# ``gDPLoadTextureBlock`` makes the tile exactly the image whichever wrap flags
# :func:`texture_header` writes. Loosen either rule and every pack already
# handed out stops matching, silently - the game draws the 64x32 and nothing
# says why. ``tests/test_rice_identity.py`` derives the rectangle the way the
# renderer does, for every size and format accepted here, and fails if the two
# ever part.

#: ``TEXTURE_RENDER_MODES`` in the asset tool, which is what the high nibble of
#: ``TextureHeader.format`` holds. ``material_init`` reads it: the two
#: transparent modes set ``RENDER_SEMI_TRANSPARENT`` on the texture.
RENDER_MODES = {
    "TRANSPARENT": 0,
    "OPAQUE": 1,
    "TRANSPARENT_2": 2,
    "OPAQUE_2": 3,
}

#: Bits per texel, by the format code in :data:`FORMAT_CODES`.
FORMAT_BITS = {0: 32, 1: 16, 2: 8, 3: 4, 4: 16, 5: 8, 6: 4, 7: 4, 8: 8}

#: The formats a custom texture may use. CI4 and CI8 are excluded on purpose:
#: their palettes are loaded from ``ASSET_EMPTY_14`` by a byte offset into that
#: section, and a track cannot add one - ``ciPaletteOffset`` would have to name
#: a palette the ROM already holds, which is not a texture an author chose.
CUSTOM_FORMATS = ("RGBA16", "RGBA32", "I8", "I4", "IA16", "IA8", "IA4")


class TextureEncodeError(Exception):
    """The image cannot be made into a texture, with the reason to act on."""


def custom_id(ordinal):
    """The sentinel id a track's ``ordinal``-th own texture is written under."""
    ordinal = int(ordinal)
    if not 0 <= ordinal < CUSTOM_ID_COUNT:
        raise TextureEncodeError(
            "a track can add at most %d textures of its own" % CUSTOM_ID_COUNT
        )
    return CUSTOM_ID_BASE + ordinal


def is_custom_id(texture_id) -> bool:
    """Whether an id in a model's texture table names the track's own artwork."""
    try:
        value = int(texture_id)
    except (TypeError, ValueError):
        return False
    return CUSTOM_ID_BASE <= value < CUSTOM_ID_BASE + CUSTOM_ID_COUNT


def custom_ordinal(texture_id):
    """Which of a track's own textures an id names, or ``None``."""
    return int(texture_id) - CUSTOM_ID_BASE if is_custom_id(texture_id) else None


def texel_bytes(width, height, texture_format) -> int:
    """``ImageHelper::image_size``: the image alone, with no header."""
    bits = FORMAT_BITS.get(int(texture_format))
    if bits is None:
        raise TextureEncodeError("no such texture format: %r" % (texture_format,))
    return (int(width) * int(height) * bits) // 8


def align16(value) -> int:
    return (int(value) + 15) & ~15


def payload_size(width, height, texture_format, frames=1) -> int:
    """How many bytes the whole asset takes, header and padding included."""
    one = TEXTURE_HEADER_SIZE + texel_bytes(width, height, texture_format)
    return align16(one * max(1, int(frames)))


def usable_sizes(texture_format) -> List:
    """``(width, height)`` a texture of this format can actually be.

    Every power of two from 4 to 64 on both sides, kept only where the image
    fits texture memory. For RGBA16 that stops at 64x32; for the eight-bit
    formats it reaches 64x64.
    """
    powers = [1 << shift for shift in range(2, 7)]
    return [
        (width, height)
        for width in powers
        for height in powers
        if texel_bytes(width, height, texture_format) <= TMEM_BYTES
    ]


def largest_size(texture_format):
    """The biggest usable size: most texels, then squarest, then widest.

    The last tiebreak is what picks 64x32 over 32x64 for a 16-bit format. They
    are the same texture turned on its side, and a track surface is far more
    often longer than it is wide.
    """
    sizes = usable_sizes(texture_format)
    if not sizes:
        return None
    return max(sizes, key=lambda wh: (wh[0] * wh[1], -abs(wh[0] - wh[1]), wh[0]))


def best_size(texture_format, width, height):
    """The size to resample a picture of this shape to.

    Always one of the largest the format allows - detail is the scarce thing
    here and there is very little of it to give away - and among those, the one
    whose proportions are closest to the image's. For a 16-bit format that is
    the choice between 64x32 and 32x64, which is exactly the difference between
    a photograph laid down the road and the same photograph on its side. A
    square picture has no preference between them and is given the wider, for
    the same reason :func:`largest_size` is.
    """
    sizes = usable_sizes(texture_format)
    if not sizes:
        return None
    widest = max(across * down for across, down in sizes)
    ratio = float(width or 1) / float(height or 1)
    return min(
        [size for size in sizes if size[0] * size[1] == widest],
        key=lambda size: (abs(math.log((float(size[0]) / size[1]) / ratio)),
                          -size[0]),
    )


def check_size(width, height, texture_format) -> None:
    """Refuse a size the hardware cannot draw, saying which limit it hit."""
    width, height = int(width), int(height)
    if width <= 0 or height <= 0:
        raise TextureEncodeError("a texture cannot be %dx%d" % (width, height))
    if width > 255 or height > 255:
        raise TextureEncodeError(
            "%dx%d does not fit the u8 width and height a TextureHeader stores"
            % (width, height)
        )
    for side, label in ((width, "width"), (height, "height")):
        if side & (side - 1):
            raise TextureEncodeError(
                "%d is not a power of two, and material_init gives any other "
                "%s G_TX_CLAMP - the texture would stretch once across each "
                "face instead of tiling" % (side, label)
            )
        if side > MAX_WRAP_SIZE:
            raise TextureEncodeError(
                "%d is past the 64 material_init's mask loop reaches, so the "
                "%s would clamp rather than wrap however the flags are set"
                % (side, label)
            )
    size = texel_bytes(width, height, texture_format)
    if size > TMEM_BYTES:
        fits = largest_size(texture_format) or (0, 0)
        raise TextureEncodeError(
            "%dx%d in this format is %d bytes and the RDP has %d of texture "
            "memory, which a level texture is loaded into as one block. "
            "%dx%d would fit"
            % (width, height, size, TMEM_BYTES, fits[0], fits[1])
        )


# ---------------------------------------------------------------------------
# Reading a PNG
# ---------------------------------------------------------------------------
#
# Decoded here rather than through Blender because the encoding below is what
# the tests hold to the retail format, and a test that needed Blender to reach
# it would not be run. ``zlib`` is in the standard library, so all that is left
# is undoing the per-scanline filters - which, for a non-interlaced image, is
# the whole of the PNG format.

_PNG_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def read_png(path):
    """``(width, height, rgba)``, eight bits a channel, top row first.

    Handles the colour types Blender writes and the ones an extraction holds:
    greyscale, RGB and palette, either with alpha, at 8 or 16 bits a channel.
    Interlaced images are refused rather than silently mangled.
    """
    import zlib

    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise TextureEncodeError("%s is not a PNG" % path)

    width = height = depth = colour = 0
    palette = b""
    transparency = b""
    data = bytearray()
    at = 8
    while at + 8 <= len(blob):
        length, kind = struct.unpack_from(">I4s", blob, at)
        body = blob[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", body
            )
            if interlace:
                raise TextureEncodeError(
                    "%s is interlaced (Adam7); save it without interlacing" % path
                )
            if colour not in _PNG_CHANNELS:
                raise TextureEncodeError(
                    "%s uses PNG colour type %d, which is not one of the five"
                    % (path, colour)
                )
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            transparency = body
        elif kind == b"IDAT":
            data += body
        elif kind == b"IEND":
            break

    if not width or not height:
        raise TextureEncodeError("%s has no IHDR" % path)
    if colour == 3 and depth != 8:
        raise TextureEncodeError(
            "%s is a %d-bit palette image; save it as 8-bit or as RGB"
            % (path, depth)
        )
    if colour != 3 and depth not in (8, 16):
        raise TextureEncodeError(
            "%s is %d bits a channel; only 8 and 16 are read" % (path, depth)
        )

    channels = _PNG_CHANNELS[colour]
    sample = 2 if depth == 16 else 1
    stride = width * channels * sample
    raw = zlib.decompress(bytes(data))
    if len(raw) < (stride + 1) * height:
        raise TextureEncodeError("%s ended early; the image data is short" % path)

    lines = _unfilter(raw, stride, height, channels * sample)
    return width, height, _to_rgba(lines, width, height, channels, depth,
                                   colour, palette, transparency)


def _unfilter(raw, stride, height, step):
    """Undo the five PNG scanline filters, dropping the filter byte off each."""
    out = bytearray(stride * height)
    previous = bytearray(stride)
    at = 0
    for row in range(height):
        method = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        if method == 1:
            for index in range(step, stride):
                line[index] = (line[index] + line[index - step]) & 0xFF
        elif method == 2:
            for index in range(stride):
                line[index] = (line[index] + previous[index]) & 0xFF
        elif method == 3:
            for index in range(stride):
                left = line[index - step] if index >= step else 0
                line[index] = (line[index] + ((left + previous[index]) >> 1)) & 0xFF
        elif method == 4:
            for index in range(stride):
                left = line[index - step] if index >= step else 0
                upleft = previous[index - step] if index >= step else 0
                up = previous[index]
                estimate = left + up - upleft
                near_left = abs(estimate - left)
                near_up = abs(estimate - up)
                near_upleft = abs(estimate - upleft)
                if near_left <= near_up and near_left <= near_upleft:
                    nearest = left
                elif near_up <= near_upleft:
                    nearest = up
                else:
                    nearest = upleft
                line[index] = (line[index] + nearest) & 0xFF
        elif method != 0:
            raise TextureEncodeError("unknown PNG filter %d" % method)
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return out


def _to_rgba(lines, width, height, channels, depth, colour, palette,
             transparency):
    """One byte a channel, four channels, whatever the file stored."""
    out = bytearray(width * height * 4)
    sample = 2 if depth == 16 else 1
    step = channels * sample
    for pixel in range(width * height):
        at = pixel * step
        values = [lines[at + index * sample] for index in range(channels)]
        if colour == 0:
            red = green = blue = values[0]
            alpha = 0xFF
        elif colour == 4:
            red = green = blue = values[0]
            alpha = values[1]
        elif colour == 2:
            red, green, blue = values
            alpha = 0xFF
        elif colour == 6:
            red, green, blue, alpha = values
        else:
            index = values[0]
            if (index + 1) * 3 > len(palette):
                red = green = blue = 0
            else:
                red, green, blue = palette[index * 3:index * 3 + 3]
            alpha = transparency[index] if index < len(transparency) else 0xFF
        out[pixel * 4:pixel * 4 + 4] = bytes((red, green, blue, alpha))
    return out


# ---------------------------------------------------------------------------
# Writing a texture asset
# ---------------------------------------------------------------------------
#
# Byte for byte what ``dkr_assets_tool``'s ``BuildTexture::build`` produces for
# an uncompressed texture, because that is what the ROM holds and what
# ``load_texture`` reads. Compression is not offered: the header's
# ``isCompressed`` byte sends the loader through ``gzip_inflate``, and a track
# would be trading a few kilobytes of disk for one more thing that can go wrong.

def _scale_8_5(value) -> int:
    return ((value + 4) * 0x1F) // 0xFF


def _intensity(red, green, blue) -> int:
    """``png2ia``'s: the mean of the three, rounded the way the tool rounds."""
    return (red + green + blue + 1) // 3


def encode_texels(rgba, width, height, texture_format) -> bytes:
    """Eight-bit RGBA into one of the RDP formats, row-major and unswizzled."""
    count = int(width) * int(height)
    if len(rgba) < count * 4:
        raise TextureEncodeError(
            "the image is %d pixels but only %d were given"
            % (count, len(rgba) // 4)
        )
    code = int(texture_format)
    out = bytearray(texel_bytes(width, height, code))

    if code == FORMAT_CODES["RGBA32"]:
        out[:] = bytes(rgba[:count * 4])
    elif code == FORMAT_CODES["RGBA16"]:
        for pixel in range(count):
            red, green, blue, alpha = rgba[pixel * 4:pixel * 4 + 4]
            red = _scale_8_5(red)
            green = _scale_8_5(green)
            blue = _scale_8_5(blue)
            out[pixel * 2] = ((red << 3) | (green >> 2)) & 0xFF
            out[pixel * 2 + 1] = (((green & 0x3) << 6) | (blue << 1)
                                  | (1 if alpha else 0)) & 0xFF
    elif code == FORMAT_CODES["I8"]:
        for pixel in range(count):
            red, green, blue = rgba[pixel * 4:pixel * 4 + 3]
            out[pixel] = _intensity(red, green, blue)
    elif code == FORMAT_CODES["I4"]:
        for pixel in range(count):
            red, green, blue = rgba[pixel * 4:pixel * 4 + 3]
            value = _intensity(red, green, blue) // 0x11
            if pixel % 2:
                out[pixel // 2] |= value
            else:
                out[pixel // 2] |= value << 4
    elif code == FORMAT_CODES["IA16"]:
        for pixel in range(count):
            red, green, blue, alpha = rgba[pixel * 4:pixel * 4 + 4]
            out[pixel * 2] = _intensity(red, green, blue)
            out[pixel * 2 + 1] = alpha
    elif code == FORMAT_CODES["IA8"]:
        for pixel in range(count):
            red, green, blue, alpha = rgba[pixel * 4:pixel * 4 + 4]
            out[pixel] = (((_intensity(red, green, blue) // 0x11) << 4)
                          | (alpha // 0x11))
    elif code == FORMAT_CODES["IA4"]:
        for pixel in range(count):
            red, green, blue, alpha = rgba[pixel * 4:pixel * 4 + 4]
            value = (((_intensity(red, green, blue) // 0x24) << 1)
                     | (1 if alpha else 0))
            if pixel % 2:
                out[pixel // 2] |= value
            else:
                out[pixel // 2] |= value << 4
    else:
        raise TextureEncodeError(
            "format %d cannot be written for a custom texture; a colour-indexed "
            "one would need a palette from ASSET_EMPTY_14, which a track cannot "
            "add" % code
        )
    return bytes(out)


def _quiet_bit(texture_format, texel):
    """``(byte, mask)`` of the least visible bit in one texel.

    Never an alpha bit - in RGBA16 that is the lowest bit of the texel, and
    flipping it would punch a hole - but the lowest bit of blue, or of the
    intensity.
    """
    code = int(texture_format)
    texel = int(texel)
    if code == FORMAT_CODES["RGBA32"]:
        return texel * 4 + 2, 0x01
    if code == FORMAT_CODES["RGBA16"]:
        return texel * 2 + 1, 0x02
    if code == FORMAT_CODES["I8"]:
        return texel, 0x01
    if code == FORMAT_CODES["IA16"]:
        return texel * 2, 0x01
    if code == FORMAT_CODES["IA8"]:
        return texel, 0x10
    if code == FORMAT_CODES["I4"]:
        return texel // 2, 0x10 if texel % 2 == 0 else 0x01
    if code == FORMAT_CODES["IA4"]:
        return texel // 2, 0x20 if texel % 2 == 0 else 0x02
    raise TextureEncodeError("format %d has no texels to nudge" % code)


def nudge_texels(texels, width, height, texture_format, nudge=0) -> bytes:
    """The texels with one invisible bit flipped, or unchanged for ``nudge`` 0.

    Why this exists: a high-resolution pack names each replacement by a hash of
    the texels. Two of a track's textures that reduce to the same 64x32 - two
    photographs of one wall, say - would share a name, and only one of their
    originals could be drawn. Flipping the lowest bit of one texel's blue is a
    change no one can see at 64x32 and gives the second its own name.

    Which texel is ``nudge - 1``. Not every nudge changes the name - the CRC
    adds the first word of each row twice, once XORed with the row number, and
    a flip there can cancel - so the export tries them in turn
    (:func:`.rice_identity.free_nudge`) and keeps the first that works.
    """
    nudge = int(nudge or 0)
    if nudge <= 0:
        return bytes(texels)
    count = int(width) * int(height)
    at, mask = _quiet_bit(texture_format, (nudge - 1) % max(1, count))
    out = bytearray(texels)
    out[at] ^= mask
    return bytes(out)


def texture_header(width, height, texture_format, render_mode="OPAQUE",
                   frames=1, frame_delay=0, clamp_s=False,
                   clamp_t=False) -> bytes:
    """The 32 bytes in front of the image. Fields the game fills stay zero.

    ``numberOfCommands`` and ``cmd`` are written by ``material_init`` once the
    texture is in RAM, and ``ciPaletteOffset`` is meaningless without a palette;
    all three are zero in the ROM too.
    """
    header = bytearray(TEXTURE_HEADER_SIZE)
    mode = RENDER_MODES.get(str(render_mode).upper())
    if mode is None:
        raise TextureEncodeError("no such render mode: %r" % (render_mode,))
    flags = (int(bool(clamp_s)) << 6) | (int(bool(clamp_t)) << 7)
    header[0x00] = int(width) & 0xFF
    header[0x01] = int(height) & 0xFF
    header[0x02] = ((mode & 0xF) << 4) | (int(texture_format) & 0xF)
    header[0x05] = 1  # numberOfInstances, always 1 in the ROM
    struct.pack_into(">h", header, 0x06, flags)
    header[0x12] = max(1, int(frames)) & 0xFF
    struct.pack_into(">H", header, 0x14, int(frame_delay) & 0xFFFF)
    struct.pack_into(
        ">h", header, 0x16,
        align16(TEXTURE_HEADER_SIZE + texel_bytes(width, height, texture_format)),
    )
    return bytes(header)


def encode_texture(png_path, texture_format=None, render_mode="OPAQUE",
                   clamp_s=False, clamp_t=False, nudge=0,
                   transparency_mode=None) -> bytes:
    """One PNG as the bytes ``ASSET_TEXTURES_3D`` holds for a texture.

    The result is padded to sixteen bytes because ``load_texture`` puts the
    display list it builds at ``align16(tex + assetSize)`` inside an allocation
    of exactly ``assetSize`` plus the display lists - so a payload that is not a
    multiple of sixteen pushes the last one past the end of its own block.

    ``nudge`` is :func:`nudge_texels`'s, and is zero for all but a texture that
    had to be told apart from another. ``transparency_mode`` is
    :func:`.transparency.prepare`'s: the pixels are made to agree with the look
    before they are encoded, and ``None`` leaves them as the PNG holds them.
    """
    code = FORMAT_CODES["RGBA16"] if texture_format is None else int(texture_format)
    width, height, rgba = read_png(png_path)
    check_size(width, height, code)
    rgba = transparency.prepare(rgba, width, height, transparency_mode, code)
    payload = bytearray()
    payload += texture_header(width, height, code, render_mode,
                              clamp_s=clamp_s, clamp_t=clamp_t)
    payload += nudge_texels(encode_texels(rgba, width, height, code),
                            width, height, code, nudge)
    while len(payload) % 16:
        payload.append(0)
    if len(payload) < TEMP_HEADER_SIZE:
        raise TextureEncodeError(
            "a %dx%d texture is only %d bytes and load_texture reads %d before "
            "it knows the size" % (width, height, len(payload), TEMP_HEADER_SIZE)
        )
    return bytes(payload)


class CustomTexture:
    """A track's own artwork, wearing the same face as a ROM texture.

    Everything that browses, applies and exports a texture reads ``index``,
    ``width``, ``height``, ``format``, ``frames`` and ``png``, so presenting
    those is the whole of what makes an added image usable everywhere a ROM one
    is - the browser draws it, ``allocate`` gives it a table entry, the UV
    arithmetic divides by its size. ``index`` is the sentinel the runtime
    rewrites; see the section comment above.
    """

    __slots__ = ("ordinal", "name", "png", "width", "height", "format",
                 "legacy_render_mode", "mode", "source", "original", "nudge",
                 "faded")

    def __init__(self, ordinal, name, png, width, height, texture_format,
                 render_mode="OPAQUE", source="", original="", nudge=0,
                 transparency_mode=None, faded=False):
        self.ordinal = int(ordinal)
        self.name = name
        self.png = png
        self.width = int(width)
        self.height = int(height)
        self.format = int(texture_format)
        #: What a texture added before transparency existed was written with.
        self.legacy_render_mode = render_mode or "OPAQUE"
        #: The look, from :data:`.transparency.MODES`, or ``None`` for a texture
        #: added before there was a choice - encoded exactly as it always was,
        #: so an old scene's payloads, and the HD pack names hashed from them,
        #: do not change under an author who did nothing.
        self.mode = (transparency.own_mode(transparency_mode, self.format)
                     if transparency_mode else None)
        #: The image the author picked, kept so the panel can say where it came
        #: from. The PNG beside it is what is encoded.
        self.source = source
        #: The picture at the resolution the author made it, as a PNG beside the
        #: reduced one. Never encoded into the track - it is what the
        #: high-resolution pack hands RT64 to draw instead.
        self.original = original
        #: :func:`nudge_texels`'s, kept so every export writes the same bytes.
        self.nudge = int(nudge or 0)
        #: Whether a material fading its faces draws it, which is the export's
        #: to know: such a texture is written see-through so the faces are drawn
        #: in the second pass - see :func:`.transparency.faded_render_mode`.
        #: The look an author sees does not change.
        self.faded = bool(faded)

    @property
    def index(self) -> int:
        return custom_id(self.ordinal)

    @property
    def render_mode(self) -> str:
        """The header's render mode: the look's, or what an old texture had."""
        if self.mode is None:
            mode = self.legacy_render_mode
        else:
            mode = transparency.render_mode_for(self.mode)
        if self.faded:
            return transparency.faded_render_mode(mode, self.format)
        return mode

    @property
    def pixel_mode(self):
        """The look the pixels are prepared for, :func:`.transparency.prepare`'s.

        The look itself, but a faded texture from before transparency existed
        that was solid is made solid: written see-through, its alpha would
        start to count, and the fade is meant to be the vertex alpha alone.
        """
        if (self.mode is None and self.faded
                and not transparency.translucent(self.format,
                                                 self.legacy_render_mode)):
            return transparency.OPAQUE
        return self.mode

    @property
    def translucent(self) -> bool:
        return transparency.translucent(self.format, self.render_mode)

    @property
    def transparency(self) -> str:
        """The look faces drawing this texture take unless told otherwise."""
        if self.mode is not None:
            return self.mode
        return transparency.BLEND if self.translucent else transparency.OPAQUE

    @property
    def own(self) -> bool:
        return True

    @property
    def group(self) -> str:
        return "yours"

    @property
    def frames(self) -> int:
        # One. An animated custom texture is several images in one asset with a
        # frame delay, and nothing here builds that yet.
        return 1

    @property
    def animated(self) -> bool:
        return False

    @property
    def asset_id(self) -> str:
        return "CUSTOM_TEXTURE_%d" % self.ordinal

    @property
    def label(self) -> str:
        return "%s  %dx%d" % (self.name, self.width, self.height)

    def encode(self) -> bytes:
        return encode_texture(self.png, self.format, self.render_mode,
                              nudge=self.nudge, transparency_mode=self.pixel_mode)

    def texels(self, nudge=None) -> bytes:
        """The image as ``encode`` writes it, without the header.

        ``nudge`` overrides the texture's own; 0 gives the plain reduction.
        """
        width, height, rgba = read_png(self.png)
        check_size(width, height, self.format)
        rgba = transparency.prepare(rgba, width, height, self.pixel_mode,
                                    self.format)
        return nudge_texels(encode_texels(rgba, width, height, self.format),
                            width, height, self.format,
                            self.nudge if nudge is None else nudge)

    def __repr__(self):
        return "CustomTexture(%d, %r, %dx%d)" % (
            self.ordinal, self.name, self.width, self.height
        )
