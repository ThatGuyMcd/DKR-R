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

EXIT = "ASSET_OBJECT_EXIT"
DESTINATION_FIELD = "destinationMapId"

#: The glTF extra an exit names a custom destination track with. Not a field of
#: the entry - the encoder writes only catalogue fields - so it travels in the
#: source glTF and the manifest, never in the game's bytes.
TRACK_FIELD = "destinationTrack"

#: What the byte holds until the runtime patches it: level 0, the Central Area
#: hub, so a link to a track that is not installed leads somewhere safe.
PLACEHOLDER_LEVEL = "ASSET_LEVEL_CENTRALAREAHUB"


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
          catalog) -> List[Dict[str, object]]:
    """The ``levelLinks`` entries for one slot's map.

    ``entry_offsets`` is :meth:`ObjectMapEncoder.entry_offsets` of the same
    map: each entry's first byte in the inflated buffer, header included.
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
        found.append({"slot": slot, "offset": int(start) + inside, "track": track})
    return found
