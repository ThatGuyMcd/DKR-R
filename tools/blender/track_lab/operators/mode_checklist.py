"""The Blender end of the Mode Checklist: reading the scene, and the Add buttons.

What each level type needs lives in :mod:`track_lab.mode_checklist`, with no
Blender in it. This module hands it the scene - quickly, since the panel asks on
every redraw - and holds the operators the checklist's buttons run: one that
adds a per-racer set (chests, flags, Head-For Points) where the start ring
says each racer's belongs, and one that points a hub's exit at a retail level
or at another custom track.
"""

from __future__ import annotations

import json
import math

import bpy
from bpy.props import EnumProperty, StringProperty
from mathutils import Vector

from .. import (catalog as catalog_module, dkrmap, level_links, level_types,
                mode_checklist, scene)
from ..gltf_io import MapObject, ObjectMap
from . import geometry as geometry_ops
from .edit import add_object

#: The fields the checklist reads; nothing else is copied off the objects.
_FIELDS = ("racerIndex", "entranceID", "unk8", "unk9", "adjacent", "playerID",
           "playerIndex", "destinationMapId", "returnSpawnIndex", "nodeID")

#: The types whose position the checklist reads.
_PLACED = (mode_checklist.AINODE, mode_checklist.BALLOON)


def scene_map(context) -> ObjectMap:
    """The scene's objects with just the fields the checklist reads.

    A full :func:`scene.export_object_map` reads every field and every world
    matrix; this runs on each redraw of the panel, so it copies a handful of
    custom properties instead. Document order follows ``dkr_order``, as the
    export's does, so positions select the same objects.
    """
    objects = sorted(scene.iter_dkr_objects(context),
                     key=lambda o: o.get("dkr_order", 1 << 30))
    found = []
    for obj in objects:
        fields = {}
        for name in _FIELDS:
            if name in obj:
                value = obj[name]
                fields[name] = value.to_list() if hasattr(value, "to_list") else value
        track = scene.destination_track(obj)
        if track:
            fields[level_links.TRACK_FIELD] = track
        object_id = str(obj[scene.PROP_ID])
        # Only the weapon-node rows measure anything, between AI nodes and
        # balloons; every other object keeps the origin and skips the matrix.
        where = (scene.to_map(obj.matrix_world.translation)
                 if object_id in _PLACED else [0.0, 0.0, 0.0])
        found.append(MapObject(object_id=object_id, name=obj.name,
                               translation=where, fields=fields))
    return ObjectMap(objects=found)


def surfaces_in_use(context):
    """``SurfaceType`` names the track geometry's materials carry, or ``None``.

    ``None`` when no geometry is loaded, so the egg nests read as unknown
    rather than missing. Per material, as the game keeps it: the surface is a
    byte on the texture entry the faces are drawn with.
    """
    meshes = geometry_ops.geometry_objects(context)
    if not meshes:
        return None
    names = set()
    for obj in meshes:
        for slot in obj.material_slots:
            material = slot.material
            if material is not None and geometry_ops.PROP_SURFACE in material:
                names.add(geometry_ops.surface_name(material[geometry_ops.PROP_SURFACE]))
    return names


def checklist(context):
    """The rows for the scene's level type; empty where the checklist is silent."""
    key = level_types.current_key(context.scene.dkr)
    if not mode_checklist.applies(key):
        return []
    boss, surfaces = validation_facts(context)
    rows = mode_checklist.rows(scene_map(context), key, boss, surfaces)
    # A row names objects by document position; Select finds them by
    # dkr_order, which deleting objects leaves with gaps.
    orders = [int(o.get("dkr_order", -1)) for o in
              sorted(scene.iter_dkr_objects(context),
                     key=lambda o: o.get("dkr_order", 1 << 30))]
    for row in rows:
        row.objects = [orders[p] for p in row.objects if 0 <= p < len(orders)]
    return rows


