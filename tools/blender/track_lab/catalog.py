"""Access to the generated object catalogue.

``data/catalog.json`` is produced by ``tools/blender/generate_catalog.py`` from
the decomp; see that script for where each piece comes from. This module only
reads it and answers the questions the rest of the addon asks: what fields does
this object type have, what should a fresh one look like, and what type must a
value be coerced back to on export.

The coercion is the load-bearing part. Blender stores custom properties as
IDProperties, which round-trip ints and floats faithfully on their own, but an
author editing a value in the UI can easily leave an int field holding a float.
Writing ``scale: 100.0`` where retail writes ``scale: 100`` changes the bytes
the asset tool emits, so every value goes through :func:`coerce` on the way out.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import json
import os
from typing import Any, Dict, List, Optional

DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
CATALOG_PATH = os.path.join(DATA_DIR, "catalog.json")

SUPPORTED_SCHEMA = 1

_CACHE: Optional["Catalog"] = None


class CatalogError(Exception):
    pass


class Field:
    """One editable field of one object type."""

    __slots__ = (
        "name", "kind", "ctype", "hint", "step", "minimum", "maximum",
        "default", "values", "enum", "length", "optional", "unused",
        "undeclared", "seen", "seen_min", "seen_max", "offset", "count",
        "label",
    )

    def __init__(self, raw: Dict[str, Any]):
        self.name = raw["name"]
        #: A human name for a panel to show, when the decomp's own is opaque.
        #: Never a substitute for :attr:`name`, which is the Blender custom
        #: property an object carries: renaming that would make an existing
        #: scene's values vanish on its next export, because ``read_object``
        #: skips a field the object does not have. Falls back to the name, so
        #: a caller can always use it.
        self.label = raw.get("label") or raw["name"]
        self.kind = raw["kind"]
        self.ctype = raw.get("ctype")
        self.hint = raw.get("hint") or {}
        self.step = raw.get("step")
        self.minimum = raw.get("min")
        self.maximum = raw.get("max")
        self.default = raw.get("default")
        self.values = raw.get("values") or []
        self.enum = raw.get("enum")
        self.length = raw.get("length", 1)
        self.optional = bool(raw.get("optional"))
        self.unused = bool(raw.get("unused"))
        self.undeclared = bool(raw.get("undeclared"))
        #: What retail maps were seen to put here. Guidance for the author,
        #: never a constraint - see :attr:`minimum` for the real limit.
        self.seen = raw.get("seen")
        self.seen_min = raw.get("seen_min")
        self.seen_max = raw.get("seen_max")
        #: Where the field sits in the encoded entry, and how many elements it
        #: has. Present only for fields the C header declares.
        self.offset = raw.get("offset")
        self.count = raw.get("count", 1)

    @property
    def is_angle(self) -> bool:
        return self.hint.get("kind") == "Angle"

    @property
    def is_raw(self) -> bool:
        """Bytes that exist to be reproduced, not authored.

        ``pad*`` is padding and ``unk*`` is a byte nobody has identified yet.
        Neither is something an author can reason about, and there are a lot of
        them: a checkpoint declares 19 editable fields of which 15 are ``unk``,
        burying the three that decide how the checkpoint behaves.
        """
        return self.name.startswith(("pad", "unk"))

    def coerce(self, value: Any) -> Any:
        """Force a value back into the JSON type the object map must contain."""
        if self.kind == "int":
            return int(round(float(value)))
        if self.kind == "float":
            value = float(value)
            if self.step:
                # Every representable value is a whole number of raw units, so
                # snapping here is what lets a rotation edited in the viewport
                # come back out as the exact float retail would have written.
                value = round(value / self.step) * self.step
            return float(value)
        if self.kind == "enum":
            return str(value)
        if self.kind == "list":
            items = [int(round(float(v))) for v in value]
            if len(items) < self.length:
                items += [0] * (self.length - len(items))
            return items[:self.length]
        return value

    def clamp(self, value: Any) -> Any:
        """Hold a numeric value inside what its C type can represent."""
        if self.kind not in ("int", "float"):
            return value
        if self.minimum is not None:
            value = max(self.minimum, value)
        if self.maximum is not None:
            value = min(self.maximum, value)
        return value

    def fresh(self) -> Any:
        return list(self.default) if self.kind == "list" else self.default


class ScaleRule:
    """How one byte of a placed object sizes it in game.

    Every ``obj_init_*`` below reads it the same way - mask, floor, divide - and
    multiplies the header's own ``scale`` by the result::

        radius = max(entry->radius & 0xFF, 10) / 64;
        obj->trans.scale = obj->header->scale * radius;

    So a tree with ``radius`` 128 is twice the size of one with 64, and anything
    under 10 is drawn as 10.

    The catalogue writes some of these bytes as they are (a tree's ``radius:
    64``) and some already divided, through the field's ``divideBy`` hint (a
    weapon balloon's ``scale: 1.0``). Both are the same byte underneath, and
    the rule works on that byte.
    """

    __slots__ = ("field", "floor", "divisor")

    def __init__(self, field: "Field", floor: int, divisor: float):
        self.field = field
        self.floor = floor
        self.divisor = divisor

    @property
    def _stored_per_byte(self) -> float:
        return float(self.field.hint.get("divideBy") or 1)

    def raw(self, value: Any) -> int:
        """The byte a stored field value holds."""
        try:
            return int(round(float(value) * self._stored_per_byte)) & 0xFF
        except (TypeError, ValueError):
            return DEFAULT_SCALE_BYTE

    def factor(self, value: Any) -> float:
        """The multiplier the game applies for a stored value."""
        return max(self.raw(value), self.floor) / self.divisor

    def value_for(self, factor: float, stored: Any = None) -> Any:
        """The field value that gives ``factor``, keeping ``stored`` when it already does.

        Keeping it is what makes an untouched object come back byte for byte: a
        retail ``radius`` of 0 is drawn at the floor, and rewriting it as the
        floor would change the entry without changing anything on screen.
        """
        if stored is not None and abs(self.factor(stored) - factor) < 0.5 / self.divisor:
            return self.field.coerce(stored)
        raw = max(self.floor, int(round(factor * self.divisor)))
        if self.field.maximum is not None:
            raw = min(int(round(self.field.maximum * self._stored_per_byte)), raw)
        raw = min(0xFF, raw)
        return self.field.coerce(raw / self._stored_per_byte)


#: What a fresh object's scale byte means: the header's size, unchanged.
DEFAULT_SCALE_BYTE = 64

#: Which entry structs size their object from a byte, read off the decomp's
#: ``obj_init_*``. Only these: a checkpoint's ``scale`` or a trigger's
#: ``radius`` is the size of an invisible gate, not of anything drawn, and a
#: MIDI fade's is a 16 bit region extent.
SCALE_RULES = {
    "LevelObjectEntry_Scenery": ("radius", 10),
    "LevelObjectEntry_Torch_Mist": ("radius", 10),
    "LevelObjectEntry_Lighthouse_RocketSignpost": ("radius", 10),
    "LevelObjectEntry_AirZippers_WaterZippers": ("radius", 10),
    "LevelObjectEntry_GroundZipper": ("scale", 10),
    "LevelObjectEntry_CharacterFlag": ("radius", 10),
    "LevelObjectEntry_Animation": ("scale", 1),
    "LevelObjectEntry_GoldenBalloon": ("scale", 10),
    "LevelObjectEntry_Door": ("scale", 10),
    "LevelObjectEntry_TTDoor": ("scale", 10),
    "LevelObjectEntry_WeaponBalloon": ("scale", 10),
    "LevelObjectEntry_Log": ("radius", 10),
}


class ObjectType:
    """One of the 85 object types a retail track can contain."""

    __slots__ = ("object_id", "node_name", "struct", "category",
                 "retail_count", "featured", "modes", "slots", "fields",
                 "_by_name")

    def __init__(self, object_id: str, raw: Dict[str, Any]):
        self.object_id = object_id
        self.node_name = raw.get("node_name") or object_id
        self.struct = raw.get("struct")
        self.category = raw.get("category", "misc")
        self.retail_count = raw.get("retail_count", 0)
        self.featured = bool(raw.get("featured"))
        #: How many retail levels of each kind place this type, keyed as
        #: :data:`level_types.KEYS`. What the Place list filters on.
        self.modes: Dict[str, int] = dict(raw.get("modes") or {})
        #: How many retail instances sit in each object map.
        self.slots: Dict[str, int] = dict(raw.get("slots") or {})
        self.fields: List[Field] = [Field(f) for f in raw.get("fields", [])]
        self._by_name = {f.name: f for f in self.fields}

    @property
    def label(self) -> str:
        """``ASSET_OBJECT_GROUNDZIPPER`` reads better as ``GroundZipper``."""
        return self.node_name or self.object_id.replace("ASSET_OBJECT_", "").title()

    def field(self, name: str) -> Optional[Field]:
        return self._by_name.get(name)

    @property
    def angle_field(self) -> Optional[Field]:
        """The field a viewport rotation should drive, if the type has one."""
        for field in self.fields:
            if field.is_angle and field.name in ("angleY", "closedRotation"):
                return field
        for field in self.fields:
            if field.is_angle:
                return field
        return None

    @property
    def scale_rule(self) -> Optional[ScaleRule]:
        """How a viewport scale maps onto this type's size byte, if it has one."""
        rule = SCALE_RULES.get(self.struct or "")
        if rule is None:
            return None
        field = self.field(rule[0])
        if field is None or field.kind not in ("int", "float"):
            return None
        return ScaleRule(field, rule[1], 64.0)

    def fresh_fields(self) -> Dict[str, Any]:
        """The ``extras`` a newly placed object of this type should carry.

        Fields no retail map ever wrote are left out; so are optional ones,
        which the asset tool omits when it has nothing to say.
        """
        out = {}
        for field in self.fields:
            if field.unused or field.optional:
                continue
            out[field.name] = field.fresh()
        return out


