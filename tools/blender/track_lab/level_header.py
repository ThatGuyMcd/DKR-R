"""Encode a level header - the 200 bytes that make a track exist.

A ``.dkrmap`` needs one of these before the game will load anything: it is what
names the track's geometry, its skybox, its lap count and which world it belongs
to. Without it the object maps have nothing to attach to.

The layout is ``LevelHeader`` from the asset tool's ``fileTypes/levelHeader.hpp``
and the field mapping is ``builder/buildTypes/buildLevelHeader.cpp``. Unlike an
object map, a header entry is **200 bytes and uncompressed** - no
``[u32][0x09]`` container.

Two fields are deliberately left at **zero**. The runtime patches
``collectables`` (0x36) and ``unkBA`` (0xBA) when it builds the extended asset
table, because a custom track's object maps only get their indices then.

Zero specifically, not -1 as a "nothing here" sentinel. ``track_spawn_objects``
guards the table with

```c
for (i = 0; objMapTable[i] != 0xFFFFFFFF; i++) {}
i--;
if (mapID >= i) { mapID = 0; }
assetOffset = objMapTable[mapID];
```

Both operands are ``s32``, so the comparison is signed and ``-1 >= i`` is false
for any positive ``i``: the clamp does not fire and the next line reads
``objMapTable[-1]``, one word before the table. Zero is in range and simply
loads map 0.

Neither value fails loudly - the engine offers no representable "no map" here,
and any out-of-range id clamps to 0 anyway - so loudness is the runtime's job.
It warns when a track ships a map for a slot and the patch did not land.

Verified the way the object-map encoder is: ``tests/test_header.py`` rebuilds
all 65 retail headers from their extracted JSON and requires the bytes to match
what ships in ``assets.bin``.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import struct
from typing import Callable, Dict, List, Optional

ENDIAN = ">"
LITTLE_ENDIAN = "<"

#: Every header entry is exactly this, uncompressed.
HEADER_SIZE = 0xC8

#: Offsets the runtime owns. See the module docstring.
OFFSET_COLLECTABLES = 0x36
OFFSET_OBJECT_MAP_2 = 0xBA
RUNTIME_OWNED = (OFFSET_COLLECTABLES, OFFSET_OBJECT_MAP_2)

#: A header may name at most this many misc assets; unused slots are -1.
MAX_MISC_ASSETS = 7


class HeaderError(Exception):
    pass


class Field:
    """One field: where it sits, how wide it is, and where its value comes from.

    ``pointer`` is the JSON path in the extracted header. ``kind`` says how to
    turn that value into an integer.
    """

    __slots__ = ("offset", "ctype", "pointer", "kind", "subject", "count",
                 "default", "endian")

    def __init__(self, offset, ctype, pointer, kind="int", subject=None,
                 count=1, default=None, endian=ENDIAN):
        self.offset = offset
        self.ctype = ctype
        self.pointer = pointer
        self.kind = kind
        self.subject = subject
        self.count = count
        self.default = default
        #: Almost always big endian; see ``unkC4``.
        self.endian = endian


_W = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "f32": 4}
_C = {"u8": "B", "s8": "b", "u16": "H", "s16": "h", "u32": "I", "s32": "i",
      "f32": "f"}
_SIGNED = {"u8": False, "s8": True, "u16": False, "s16": True,
           "u32": False, "s32": True}


def _rgb(base, pointer):
    return [
        Field(base + i, "u8", "%s/%s" % (pointer, name))
        for i, name in enumerate(("red", "green", "blue"))
    ]


def _ai_levels(base, pointer):
    names = ("base", "silver-coins", "completed", "tracks-mode", "trophy-race")
    return [
        Field(base + i, "s8", "%s/%s" % (pointer, name))
        for i, name in enumerate(names)
    ]


def _array(base, ctype, pointer, count):
    width = _W[ctype]
    return [
        Field(base + i * width, ctype, "%s/%d" % (pointer, i))
        for i in range(count)
    ]


#: The whole 200-byte layout, in offset order. Derived from
#: ``fileTypes/levelHeader.hpp`` and ``buildLevelHeader.cpp``.
LAYOUT: List[Field] = (
    [
        Field(0x00, "s8", "/world", "enum", "World"),
        Field(0x01, "u8", "/unknown/unk1"),
        Field(0x02, "s8", "/unknown/unk2"),
        Field(0x03, "s8", "/unknown/unk3"),
    ]
    + _array(0x04, "s8", "/unknown/unk4", 4)
    + [Field(0x08, "f32", "/course-height", "float")]
    + _array(0x0C, "u8", "/unknown/unkC", 10)
    + _array(0x16, "u8", "/unknown/unk16", 10)
    + _ai_levels(0x20, "/ai-levels/adv1")
    + _ai_levels(0x25, "/ai-levels/adv2")
    + [Field(0x2A, "u8", "/unknown/unk2A")]
    + _array(0x2B, "u8", "/unknown/unk2B", 9)
    + [
        Field(0x34, "s16", "/model", "asset", "ASSET_LEVEL_MODELS"),
        # 0x36 collectables and 0xBA objectMap2 belong to the runtime.
        Field(0x38, "s16", "/background/skybox/id", "asset", "ASSET_OBJECTS",
              default=-1),
        Field(0x3A, "s16", "/fog/near"),
        Field(0x3C, "s16", "/fog/far"),
    ]
    + [
        Field(0x3E + i * 2, "s16", "/fog/colour/%s" % name)
        for i, name in enumerate(("red", "green", "blue"))
    ]
    + _array(0x44, "u8", "/unknown/unk44", 5)
    + [
        Field(0x49, "s8", "/background/skybox/special-sky", default=0),
        Field(0x4A, "s8", "/max-velocity", default=0),
        Field(0x4B, "s8", "/lap-count", default=3),
        Field(0x4C, "s8", "/race-type", "enum", "RaceType"),
        Field(0x4D, "s8", "/default-vehicle", "enum", "Vehicle",
              default="VEHICLE_CAR"),
        Field(0x4E, "s8", "/avaliable-vehicles", "bitfield", "Vehicle", default=0),
    ]
    + _array(0x4F, "u8", "/unknown/unk4F", 3)
    + [
        Field(0x52, "u8", "/music"),
        Field(0x53, "u8", "/unknown/unk53"),
        Field(0x54, "u16", "/instruments"),
        Field(0x56, "u8", "/waves/subdivisions"),
        Field(0x57, "u8", "/waves/unk57"),
        Field(0x58, "u8", "/waves/sine-step-0"),
        Field(0x59, "u8", "/waves/sine-base-0"),
        Field(0x5A, "s16", "/waves/sine-height-0"),
        Field(0x5C, "u8", "/waves/sine-step-1"),
        Field(0x5D, "u8", "/waves/sine-base-1"),
        Field(0x5E, "s16", "/waves/sine-height-1"),
        Field(0x60, "s16", "/waves/seed-size"),
        Field(0x62, "s16", "/waves/wave-power"),
        Field(0x64, "s16", "/waves/unk64"),
        Field(0x66, "s16", "/waves/unk66"),
        Field(0x68, "s16", "/waves/texture-ID", "asset", "ASSET_TEXTURES_2D"),
        Field(0x6A, "u8", "/waves/UV-Scale-X"),
        Field(0x6B, "u8", "/waves/UV-Scale-Y"),
        Field(0x6C, "s8", "/waves/UV-Scroll-X"),
        Field(0x6D, "s8", "/waves/UV-Scroll-Y"),
        Field(0x6E, "s16", "/waves/view-distance"),
        Field(0x70, "u8", "/waves/unk70"),
        Field(0x71, "u8", "/waves/unk71"),
        Field(0x72, "u8", "/unknown/unk72"),
        Field(0x73, "u8", "/unknown/unk73"),
    ]
    + [
        Field(0x74 + i * 4, "s32", "/misc-assets/%d" % i, "asset", "ASSET_MISC",
              default=-1)
        for i in range(MAX_MISC_ASSETS)
    ]
    + [
        Field(0x90, "s16", "/weather/enable"),
        Field(0x92, "s16", "/weather/type"),
        Field(0x94, "u8", "/weather/intensity"),
        Field(0x95, "u8", "/weather/opacity"),
        Field(0x96, "s16", "/weather/velocity/x"),
        Field(0x98, "s16", "/weather/velocity/y"),
        Field(0x9A, "s16", "/weather/velocity/z"),
        Field(0x9C, "s8", "/fov", default=60),
    ]
    + _rgb(0x9D, "/background/colour")
    + [
        Field(0xA0, "s16", "/unknown/unkA0"),
        Field(0xA2, "s8", "/unknown/unkA2"),
        Field(0xA3, "s8", "/unknown/unkA3"),
        Field(0xA4, "u32", "/background/skybox/special-sky-texture", default=-1),
        Field(0xA8, "s16", "/unknown/unkA8"),
        Field(0xAA, "s16", "/unknown/unkAA"),
        Field(0xAC, "s32", "/pulsating-lights", "asset", "ASSET_MISC", default=-1),
        Field(0xB0, "s16", "/unknown/unkB0"),
        Field(0xB2, "u8", "/unknown/unkB2"),
        Field(0xB3, "u8", "/unknown/unkB3"),
    ]
    + _rgb(0xB4, "/void/colour")
    + [
        Field(0xB7, "u8", "/void/enabled"),
        Field(0xB8, "s8", "/boss-race-id", "enum", "BossSetupTypes"),
        Field(0xB9, "u8", "/unknown/unkB9"),
        # 0xBA objectMap2 - runtime owned.
        Field(0xBC, "u8", "/unknown/unkBC"),
        Field(0xBD, "s8", "/unknown/unkBD"),
    ]
    + _rgb(0xBE, "/background/multiplayer/gradient-colour/bottom")
    + _rgb(0xC1, "/background/multiplayer/gradient-colour/top")
    # The last field is the one exception to big endian. Every other multi-byte
    # member of LevelHeader is declared ``be_*`` in the asset tool; ``unkC4`` is
    # a plain ``uint32_t``, so it is written in the build machine's order - x86,
    # little endian. Almost certainly an oversight, but shipped data depends on
    # it: retail holds 10 05 D3 58 where big endian would give 58 D3 05 10.
    + [Field(0xC4, "u32", "/unknown/unkC4", endian=LITTLE_ENDIAN)]
)


def _lookup(document, pointer):
    """Resolve a ``/a/b/0`` JSON pointer, or ``None`` when it is absent."""
    node = document
    for part in pointer.strip("/").split("/"):
        if isinstance(node, list):
            try:
                node = node[int(part)]
            except (ValueError, IndexError):
                return None
        elif isinstance(node, dict):
            if part not in node:
                return None
            node = node[part]
        else:
            return None
    return node


class LevelHeaderEncoder:
    """Turns an extracted level-header JSON into its 200 bytes.

    ``enum_values`` maps an enum name to ``{member: value}``; ``asset_index``
    resolves a section name and build id to an index.
    """

    def __init__(self, enum_values: Dict[str, Dict[str, int]],
                 asset_index: Optional[Callable[[str, str], int]] = None):
        self.enum_values = enum_values or {}
        self.asset_index = asset_index

    def encode(self, document: Dict, patch: Optional[Dict[int, int]] = None) -> bytes:
        """Build the header. ``patch`` overrides fields by offset."""
        buffer = bytearray(HEADER_SIZE)
        for field in LAYOUT:
            self._write(buffer, field, document)
        for offset, value in (patch or {}).items():
            self._raw(buffer, offset, "s16", value)
        return bytes(buffer)

    # -- one field -------------------------------------------------------

    def _write(self, buffer, field, document):
        value = _lookup(document, field.pointer)
        if value is None:
            value = field.default
        if value is None:
            return  # leave the zero already there

        if field.kind == "float":
            struct.pack_into(field.endian + "f", buffer, field.offset, float(value))
            return

        self._raw(buffer, field.offset, field.ctype, self._integer(field, value),
                  field.endian)

    def _integer(self, field, value) -> int:
        if field.kind == "enum":
            return self._enum(field.subject, value)
        if field.kind == "bitfield":
            return self._bitfield(field.subject, value)
        if field.kind == "asset":
            return self._asset(field.subject, value)
        if isinstance(value, bool):
            return int(value)
        return int(value)

    def _enum(self, subject, value) -> int:
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            return int(value)
        table = self.enum_values.get(subject) or {}
        name = str(value)
        if name not in table:
            raise HeaderError(
                "%r is not a member of %s" % (name, subject)
            )
        return table[name]

    def _bitfield(self, subject, value) -> int:
        """``build_enum_bitfield``: one bit per member named in the list."""
        if isinstance(value, int):
            return value
        table = self.enum_values.get(subject) or {}
        out = 0
        for name in value or []:
            if name not in table:
                raise HeaderError("%r is not a member of %s" % (name, subject))
            out |= 1 << table[name]
        return out

    def _asset(self, section, value) -> int:
        """An asset index, or -1 when the field names nothing.

        A number is already an index and is taken as one - which is what makes
        a header encodable with no asset tree configured. The sentinel matters:
        an unset asset field defaults to -1, and treating that as a name to
        resolve made every header without a skybox refuse rather than write
        "nothing here". Same shape as ``_enum``, which passes a number through
        for the same reason.
        """
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            return int(value)
        name = str(value or "")
        if not name:
            return -1
        if self.asset_index is None:
            raise HeaderError(
                "%s refers to asset %r but no asset index is available"
                % (section, name)
            )
        return self.asset_index(section, name)

    def _raw(self, buffer, offset, ctype, raw, endian=ENDIAN):
        width = _W[ctype]
        if offset + width > len(buffer):
            return
        bits = width * 8
        masked = int(raw) & ((1 << bits) - 1)
        if _SIGNED[ctype] and masked >= (1 << (bits - 1)):
            masked -= 1 << bits
        struct.pack_into(endian + _C[ctype], buffer, offset, masked)


def encode(document, enum_values, asset_index=None, patch=None) -> bytes:
    return LevelHeaderEncoder(enum_values, asset_index).encode(document, patch)
