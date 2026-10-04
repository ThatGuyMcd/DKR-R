"""Decoder for DKR level geometry.

docs/LEVEL_MODEL_FORMAT.md describes this format for the benefit of a future
*encoder*, which is what Phase 2 of the addon plan is blocked on. Reading it is a
separate and much smaller problem, and worth solving now: without the track in
front of them, an author is placing zippers and checkpoints against an empty
viewport with nothing to aim at.

So this module decodes a level model into plain vertex, triangle and batch
lists. The addon builds a read-only reference mesh out of that, which gives
placement something to sit on and makes Blender's own face snapping work. None
of it is written back - the geometry an author sees is exactly the geometry the
game already ships.

Every offset here was verified against Ancient Lake
(``levels/models/dino_domain/ancient_lake.bin``), whose decoded values match the
figures in the format document: 25 textures, 24 segments, 7 animated textures,
bounds X -5918..-23, Y -56..885, Z -12559..-2048.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import os
import struct
import zlib
from typing import List, Optional, Tuple

#: Container: decompressed size as u32 little endian, then this tag byte, then a
#: raw DEFLATE stream with no zlib or gzip wrapper.
CONTAINER_TAG = 0x09
CONTAINER_HEADER = 5

#: Everything past the container is N64 data, so big endian.
ENDIAN = ">"

SEGMENT_SIZE = 0x44
BATCH_SIZE = 12
TRIANGLE_SIZE = 16
VERTEX_SIZE = 10

#: ``textures_sprites.h``. The bits are independent: an invisible wall is
#: HIDDEN set with NO_COLLISION clear.
RENDER_HIDDEN = 1 << 8
RENDER_NO_COLLISION = 1 << 9

#: ``textures_sprites.h``: this batch's texture is an animated one, and
#: ``track_tex_anim`` should advance it. Unlike the other two this one is not a
#: choice - it says something true or false about the artwork the batch draws,
#: and retail agrees with itself perfectly: across all 10,389 batches in the 55
#: level models, the bit is set on exactly the 619 whose texture has more than
#: one frame and on none of the other 9,770. So the addon sets it from the
#: texture rather than exposing it, and an author who picks the water texture
#: gets water that moves.
RENDER_TEX_ANIM = 1 << 16

#: ``TriangleBatchInfo.textureIndex``.
NO_TEXTURE = 0xFF

#: How many entries a model's texture table can hold. The index above is a
#: ``u8`` and 0xFF is spoken for, so 255 - which is a real ceiling only for a
#: track that goes looking for one: retail's largest table is Spaceport Alpha's
#: 63, and the median track has 23.
MAX_TEXTURES = NO_TEXTURE

#: ``Triangle.flags``.
TRIANGLE_DRAW_BACKFACE = 0x40

#: ``DkrTextureInfo``: an id indexing the global 3D texture list, then the size
#: and format. Shared with object models, which use the same table.
TEXTURE_INFO_SIZE = 8

#: A triangle's UVs are fixed point with five fractional bits, in texels rather
#: than normalised, so a normalised coordinate is ``raw / 32 / texture_size``.
#: Track surfaces tile heavily, so values far outside 0..1 are normal.
UV_FRACTIONAL_BITS = 32.0


class TextureRef:
    """One entry of a model's texture table."""

    __slots__ = ("texture_id", "width", "height", "format", "surface_type",
                 "raw_width", "raw_height")

    def __init__(self, texture_id, width, height, texture_format, surface_type):
        #: Index into ``ASSET_TEXTURES_3D``, which is what names the PNG.
        self.texture_id = texture_id
        #: As stored. ``width`` and ``height`` below substitute 1 for 0 so a
        #: UV divide is safe, and that substitution must not reach the file
        #: again - hence keeping both.
        self.raw_width = width
        self.raw_height = height
        self.width = width or 1
        self.height = height or 1
        self.format = texture_format
        #: ``SurfaceType`` for level geometry - what the ground behaves like.
        self.surface_type = surface_type

    def __repr__(self):
        return "TextureRef(%d, %dx%d)" % (self.texture_id, self.width, self.height)


