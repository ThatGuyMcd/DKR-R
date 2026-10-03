"""Generate a start grid: the right number of start positions, in one piece.

A missing ``racerIndex`` does not fail loudly. The game zeroes every start
position before reading the map and never zeroes the angle, so a race whose
grid has no index 5 starts the sixth racer at the map origin facing wherever
(``objects.c:1121-1136``). Placing a grid one start position at a time is how
that happens, which is why this makes the whole set at once, with the indices
already filled in, from the count the Level Type implies.

The start positions are parented to one Empty, so the grid moves and turns as a
whole. The root carries no ``dkr_id`` and is never exported; the export reads
each start position's world position and world angle (:func:`scene.world_yaw`).
"""

from __future__ import annotations

import math

import bpy
from bpy.props import BoolProperty, FloatProperty, IntProperty, StringProperty
from mathutils import Matrix

from .. import catalog as catalog_module, level_types, prefs, scene
from ..gltf_io import MapObject
from . import geometry as geometry_ops

SETUPPOINT = level_types.SETUPPOINT


def children_of(root) -> list:
    return [child for child in root.children if scene.is_dkr_object(child)]


def root_for(context, entrance: int):
    for root in scene.grid_roots(context):
        if int(root.get(scene.PROP_GRID_ENTRANCE, 0)) == entrance:
            return root
    return None


def next_free_entrance(context) -> int:
    """The lowest entranceID nothing uses yet, for a hub's next arrival point."""
    used = {
        int(obj.get("entranceID", 0)) for obj in scene.iter_dkr_objects(context)
        if str(obj.get(scene.PROP_ID)) == SETUPPOINT
    }
    used |= {int(r.get(scene.PROP_GRID_ENTRANCE, 0)) for r in scene.grid_roots(context)}
    entrance = 0
    while entrance in used:
        entrance += 1
    return entrance


def root_name(key: str, entrance: int) -> str:
    if key == level_types.HUB:
        return "DKR Entrance e%d" % entrance
    return "DKR Start Grid" + (" e%d" % entrance if entrance else "")


def loose_start_positions(context) -> dict:
    """``{entranceID: count}`` of start positions in no grid."""
    found = {}
    for obj in scene.iter_dkr_objects(context):
        if str(obj.get(scene.PROP_ID)) != SETUPPOINT or scene.is_grid_root(obj.parent):
            continue
        entrance = int(obj.get("entranceID", 0))
        found[entrance] = found.get(entrance, 0) + 1
    return found


def build_grid(context, key, entrance=0, spacing=1.0,
               radius=level_types.CHALLENGE_RADIUS, facing=0.0, replace=True,
               drop=True, root=None):
    """Make or remake one grid; returns ``(root, removed, dropped)``.

    Remaking keeps the root, so a grid rebuilt for another level type stands
    where the old one stood and faces the same way.
    """
    catalog = catalog_module.load()
    object_type = catalog.get(SETUPPOINT)
    template = level_types.grid_template(key, spacing, radius, facing)

    removed = 0
    if replace:
        if root is not None:
            for child in children_of(root):
                bpy.data.objects.remove(child, do_unlink=True)
                removed += 1
        for obj in list(scene.iter_dkr_objects(context)):
            if (obj.parent is None and str(obj.get(scene.PROP_ID)) == SETUPPOINT
                    and int(obj.get("entranceID", 0)) == entrance):
                bpy.data.objects.remove(obj, do_unlink=True)
                removed += 1

    collection_root = scene.ensure_root(context)
    if root is None:
        root = bpy.data.objects.new(root_name(key, entrance), None)
        root.empty_display_type = "ARROWS"
        root.empty_display_size = 120.0
        cursor = context.scene.cursor
        root.location = cursor.location.copy()
        # Rounded to the step an angleY can store, or the positions (exact)
        # and the directions (rounded on export) disagree by up to 2.8 degrees.
        yaw = level_types.snap_angle(math.degrees(cursor.rotation_euler.z))
        root.rotation_euler = (0.0, 0.0, math.radians(yaw))
        root.lock_rotation = (True, True, False)
        root.lock_scale = (True, True, True)
        scene.collection_for("racing", collection_root).objects.link(root)
    root[scene.PROP_GRID_ROOT] = True
    root[scene.PROP_GRID_KEY] = key
    root[scene.PROP_GRID_ENTRANCE] = int(entrance)
    root[scene.PROP_GRID_SPACING] = float(spacing)
    root[scene.PROP_GRID_RADIUS] = float(radius)
    root[scene.PROP_GRID_FACING] = float(facing)

    tree = prefs.resolve(context)
    order = scene.next_order(context)
    created = []
    for index, (x, y, yaw) in enumerate(template):
        fields = object_type.fresh_fields()
        fields["racerIndex"] = index
        fields["entranceID"] = int(entrance)
        if "vehicle" in fields:
            fields["vehicle"] = "VEHICLE_NO_OVERRIDE"
        placed = MapObject(object_id=SETUPPOINT, name=object_type.node_name,
                           translation=[0.0, 0.0, 0.0], fields=fields)
        child = scene.create_empty(context, placed, catalog, collection_root,
                                   tree, scene.SLOT_STRUCTURE)
        child.parent = root
        child.matrix_parent_inverse = Matrix.Identity(4)
        child.location = (x, y, 0.0)
        child.rotation_euler = (0.0, 0.0, math.radians(yaw))
        child["dkr_order"] = order
        order += 1
        created.append(child)

    dropped = 0
    targets = geometry_ops.geometry_objects(context) if drop else []
    if targets:
        # One by one, because a start straight can slope.
        context.view_layer.update()
        depsgraph = context.evaluated_depsgraph_get()
        for child in created:
            landing = geometry_ops.surface_below(
                child.matrix_world.translation.copy(), targets, depsgraph
            )
            if landing is not None:
                child.matrix_world.translation = landing
                dropped += 1
    return root, removed, dropped


