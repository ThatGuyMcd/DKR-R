"""Decoder for DKR object models - the meshes objects are drawn with.

An object model uses the same container as a level model and the same vertex,
triangle and batch primitives; only the header differs. The layout below is
``ObjectModel`` from the asset tool's ``fileTypes/objectModel.hpp``, and it
self-checks: ``fileSize`` at 0x2C must equal the inflated length, which it does
for all 390 extracted models.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import struct
from typing import List, Optional, Tuple

from .level_model import (
    BATCH_SIZE, ENDIAN, NO_TEXTURE, TEXTURE_INFO_SIZE, TRIANGLE_SIZE,
    VERTEX_SIZE, Batch, LevelModelError, TextureRef, decompress, normalise_uv,
    parse_texture_table,
)


class ObjectModelError(LevelModelError):
    pass


class ObjectModel:
    __slots__ = ("vertices", "colours", "triangles", "uvs", "batches",
                 "textures", "file_size")

    def __init__(self):
        #: ``[(x, y, z), ...]`` in model space, Y-up and centred on the origin.
        self.vertices: List[Tuple[int, int, int]] = []
        self.colours: List[Tuple[int, int, int, int]] = []
        #: ``[(flags, vi0, vi1, vi2), ...]`` with batch-local indices.
        self.triangles: List[Tuple[int, int, int, int]] = []
        #: ``[((s0, t0), (s1, t1), (s2, t2)), ...]``, raw fixed point per triangle.
        self.uvs: List[Tuple[Tuple[int, int], ...]] = []
        self.batches: List[Batch] = []
        self.textures: List[TextureRef] = []
        self.file_size = 0

    @property
    def texture_count(self) -> int:
        return len(self.textures)

    def texture_for(self, batch: Batch) -> Optional[TextureRef]:
        index = batch.texture_index
        if index == NO_TEXTURE or not (0 <= index < len(self.textures)):
            return None
        return self.textures[index]

    def faces(self):
        """Yield model-local triangles with their indices already resolved."""
        for batch in self.batches:
            for face in range(batch.face_offset, batch.face_offset + batch.face_count):
                if face >= len(self.triangles):
                    break
                _flags, vi0, vi1, vi2 = self.triangles[face]
                indices = tuple(batch.vertex_offset + i for i in (vi0, vi1, vi2))
                if max(indices) >= len(self.vertices):
                    continue
                yield indices, batch

    def face_uvs(self, face: int, texture: Optional[TextureRef]):
        """Normalised UVs for one triangle, or ``None`` when it has no texture."""
        if texture is None or face >= len(self.uvs):
            return None
        return normalise_uv(self.uvs[face], texture)

    def bounds(self):
        if not self.vertices:
            return (0, 0, 0), (0, 0, 0)
        xs, ys, zs = zip(*self.vertices)
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def parse(blob: bytes) -> ObjectModel:
    if len(blob) < 0x58:
        raise ObjectModelError("blob is too short to hold an ObjectModel header")

    model = ObjectModel()
    textures_ptr, vertices_ptr, triangles_ptr = struct.unpack_from(
        ENDIAN + "3I", blob, 0x00
    )
    texture_count, vertex_count, triangle_count, batch_count = struct.unpack_from(
        ENDIAN + "4h", blob, 0x22
    )
    file_size, = struct.unpack_from(ENDIAN + "I", blob, 0x2C)
    batches_ptr, = struct.unpack_from(ENDIAN + "I", blob, 0x38)

    model.file_size = file_size

    model.textures = parse_texture_table(blob, textures_ptr, texture_count)

    if file_size and file_size != len(blob):
        raise ObjectModelError(
            "header declares fileSize %d but the blob is %d bytes"
            % (file_size, len(blob))
        )

    for i in range(max(0, vertex_count)):
        offset = vertices_ptr + i * VERTEX_SIZE
        if offset + VERTEX_SIZE > len(blob):
            break
        model.vertices.append(struct.unpack_from(ENDIAN + "3h", blob, offset))
        model.colours.append(tuple(blob[offset + 6:offset + 10]))

    for i in range(max(0, triangle_count)):
        offset = triangles_ptr + i * TRIANGLE_SIZE
        if offset + TRIANGLE_SIZE > len(blob):
            break
        model.triangles.append(tuple(blob[offset:offset + 4]))
        raw = struct.unpack_from(ENDIAN + "6h", blob, offset + 4)
        model.uvs.append(((raw[0], raw[1]), (raw[2], raw[3]), (raw[4], raw[5])))

    # As in a level model, the batch array carries a terminator holding the end
    # offsets, so a batch's span is the difference to the next entry.
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
        model.batches.append(batch)
    return model


def load(path: str) -> ObjectModel:
    with open(path, "rb") as handle:
        return parse(decompress(handle.read()))
