"""Turn a curve into an AI node graph.

The author draws the line they want the AI to follow; this samples it into
``ASSET_OBJECT_AINODE`` objects with the adjacency already wired. The graph
rules live in :mod:`track_lab.ai_graph`; this file is the Blender end of
it - reading a curve's shape, and writing the nodes back into the scene.
"""

from __future__ import annotations

import traceback

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty

from .. import ai_graph, catalog as catalog_module, prefs, scene
from ..gltf_io import MapObject

AINODE = "ASSET_OBJECT_AINODE"


def _curve_points(depsgraph, curve_object):
    """The curve's shape as world-space points, however it was built.

    Evaluating through the dependency graph turns Bezier and NURBS control
    points into the tessellated polyline the sampler wants, and picks up any
    modifiers, so what gets sampled is the line the author sees.
    """
    evaluated = curve_object.evaluated_get(depsgraph)
    try:
        mesh = evaluated.to_mesh()
    except RuntimeError:
        mesh = None

    matrix = curve_object.matrix_world
    if mesh is not None and len(mesh.vertices) > 1:
        points = [tuple(matrix @ v.co) for v in mesh.vertices]
        evaluated.to_mesh_clear()
        return points
    if mesh is not None:
        evaluated.to_mesh_clear()

    # A curve with no thickness produces no mesh, so fall back to its points.
    points = []
    for spline in curve_object.data.splines:
        if spline.type == "BEZIER":
            points += [tuple(matrix @ p.co) for p in spline.bezier_points]
        else:
            points += [tuple(matrix @ p.co.to_3d()) for p in spline.points]
    return points


def _is_closed(curve_object):
    return any(spline.use_cyclic_u for spline in curve_object.data.splines)


def _write_nodes(context, graph, catalog, elevation, unk8):
    """Replace the scene's AI nodes with the ones this graph describes."""
    for existing in list(scene.iter_dkr_objects(context)):
        if str(existing.get(scene.PROP_ID)) == AINODE:
            bpy.data.objects.remove(existing, do_unlink=True)

    object_type = catalog.get(AINODE)
    root = scene.ensure_root(context)
    tree = prefs.resolve(context)
    order = scene.next_order(context)
    created = []
    for node in graph.nodes:
        fields = object_type.fresh_fields()
        fields["nodeID"] = node.node_id
        fields["adjacent"] = node.adjacent
        fields["elevation"] = int(elevation)
        if "unk8" in fields:
            fields["unk8"] = int(unk8)
        placed = MapObject(
            object_id=AINODE,
            name=object_type.node_name,
            translation=list(node.position),
            fields=fields,
        )
        empty = scene.create_empty(context, placed, catalog, root, tree,
                                   context.scene.dkr.slot)
        empty["dkr_order"] = order
        order += 1
        created.append(empty)
    return created


class DKR_OT_ai_from_curve(bpy.types.Operator):
    """Sample the selected curve into an AI node graph"""

    bl_idname = "dkr.ai_from_curve"
    bl_label = "AI Nodes From Curve"
    bl_options = {"REGISTER", "UNDO"}

    spacing: FloatProperty(
        name="Spacing",
        description=(
            "Distance between nodes in world units. Retail tracks run about 40 "
            "nodes around a lap, so a few hundred units is typical"
        ),
        default=300.0,
        min=1.0,
        soft_max=2000.0,
    )
    closed: EnumProperty(
        name="Loop",
        description="Whether the line closes back on itself",
        items=[
            ("AUTO", "From Curve", "Close it if the curve itself is cyclic"),
            ("YES", "Closed", "Join the last node back to the first"),
            ("NO", "Open", "Leave the line as a chain"),
        ],
        default="AUTO",
    )
    elevation: IntProperty(
        name="Elevation",
        description=(
            "The elevation field on every node. Retail uses it as a route tier, "
            "not a height, and leaves it 0 on most tracks"
        ),
        default=0, min=-128, max=127,
    )
    unk8: IntProperty(
        name="Destination Class",
        description="What this node is a destination for, which is how the game's pathfinder picks a target: 0 is an ordinary path node, 1 a weapon balloon, 3 a (re)entry point, and 4 to 7 the base of player 0 to 3. Retail uses 0 to 7",
        default=0, min=0, max=255,
    )
    replace: BoolProperty(
        name="Replace Existing Nodes",
        description="Remove AI nodes already in the scene first",
        default=True,
    )

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.type == "CURVE"

    def execute(self, context):
        curve_object = context.active_object
        points = [
            tuple(scene.to_map(p)) for p in _curve_points(
                context.evaluated_depsgraph_get(), curve_object
            )
        ]
        if len(points) < 2:
            self.report({"ERROR"}, "the curve has no usable shape")
            return {"CANCELLED"}

        closed = _is_closed(curve_object) if self.closed == "AUTO" else self.closed == "YES"

        try:
            catalog = catalog_module.load()
            graph = ai_graph.build_from_path(
                points, self.spacing, closed=closed, elevation=self.elevation
            )
        except ai_graph.AiGraphError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "could not build the graph: %s" % error)
            return {"CANCELLED"}

        if not self.replace:
            existing = sum(
                1 for o in scene.iter_dkr_objects(context)
                if str(o.get(scene.PROP_ID)) == AINODE
            )
            if existing:
                self.report(
                    {"ERROR"},
                    "the scene already has %d AI node(s); node ids would collide. "
                    "Use Replace, or clear them first" % existing,
                )
                return {"CANCELLED"}

        created = _write_nodes(context, graph, catalog, self.elevation, self.unk8)
        self.report(
            {"INFO"},
            "%d AI nodes from %s (%s)"
            % (len(created), curve_object.name, "loop" if closed else "open chain"),
        )
        return {"FINISHED"}