def validation_facts(context):
    """``(boss, surfaces)`` for :func:`validate.validate`, from the scene."""
    key = level_types.current_key(context.scene.dkr)
    boss = context.scene.dkr.boss if key == level_types.BOSS else None
    surfaces = surfaces_in_use(context) if key == level_types.EGGS else None
    return boss, surfaces


# ---------------------------------------------------------------------------
# Per-racer sets
# ---------------------------------------------------------------------------

#: kind -> (object id, the field naming the racer, what racer 0 stores,
#: fields that select which of the type's objects count, how far behind the
#: start it goes, how high above the ground, the label for a report).
SETS = {
    "CHESTS": (mode_checklist.CHEST, "playerID", 1, None, 300.0, 0.0,
               "treasure chest"),
    "FLAGS": (mode_checklist.FLAG, "playerIndex", 0, None, 300.0, 230.0,
              "racer flag"),
    "HEAD_DROP": (mode_checklist.HEAD_FOR, "unk9", 0,
                  {"unk8": mode_checklist.HEAD_DROP}, 500.0, 0.0,
                  "Head-For Point"),
    "HEAD_HOME": (mode_checklist.HEAD_FOR, "unk9", 0,
                  {"unk8": mode_checklist.HEAD_HOME}, 250.0, 0.0,
                  "Head-For Point"),
}


def racer_starts(context):
    """``{racer: Blender start position object}`` at entrance 0, racers 0-3."""
    found = {}
    for obj in scene.iter_dkr_objects(context):
        if str(obj.get(scene.PROP_ID)) != level_types.SETUPPOINT:
            continue
        if int(obj.get("entranceID", 0)) != 0:
            continue
        racer = int(obj.get("racerIndex", -1))
        if racer in mode_checklist.RACERS:
            found.setdefault(racer, obj)
    return found


def _behind(start, distance):
    """A point ``distance`` behind a start position, on its own level."""
    yaw = scene.world_yaw(start)
    origin = start.matrix_world.translation
    # Local +Y is forward (see the start grid), so behind is -Y turned by yaw.
    return Vector((origin.x + math.sin(yaw) * distance,
                   origin.y - math.cos(yaw) * distance, origin.z))


def _anchor(context, kind, racer, start):
    """Where one racer's object of this kind goes.

    A flag stands over that racer's chest, or over its nest's Head-For Point
    in Eggs, when there is one: that is what the flag is for. Everything else
    goes behind the racer's start position, which is the one place the scene
    already says belongs to that racer.
    """
    if kind == "FLAGS":
        for obj in scene.iter_dkr_objects(context):
            object_id = str(obj.get(scene.PROP_ID))
            if object_id == mode_checklist.CHEST and int(obj.get("playerID", 0)) == racer + 1:
                return obj.matrix_world.translation.copy()
            if (object_id == mode_checklist.HEAD_FOR
                    and int(obj.get("unk8", -1)) == mode_checklist.HEAD_DROP
                    and int(obj.get("unk9", -1)) == racer):
                return obj.matrix_world.translation.copy()
    return _behind(start, SETS[kind][4])


def add_racer_set(context, kind):
    """Add the objects of one kind that racers 0-3 are missing; the new objects."""
    object_id, field, base, match, _distance, lift, _label = SETS[kind]
    catalog = catalog_module.load()
    object_type = catalog.get(object_id)
    starts = racer_starts(context)
    missing = [r for r in mode_checklist.missing_racers(
        scene_map(context), object_id, field, match) if r in starts]
    targets = geometry_ops.geometry_objects(context)
    context.view_layer.update()
    depsgraph = context.evaluated_depsgraph_get() if targets else None
    created = []
    for racer in missing:
        location = _anchor(context, kind, racer, starts[racer])
        if targets and not lift:
            landing = geometry_ops.surface_below(location, targets, depsgraph)
            if landing is not None:
                location = landing
        location = location + Vector((0.0, 0.0, lift))
        fields = {field: racer + base}
        fields.update(match or {})
        empty = add_object(context, object_type, location, fields,
                           slot=level_types.tab_of(object_type))
        created.append(empty)
    return created


