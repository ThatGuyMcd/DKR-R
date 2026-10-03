"""Move object maps between the file format and Blender's scene.

Every placed object becomes an Empty carrying its decoded fields as custom
properties. That choice is what keeps the round trip exact: Blender's
IDProperties store ints as ints and floats as floats, so nothing is coerced on
the way through, and anything the catalogue does not model is kept verbatim and
written straight back out.

Three details are worth knowing before changing anything here.

**Axes.** Object maps are Y-up, as glTF is; Blender is Z-up. The mapping is
``blender = (x, -z, y)``. A rotation about the map's +Y axis comes out as the
same rotation about Blender's +Z, with the same sign, which is what lets an
author rotate an object in the viewport and have ``angleY`` follow.

**Names.** Blender makes object names unique by appending ``.001``, but the
document needs the original. The exact node name is kept in ``dkr_node_name``
and the Blender name is only a label.

**Absent fields.** Some fields are optional and a retail map omits them. Writing
one back where retail had none would change the encoded entry, so the names that
were missing on import are remembered in ``dkr_absent`` and omitted again.
"""

from __future__ import annotations

import json
import math
from typing import Dict, List, Optional

import bpy
from mathutils import Vector

from . import catalog as catalog_module
from .gltf_io import MapObject, ObjectMap

#: Custom property names the addon owns. Everything else on an object is a
#: decoded field.
PROP_ID = "dkr_id"
PROP_NODE_NAME = "dkr_node_name"
PROP_ABSENT = "dkr_absent"
PROP_UNKNOWN = "dkr_unknown"
PROP_SLOT = "dkr_slot"
RESERVED = {PROP_ID, PROP_NODE_NAME, PROP_ABSENT, PROP_UNKNOWN, PROP_SLOT}

#: A start grid is an Empty that the start positions are parented to, so the
#: whole grid moves and turns as one. It carries no ``dkr_id`` and so is never
#: exported; what it carries is what it was built from, so it can be rebuilt in
#: place when the level type changes.
PROP_GRID_ROOT = "dkr_grid_root"
PROP_GRID_KEY = "dkr_grid_key"
PROP_GRID_ENTRANCE = "dkr_grid_entrance"
PROP_GRID_SPACING = "dkr_grid_spacing"
PROP_GRID_RADIUS = "dkr_grid_radius"
PROP_GRID_FACING = "dkr_grid_facing"

#: A level holds **two** object maps and the game loads them into a two-element
#: array, spawning both the same way. So the split is not semantic: 39 of the 85
#: object types appear in both across retail tracks, many near half and half.
#: Which map an object lives in is therefore remembered, never inferred - an
#: imported object goes back to the map it came from.
SLOT_STRUCTURE = "structure"
SLOT_COLLECTABLES = "collectables"
SLOTS = (SLOT_STRUCTURE, SLOT_COLLECTABLES)

ROOT_COLLECTION = "DKR Track"

#: How each category is drawn in the viewport, so a track reads at a glance.
CATEGORY_DISPLAY = {
    "racing": ("PLAIN_AXES", 24.0),
    "pickups": ("SPHERE", 16.0),
    "hub": ("CUBE", 32.0),
    "camera": ("CONE", 24.0),
    "audio": ("SPHERE", 20.0),
    "effects": ("CIRCLE", 24.0),
    "actors": ("CUBE", 20.0),
    "scenery": ("CIRCLE", 20.0),
    "misc": ("PLAIN_AXES", 16.0),
}

#: Types that deserve their own look regardless of category.
TYPE_DISPLAY = {
    "ASSET_OBJECT_AINODE": ("SPHERE", 20.0),
    "ASSET_OBJECT_CHECKPOINT": ("CUBE", 40.0),
    "ASSET_OBJECT_SETUPPOINT": ("SINGLE_ARROW", 48.0),
    "ASSET_OBJECT_GROUNDZIPPER": ("ARROWS", 32.0),
    "ASSET_OBJECT_AIRZIPPERS": ("ARROWS", 32.0),
    "ASSET_OBJECT_WATERZIPPERS": ("ARROWS", 32.0),
    "ASSET_OBJECT_EXIT": ("CUBE", 40.0),
    # Without artwork, labelled axes: which way a loop's mode changer faces
    # decides whether it works, and its Y is the way racers drive through it.
    "ASSET_OBJECT_MODECHANGE": ("ARROWS", 64.0),
}