class DKR_OT_ai_add_branch(bpy.types.Operator):
    """Splice the selected curve onto the existing AI line as an alternate route"""

    bl_idname = "dkr.ai_add_branch"
    bl_label = "Add AI Branch From Curve"
    bl_options = {"REGISTER", "UNDO"}

    spacing: FloatProperty(
        name="Spacing",
        description="Distance between nodes in world units",
        default=300.0, min=1.0, soft_max=2000.0,
    )
    elevation: IntProperty(
        name="Elevation",
        description=(
            "Which floor of a multi-level arena these nodes are on, 0 to 3. It "
            "is a tier and not a height: the game sorts the nodes by Y and "
            "derives the boundary between floors from where the tier changes"
        ),
        default=0, min=-128, max=127,
    )
    unk8: IntProperty(
        name="Destination Class",
        description="What this node is a destination for, which is how the game's pathfinder picks a target: 0 is an ordinary path node, 1 a weapon balloon, 3 a (re)entry point, and 4 to 7 the base of player 0 to 3. Retail uses 0 to 7",
        default=0, min=0, max=255,
    )

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.type == "CURVE"

    def execute(self, context):
        catalog = catalog_module.load()
        graph, empties = _read_existing_graph(context)
        if graph is None:
            self.report({"ERROR"}, "no AI nodes in the scene to branch from")
            return {"CANCELLED"}

        points = [
            tuple(scene.to_map(p)) for p in _curve_points(
                context.evaluated_depsgraph_get(), context.active_object
            )
        ]
        try:
            added = ai_graph.attach_branch(
                graph, points, self.spacing, elevation=self.elevation
            )
        except ai_graph.AiGraphError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        # Not 0 unconditionally: a branch written with destination class 0 can
        # never be a target the pathfinder is asked for, which quietly made
        # every branch this operator produced unreachable as a destination.
        _write_nodes(context, graph, catalog, self.elevation, self.unk8)
        self.report({"INFO"}, "added %d branch node(s)" % len(added))
        return {"FINISHED"}


def _read_existing_graph(context):
    """Rebuild an :class:`AiGraph` from the AI nodes already in the scene."""
    empties = [
        o for o in scene.iter_dkr_objects(context)
        if str(o.get(scene.PROP_ID)) == AINODE
    ]
    if not empties:
        return None, []

    empties.sort(key=lambda o: int(o.get("nodeID", 0)))
    graph = ai_graph.AiGraph()
    index_of = {}
    for empty in empties:
        node = graph.add(
            tuple(scene.to_map(empty.matrix_world.translation)),
            elevation=int(empty.get("elevation", 0)),
        )
        index_of[int(empty.get("nodeID", node.node_id))] = node

    for empty in empties:
        node = index_of[int(empty.get("nodeID", 0))]
        for neighbour_id in list(empty.get("adjacent", [])):
            if neighbour_id == ai_graph.NO_NEIGHBOUR:
                continue
            other = index_of.get(int(neighbour_id))
            if other is not None and graph.can_link(node, other):
                graph.link(node, other)
    return graph, empties


CLASSES = (
    DKR_OT_ai_from_curve,
    DKR_OT_ai_add_branch,
)