def parse_texture_table(blob, offset, count):
    """Decode ``count`` :class:`TextureRef` entries starting at ``offset``."""
    textures = []
    for index in range(max(0, count)):
        at = offset + index * TEXTURE_INFO_SIZE
        if at + TEXTURE_INFO_SIZE > len(blob):
            break
        texture_id, = struct.unpack_from(ENDIAN + "i", blob, at)
        textures.append(TextureRef(
            texture_id, blob[at + 4], blob[at + 5], blob[at + 6], blob[at + 7]
        ))
    return textures


def normalise_uv(raw_uv, texture):
    """Fixed-point texel UVs to Blender's normalised, V-flipped convention."""
    if texture is None:
        return None
    return tuple(
        (
            s / UV_FRACTIONAL_BITS / texture.width,
            1.0 - (t / UV_FRACTIONAL_BITS / texture.height),
        )
        for s, t in raw_uv
    )


class LevelModelError(Exception):
    pass


class Batch:
    """One draw call: a window of vertices and triangles sharing a texture."""

    __slots__ = ("texture_index", "vertex_offset", "face_offset", "flags",
                 "vertex_count", "face_count", "vertex_override", "misc", "texture_offset")

    def __init__(self, texture_index, vertex_offset, face_offset, flags,
                 vertex_override=0, misc=0, texture_offset=0):
        self.texture_index = texture_index
        self.vertex_offset = vertex_offset
        self.face_offset = face_offset
        self.flags = flags
        self.vertex_override = vertex_override
        self.misc = misc
        self.texture_offset = texture_offset
        self.vertex_count = 0
        self.face_count = 0

    @property
    def hidden(self) -> bool:
        return bool(self.flags & RENDER_HIDDEN)

    @property
    def collidable(self) -> bool:
        return not (self.flags & RENDER_NO_COLLISION)

    @property
    def invisible_wall(self) -> bool:
        """Not drawn, but still solid - the reason the two bits are separate."""
        return self.hidden and self.collidable

    @property
    def textured(self) -> bool:
        return self.texture_index != NO_TEXTURE


#: Fields of ``LevelModelSegment`` the loader overwrites straight after
#: inflating, so what the asset holds is whatever was in the build machine's
#: memory. ``unk8`` is the same value in every segment of every model, which is
#: what gives it away. They are still carried through verbatim, because a
#: re-encode has to reproduce the file and not merely a file the game accepts.
#: See ``tracks.c`` in the decomp, the fixup loop after ``gzip_inflate``.
SCRATCH_FIELDS = ("unk8", "unk10", "collision_planes", "unk30", "unk32",
                  "unk34")

#: ``LevelModelSegment``, 0x44 bytes. ``(offset, struct code, attribute)``.
#: Everything in the struct is listed, so writing all of it back reproduces the
#: segment exactly; nothing is inferred on the way out.
SEGMENT_FIELDS = (
    (0x00, "I", "vertices_ptr"),
    (0x04, "I", "triangles_ptr"),
    (0x08, "I", "unk8"),
    (0x0C, "I", "batches_ptr"),
    (0x10, "I", "unk10"),
    (0x14, "I", "collision_facets_ptr"),
    (0x18, "I", "collision_planes"),
    (0x1C, "h", "vertex_count_field"),
    (0x1E, "h", "triangle_count_field"),
    (0x20, "h", "batch_count_field"),
    (0x22, "H", "pad22"),
    (0x24, "I", "pad24"),
    (0x28, "h", "unk28"),
    (0x2A, "b", "unk2A"),
    (0x2B, "b", "has_waves"),
    (0x2C, "I", "unk2C"),
    (0x30, "h", "unk30"),
    (0x32, "h", "unk32"),
    (0x34, "I", "unk34"),
    (0x38, "h", "unk38"),
    (0x3A, "h", "pad3A"),
    (0x3C, "I", "unk3C"),
    (0x40, "B", "opaque_batches"),
)