# ---------------------------------------------------------------------------
# Coordinates
# ---------------------------------------------------------------------------

def to_blender(translation) -> Vector:
    """Map-space (Y-up) to Blender-space (Z-up)."""
    x, y, z = translation
    return Vector((x, -z, y))


def to_map(vector) -> List[float]:
    """Blender-space (Z-up) back to map-space (Y-up)."""
    return [vector[0], vector[2], -vector[1]]


# ---------------------------------------------------------------------------
# Import
# ---------------------------------------------------------------------------

def collection_for(category: str, root: bpy.types.Collection) -> bpy.types.Collection:
    """One collection per category, so the outliner is navigable."""
    for child in root.children:
        if child.name == category:
            return child
    collection = bpy.data.collections.new(category)
    root.children.link(collection)
    return collection


def ensure_root(context) -> bpy.types.Collection:
    scene_collection = context.scene.collection
    for child in scene_collection.children:
        if child.name == ROOT_COLLECTION:
            return child
    root = bpy.data.collections.new(ROOT_COLLECTION)
    scene_collection.children.link(root)
    return root


def slot_of(obj: bpy.types.Object) -> str:
    """Which object map this object belongs to."""
    value = str(obj.get(PROP_SLOT, SLOT_STRUCTURE))
    return value if value in SLOTS else SLOT_STRUCTURE


def create_empty(context, obj: MapObject, catalog, root, tree=None,
                 slot: str = SLOT_STRUCTURE) -> bpy.types.Object:
    """Build the Blender object representing one placed object.

    ``tree`` is an :class:`~track_lab.assets.AssetTree`. When one is
    given the object is drawn with the artwork the game uses - a sprite
    billboard, or the decoded object model - instead of a bare Empty. That is
    what lets an author tell a balloon from a checkpoint at a glance.
    """
    from . import preview

    object_type = catalog.get(obj.object_id)
    category = object_type.category if object_type else "misc"

    # A Blender object's type is fixed when it is created, so whether this one
    # gets a mesh has to be settled first. Types with no artwork - AI nodes,
    # triggers, camera hints - stay Empties, which is right: they are markers,
    # not things.
    mesh, kind = (None, "none")
    if tree is not None and preview.has_artwork(obj.object_id, tree):
        mesh, kind = preview.mesh_for(obj.object_id, obj.fields, tree, catalog)

    empty = bpy.data.objects.new(obj.name or obj.object_id, mesh)

    if mesh is None:
        display, size = TYPE_DISPLAY.get(
            obj.object_id, CATEGORY_DISPLAY.get(category, ("PLAIN_AXES", 16.0))
        )
        empty.empty_display_type = display
        empty.empty_display_size = size
    else:
        empty[preview.PROP_PREVIEW] = kind

    empty.location = to_blender(obj.translation)

    empty[PROP_ID] = obj.object_id
    empty[PROP_NODE_NAME] = obj.name
    empty[PROP_SLOT] = slot if slot in SLOTS else SLOT_STRUCTURE

    known = {f.name for f in object_type.fields} if object_type else set()
    absent = []
    unknown = {}

    if object_type:
        for field in object_type.fields:
            if field.name in obj.fields:
                empty[field.name] = _to_id_property(obj.fields[field.name])
                _apply_ui(empty, field)
            elif not field.unused:
                absent.append(field.name)

    for name, value in obj.fields.items():
        if name not in known:
            unknown[name] = value

    if absent:
        empty[PROP_ABSENT] = json.dumps(sorted(absent))
    if unknown:
        empty[PROP_UNKNOWN] = json.dumps(unknown, sort_keys=True)

    _apply_rotation(empty, object_type, obj.fields)
    _apply_scale(empty, object_type, obj.fields)

    collection_for(category, root).objects.link(empty)
    return empty


def _to_id_property(value):
    """Blender stores lists as IDProperty arrays; everything else is native."""
    return list(value) if isinstance(value, list) else value