class DKR_OT_add_racer_set(bpy.types.Operator):
    """Add one object per racer that does not have one yet, behind each
    racer's start position, with the racer already filled in"""

    bl_idname = "dkr.add_racer_set"
    bl_label = "Add Per-Racer Objects"
    bl_options = {"REGISTER", "UNDO"}

    kind: EnumProperty(
        name="Kind",
        items=[
            ("CHESTS", "Treasure Chests",
             "One chest per racer, playerID = racer + 1: where that racer banks "
             "bananas, from within 225 units"),
            ("FLAGS", "Racer Flags",
             "One Character Flag per racer, over its chest or nest, showing "
             "that racer's portrait"),
            ("HEAD_FOR", "Head-For Points",
             "Per racer, a kind 0 point at the nest - where a bot carrying an "
             "egg drops it - and a kind 2 point on the way there"),
        ],
        options={"SKIP_SAVE"},
    )

    @classmethod
    def description(cls, context, properties):
        for item in cls.bl_rna.properties["kind"].enum_items:
            if item.identifier == properties.kind:
                return ("%s. Only racers without one get one, placed behind "
                        "their start position and dropped onto the track: "
                        "move them where they belong" % item.description)
        return cls.__doc__

    @classmethod
    def poll(cls, context):
        if len(racer_starts(context)) < len(mode_checklist.RACERS):
            cls.poll_message_set(
                "Build the start ring first: each racer's objects go behind "
                "that racer's start position, racerIndex 0-3 at entrance 0")
            return False
        return True

    def execute(self, context):
        kinds = ("HEAD_DROP", "HEAD_HOME") if self.kind == "HEAD_FOR" else (self.kind,)
        created = []
        for kind in kinds:
            created += add_racer_set(context, kind)
        if not created:
            self.report({"INFO"}, "every racer already has one")
            return {"CANCELLED"}
        for obj in context.selected_objects:
            obj.select_set(False)
        for obj in created:
            obj.select_set(True)
        context.view_layer.objects.active = created[-1]
        label = SETS[kinds[0]][6]
        message = "added %d %s%s" % (len(created), label, "s" if len(created) != 1 else "")
        if self.kind == "HEAD_FOR":
            message += ("; now give each nest floor its surface, Egg 1 to 4, "
                        "in Geometry > Surface")
        self.report({"INFO"}, message)
        return {"FINISHED"}


# ---------------------------------------------------------------------------
# Hub exits
# ---------------------------------------------------------------------------

_LEVEL_ITEMS = []


def retail_label(asset_id: str, tree=None) -> str:
    """``ASSET_LEVEL_FOSSILCANYON`` -> ``Fossil Canyon``, from the extraction
    when one is configured, which knows where the words break."""
    bare = asset_id.replace("ASSET_LEVEL_", "")
    if tree is not None:
        try:
            for level in tree.levels():
                if level.name.split("_v")[0].upper() == bare:
                    return level.label
        except Exception:  # noqa: BLE001 - a label must not raise
            pass
    return bare.title()


def _level_items(self, context):
    """Every level an exit can name, by its retail name, built once."""
    if _LEVEL_ITEMS:
        return _LEVEL_ITEMS
    from .. import prefs  # noqa: PLC0415
    try:
        object_type = catalog_module.load().get(level_links.EXIT)
        field = object_type.field(level_links.DESTINATION_FIELD)
        values = list(field.values)
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        values = []
    tree = prefs.resolve(context)
    rows = sorted(((retail_label(v, tree), v) for v in values), key=lambda r: r[0])
    _LEVEL_ITEMS.extend((value, text, value) for text, value in rows)
    return _LEVEL_ITEMS or [("NONE", "no levels in the catalogue", "")]


