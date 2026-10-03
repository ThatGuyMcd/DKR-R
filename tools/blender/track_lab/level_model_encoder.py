"""Encode a decoded level model back into the bytes the game loads.

This is to :mod:`level_model` what :mod:`object_map_encoder` is to
:mod:`gltf_io`: the decoder reads a track's geometry so an author can see it,
and this module turns the same data back into a ``LEVEL_MODELS`` section
payload. Together they are what lets the addon change the track itself rather
than only the objects standing on it.

The gate is byte equality. ``tests/test_level_model_roundtrip.py`` decodes every
extracted retail level model, re-encodes it from the decoded values alone, and
requires the result to be identical. That is the same proof the object map
encoder rests on, and it is available for the same reason: the correct bytes
already exist, so an encoder can be shown right rather than argued right.

**What this module does not do.** It reproduces a model's *layout* - every array
stays where it was, at the offset the decoder found it. Nothing here re-packs
the blob, so a model whose triangle or vertex counts changed cannot be written
by this module yet; that is the next step, and :func:`check_layout` is what says
so rather than emitting a file the game would misread.

Two regions come through verbatim, and deliberately. The **PVS**
(``segmentsBitfields``) is authored visibility data that only a new segmentation
invalidates. The **collision facet arrays** are uninitialised space that
``track_init_collision`` fills at load time, so their contents are whatever the
build machine left there; reproducing them is required for byte equality and
meaningless for behaviour.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import struct
import zlib
from typing import List

from .level_model import (
    BATCH_SIZE, BOUNDING_BOX_SIZE, BSP_NODE_SIZE, CONTAINER_TAG, ENDIAN,
    HEADER_FIELDS, SEGMENT_FIELDS, SEGMENT_SIZE, SEGMENT_TAIL,
    TEXTURE_INFO_SIZE, TRIANGLE_SIZE, VERTEX_SIZE, LevelModel, Segment,
)


class LevelModelEncodeError(Exception):
    pass


def check_layout(model: LevelModel) -> List[str]:
    """Reasons this model cannot be written at its recorded offsets.

    An in-place encoder is only correct while every array still fits the window
    the decoder read it from. Editing a vertex position keeps that true; adding
    a vertex does not. The reasons are returned rather than raised so a caller
    can report all of them at once.
    """
    problems = []
    if model.blob_size <= 0:
        problems.append("the model carries no blob size, so nothing sizes the output")

    # The texture table is the first array after the header, so an extra entry
    # does not run off the end of the blob - it runs into the segment array,
    # which every bounds check here would happily allow. Giving a track a
    # texture it did not ship with is therefore a count change like any other,
    # and takes the same road: rebuild the layout.
    if len(model.textures) != model.texture_count_field:
        problems.append(
            "the model holds %d textures but its count field says %d; the "
            "layout would have to be rebuilt"
            % (len(model.textures), model.texture_count_field)
        )

    for segment in model.segments:
        label = "segment %d" % segment.index
        if len(segment.vertices) != segment.vertex_count_field:
            problems.append(
                "%s holds %d vertices but its count field says %d; the layout "
                "would have to be rebuilt"
                % (label, len(segment.vertices), segment.vertex_count_field)
            )
        if len(segment.triangles) != segment.triangle_count_field:
            problems.append(
                "%s holds %d triangles but its count field says %d"
                % (label, len(segment.triangles), segment.triangle_count_field)
            )
        if len(segment.batches) != segment.batch_count_field:
            problems.append(
                "%s holds %d batches but its count field says %d"
                % (label, len(segment.batches), segment.batch_count_field)
            )
        if segment.batches and segment.batch_terminator is None:
            problems.append(
                "%s has batches but no terminator entry, so no batch has an end"
                % label
            )
        for index, batch in enumerate(segment.batches):
            # gSPPolygon encodes count - 1 in four bits; a larger array can
            # fit in the file while most of its triangles disappear in game.
            if batch.face_count > 16:
                problems.append(
                    "%s batch %d holds %d triangles, but the game draws at "
                    "most 16 per batch; rebuild the batches before exporting"
                    % (label, index, batch.face_count)
                )
    return problems


def encode(model: LevelModel) -> bytes:
    """The decompressed section payload for one level model."""
    problems = check_layout(model)
    if problems:
        raise LevelModelEncodeError(problems[0])

    buffer = bytearray(model.blob_size)

    # Carried-through regions go down first. Every structured write below lands
    # on a range none of them covers, so an overlap means the decoder claimed
    # something twice - and writing in this order lets the round trip surface
    # that instead of hiding it under a later write.
    for offset, data in model.gaps:
        _blit(buffer, offset, data)
    for offset, data in model.opaque:
        _blit(buffer, offset, data)

    _write_header(buffer, model)
    _write_textures(buffer, model)
    _write_side_tables(buffer, model)
    for index, segment in enumerate(model.segments):
        _write_segment(buffer, model.segments_ptr + index * SEGMENT_SIZE, segment)
    return bytes(buffer)


#: Where the DEFLATE stream's first block's data begins, counting from the
#: start of the payload: five bytes of container, then a stored block's own
#: header. That header is one byte of ``BFINAL``/``BTYPE``, then - after the
#: reader discards the five bits left in it, which is what a stored block does -
#: ``LEN`` and ``NLEN``, two bytes each. See :func:`pack`.
STORED_PREFIX_AT = 10

#: The most a single stored block can hold. A model's header and texture table
#: are two thousand bytes at the very most, so this is a check rather than a
#: case to handle.
STORED_BLOCK_MAX = 0xFFFF


def stored_prefix_size(model: LevelModel) -> int:
    """How much of the model :func:`pack` writes without compressing it.

    Everything up to the end of the texture table: the header names the table's
    offset at 0x00 and its length at 0x18, and the table is the first array
    after the header, so this is a short region at the very front of the file.
    """
    return model.textures_ptr + len(model.textures) * TEXTURE_INFO_SIZE


def pack(model: LevelModel, level: int = 9) -> bytes:
    """The payload inside the five-byte container the asset table stores.

    **The header and the texture table are written uncompressed, and that is
    load-bearing.** A track that ships artwork of its own cannot know the
    texture ids it will get: the index is the ROM's retail texture count plus an
    ordinal, and the count belongs to the player's cartridge. So the exporter
    writes a placeholder and DKR-R rewrites it as the model is served - the same
    thing it already does to a header's model and object-map fields.

    It can only do that to bytes it can find. A DEFLATE stream is bit-packed,
    so a four-byte field inside a compressed block has no byte offset to patch.
    The answer is the format's own: DEFLATE block type 00 is *stored*, which is
    byte-aligned and verbatim, and ``gzip_inflate_block`` dispatches to
    ``gzip_inflate_stored`` for it exactly as it does to the Huffman decoders
    for the other two. So the front of the model - through the end of the
    texture table - goes in one stored block and the rest is compressed as
    before. The stream stays a legal DEFLATE stream, the game inflates it with
    the code it always used, and the texture ids sit at
    :data:`STORED_PREFIX_AT` plus their own offsets where anything can find
    them.

    The cost is the prefix's own size, about two kilobytes on the largest
    possible table, against models of a hundred to five hundred. It is paid by
    every packed model rather than only by tracks with their own artwork,
    because one format that is always patchable is worth more than two that
    differ in a way nothing downstream can see.
    """
    blob = encode(model)
    prefix = stored_prefix_size(model)
    if prefix > STORED_BLOCK_MAX or prefix > len(blob):
        raise LevelModelEncodeError(
            "the header and texture table come to %d bytes, which one stored "
            "DEFLATE block cannot hold; the texture table would have to be "
            "past %d entries for that" % (prefix, STORED_BLOCK_MAX // TEXTURE_INFO_SIZE)
        )

    head = blob[:prefix]
    # BFINAL 0, BTYPE 00. LEN and NLEN are little endian and NLEN is LEN's
    # complement, which is what lets a reader tell a stored block from noise.
    stream = bytearray(b"\x00")
    stream += struct.pack("<HH", prefix, (~prefix) & 0xFFFF)
    stream += head

    # The compressor's own output is a complete stream ending in a block with
    # BFINAL set, which is exactly what the tail should be. It begins on a byte
    # boundary because a stored block ends on one; DEFLATE allows a block to
    # start anywhere, so that is a convenience rather than a requirement.
    compressor = zlib.compressobj(level, zlib.DEFLATED, -15)
    stream += compressor.compress(blob[prefix:]) + compressor.flush()

    return struct.pack("<IB", len(blob), CONTAINER_TAG) + bytes(stream)


# ---------------------------------------------------------------------------
# Pieces
# ---------------------------------------------------------------------------

def _check_s16(values, what: str, why: str) -> None:
    """Refuse a value the format cannot hold, and say what the ceiling is.

    Without this ``struct.pack_into`` raises its own ``struct.error``, which
    names a format character and nothing about tracks. These two ceilings are
    the ones step 3 put within reach - before it, an author could not create a
    coordinate or a UV, only move one that already fitted.
    """
    for value in values:
        if not -32768 <= int(value) <= 32767:
            raise LevelModelEncodeError("%s, which does not fit: %s" % (what, why))


def _blit(buffer: bytearray, offset: int, data: bytes) -> None:
    if offset < 0 or offset + len(data) > len(buffer):
        raise LevelModelEncodeError(
            "a %d byte region at 0x%X runs past the %d byte model"
            % (len(data), offset, len(buffer))
        )
    buffer[offset:offset + len(data)] = data


def _pack_into(buffer: bytearray, offset: int, code: str, value) -> None:
    if offset < 0 or offset + struct.calcsize(ENDIAN + code) > len(buffer):
        raise LevelModelEncodeError(
            "a %s field at 0x%X runs past the %d byte model"
            % (code, offset, len(buffer))
        )
    struct.pack_into(ENDIAN + code, buffer, offset, value)


def _write_header(buffer: bytearray, model: LevelModel) -> None:
    for offset, code, name in HEADER_FIELDS:
        _pack_into(buffer, offset, code, getattr(model, name))
    struct.pack_into(ENDIAN + "6h", buffer, 0x3C, *model.bounds)


def _write_textures(buffer: bytearray, model: LevelModel) -> None:
    for index, texture in enumerate(model.textures):
        at = model.textures_ptr + index * TEXTURE_INFO_SIZE
        _pack_into(buffer, at, "i", texture.texture_id)
        _blit(buffer, at + 4, bytes((
            texture.raw_width & 0xFF, texture.raw_height & 0xFF,
            texture.format & 0xFF, texture.surface_type & 0xFF,
        )))


def _write_side_tables(buffer: bytearray, model: LevelModel) -> None:
    for index, box in enumerate(model.bounding_boxes):
        at = model.bounding_boxes_ptr + index * BOUNDING_BOX_SIZE
        if at < 0 or at + BOUNDING_BOX_SIZE > len(buffer):
            raise LevelModelEncodeError("bounding box %d runs past the model" % index)
        struct.pack_into(ENDIAN + "6h", buffer, at, *box)

    for index, node in enumerate(model.bsp):
        at = model.bsp_ptr + index * BSP_NODE_SIZE
        left, right, split_type, segment_index, split_value = node
        _pack_into(buffer, at, "h", left)
        _pack_into(buffer, at + 2, "h", right)
        _blit(buffer, at + 4, bytes((split_type & 0xFF, segment_index & 0xFF)))
        _pack_into(buffer, at + 6, "h", split_value)


def _write_segment(buffer: bytearray, base: int, segment: Segment) -> None:
    for offset, code, name in SEGMENT_FIELDS:
        _pack_into(buffer, base + offset, code, getattr(segment, name))
    _blit(buffer, base + SEGMENT_TAIL, segment.tail_bytes)

    for index, position in enumerate(segment.vertices):
        at = segment.vertices_ptr + index * VERTEX_SIZE
        if at < 0 or at + VERTEX_SIZE > len(buffer):
            raise LevelModelEncodeError(
                "segment %d vertex %d runs past the model" % (segment.index, index)
            )
        _check_s16(
            position,
            "segment %d vertex %d sits at %r" % (segment.index, index, tuple(position)),
            "a level model stores positions as s16, so a track reaches "
            "-32768..32767 on each axis - about three times the extent of the "
            "largest retail track",
        )
        struct.pack_into(ENDIAN + "3h", buffer, at, *position)
        colour = segment.colours[index] if index < len(segment.colours) else (0, 0, 0, 0)
        _blit(buffer, at + 6, bytes(c & 0xFF for c in colour))

    for index, triangle in enumerate(segment.triangles):
        at = segment.triangles_ptr + index * TRIANGLE_SIZE
        if at < 0 or at + TRIANGLE_SIZE > len(buffer):
            raise LevelModelEncodeError(
                "segment %d triangle %d runs past the model" % (segment.index, index)
            )
        _blit(buffer, at, bytes(v & 0xFF for v in triangle))
        uv = segment.uvs[index] if index < len(segment.uvs) else ((0, 0),) * 3
        flat = [component for pair in uv for component in pair]
        _check_s16(
            flat,
            "segment %d triangle %d has UVs %r" % (segment.index, index, uv),
            "UVs are s16 fixed point with five fractional bits, measured in "
            "texels, so a face can span at most 1024 texels of its texture - "
            "roughly 32 tiles of a 32 texel texture. Retail's widest is 1023.7, "
            "so this is a real ceiling and not a wide one: a face bigger than "
            "that has to stretch its texture rather than tile it further",
        )
        struct.pack_into(ENDIAN + "6h", buffer, at + 4, *flat)

    entries = list(segment.batches)
    if segment.batch_terminator is not None:
        entries.append(segment.batch_terminator)
    for index, batch in enumerate(entries):
        _write_batch(buffer, segment.batches_ptr + index * BATCH_SIZE, batch)


def _write_batch(buffer: bytearray, at: int, batch) -> None:
    _blit(buffer, at, bytes((batch.texture_index & 0xFF,)))
    _pack_into(buffer, at + 1, "b", batch.vertex_override)
    _pack_into(buffer, at + 2, "h", batch.vertex_offset)
    _pack_into(buffer, at + 4, "h", batch.face_offset)
    _blit(buffer, at + 6, bytes((batch.misc & 0xFF, batch.texture_offset & 0xFF)))
    _pack_into(buffer, at + 8, "I", batch.flags)