#: ``LevelModel``, 0x4C bytes. Same contract as :data:`SEGMENT_FIELDS`.
HEADER_FIELDS = (
    (0x00, "I", "textures_ptr"),
    (0x04, "I", "segments_ptr"),
    (0x08, "I", "bounding_boxes_ptr"),
    (0x0C, "I", "unk_c_ptr"),
    (0x10, "I", "bitfields_ptr"),
    (0x14, "I", "bsp_ptr"),
    (0x18, "h", "texture_count_field"),
    (0x1A, "h", "segment_count_field"),
    (0x1C, "h", "unk1C"),
    (0x1E, "h", "animated_texture_count"),
    (0x20, "i", "minimap_sprite_index"),
    (0x24, "H", "minimap_rotation"),
    (0x26, "H", "unk26"),
    (0x28, "I", "minimap_x_scale"),
    (0x2C, "I", "minimap_y_scale"),
    (0x30, "h", "minimap_offset_x_adv1"),
    (0x32, "h", "minimap_offset_y_adv1"),
    (0x34, "h", "minimap_offset_x_adv2"),
    (0x36, "h", "minimap_offset_y_adv2"),
    (0x38, "I", "minimap_colour"),
    (0x48, "i", "model_size"),
)

HEADER_SIZE = 0x4C
BOUNDING_BOX_SIZE = 12
BSP_NODE_SIZE = 8

#: The three bytes after ``numberofOpaqueBatches``, kept as one blob because
#: nothing reads them.
SEGMENT_TAIL = 0x41
SEGMENT_TAIL_SIZE = 3


class Segment:
    """One spatial partition of the track."""

    __slots__ = ("index", "vertices", "colours", "triangles", "uvs", "batches",
                 "opaque_batches", "batch_terminator", "vertices_ptr",
                 "triangles_ptr", "batches_ptr", "collision_facets_ptr",
                 "vertex_count_field", "triangle_count_field",
                 "batch_count_field", "unk8", "unk10", "collision_planes",
                 "pad22", "pad24", "unk28", "unk2A", "has_waves", "unk2C",
                 "unk30", "unk32", "unk34", "unk38", "pad3A", "unk3C",
                 "tail_bytes")

    def __init__(self, index):
        self.index = index
        #: ``[(x, y, z), ...]`` in map space, still Y-up.
        self.vertices: List[Tuple[int, int, int]] = []
        #: ``[(r, g, b, a), ...]``, the baked lighting.
        self.colours: List[Tuple[int, int, int, int]] = []
        #: ``[(flags, vi0, vi1, vi2), ...]`` with batch-local indices.
        self.triangles: List[Tuple[int, int, int, int]] = []
        #: ``[((s0, t0), (s1, t1), (s2, t2)), ...]``, raw fixed point per triangle.
        self.uvs: List[Tuple[Tuple[int, int], ...]] = []
        self.batches: List[Batch] = []
        self.opaque_batches = 0
        #: The entry past the last real batch, holding the end offsets. It is
        #: part of the file, so a re-encode has to put it back.
        self.batch_terminator: Optional[Batch] = None
        self.tail_bytes = b"\x00" * SEGMENT_TAIL_SIZE
        for _offset, _code, name in SEGMENT_FIELDS:
            if not hasattr(self, name):
                setattr(self, name, 0)