def describe_destination(obj, tree=None) -> str:
    """What an exit leads to, in the words the panel shows."""
    track = scene.destination_track(obj)
    if track:
        return track
    value = obj.get(level_links.DESTINATION_FIELD)
    absent = scene._load_json(obj, scene.PROP_ABSENT, [])
    if not value or level_links.DESTINATION_FIELD in absent:
        return ""
    return retail_label(str(value), tree)


class DKR_OT_set_exit_destination(bpy.types.Operator):
    """Choose where this exit leads: a retail level, or another custom track
    by its id"""

    bl_idname = "dkr.set_exit_destination"
    bl_label = "Exit Destination"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    kind: EnumProperty(
        name="Leads To",
        items=[
            ("RETAIL", "Retail Level",
             "A level of the original game. Its id is written into the exit"),
            ("CUSTOM", "Custom Track",
             "Another .dkrmap track, by the id its Package panel shows. Its "
             "level id only exists once DKR-R knows which tracks are "
             "installed, so the exit ships pointing at the Central Area hub "
             "and DKR-R fills the real one in as the hub loads. If the track "
             "is not installed, the exit leads to the Central Area hub"),
        ],
        default="RETAIL",
    )
    level: EnumProperty(name="Level", items=_level_items,
                        description="The retail level this exit warps to")
    track: StringProperty(
        name="Track Id",
        description=("The id of the custom track this exit leads to, as that "
                     "track's Package panel shows it: lowercase words joined "
                     "by hyphens"),
    )

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        if obj is None or str(obj.get(scene.PROP_ID, "")) != level_links.EXIT:
            cls.poll_message_set("Select an exit first")
            return False
        return True

    def invoke(self, context, event):
        obj = context.active_object
        track = scene.destination_track(obj)
        self.kind = "CUSTOM" if track else "RETAIL"
        self.track = track
        current = str(obj.get(level_links.DESTINATION_FIELD, ""))
        if current and any(item[0] == current for item in _level_items(self, context)):
            self.level = current
        return context.window_manager.invoke_props_dialog(self, width=360)

    def draw(self, context):
        layout = self.layout
        layout.row().prop(self, "kind", expand=True)
        if self.kind == "RETAIL":
            layout.prop(self, "level")
        else:
            layout.prop(self, "track")
            column = layout.column(align=True)
            column.active = False
            column.label(text="Filled in by DKR-R when the hub loads.")
            column.label(text="Not installed: leads to the Central Area hub.")

    def execute(self, context):
        obj = context.active_object
        if self.kind == "CUSTOM":
            track = self.track.strip()
            try:
                dkrmap.validate_id(track)
            except dkrmap.DkrMapError:
                suggestion = dkrmap.normalise_id(track)
                self.report({"ERROR"}, "%r is not a track id; did you mean %r?"
                            % (track, suggestion))
                return {"CANCELLED"}
            obj[scene.PROP_DESTINATION_TRACK] = track
            value = level_links.PLACEHOLDER_LEVEL
        else:
            if self.level in ("", "NONE"):
                self.report({"ERROR"}, "no level chosen")
                return {"CANCELLED"}
            if scene.PROP_DESTINATION_TRACK in obj:
                del obj[scene.PROP_DESTINATION_TRACK]
            value = self.level
        obj[level_links.DESTINATION_FIELD] = value
        # An exit imported without a destination lists the field as absent,
        # and an absent field is skipped on export whatever it now holds.
        absent = scene._load_json(obj, scene.PROP_ABSENT, [])
        if level_links.DESTINATION_FIELD in absent:
            absent = [name for name in absent if name != level_links.DESTINATION_FIELD]
            if absent:
                obj[scene.PROP_ABSENT] = json.dumps(sorted(absent))
            else:
                del obj[scene.PROP_ABSENT]
        for area in context.screen.areas if context.screen else ():
            area.tag_redraw()
        self.report({"INFO"}, "exit leads to %s" % describe_destination(obj))
        return {"FINISHED"}


CLASSES = (
    DKR_OT_add_racer_set,
    DKR_OT_set_exit_destination,
)