def regenerate(context, root):
    """Rebuild a grid for the current level type, in the same place."""
    key = level_types.current_key(context.scene.dkr)
    return build_grid(
        context, key,
        entrance=int(root.get(scene.PROP_GRID_ENTRANCE, 0)),
        spacing=float(root.get(scene.PROP_GRID_SPACING, 1.0)),
        radius=float(root.get(scene.PROP_GRID_RADIUS, level_types.CHALLENGE_RADIUS)),
        facing=float(root.get(scene.PROP_GRID_FACING, 0.0)),
        replace=True, drop=True, root=root,
    )


class DKR_OT_generate_start_grid(bpy.types.Operator):
    """Place the start positions this level type needs, parented to one root"""

    bl_idname = "dkr.generate_start_grid"
    bl_label = "Generate Start Grid"
    bl_options = {"REGISTER", "UNDO"}

    entrance: IntProperty(
        name="Entrance",
        description=(
            "entranceID of every start position in the grid: which door or "
            "warp the level was entered through"
        ),
        default=0, min=0, max=7, options={"SKIP_SAVE"},
    )
    spacing: FloatProperty(
        name="Spacing",
        description=(
            "Scale of the retail grid. Retail tracks vary: the lateral step "
            "runs from 91 to 136 units"
        ),
        default=1.0, min=0.5, max=2.0, step=5, precision=2,
        options={"SKIP_SAVE"},
    )
    radius: FloatProperty(
        name="Radius",
        description=(
            "Distance from the centre. Retail: about 1200 (Icicle Pyramid), "
            "2000 (Fire Mountain), 3400 (Darkwater Beach)"
        ),
        default=level_types.CHALLENGE_RADIUS, min=200.0, max=5000.0,
        step=5000, options={"SKIP_SAVE"},
    )
    facing: FloatProperty(
        name="Facing Offset",
        description="0 faces the centre. Smokey Castle turns them 45° further, a pinwheel",
        default=0.0, min=-180.0, max=180.0, step=562.5, precision=3,
        options={"SKIP_SAVE"},
    )
    replace: BoolProperty(
        name="Replace Existing",
        description="Remove the start positions already at this entrance first",
        default=True,
    )
    drop: BoolProperty(
        name="Drop To Surface",
        description=(
            "Drop each start position onto the track, one by one - a start "
            "straight can slope"
        ),
        default=True,
    )
    root_name: StringProperty(options={"HIDDEN", "SKIP_SAVE"})

    @classmethod
    def poll(cls, context):
        key = level_types.current_key(context.scene.dkr)
        if not level_types.spawn_count(key):
            cls.poll_message_set(
                "No racers are spawned in a %s, so there is no start grid to make"
                % level_types.label(key)
            )
            return False
        return True

    @classmethod
    def description(cls, context, properties):
        key = level_types.current_key(context.scene.dkr)
        count = level_types.spawn_count(key)
        if key == level_types.HUB:
            return (
                "Place one start position for the next free entrance at the 3D "
                "cursor. A hub is entered through doors and warps, and each has "
                "its own entranceID"
            )
        if not count:
            return "Not available: this level type spawns no racers"
        return (
            "%s at the 3D cursor, parented to one root you move and rotate as a "
            "whole. racerIndex 0-%d and the entranceID are filled in, and a grid "
            "already there is replaced where it stands"
            % (level_types.grid_blurb(key), count - 1)
        )

    def _root(self, context, key):
        if self.root_name:
            obj = context.scene.objects.get(self.root_name)
            return obj if scene.is_grid_root(obj) else None
        if key == level_types.HUB or not self.replace:
            return None
        return root_for(context, self.entrance)

    def invoke(self, context, event):
        # Rebuilding an existing grid starts from how it was built.
        root = self._root(context, level_types.current_key(context.scene.dkr))
        if root is not None:
            self.entrance = int(root.get(scene.PROP_GRID_ENTRANCE, self.entrance))
            self.spacing = float(root.get(scene.PROP_GRID_SPACING, self.spacing))
            self.radius = float(root.get(scene.PROP_GRID_RADIUS, self.radius))
            self.facing = float(root.get(scene.PROP_GRID_FACING, self.facing))
        return self.execute(context)

    def draw(self, context):
        key = level_types.current_key(context.scene.dkr)
        layout = self.layout
        if key != level_types.HUB:
            layout.prop(self, "entrance")
        if key in (level_types.RACE, level_types.TEST_RACE, level_types.BOSS):
            layout.prop(self, "spacing")
        if level_types.is_challenge(key):
            layout.prop(self, "radius")
            layout.prop(self, "facing")
        layout.prop(self, "replace")
        layout.prop(self, "drop")

    def execute(self, context):
        key = level_types.current_key(context.scene.dkr)
        if not level_types.spawn_count(key):
            self.report({"ERROR"}, "No racers are spawned in a %s, so there is "
                        "no start grid to make" % level_types.label(key))
            return {"CANCELLED"}

        root = self._root(context, key)
        entrance = self.entrance
        if key == level_types.HUB and root is None:
            entrance = next_free_entrance(context)
        elif root is not None and self.root_name and \
                not self.properties.is_property_set("entrance"):
            entrance = int(root.get(scene.PROP_GRID_ENTRANCE, 0))

        root, removed, dropped = build_grid(
            context, key, entrance, self.spacing, self.radius, self.facing,
            self.replace, self.drop, root,
        )

        for obj in context.selected_objects:
            obj.select_set(False)
        root.select_set(True)
        context.view_layer.objects.active = root

        message = "%s, entrance %d" % (level_types.grid_blurb(key), entrance)
        if removed:
            message += ", replacing %d" % removed
        if self.drop:
            message += ("; dropped to the surface" if dropped else
                        "; no geometry loaded, nothing to drop onto"
                        if not geometry_ops.geometry_objects(context) else
                        "; found no surface below")
        self.report({"INFO"}, message)
        return {"FINISHED"}


class DKR_OT_select_grid_children(bpy.types.Operator):
    """Select every start position in this grid"""

    bl_idname = "dkr.select_grid_children"
    bl_label = "Select Children"
    bl_options = {"REGISTER", "UNDO"}

    root_name: StringProperty(options={"HIDDEN"})

    def execute(self, context):
        root = context.scene.objects.get(self.root_name) or context.active_object
        if not scene.is_grid_root(root):
            self.report({"ERROR"}, "no start grid selected")
            return {"CANCELLED"}
        children = children_of(root)
        for obj in context.selected_objects:
            obj.select_set(False)
        for child in children:
            child.select_set(True)
        if children:
            context.view_layer.objects.active = children[0]
        self.report({"INFO"}, "selected %d start position(s)" % len(children))
        return {"FINISHED"}


CLASSES = (
    DKR_OT_generate_start_grid,
    DKR_OT_select_grid_children,
)