class Catalog:
    def __init__(self, raw: Dict[str, Any]):
        if raw.get("schemaVersion") != SUPPORTED_SCHEMA:
            raise CatalogError(
                "catalog.json is schema %r, this addon speaks %r"
                % (raw.get("schemaVersion"), SUPPORTED_SCHEMA)
            )
        self.raw = raw
        # DKR-R's additive Track Select category, beyond the retail worlds.
        raw.setdefault("enumValues", {}).setdefault("World", {})["WORLD_CUSTOM_TRACKS"] = 6
        worlds = raw.setdefault("enums", {}).setdefault("World", [])
        if "WORLD_CUSTOM_TRACKS" not in worlds:
            worlds.append("WORLD_CUSTOM_TRACKS")
        self.enums: Dict[str, List[str]] = raw.get("enums", {})
        self.levels: List[str] = raw.get("levels", [])
        self.categories: List[str] = raw.get("categories", [])
        self.types: Dict[str, ObjectType] = {
            object_id: ObjectType(object_id, entry)
            for object_id, entry in raw.get("objects", {}).items()
        }

    def __contains__(self, object_id: str) -> bool:
        return object_id in self.types

    def get(self, object_id: str) -> Optional[ObjectType]:
        return self.types.get(object_id)

    def in_category(self, category: str) -> List[ObjectType]:
        found = [t for t in self.types.values() if t.category == category]
        return sorted(found, key=lambda t: (-t.retail_count, t.object_id))

    def featured(self) -> List[ObjectType]:
        found = [t for t in self.types.values() if t.featured]
        return sorted(found, key=lambda t: t.object_id)

    def enum_members(self, field: Field) -> List[str]:
        """Every member an enum field may hold.

        Prefer the whole declared enum over the members retail happens to use,
        so an author is not limited to recreating what already shipped. Asset-id
        fields draw on the level list instead, which is the same list a custom
        track extends.
        """
        if field.enum in self.enums:
            return self.enums[field.enum]
        if field.hint.get("kind") == "AssetId" and self.levels:
            return self.levels
        return list(field.values)


def load(path: str = CATALOG_PATH) -> Catalog:
    """Load and cache the catalogue."""
    global _CACHE
    if _CACHE is None:
        if not os.path.exists(path):
            raise CatalogError(
                "catalog.json is missing; run tools/blender/generate_catalog.py"
            )
        with open(path, "r", encoding="utf-8") as handle:
            _CACHE = Catalog(json.load(handle))
    return _CACHE


def reload(path: str = CATALOG_PATH) -> Catalog:
    global _CACHE
    _CACHE = None
    return load(path)
