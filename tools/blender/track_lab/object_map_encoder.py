"""Encode an object map into the bytes the game loads.

This is the other half of :mod:`gltf_io`: that module reads and writes the
human-readable glTF an author edits, and this one turns the same data into the
``LEVEL_OBJECT_MAPS`` section payload a ``.dkrmap`` carries. Together they close
the loop from Blender to the running game without the decomp's
``dkr_assets_tool``, which builds a whole ``assets.bin`` and ships as a Linux
binary.

The format is specified in ``docs/LEVEL_OBJECT_MAP_FORMAT.md``, verified entry by
entry against the retail ``assets.bin``. That verification is what makes this
module testable rather than merely plausible: the correct bytes for all 136
retail maps already exist, so ``tests/test_encoder.py`` encodes each one from its
glTF and requires the result to be identical.

Three things the encoder has to get right, each of which produces plausible but
wrong bytes if missed:

* ``objectID`` indexes the **level-object translation table**, not the global
  object list, and it is 9 bits split across two bytes.
* an enum field stores the member's **declared value**, not its position -
  ``VEHICLE_NO_OVERRIDE`` is -1 while sitting fifteenth in its enum.
* a field absent from the glTF is not zero. An ``AssetId`` field encodes as -1
  when it has nothing to point at, which is exactly what absence means.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import struct
import zlib
from typing import Callable, Dict, List, Optional, Sequence

from .gltf_io import ObjectMap

#: Big endian, as everything past the container is.
ENDIAN = ">"

HEADER_SIZE = 0x10
COMMON_SIZE = 8

#: The ``size`` byte is seven bits wide, the eighth belonging to the object id -
#: but the game reads it with ``& 0x3F``. ``track_spawn_objects`` in
#: ``src/objects.c`` advances by ``gObjectMapSpawnList[index][1] & 0x3F``, so an
#: entry of 64 bytes or more would be walked at the wrong stride and every entry
#: after it misread. The real limit is 63, not 127. No retail type reaches it -
#: the largest is 50 - which is why the discrepancy never bites in shipped data.
MAX_ENTRY_SIZE = 0x3F
MAX_OBJECT_ID = 0x1FF

#: The decompressed payload is padded out to this with zero bytes.
BLOB_ALIGNMENT = 8

#: Container: decompressed size as u32 little endian, then this tag.
CONTAINER_TAG = 0x09

#: ``get_value_from_hint_time`` divides by the tick rate, which is 60.
TICK_RATE = 60.0

#: Hints whose field the extractor leaves out of the glTF when it holds -1, and
#: which therefore have to be written back as -1 when the glTF does not carry
#: them. See ``put_struct_entry_into_gltf_node_extra`` in the asset tool.
OMITTED_AS_MINUS_ONE = ("AssetId", "Object", "Time")

_PACK = {
    ("u8", 1): "B", ("s8", 1): "b",
    ("u16", 2): "H", ("s16", 2): "h",
    ("u32", 4): "I", ("s32", 4): "i",
}

_WIDTH = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "f32": 4}
_SIGNED = {"u8": False, "s8": True, "u16": False, "s16": True,
           "u32": False, "s32": True}


class EncodeError(Exception):
    pass


def _clamp(ctype: str, value: int) -> int:
    """Hold a value inside its C type, as ``CTypes::clamp_int`` does."""
    width = _WIDTH.get(ctype, 1)
    bits = width * 8
    if _SIGNED.get(ctype, True):
        low, high = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    else:
        low, high = 0, (1 << bits) - 1
    return max(low, min(high, int(value)))




class ObjectMapEncoder:
    """Turns an :class:`ObjectMap` into section bytes.

    ``translation_table`` is the list from
    ``objects/level_object_translation_table.json``; an object's index in it is
    what the entry stores. ``asset_index`` resolves an ``AssetId`` hint - given a
    section name and a build id it returns that id's index, or -1.
    """

    def __init__(self, catalog, translation_table: Sequence[str],
                 asset_index: Optional[Callable[[str, str], int]] = None):
        self.catalog = catalog
        self.translation_table = list(translation_table)
        self._index_of = {
            name: index for index, name in enumerate(self.translation_table)
        }
        self.asset_index = asset_index

    # -- whole map -------------------------------------------------------

    def encode(self, object_map: ObjectMap) -> bytes:
        """The decompressed section payload for one map."""
        entries = b"".join(self.encode_entry(obj) for obj in object_map.objects)

        blob = bytearray(HEADER_SIZE + len(entries))
        # fileSize counts the entries only, never the 16-byte header.
        struct.pack_into(ENDIAN + "I", blob, 0, len(entries))
        blob[HEADER_SIZE:] = entries

        remainder = len(blob) % BLOB_ALIGNMENT
        if remainder:
            blob.extend(b"\x00" * (BLOB_ALIGNMENT - remainder))
        return bytes(blob)

    def pack(self, object_map: ObjectMap, level: int = 9) -> bytes:
        """The payload wrapped in the container the asset table stores."""
        blob = self.encode(object_map)
        compressor = zlib.compressobj(level, zlib.DEFLATED, -15)
        deflated = compressor.compress(blob) + compressor.flush()
        return struct.pack("<IB", len(blob), CONTAINER_TAG) + deflated

    # -- one entry -------------------------------------------------------

    def object_index(self, object_id: str) -> int:
        index = self._index_of.get(object_id)
        if index is None:
            raise EncodeError(
                "%s is not in the level-object translation table, so no entry "
                "can refer to it; a track using it has to extend the table"
                % object_id
            )
        if index > MAX_OBJECT_ID:
            raise EncodeError(
                "translation-table index %d for %s does not fit the 9-bit field"
                % (index, object_id)
            )
        return index

    def encode_entry(self, obj) -> bytes:
        object_type = self.catalog.get(obj.object_id)
        if object_type is None:
            raise EncodeError("%s is not in the catalogue" % obj.object_id)

        entry = self.catalog.raw.get("objects", {}).get(obj.object_id, {})
        size = entry.get("entry_size", COMMON_SIZE)
        if not COMMON_SIZE <= size <= MAX_ENTRY_SIZE:
            raise EncodeError(
                "%s encodes to %d bytes; the game walks entries with a 6-bit "
                "size, so 8..%d is all it can read"
                % (obj.object_id, size, MAX_ENTRY_SIZE)
            )

        buffer = bytearray(size)
        index = self.object_index(obj.object_id)
        buffer[0] = index & 0xFF
        buffer[1] = (size & 0x7F) | ((index >> 1) & 0x80)

        position = [int(round(float(c))) for c in obj.translation]
        for axis, value in enumerate(position):
            if not -32768 <= value <= 32767:
                raise EncodeError(
                    "%s sits at %r, outside the s16 a position is stored in"
                    % (obj.object_id, position)
                )
            struct.pack_into(ENDIAN + "h", buffer, 2 + axis * 2, value)

        for field in object_type.fields:
            self._write_field(buffer, field, obj)
        return bytes(buffer)

    def _write_field(self, buffer: bytearray, field, obj) -> None:
        offset = getattr(field, "offset", None)
        if offset is None or field.ctype not in _WIDTH:
            return

        present = field.name in obj.fields
        value = obj.fields.get(field.name)

        if not present:
            # Absence is not zero. ``put_struct_entry_into_gltf_node_extra`` omits
            # a field from the glTF in exactly three cases, and all three use -1
            # as the sentinel: an asset id or object reference that resolves to
            # nothing, and a time that is already -1. Writing 0 instead would
            # make an Animation spawn object 0 and pause for zero frames rather
            # than not at all.
            if field.hint.get("kind") in OMITTED_AS_MINUS_ONE:
                self._pack(buffer, field, offset, -1)
            return

        if field.kind == "list":
            width = _WIDTH[field.ctype]
            for i, item in enumerate(value[:field.count]):
                self._pack(buffer, field, offset + i * width, int(item))
            return

        self._pack(buffer, field, offset, self.raw_value(field, value))

    def _pack(self, buffer: bytearray, field, offset: int, raw: int) -> None:
        """Store a value in its field, truncating rather than saturating.

        The asset tool clamps in exactly one place - ``get_value_from_hint_angle``
        calls ``CTypes::clamp_int`` - and otherwise just writes the integer into
        the field, which truncates. The difference shows on the -1 sentinel: an
        absent ``AssetId`` on a ``u8`` has to come out 0xFF, and saturating would
        make it 0, pointing the exit at the first level instead of at nothing.
        """
        ctype = field.ctype
        width = _WIDTH[ctype]
        code = _PACK.get((ctype, width))
        if code is None or offset + width > len(buffer):
            return
        bits = width * 8
        masked = int(raw) & ((1 << bits) - 1)
        if _SIGNED.get(ctype, True) and masked >= (1 << (bits - 1)):
            masked -= 1 << bits
        struct.pack_into(ENDIAN + code, buffer, offset, masked)

    # -- hints -----------------------------------------------------------

    def raw_value(self, field, value) -> int:
        """The integer a decoded field value encodes to.

        Mirrors ``helpers/c/cStructGltfHelper.cpp`` in the asset tool; see
        docs/LEVEL_OBJECT_MAP_FORMAT.md for why each one is shaped this way.
        """
        kind = field.hint.get("kind")

        if kind == "Angle":
            return self._angle(field, float(value))
        if kind == "Scale":
            divide_by = float(field.hint.get("divideBy", 64))
            return int(round(float(value) * divide_by))
        if kind == "Time":
            return int(round(float(value) * TICK_RATE))
        if kind == "Object":
            return self._object_reference(value)
        if kind == "AssetId":
            return self._asset(field, value)
        if kind == "Enum" or field.kind == "enum":
            return self._enum(field, value)
        if isinstance(value, bool):
            return int(value)
        return int(round(float(value)))

    def _angle(self, field, angle: float) -> int:
        """``get_value_from_hint_angle``: wrap negative back onto an unsigned type."""
        divide_by = float(field.hint.get("divideBy", 64))
        if angle < 0.0 and not _SIGNED.get(field.ctype, True):
            width = _WIDTH.get(field.ctype, 1)
            max_plus_one = 1 << (width * 8)
            angle += max_plus_one / divide_by * 360.0
        # The one place the asset tool clamps rather than truncates.
        return _clamp(field.ctype, int(round(angle / 360.0 * divide_by)))

    def _object_reference(self, value) -> int:
        """``get_value_from_hint_object``: an index into the translation table.

        Not an asset section. An Animation's ``objectIdToSpawn`` names the object
        it spawns, and the entry stores where that object sits in the same table
        the entry's own ``objectID`` indexes. Empty means -1, spawning nothing.
        """
        name = str(value or "")
        if not name:
            return -1
        return self._index_of.get(name, -1)

    def _enum(self, field, value) -> int:
        """The member's declared value, which is not its position in the list."""
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            return int(value)
        name = str(value)
        values = self.catalog.raw.get("enumValues", {})
        table = values.get(field.enum) or values.get(field.hint.get("subject"))
        if table and name in table:
            return int(table[name])
        # Not an enum the catalogue exports - an asset section, most likely.
        return self._asset(field, name)

    def _asset(self, field, value) -> int:
        """``get_value_from_hint_asset_build_id``: an index, or -1 for nothing."""
        name = str(value or "")
        if not name:
            return -1
        section = field.hint.get("subject") or field.enum
        if self.asset_index is None or not section:
            raise EncodeError(
                "%s refers to asset %r but no asset index is available to "
                "resolve it" % (field.name, name)
            )
        return self.asset_index(section, name)


def encode(object_map: ObjectMap, catalog, translation_table, asset_index=None) -> bytes:
    return ObjectMapEncoder(catalog, translation_table, asset_index).encode(object_map)


def pack(object_map: ObjectMap, catalog, translation_table, asset_index=None) -> bytes:
    return ObjectMapEncoder(catalog, translation_table, asset_index).pack(object_map)