def _apply_ui(empty, field):
    """Teach Blender the field's range and step so the N panel behaves.

    The range comes from the C type, not from what retail happens to use, so an
    author is never blocked from a value the game accepts.
    """
    try:
        ui = empty.id_properties_ui(field.name)
    except (TypeError, KeyError):
        return
    settings = {}
    if field.minimum is not None and field.kind in ("int", "float"):
        settings["min"] = field.minimum
        settings["soft_min"] = field.minimum
    if field.maximum is not None and field.kind in ("int", "float"):
        settings["max"] = field.maximum
        settings["soft_max"] = field.maximum
    if field.step and field.kind == "float":
        settings["step"] = field.step
        settings["precision"] = 4
    description = _describe(field)
    if description:
        settings["description"] = description
    if settings:
        try:
            ui.update(**settings)
        except (TypeError, ValueError):
            pass


def _describe(field) -> str:
    """Tooltip text: what the field is, and what retail puts in it."""
    parts = []
    if field.ctype:
        parts.append(field.ctype)
    if field.hint.get("kind"):
        parts.append(field.hint["kind"].lower())
    if field.seen:
        parts.append("retail uses %s" % ", ".join(_short(s) for s in field.seen[:8]))
    elif field.seen_min is not None and field.seen_max is not None:
        parts.append(
            "retail range %s..%s" % (_short(field.seen_min), _short(field.seen_max))
        )
    return " | ".join(parts)


def _short(value) -> str:
    return "%g" % value if isinstance(value, float) else str(value)


def _apply_rotation(empty, object_type, fields):
    """Drive the Empty's Z rotation from the type's angle field, if it has one."""
    if not object_type:
        return
    field = object_type.angle_field
    if field is None or field.name not in fields:
        return
    try:
        empty.rotation_euler.z = math.radians(float(fields[field.name]))
    except (TypeError, ValueError):
        pass


def _apply_scale(empty, object_type, fields):
    """Size the object the way the game does, from the type's scale byte.

    The artwork is built at the header's own scale and shared by every instance
    of the type, so the per-object part - ``radius / 64`` on a tree, ``scale /
    64`` on a balloon - goes on the object. That is also what lets an author
    resize a tree with S and have the byte follow, as R does for the angle.
    """
    rule = object_type.scale_rule if object_type else None
    if rule is None or rule.field.name not in fields:
        return
    factor = rule.factor(fields[rule.field.name])
    empty.scale = (factor, factor, factor)


def world_scale(obj) -> float:
    """The uniform scale an object is drawn at, through its parents.

    A size byte holds one number, so a non-uniform scale is averaged rather than
    taking whichever axis happened to be dragged.
    """
    scale = obj.matrix_world.to_scale()
    return (abs(scale[0]) + abs(scale[1]) + abs(scale[2])) / 3.0


def import_object_map(context, object_map: ObjectMap, catalog=None, tree=None,
                      slot: str = SLOT_STRUCTURE,
                      order_base: int = 0) -> List[bpy.types.Object]:
    """Load one object map, stamping document order from ``order_base``.

    Loading a level means loading two maps, and each has to keep its own
    document order for its own export. Re-stamping the whole scene afterwards
    would use Blender's object order, which is not document order, and scramble
    the first map. So each map is given a disjoint range instead.
    """
    if catalog is None:
        catalog = catalog_module.load()
    root = ensure_root(context)
    created = [
        create_empty(context, obj, catalog, root, tree, slot)
        for obj in object_map.objects
    ]
    stamp_order(created, order_base)
    from .operators import waterfall
    waterfall.bind_imported(context, created)
    return created


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------

def is_dkr_object(obj: bpy.types.Object) -> bool:
    return PROP_ID in obj


def iter_dkr_objects(context, selected_only=False):
    source = context.selected_objects if selected_only else context.scene.objects
    return [obj for obj in source if is_dkr_object(obj)]


def is_grid_root(obj) -> bool:
    return obj is not None and PROP_GRID_ROOT in obj


def grid_roots(context) -> List[bpy.types.Object]:
    return [obj for obj in context.scene.objects if is_grid_root(obj)]