class LevelModel:
    def __init__(self):
        self.segments: List[Segment] = []
        self.textures: List[TextureRef] = []
        self.animated_texture_count = 0
        self.bounds = (0, 0, 0, 0, 0, 0)
        self.minimap_sprite_index = 0
        #: Total inflated length. An encoder has to reproduce it, and ``0x48``
        #: declares it to the loader, which allocates its scratch from there.
        self.blob_size = 0
        #: ``[(x1, y1, z1, x2, y2, z2), ...]``, one per segment.
        self.bounding_boxes: List[Tuple[int, ...]] = []
        #: ``[(left, right, split_type, segment_index, split_value), ...]``.
        self.bsp: List[Tuple[int, int, int, int, int]] = []
        #: Byte ranges carried through untouched, as ``[(offset, bytes), ...]``.
        #: Two things live here. The **PVS** - ``segmentsBitfields``, which is
        #: ``numberOfSegments * ceil(numberOfSegments / 8)`` bytes of one
        #: visibility bitmask per segment - is real authored data that nothing
        #: short of a new segmentation needs to rewrite. The **collision facet
        #: arrays**, ``numberOfTriangles * 8`` bytes per segment, are authored
        #: too: each triangle's plane index and the neighbour across each edge,
        #: which ``track_init_collision`` reads to bound the triangle. A rebuilt
        #: layout regenerates both (:func:`level_model_layout.collision_facets`);
        #: otherwise both have to come out byte for byte.
        self.opaque: List[Tuple[int, bytes]] = []
        #: Bytes no structure claimed and that sit before the PVS, as
        #: ``[(offset, bytes), ...]``. In retail these are alignment padding
        #: between tables. Recorded rather than assumed away: the total is the
        #: honest measure of how much of the format is still unaccounted for,
        #: and ``tests/test_level_model_roundtrip.py`` holds it to a handful of
        #: bytes per model so that losing a whole array cannot pass as padding.
        self.gaps: List[Tuple[int, bytes]] = []
        #: ``segmentsBitfields`` on its own, without the padding that follows
        #: it. :attr:`opaque` still carries the same bytes, so the in-place
        #: encoder stays byte-exact; this is what a *rebuilt* layout writes,
        #: where the padding is the builder's to decide and the PVS is not.
        self.pvs: bytes = b""
        for _offset, _code, name in HEADER_FIELDS:
            setattr(self, name, 0)

    @property
    def texture_count(self) -> int:
        return len(self.textures)

    def texture_for(self, batch: Batch) -> Optional[TextureRef]:
        index = batch.texture_index
        if index == NO_TEXTURE or not (0 <= index < len(self.textures)):
            return None
        return self.textures[index]

    @property
    def vertex_count(self) -> int:
        return sum(len(s.vertices) for s in self.segments)

    @property
    def triangle_count(self) -> int:
        return sum(len(s.triangles) for s in self.segments)

    @property
    def batch_count(self) -> int:
        return sum(len(s.batches) for s in self.segments)

    def faces(self, include_hidden=False):
        """Yield ``(triangle_indices, colour_indices, batch)`` in world terms.

        Triangle vertex indices are batch-local, so they are resolved against
        the batch's vertex window here and come out as segment-local indices.
        The caller still offsets those by where the segment landed in a merged
        mesh.
        """
        for segment in self.segments:
            for batch in segment.batches:
                if batch.hidden and not include_hidden:
                    continue
                base = batch.vertex_offset
                for face in range(batch.face_offset,
                                  batch.face_offset + batch.face_count):
                    if face >= len(segment.triangles):
                        break
                    _flags, vi0, vi1, vi2 = segment.triangles[face]
                    yield segment, (base + vi0, base + vi1, base + vi2), batch


def decompress(data: bytes) -> bytes:
    """Unwrap the five byte container and inflate the DEFLATE stream."""
    if len(data) < CONTAINER_HEADER:
        raise LevelModelError("file is too short to be a level model")
    size, tag = struct.unpack_from("<IB", data, 0)
    if tag != CONTAINER_TAG:
        raise LevelModelError(
            "container tag is 0x%02X, expected 0x%02X; this is not a compressed "
            "level model" % (tag, CONTAINER_TAG)
        )
    try:
        blob = zlib.decompress(data[CONTAINER_HEADER:], -15)
    except zlib.error as error:
        raise LevelModelError("DEFLATE stream is corrupt: %s" % error)
    if len(blob) != size:
        raise LevelModelError(
            "inflated to %d bytes but the header declares %d" % (len(blob), size)
        )
    return blob


