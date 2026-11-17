"""Hub exits that lead to other custom tracks.

An ``ASSET_OBJECT_EXIT`` names where it leads in one byte, ``destinationMapId``
(``LevelObjectEntry_Exit`` offset 0x08). A retail level's id is fixed, so an
exit to Fossil Canyon simply stores it. A ``.dkrmap`` track has no fixed id: the
runtime numbers custom tracks from the set installed, so the byte cannot be
known when the hub is exported.

So such an exit carries the destination track's **id** instead, in the glTF as
:data:`TRACK_FIELD`, and is encoded with a harmless placeholder destination
(:data:`PLACEHOLDER_LEVEL`, the Central Area hub, level 0). The package's
manifest then lists where that byte sits:

.. code-block:: json

    "levelLinks": [
        {"slot": "structure", "offset": 42, "track": "frosty-ridge"}
    ]

``offset`` is the byte's position in the slot's object map **as the game holds
it after inflating it**: the 16-byte map header (``u32`` entry byte count, then
12 bytes of padding - ``gObjectMap[index] + sizeof(uintptr_t)`` on an ``s32 *``
in ``track_spawn_objects``, ``src/objects.c:980``), then the entries in order.
The runtime writes the resolved level id there before the exit spawns, and
leaves the placeholder when the track is not installed. Documented for track
authors in ``docs/CUSTOM_TRACKS.md``, *Hub doors*.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

from typing import Dict, List, Optional

from .gltf_io import MapObject, ObjectMap

#: The game inflates an object map into a fixed buffer this long, and a link's
#: ``offset`` indexes it, so no byte past it may be named.
MAX_OBJECT_MAP_BYTES = 0x3000

#: The 16-byte map header; the first entry, and so the first byte a link can
#: name, starts after it.
MAP_HEADER_BYTES = 16

#: A level id fits the game's level tables from 0 up to this, inclusive.
MAX_LEVEL_ID = 127

EXIT = "ASSET_OBJECT_EXIT"
DESTINATION_FIELD = "destinationMapId"

#: The glTF extra an exit names a custom destination track with. Not a field of
#: the entry - the encoder writes only catalogue fields - so it travels in the
#: source glTF and the manifest, never in the game's bytes.
TRACK_FIELD = "destinationTrack"

#: What the byte holds until the runtime patches it: level 0, the Central Area
#: hub, so a link to a track that is not installed leads somewhere safe.
PLACEHOLDER_LEVEL = "ASSET_LEVEL_CENTRALAREAHUB"


class LevelLinkError(ValueError):
    """A link or an exit destination the game could not use safely."""


def check_offset(offset, map_size: Optional[int] = None) -> int:
    """``offset`` if it names a byte inside an object map, else raise.

    A link's byte must lie past the header and inside the inflated map, which
    itself must fit the game's buffer. ``map_size`` is the inflated map's
    length when it is known.
    """
    if isinstance(offset, bool) or not isinstance(offset, int):
        raise LevelLinkError("levelLinks offset %r is not an integer" % (offset,))
    limit = MAX_OBJECT_MAP_BYTES
    if map_size is not None:
        limit = min(limit, int(map_size))
    if offset < MAP_HEADER_BYTES or offset >= limit:
        raise LevelLinkError(
            "levelLinks offset %d is outside the object map (valid: %d to %d)"
            % (offset, MAP_HEADER_BYTES, limit - 1))
    return offset


def check_level_id(level_id, what: str = "level id") -> int:
    """``level_id`` if it is in 0..127, else raise."""
    if (isinstance(level_id, bool) or not isinstance(level_id, int)
            or not 0 <= level_id <= MAX_LEVEL_ID):
        raise LevelLinkError(
            "%s %r is outside 0 to %d" % (what, level_id, MAX_LEVEL_ID))
    return level_id


def check_links(links, map_sizes: Optional[Dict[str, int]] = None) -> None:
    """Check a manifest's ``levelLinks`` list the way the exporter does.

    ``map_sizes`` maps a slot to its inflated map's length, where known. A
    ``level_id`` on a link, which the runtime never reads from a package but a
    hand-written manifest may carry, is held to 0..127 as well.
    """
    for link in links:
        slot = link.get("slot")
        size = (map_sizes or {}).get(slot)
        check_offset(link.get("offset"), size)
        if "level_id" in link:
            check_level_id(link["level_id"], "levelLinks level_id")


def check_exit_destinations(object_map: ObjectMap, entry_offsets: List[int],
                            blob: bytes, catalog) -> None:
    """Raise if an exit with an explicit retail destination encoded outside 0..127.

    Reads the byte the encoder wrote, so a name the asset tree does not know
    (encoded as 0xFF) is caught along with a numeric id that is too large.
    Linked exits hold the placeholder and an exit with no destination is left
    to the validator.
    """
    inside = None
    for index, (obj, start) in enumerate(zip(object_map.objects, entry_offsets)):
        if obj.object_id != EXIT or linked_track(obj):
            continue
        if obj.fields.get(DESTINATION_FIELD) in (None, ""):
            continue
        if inside is None:
            inside = field_offset(catalog)
        check_level_id(blob[start + inside],
                       "exit %d destination %r encodes level id"
                       % (index, obj.fields.get(DESTINATION_FIELD)))


def linked_track(obj: MapObject) -> str:
    """The custom track an exit leads to, or ``""`` for a retail destination."""
    if obj.object_id != EXIT:
        return ""
    value = obj.fields.get(TRACK_FIELD)
    return str(value).strip() if isinstance(value, str) else ""


def with_placeholders(object_map: ObjectMap) -> ObjectMap:
    """A copy whose linked exits encode :data:`PLACEHOLDER_LEVEL`.

    Whatever ``destinationMapId`` a linked exit was left holding - the last
    retail level picked before the author switched it to a custom track - it
    must not ship, or a missing track would send the player there instead.
    """
    objects = []
    for obj in object_map.objects:
        if linked_track(obj):
            fields = dict(obj.fields)
            fields[DESTINATION_FIELD] = PLACEHOLDER_LEVEL
            obj = MapObject(object_id=obj.object_id, name=obj.name,
                            translation=list(obj.translation), fields=fields)
        objects.append(obj)
    return ObjectMap(objects=objects, root_name=object_map.root_name)


def field_offset(catalog, object_id: str = EXIT,
                 field_name: str = DESTINATION_FIELD) -> int:
    """Where a field sits inside its entry, from the catalogue's struct layout."""
    object_type = catalog.get(object_id)
    for field in object_type.fields if object_type else ():
        if field.name == field_name and getattr(field, "offset", None) is not None:
            return int(field.offset)
    raise KeyError("%s has no field %s in the catalogue" % (object_id, field_name))


def links(object_map: ObjectMap, entry_offsets: List[int], slot: str,
          catalog, map_size: Optional[int] = None) -> List[Dict[str, object]]:
    """The ``levelLinks`` entries for one slot's map.

    ``entry_offsets`` is :meth:`ObjectMapEncoder.entry_offsets` of the same
    map: each entry's first byte in the inflated buffer, header included.
    ``map_size`` is that buffer's length; every offset is checked against it
    and the game's buffer size (:func:`check_offset`).
    """
    if len(entry_offsets) != len(object_map.objects):
        raise ValueError("one entry offset per object is needed")
    found = []
    inside: Optional[int] = None
    for obj, start in zip(object_map.objects, entry_offsets):
        track = linked_track(obj)
        if not track:
            continue
        if inside is None:
            inside = field_offset(catalog)
        offset = check_offset(int(start) + inside, map_size)
        found.append({"slot": slot, "offset": offset, "track": track})
    return found