def world_yaw(obj) -> float:
    """The Z rotation an object points at, in radians, through its parents.

    Summed rather than read off ``matrix_world``, because a matrix wraps the
    angle into one turn and a retail object can sit at -410.625 degrees - an
    unparented object has to come back exactly as it went in, which the plain
    ``rotation_euler.z`` it starts from guarantees.
    """
    total = 0.0
    while obj is not None:
        total += obj.rotation_euler.z
        obj = obj.parent
    return total


def read_object(empty: bpy.types.Object, catalog) -> MapObject:
    """Rebuild one :class:`MapObject` from an Empty."""
    object_id = str(empty[PROP_ID])
    object_type = catalog.get(object_id)

    absent = set(_load_json(empty, PROP_ABSENT, []))
    fields: Dict[str, object] = {}

    if object_type:
        angle_field = object_type.angle_field
        scale_rule = object_type.scale_rule
        for field in object_type.fields:
            if field.name in absent or field.name not in empty:
                continue
            value = empty[field.name]
            if isinstance(value, str):
                fields[field.name] = value
                continue
            if hasattr(value, "to_list"):
                value = value.to_list()
            elif hasattr(value, "__len__") and not isinstance(value, (str, bytes)):
                value = list(value)
            if scale_rule is not None and field is scale_rule.field:
                # The viewport scale is authoritative too, as the angle is. A
                # byte that already gives the scale shown is kept as it was.
                fields[field.name] = scale_rule.value_for(world_scale(empty), value)
                continue
            if angle_field is not None and field is angle_field:
                # The viewport rotation is authoritative, so an object turned
                # with R writes the angle it now points at. Coercion snaps it
                # back onto the format's step, which is what makes an untouched
                # object come out byte-identical. Through the parents, so a
                # start position turns with the grid it belongs to.
                value = math.degrees(world_yaw(empty))
            # Coerce, but do not clamp. The catalogue's range is a UI guide, and
            # the asset tool clamps to the C type itself on encode; clipping a
            # value here would silently change what the author placed.
            fields[field.name] = field.coerce(value)

    fields.update(_load_json(empty, PROP_UNKNOWN, {}))

    return MapObject(
        object_id=object_id,
        name=str(empty.get(PROP_NODE_NAME, empty.name)),
        translation=to_map(empty.matrix_world.translation),
        fields=fields,
    )


def _load_json(empty, key, fallback):
    raw = empty.get(key)
    if not raw:
        return fallback
    try:
        return json.loads(raw)
    except (ValueError, TypeError):
        return fallback


def export_object_map(context, catalog=None, selected_only=False,
                      slot=None, resolve_scroll=True) -> ObjectMap:
    """Collect the scene back into an object map, in a stable order.

    Document order is preserved from the Blender object name, which import set
    from the node name and Blender kept unique. That keeps a re-export of an
    untouched import byte-identical to what came in.
    """
    if catalog is None:
        catalog = catalog_module.load()
    # ``matrix_world`` is derived, and objects linked or moved since the last
    # depsgraph evaluation still carry the identity matrix. Without this every
    # object exports at the origin.
    context.view_layer.update()
    empties = iter_dkr_objects(context, selected_only)
    if slot is not None:
        empties = [o for o in empties if slot_of(o) == slot]
    empties.sort(key=lambda o: o.get("dkr_order", 1 << 30))
    objects = [read_object(e, catalog) for e in empties]
    if resolve_scroll:
        from .operators import waterfall
        waterfall.resolve_export(context, empties, objects)
    return ObjectMap(objects=objects)


def slot_counts(context) -> Dict[str, int]:
    counts = {slot: 0 for slot in SLOTS}
    for obj in iter_dkr_objects(context):
        counts[slot_of(obj)] = counts.get(slot_of(obj), 0) + 1
    return counts


def stamp_order(objects: List[bpy.types.Object], base: int = 0) -> None:
    """Record document order so an export can reproduce it."""
    for index, obj in enumerate(objects):
        obj["dkr_order"] = base + index


def next_order(context) -> int:
    """Order index for a newly placed object: after everything already there."""
    existing = [o.get("dkr_order", -1) for o in iter_dkr_objects(context)]
    return (max(existing) + 1) if existing else 0