def parse(blob: bytes) -> LevelModel:
    """Decode an already decompressed ``LevelModel``.

    Everything the file holds is kept, including the fields the loader
    overwrites and the regions nothing models yet, so that
    :mod:`level_model_encoder` can put the same bytes back. What a structure
    does not claim is recorded as a gap rather than dropped; see
    :attr:`LevelModel.gaps`.
    """
    if len(blob) < HEADER_SIZE:
        raise LevelModelError("blob is too short to hold a LevelModel header")

    model = LevelModel()
    model.blob_size = len(blob)
    for offset, code, name in HEADER_FIELDS:
        setattr(model, name, struct.unpack_from(ENDIAN + code, blob, offset)[0])
    model.bounds = struct.unpack_from(ENDIAN + "6h", blob, 0x3C)

    segment_count = model.segment_count_field
    if segment_count < 0 or segment_count > 4096:
        raise LevelModelError("implausible segment count %d" % segment_count)

    model.textures = parse_texture_table(
        blob, model.textures_ptr, model.texture_count_field
    )

    for index in range(segment_count):
        base = model.segments_ptr + index * SEGMENT_SIZE
        if base + SEGMENT_SIZE > len(blob):
            raise LevelModelError("segment %d runs past the blob" % index)
        model.segments.append(_parse_segment(blob, base, index))

    _parse_side_tables(blob, model, segment_count)
    start, size = model.bitfields_ptr, pvs_size(segment_count)
    if 0 < start and start + size <= len(blob):
        model.pvs = bytes(blob[start:start + size])
    _record_unclaimed(blob, model)
    return model


def _parse_side_tables(blob: bytes, model: LevelModel, segment_count: int) -> None:
    """Bounding boxes and the BSP, both one entry per segment."""
    for index in range(segment_count):
        at = model.bounding_boxes_ptr + index * BOUNDING_BOX_SIZE
        if at + BOUNDING_BOX_SIZE > len(blob):
            break
        model.bounding_boxes.append(struct.unpack_from(ENDIAN + "6h", blob, at))

    for index in range(segment_count):
        at = model.bsp_ptr + index * BSP_NODE_SIZE
        if at + BSP_NODE_SIZE > len(blob):
            break
        left, right = struct.unpack_from(ENDIAN + "hh", blob, at)
        split_value, = struct.unpack_from(ENDIAN + "h", blob, at + 6)
        model.bsp.append((left, right, blob[at + 4], blob[at + 5], split_value))


def _claimed(model: LevelModel) -> List[Tuple[int, int]]:
    """Every ``(start, end)`` a decoded structure occupies."""
    spans = [(0, HEADER_SIZE)]
    spans.append((model.textures_ptr,
                  model.textures_ptr + len(model.textures) * TEXTURE_INFO_SIZE))
    spans.append((model.segments_ptr,
                  model.segments_ptr + len(model.segments) * SEGMENT_SIZE))
    spans.append((model.bounding_boxes_ptr,
                  model.bounding_boxes_ptr
                  + len(model.bounding_boxes) * BOUNDING_BOX_SIZE))
    spans.append((model.bsp_ptr, model.bsp_ptr + len(model.bsp) * BSP_NODE_SIZE))
    for segment in model.segments:
        spans.append((segment.vertices_ptr,
                      segment.vertices_ptr + len(segment.vertices) * VERTEX_SIZE))
        spans.append((segment.triangles_ptr,
                      segment.triangles_ptr
                      + len(segment.triangles) * TRIANGLE_SIZE))
        count = len(segment.batches) + (1 if segment.batch_terminator else 0)
        spans.append((segment.batches_ptr,
                      segment.batches_ptr + count * BATCH_SIZE))
    return spans


def _record_unclaimed(blob: bytes, model: LevelModel) -> None:
    """Split what no structure covers into carried-through and unexplained.

    The dividing line is ``segmentsBitfields``. At or past it lie the PVS and
    the collision facet scratch, both of which are carried verbatim on purpose.
    Anything before it is alignment padding between tables - or a decoding
    mistake, which is why it is counted separately rather than lumped in.
    """
    covered = bytearray(len(blob))
    for start, end in _claimed(model):
        if 0 <= start <= end <= len(blob):
            covered[start:end] = b"\x01" * (end - start)

    boundary = model.bitfields_ptr if 0 < model.bitfields_ptr <= len(blob) else len(blob)
    index = 0
    while index < len(covered):
        if covered[index]:
            index += 1
            continue
        end = index
        while end < len(covered) and not covered[end]:
            end += 1
        # A run can straddle the boundary, and several models do: the padding
        # before segmentsBitfields is unclaimed too, so it joins the PVS into
        # one run. Splitting it keeps the padding counted as padding instead of
        # dragging the whole carried-through tail into the gap total.
        for start, stop in _split_at(index, end, boundary):
            run = (start, bytes(blob[start:stop]))
            (model.opaque if start >= boundary else model.gaps).append(run)
        index = end


def _split_at(start: int, stop: int, boundary: int) -> List[Tuple[int, int]]:
    if start < boundary < stop:
        return [(start, boundary), (boundary, stop)]
    return [(start, stop)]


def pvs_size(segment_count: int) -> int:
    """Bytes of ``segmentsBitfields``: one visibility bitmask per segment.

    Confirmed against every extracted level model - the distance from
    ``segmentsBitfields`` to ``unkC`` is exactly this, give or take the
    alignment slack that follows it.
    """
    return segment_count * ((segment_count + 7) // 8)


def _parse_segment(blob: bytes, base: int, index: int) -> Segment:
    segment = Segment(index)
    for offset, code, name in SEGMENT_FIELDS:
        setattr(segment, name,
                struct.unpack_from(ENDIAN + code, blob, base + offset)[0])
    segment.tail_bytes = bytes(
        blob[base + SEGMENT_TAIL:base + SEGMENT_TAIL + SEGMENT_TAIL_SIZE]
    )

    vertices_ptr = segment.vertices_ptr
    triangles_ptr = segment.triangles_ptr
    batches_ptr = segment.batches_ptr
    vertex_count = segment.vertex_count_field
    triangle_count = segment.triangle_count_field
    batch_count = segment.batch_count_field

    for i in range(max(0, vertex_count)):
        offset = vertices_ptr + i * VERTEX_SIZE
        if offset + VERTEX_SIZE > len(blob):
            break
        x, y, z = struct.unpack_from(ENDIAN + "3h", blob, offset)
        segment.vertices.append((x, y, z))
        segment.colours.append(tuple(blob[offset + 6:offset + 10]))

    for i in range(max(0, triangle_count)):
        offset = triangles_ptr + i * TRIANGLE_SIZE
        if offset + TRIANGLE_SIZE > len(blob):
            break
        segment.triangles.append(tuple(blob[offset:offset + 4]))
        raw = struct.unpack_from(ENDIAN + "6h", blob, offset + 4)
        segment.uvs.append(((raw[0], raw[1]), (raw[2], raw[3]), (raw[4], raw[5])))

    # The batch array carries a terminator holding the end offsets, so each
    # batch's span is the difference to the next entry. That is the same
    # size-by-difference convention the asset tables use.
    raw = []
    for i in range(max(0, batch_count) + 1):
        offset = batches_ptr + i * BATCH_SIZE
        if offset + BATCH_SIZE > len(blob):
            break
        texture_index = blob[offset]
        vertex_override, = struct.unpack_from(ENDIAN + "b", blob, offset + 1)
        vertex_offset, face_offset = struct.unpack_from(ENDIAN + "hh", blob, offset + 2)
        misc, texture_offset = blob[offset + 6], blob[offset + 7]
        flags, = struct.unpack_from(ENDIAN + "I", blob, offset + 8)
        raw.append(Batch(texture_index, vertex_offset, face_offset, flags,
                         vertex_override, misc, texture_offset))

    for i in range(len(raw) - 1):
        batch = raw[i]
        batch.vertex_count = raw[i + 1].vertex_offset - batch.vertex_offset
        batch.face_count = raw[i + 1].face_offset - batch.face_offset
        segment.batches.append(batch)
    if raw:
        segment.batch_terminator = raw[-1]
    return segment


def load(path: str) -> LevelModel:
    """Read a ``*.bin`` level model off disk."""
    with open(path, "rb") as handle:
        data = handle.read()
    return parse(decompress(data))


#: A level model's sidecar names the binary under ``raw``, as
#: ``{"raw": "ancient_lake.bin", "type": "LevelModel"}``.
SIDECAR_TYPE = "LevelModel"


def sidecar_target(path: str) -> Optional[str]:
    """Resolve a level model's ``.json`` sidecar to the ``.bin`` beside it."""
    import json

    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    if data.get("type") != SIDECAR_TYPE:
        raise LevelModelError("%s is not a %s sidecar" % (path, SIDECAR_TYPE))
    name = data.get("raw")
    return os.path.join(os.path.dirname(path), name) if name else None
