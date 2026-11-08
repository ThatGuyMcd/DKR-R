"""The AI node graph in Blender: building it, editing it and drawing it.

The author draws the line they want the AI to follow; this samples it into
``ASSET_OBJECT_AINODE`` objects with the adjacency already wired. The graph
rules live in :mod:`track_lab.ai_graph`; this file is the Blender end of
it - reading a curve's shape, and writing the nodes back into the scene.

It also holds the hand tools - linking and unlinking two nodes, the weapon
node a balloon gets when it is placed in Battle or Bananas - and the viewport
overlay, whose geometry :mod:`track_lab.ai_graph_view` works out.

**Weapon nodes.** Arena bots never look at a balloon; they drive to AI nodes
of destination class 1 (see :mod:`track_lab.ai_graph`). So a balloon placed
in Battle or Bananas gets a class-1 node on it, *parented* to it so moving
the balloon moves the node. The parent inverse is the balloon's matrix at the
time, so the node's own location stays its world position: export reads
``matrix_world`` either way, and if the balloon is deleted the node stays
where it was instead of jumping to the origin.
"""

from __future__ import annotations

import traceback

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty

from .. import (ai_graph, ai_graph_view, catalog as catalog_module, level_types,
                mode_checklist, prefs, scene)
from ..gltf_io import MapObject

AINODE = "ASSET_OBJECT_AINODE"
BALLOON = level_types.WEAPON_BALLOON
WEAPON_NODE_NAME = "Weapon Node · %s"


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


def _write_nodes(context, graph, catalog, slots=None, keep=(), extras=None):
    """Replace the scene's AI nodes with the ones this graph describes.

    Each node keeps its own elevation and destination class, and ``slots``
    maps a node id to the object map it came from, so rewriting the graph to
    splice a branch on does not flatten what the author set on the others.
    ``keep`` are nodes left in place, and ``extras`` maps a node id to the
    ``(parent, name)`` it had, so a weapon node stays on its balloon.
    """
    kept = {id(obj) for obj in keep}
    for existing in list(scene.iter_dkr_objects(context)):
        if str(existing.get(scene.PROP_ID)) == AINODE and id(existing) not in kept:
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
        fields["elevation"] = int(node.elevation)
        if "unk8" in fields:
            fields["unk8"] = int(node.destination)
        placed = MapObject(
            object_id=AINODE,
            name=object_type.node_name,
            translation=list(node.position),
            fields=fields,
        )
        slot = (slots or {}).get(node.node_id, context.scene.dkr.slot)
        empty = scene.create_empty(context, placed, catalog, root, tree, slot)
        empty["dkr_order"] = order
        order += 1
        parent, name = (extras or {}).get(node.node_id, (None, None))
        if name:
            empty.name = name
        if parent is not None:
            attach(empty, parent)
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
        return _curve_selected(cls, context)

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
                points, self.spacing, closed=closed, elevation=self.elevation,
                destination=self.unk8,
            )
        except ai_graph.AiGraphError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "could not build the graph: %s" % error)
            return {"CANCELLED"}

        # A balloon's weapon node belongs to the balloon, not to the line
        # being replaced: it stays, takes an id after the new nodes, and is
        # linked to them - which is what lets an author place the balloons
        # first and draw the graph after.
        kept = [o for o in node_empties(context) if balloon_of(o) is not None]
        if not self.replace:
            existing = len(node_empties(context)) - len(kept)
            if existing:
                self.report(
                    {"ERROR"},
                    "the scene already has %d AI node(s); node ids would collide. "
                    "Use Replace, or clear them first" % existing,
                )
                return {"CANCELLED"}
        if len(graph.nodes) + len(kept) > ai_graph.MAX_NODES:
            self.report({"ERROR"}, "%d nodes plus the %d weapon nodes on balloons "
                        "is over the %d the game keeps; increase the spacing"
                        % (len(graph.nodes), len(kept), ai_graph.MAX_NODES))
            return {"CANCELLED"}

        created = _write_nodes(context, graph, catalog, keep=kept)
        message = "%d AI nodes from %s (%s)" % (
            len(created), curve_object.name, "loop" if closed else "open chain")
        if kept:
            context.view_layer.update()
            for offset, node in enumerate(kept):
                node["nodeID"] = len(graph.nodes) + offset
                node["adjacent"] = [ai_graph.NO_NEIGHBOUR] * ai_graph.MAX_NEIGHBOURS
            joined = sum(1 for node in kept if auto_link(context, node))
            message += "; %d of %d weapon node(s) on balloons linked to it" % (
                joined, len(kept))
        self.report({"INFO"}, message)
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
        return _curve_selected(cls, context)

    def execute(self, context):
        catalog = catalog_module.load()
        graph, empties, slots, extras = _read_existing_graph(context)
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
                graph, points, self.spacing, elevation=self.elevation,
                destination=self.unk8,
            )
        except ai_graph.AiGraphError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        # The branch's nodes take this operator's elevation and destination
        # class; every node already there keeps its own. Writing these two on
        # the whole graph used to flatten every node's class to the branch's.
        _write_nodes(context, graph, catalog, slots, extras=extras)
        self.report({"INFO"}, "added %d branch node(s)" % len(added))
        return {"FINISHED"}


def _curve_selected(cls, context):
    obj = context.active_object
    if obj is None or obj.type != "CURVE":
        cls.poll_message_set(
            "Select the curve to sample first: draw it through the arena "
            "(Add > Curve), then click this")
        return False
    return True


def _read_existing_graph(context):
    """Rebuild an :class:`AiGraph` from the AI nodes already in the scene.

    Also ``{node id: slot}`` and ``{node id: (parent, name)}`` for
    :func:`_write_nodes`, keyed by the ids the rebuilt graph gave them, so a
    weapon node comes back on its balloon under its own name.
    """
    empties = node_empties(context)
    if not empties:
        return None, [], {}, {}

    empties.sort(key=lambda o: int(o.get("nodeID", 0)))
    graph = ai_graph.AiGraph()
    index_of = {}
    slots = {}
    extras = {}
    for empty in empties:
        node = graph.add(
            tuple(scene.to_map(empty.matrix_world.translation)),
            elevation=int(empty.get("elevation", 0)),
            destination=int(empty.get("unk8", 0)),
        )
        slots[node.node_id] = scene.slot_of(empty)
        parent = empty.parent if not is_node(empty.parent) else None
        extras[node.node_id] = (parent, empty.name)
        index_of[int(empty.get("nodeID", node.node_id))] = node

    for empty in empties:
        node = index_of[int(empty.get("nodeID", 0))]
        for neighbour_id in list(empty.get("adjacent", [])):
            if neighbour_id == ai_graph.NO_NEIGHBOUR:
                continue
            other = index_of.get(int(neighbour_id))
            if other is not None and graph.can_link(node, other):
                graph.link(node, other)
    return graph, empties, slots, extras


class DKR_OT_ai_tag_destinations(bpy.types.Operator):
    """Give the arena AI its targets: the AI node nearest each weapon balloon
    becomes destination class 1, and in Bananas the node nearest each racer's
    chest becomes 4 + that racer. Bots drive to those nodes to fetch a weapon
    and to bank. A node already given a class keeps it"""

    bl_idname = "dkr.ai_tag_destinations"
    bl_label = "Tag Destinations"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        if not any(str(o.get(scene.PROP_ID)) == AINODE
                   for o in scene.iter_dkr_objects(context)):
            cls.poll_message_set("There are no AI nodes to tag yet: sample a "
                                 "curve with AI Line From Curve first")
            return False
        return True

    def execute(self, context):
        context.view_layer.update()
        objects = scene.iter_dkr_objects(context)
        bananas = level_types.current_key(context.scene.dkr) == level_types.BANANAS
        nodes = [o for o in objects if str(o.get(scene.PROP_ID)) == AINODE]
        balloons = [o for o in objects
                    if str(o.get(scene.PROP_ID)) == level_types.WEAPON_BALLOON]
        chests = []
        if bananas:
            chests = [(int(o.get("playerID", 1)) - 1, o) for o in objects
                      if str(o.get(scene.PROP_ID)) == mode_checklist.CHEST]
        if not balloons and not chests:
            self.report({"WARNING"}, "no weapon balloons%s to tag nodes for"
                        % (" or chests" if bananas else ""))
            return {"CANCELLED"}
        tags = ai_graph.destination_tags(
            [tuple(n.matrix_world.translation) for n in nodes],
            [tuple(b.matrix_world.translation) for b in balloons],
            [(racer, tuple(c.matrix_world.translation)) for racer, c in chests],
            [int(n.get("unk8", 0)) for n in nodes],
        )
        for index, value in tags:
            nodes[index]["unk8"] = value
        weapons = sum(1 for _index, value in tags
                      if value == mode_checklist.CLASS_BALLOON)
        if tags:
            message = "tagged %d weapon node(s)" % weapons
            if bananas:
                message += " and %d bank node(s)" % (len(tags) - weapons)
        else:
            message = "every balloon%s already has its node" % (
                " and chest" if chests else "")

        # Tagged whatever the distance, as before; but a bot turns for its
        # next node 50 units out, so a balloon further than that from the
        # node it was given is driven past.
        far = ai_graph.balloons_off_their_node(
            [tuple(n.matrix_world.translation) for n in nodes],
            [tuple(b.matrix_world.translation) for b in balloons])
        if far:
            named = ["%s (%d units)" % (balloons[index].name, distance)
                     for index, distance in far[:4]]
            if len(far) > 4:
                named.append("%d more" % (len(far) - 4))
            if len(named) > 1:
                named = "%s and %s are" % (", ".join(named[:-1]), named[-1])
            else:
                named = "%s is" % named[0]
            self.report({"WARNING"}, "%s, but %s more than %d units from the "
                        "nearest node: the bot will reach the node without "
                        "touching the balloon. Put a node on it - Auto Weapon "
                        "Node in Place does that for new balloons"
                        % (message, named, ai_graph.WEAPON_REACH))
        else:
            self.report({"INFO"}, message)
        return {"FINISHED"}


# ---------------------------------------------------------------------------
# Weapon nodes and links by hand
# ---------------------------------------------------------------------------

def is_node(obj) -> bool:
    return obj is not None and str(obj.get(scene.PROP_ID, "")) == AINODE


def is_balloon(obj) -> bool:
    return obj is not None and str(obj.get(scene.PROP_ID, "")) == BALLOON


def node_empties(context):
    return [o for o in scene.iter_dkr_objects(context) if is_node(o)]


def balloon_of(node):
    """The weapon balloon a node is parented to, or ``None``."""
    parent = node.parent if node is not None else None
    return parent if is_balloon(parent) else None


def weapon_nodes_of(balloon):
    """The AI nodes parented to a balloon."""
    return [child for child in balloon.children if is_node(child)]


def balloon_label(balloon) -> str:
    """``"Red Balloon"``, from the colour preset its fields match."""
    chosen = level_types.preset_for(BALLOON, balloon)
    return chosen.label if chosen else "Weapon Balloon"


def node_id(obj):
    value = obj.get("nodeID") if obj is not None else None
    return value if isinstance(value, int) else None


def slots_of(obj):
    value = obj.get("adjacent", [])
    return [int(v) for v in (value.to_list() if hasattr(value, "to_list") else value)]


def neighbours(obj, by_id=None):
    """The ids ``obj`` links to that name another node in the scene."""
    if by_id is None:
        by_id = {node_id(o): o for o in node_empties(bpy.context)}
    own = node_id(obj)
    return [value for value in slots_of(obj)
            if value != ai_graph.NO_NEIGHBOUR and value in by_id and value != own]


def attach(node, balloon):
    """Parent ``node`` to ``balloon`` without moving it (see the module)."""
    if balloon.parent is not None:
        bpy.context.view_layer.update()
        matrix = balloon.matrix_world.copy()
    else:
        matrix = balloon.matrix_basis.copy()
    world = node.matrix_basis.copy()
    node.parent = balloon
    node.matrix_parent_inverse = matrix.inverted()
    node.matrix_basis = world


def link(a, b):
    """Write each node's id into the other's first free slot."""
    a_slots = ai_graph.link_slots(slots_of(a), node_id(b))
    b_slots = ai_graph.link_slots(slots_of(b), node_id(a))
    a["adjacent"], b["adjacent"] = a_slots, b_slots


def unlink(a, b):
    a["adjacent"] = ai_graph.unlink_slots(slots_of(a), node_id(b))
    b["adjacent"] = ai_graph.unlink_slots(slots_of(b), node_id(a))


def auto_link(context, node):
    """Link ``node`` to the nearest nodes already in a graph; their ids.

    Only nodes holding a link count as the graph, so a weapon node placed
    before any graph exists stays unlinked rather than pairing up with
    another loose one. Straight-line distance only: see
    :data:`ai_graph.AUTO_LINK_RANGE`.
    """
    by_id = {node_id(o): o for o in node_empties(context) if node_id(o) is not None}
    own = slots_of(node)
    candidates = []
    for other_id, other in by_id.items():
        if other is node or other_id in own or not neighbours(other, by_id):
            continue
        candidates.append((other_id, tuple(other.matrix_world.translation),
                           ai_graph.free_slots(slots_of(other))))
    room = ai_graph.free_slots(own)
    chosen = ai_graph.link_targets(tuple(node.matrix_world.translation), candidates,
                                   count=min(room, ai_graph.AUTO_LINK_COUNT))
    for other_id in chosen:
        link(node, by_id[other_id])
    return chosen


def add_weapon_node(context, balloon):
    """Put a class-1 AI node on a balloon and link it.

    ``(node, linked ids)``, or ``(None, reason)`` when the track has no node
    id left under the 128 the game keeps.
    """
    from .edit import add_object  # noqa: PLC0415 - edit imports this module

    object_type = catalog_module.load().get(AINODE)
    context.view_layer.update()
    node = add_object(context, object_type, balloon.matrix_world.translation.copy(),
                      {"unk8": ai_graph.CLASS_WEAPON},
                      slot=level_types.tab_of(object_type))
    if node_id(node) is None or node_id(node) >= ai_graph.MAX_NODES:
        bpy.data.objects.remove(node, do_unlink=True)
        return None, ("the track already has the %d AI nodes the game keeps"
                      % ai_graph.MAX_NODES)
    node.name = WEAPON_NODE_NAME % balloon_label(balloon)
    attach(node, balloon)
    context.view_layer.update()
    return node, auto_link(context, node)


def _two_nodes(cls, context):
    """The two selected AI nodes, or ``None`` after saying why not."""
    found = [o for o in context.selected_objects if is_node(o)]
    if len(found) != 2:
        cls.poll_message_set(
            "Select exactly two AI nodes: click one, then Shift+click the "
            "other (%d selected)" % len(found))
        return None
    if node_id(found[0]) is None or node_id(found[0]) == node_id(found[1]):
        cls.poll_message_set("The two nodes need different nodeIDs: give each "
                             "its own in the DKR Object panel")
        return None
    return found


def _linked(a, b) -> bool:
    return node_id(b) in slots_of(a) or node_id(a) in slots_of(b)


class DKR_OT_ai_link_nodes(bpy.types.Operator):
    """Link the two selected AI nodes both ways, as every retail link is.
    Bots drive in a straight line from one to the other, so keep walls out
    of the way"""

    bl_idname = "dkr.ai_link_nodes"
    bl_label = "Link Nodes"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        found = _two_nodes(cls, context)
        if found is None:
            return False
        a, b = found
        if _linked(a, b):
            cls.poll_message_set("Nodes %d and %d are already linked"
                                 % (node_id(a), node_id(b)))
            return False
        for obj in found:
            if not ai_graph.free_slots(slots_of(obj)):
                cls.poll_message_set(
                    "Node %d already has %d neighbours, the most the format "
                    "holds: unlink one first"
                    % (node_id(obj), ai_graph.MAX_NEIGHBOURS))
                return False
        return True

    def execute(self, context):
        a, b = [o for o in context.selected_objects if is_node(o)]
        try:
            link(a, b)
        except ai_graph.AiGraphError as error:
            self.report({"ERROR"}, "node %s %s" % (node_id(a), error))
            return {"CANCELLED"}
        _redraw(context)
        self.report({"INFO"}, "linked node %d and node %d"
                    % (node_id(a), node_id(b)))
        return {"FINISHED"}


class DKR_OT_ai_unlink_nodes(bpy.types.Operator):
    """Remove the link between the two selected AI nodes, from both of them"""

    bl_idname = "dkr.ai_unlink_nodes"
    bl_label = "Unlink Nodes"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        found = _two_nodes(cls, context)
        if found is None:
            return False
        if not _linked(*found):
            cls.poll_message_set("Nodes %d and %d are not linked"
                                 % (node_id(found[0]), node_id(found[1])))
            return False
        return True

    def execute(self, context):
        a, b = [o for o in context.selected_objects if is_node(o)]
        unlink(a, b)
        _redraw(context)
        self.report({"INFO"}, "unlinked node %d and node %d"
                    % (node_id(a), node_id(b)))
        return {"FINISHED"}


def loose_weapon_nodes(context):
    """Weapon nodes with no link to another node in the scene."""
    by_id = {node_id(o): o for o in node_empties(context)}
    return [o for o in node_empties(context)
            if int(o.get("unk8", 0)) == ai_graph.CLASS_WEAPON
            and not neighbours(o, by_id)]


class DKR_OT_ai_link_weapon_nodes(bpy.types.Operator):
    """Link each weapon node that has no link yet to the nearest nodes of the
    graph, so the bots can reach it. It goes by straight-line distance and
    cannot see walls: check the new links with Show AI Graph"""

    bl_idname = "dkr.ai_link_weapon_nodes"
    bl_label = "Link Weapon Nodes"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        if not loose_weapon_nodes(context):
            cls.poll_message_set("Every weapon node is already linked")
            return False
        return True

    def execute(self, context):
        context.view_layer.update()
        joined, stranded = 0, []
        for node in loose_weapon_nodes(context):
            if auto_link(context, node):
                joined += 1
            else:
                stranded.append(str(node_id(node)))
        _redraw(context)
        if stranded:
            self.report({"WARNING"}, "linked %d weapon node(s); node %s found no "
                        "graph node within %d units with a free slot: draw the "
                        "graph out to it, or link it by hand with Link Nodes"
                        % (joined, ", ".join(stranded), ai_graph.AUTO_LINK_RANGE))
        else:
            self.report({"INFO"}, "linked %d weapon node(s) to the graph" % joined)
        return {"FINISHED"} if joined else {"CANCELLED"}


class DKR_OT_ai_select_balloon(bpy.types.Operator):
    """Select the weapon balloon this node is parented to"""

    bl_idname = "dkr.ai_select_balloon"
    bl_label = "Select Balloon"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        if balloon_of(context.active_object) is None:
            cls.poll_message_set("The active object is not a weapon node on a "
                                 "balloon")
            return False
        return True

    def execute(self, context):
        balloon = balloon_of(context.active_object)
        for obj in context.selected_objects:
            obj.select_set(False)
        balloon.select_set(True)
        context.view_layer.objects.active = balloon
        return {"FINISHED"}


def _redraw(context):
    for area in context.screen.areas if context.screen else ():
        if area.type in ("VIEW_3D", "PROPERTIES"):
            area.tag_redraw()


# ---------------------------------------------------------------------------
# The overlay
# ---------------------------------------------------------------------------

LINK_WIDTH = 3.5
#: Every line is drawn over a darker, wider copy of itself, so it reads on a
#: pale floor as well as a dark one.
OUTLINE = (0.0, 0.0, 0.0, 0.55)
OUTLINE_EXTRA = 3.0
CIRCLE_WIDTH = 3.0
ROUTE_WIDTH = 6.0
ROUTE_OUTLINE = 10.0
LABEL_SIZE = 14

_GRAPH = {"handle": None, "labels": None, "signature": None, "geometry": None,
          "batches": None, "error": None}


def _plain(value):
    if hasattr(value, "to_list"):
        return tuple(value.to_list())
    return value


def graph_views(context):
    """``(nodes, balloons, active id)`` as :func:`ai_graph_view.build` takes them."""
    nodes, balloons = [], []
    for obj in scene.iter_dkr_objects(context):
        object_id = str(obj.get(scene.PROP_ID))
        if object_id == AINODE:
            parent = balloon_of(obj)
            colour = (ai_graph_view.BALLOON_COLOURS.get(
                str(parent.get("balloonType")), ai_graph_view.UNKNOWN_BALLOON)
                if parent is not None else None)
            nodes.append(ai_graph_view.NodeView(
                node_id(obj), tuple(obj.matrix_world.translation),
                _int(obj.get("unk8", 0)), slots_of(obj), colour))
        elif object_id == BALLOON:
            balloons.append((tuple(obj.matrix_world.translation),
                             ai_graph_view.BALLOON_COLOURS.get(
                                 str(obj.get("balloonType")),
                                 ai_graph_view.UNKNOWN_BALLOON)))
    active = context.view_layer.objects.active if context.view_layer else None
    return nodes, balloons, node_id(active) if is_node(active) else None


def _int(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return value


def _signature(context, settings):
    items = []
    for obj in scene.iter_dkr_objects(context):
        if str(obj.get(scene.PROP_ID)) not in (AINODE, BALLOON):
            continue
        items.append((obj.name,
                      tuple(round(c, 3) for c in obj.matrix_world.translation),
                      _plain(obj.get("nodeID")), _plain(obj.get("unk8")),
                      _plain(obj.get("adjacent")), _plain(obj.get("balloonType")),
                      obj.parent.name if obj.parent else ""))
    active = context.view_layer.objects.active if context.view_layer else None
    return (tuple(items), active.name if is_node(active) else "",
            settings.ai_graph_colour, settings.ai_graph_show_ids)


def graph_geometry(context):
    """The overlay's geometry for the scene now, rebuilt only when something
    it depends on - a node, a link, a balloon, the active node - changed."""
    settings = context.scene.dkr
    signature = _signature(context, settings)
    if signature != _GRAPH["signature"]:
        nodes, balloons, active = graph_views(context)
        _GRAPH.update(signature=signature, batches=None,
                      geometry=ai_graph_view.build(
                          nodes, balloons, active, settings.ai_graph_colour,
                          settings.ai_graph_show_ids))
    return _GRAPH["geometry"]


_ROUND_POINT = {"shader": None, "tried": False}

_ROUND_VERTEX = """
void main()
{
  gl_Position = viewProjectionMatrix * vec4(pos, 1.0);
  gl_PointSize = size;
}
"""

#: A disc with a one-pixel soft edge, cut out of the square a point is.
_ROUND_FRAGMENT = """
void main()
{
  float d = length(gl_PointCoord - vec2(0.5)) * size;
  float a = clamp(size * 0.5 - d + 0.5, 0.0, 1.0);
  if (a <= 0.0) {
    discard;
  }
  fragColor = vec4(color.rgb, color.a * a);
}
"""


def _round_point_shader():
    """A round point shader, or ``None``.

    Blender 5.2's built-in point shaders draw squares here, and a node graph
    of squares reads as a circuit diagram rather than as places on a track.
    Compiled once; any failure falls back to the built-in squares.
    """
    if not _ROUND_POINT["tried"]:
        _ROUND_POINT["tried"] = True
        try:
            import gpu  # noqa: PLC0415

            info = gpu.types.GPUShaderCreateInfo()
            info.push_constant("MAT4", "viewProjectionMatrix")
            info.push_constant("VEC4", "color")
            info.push_constant("FLOAT", "size")
            info.vertex_in(0, "VEC3", "pos")
            info.fragment_out(0, "VEC4", "fragColor")
            info.vertex_source(_ROUND_VERTEX)
            info.fragment_source(_ROUND_FRAGMENT)
            _ROUND_POINT["shader"] = gpu.shader.create_from_info(info)
        except Exception:  # noqa: BLE001 - the squares still work
            traceback.print_exc()
            _ROUND_POINT["shader"] = None
    return _ROUND_POINT["shader"]


def _to_framebuffer(colour):
    """An sRGB colour as the viewport's linear framebuffer wants it.

    Blender's own shaders convert their colour on the way out; a custom one
    has to be handed the converted value, or every colour comes out pale.
    """
    def linear(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return tuple(linear(c) for c in colour[:3]) + (colour[3],)


def _build_graph_batches(found):
    from gpu_extras.batch import batch_for_shader  # noqa: PLC0415

    from .race_ai import _shader  # noqa: PLC0415

    smooth = _shader("POLYLINE_SMOOTH_COLOR")
    uniform = _shader("POLYLINE_UNIFORM_COLOR")
    # One batch per size and colour, since the point shaders take a single
    # colour: a few dozen at most, built once per change.
    point = _round_point_shader() or _shader("POINT_UNIFORM_COLOR", "UNIFORM_COLOR")
    batches = []
    for key, width in (("circles", CIRCLE_WIDTH), ("links", LINK_WIDTH)):
        pos, colour = found[key]
        if smooth is None or uniform is None or not pos:
            continue
        batches.append(("lines", uniform, batch_for_shader(
            uniform, "LINES", {"pos": pos}), OUTLINE, width + OUTLINE_EXTRA, False))
        batches.append(("lines", smooth, batch_for_shader(
            smooth, "LINES", {"pos": pos, "color": colour}), None, width, False))
    if uniform is not None and found["route"]:
        route = batch_for_shader(uniform, "LINES", {"pos": found["route"]})
        batches.append(("lines", uniform, route, OUTLINE, ROUTE_OUTLINE, True))
        batches.append(("lines", uniform, route, ai_graph_view.ROUTE_COLOUR,
                        ROUTE_WIDTH, True))
    if point is not None:
        for size, pos, colours in found["points"]:
            grouped = {}
            for where, colour in zip(pos, colours):
                grouped.setdefault(tuple(colour), []).append(where)
            for colour, where in grouped.items():
                if point is _ROUND_POINT["shader"]:
                    colour = _to_framebuffer(colour)
                batches.append(("points", point, batch_for_shader(
                    point, "POINTS", {"pos": where}), colour, size, False))
    return batches


def _draw_graph():
    """The draw handler. Like the bot lines', it must never raise: a failure
    is printed once, and again only when it changes."""
    context = bpy.context
    if bpy.app.background:
        return  # no GPU to draw with
    try:
        settings = getattr(context.scene, "dkr", None)
        if settings is None or not settings.show_ai_graph:
            return
        found = graph_geometry(context)
        if not found["points"]:
            return
        if _GRAPH["batches"] is None:
            _GRAPH["batches"] = _build_graph_batches(found)

        import gpu  # noqa: PLC0415

        region = context.region
        size = (float(region.width), float(region.height)) if region else (1.0, 1.0)
        view = context.region_data.perspective_matrix if context.region_data else None
        gpu.state.blend_set("ALPHA")
        try:
            for kind, shader, batch, colour, width, on_top in _GRAPH["batches"]:
                gpu.state.depth_test_set(
                    "NONE" if on_top or settings.ai_graph_on_top else "LESS_EQUAL")
                shader.bind()
                if colour is not None:
                    shader.uniform_float("color", colour)
                if kind == "points":
                    if shader is _ROUND_POINT["shader"]:
                        if view is None:
                            continue
                        shader.uniform_float("viewProjectionMatrix", view)
                        shader.uniform_float("size", width)
                        gpu.state.program_point_size_set(True)
                    else:
                        gpu.state.program_point_size_set(False)
                        gpu.state.point_size_set(width)
                else:
                    shader.uniform_float("viewportSize", size)
                    shader.uniform_float("lineWidth", width)
                batch.draw(shader)
        finally:
            gpu.state.blend_set("NONE")
            gpu.state.depth_test_set("NONE")
            gpu.state.point_size_set(1.0)
            gpu.state.program_point_size_set(False)
        _GRAPH["error"] = None
    except Exception:  # noqa: BLE001 - a draw handler must never raise
        message = traceback.format_exc()
        if message != _GRAPH["error"]:
            _GRAPH["error"] = message
            print("Track Lab: could not draw the AI graph\n" + message)


def _draw_graph_labels():
    """Each node's id beside it when Show Node IDs is on, and the words under
    a weapon node that has no balloon in reach."""
    context = bpy.context
    if bpy.app.background:
        return
    try:
        settings = getattr(context.scene, "dkr", None)
        if settings is None or not settings.show_ai_graph:
            return
        region, view3d = context.region, context.region_data
        if region is None or view3d is None:
            return
        import blf  # noqa: PLC0415
        from bpy_extras.view3d_utils import location_3d_to_region_2d  # noqa: PLC0415

        found = graph_geometry(context)
        font = 0
        blf.size(font, LABEL_SIZE)
        blf.enable(font, blf.SHADOW)
        blf.shadow(font, 3, 0.0, 0.0, 0.0, 1.0)
        blf.shadow_offset(font, 1, -1)
        try:
            for where, text, colour in found["labels"]:
                point = location_3d_to_region_2d(region, view3d, where)
                if point is None:
                    continue
                if text == ai_graph_view.NO_BALLOON:
                    # Centred under the node and clear of its reach circle,
                    # however far the view is zoomed in.
                    rim = location_3d_to_region_2d(
                        region, view3d, (where[0], where[1] - ai_graph.WEAPON_REACH,
                                         where[2]))
                    drop = 30.0
                    if rim is not None:
                        drop = max(drop, (rim - point).length + 16.0)
                    width, _height = blf.dimensions(font, text)
                    blf.position(font, point.x - width / 2.0, point.y - drop, 0.0)
                else:
                    blf.position(font, point.x + 14.0, point.y + 8.0, 0.0)
                blf.color(font, *colour)
                blf.draw(font, text)
        finally:
            blf.disable(font, blf.SHADOW)
    except Exception:  # noqa: BLE001 - a draw handler must never raise
        message = traceback.format_exc()
        if message != _GRAPH["error"]:
            _GRAPH["error"] = message
            print("Track Lab: could not draw the AI node ids\n" + message)


def register_overlay():
    if _GRAPH["handle"] is None:
        _GRAPH["handle"] = bpy.types.SpaceView3D.draw_handler_add(
            _draw_graph, (), "WINDOW", "POST_VIEW")
    if _GRAPH["labels"] is None:
        _GRAPH["labels"] = bpy.types.SpaceView3D.draw_handler_add(
            _draw_graph_labels, (), "WINDOW", "POST_PIXEL")


def unregister_overlay():
    for key in ("handle", "labels"):
        if _GRAPH[key] is not None:
            bpy.types.SpaceView3D.draw_handler_remove(_GRAPH[key], "WINDOW")
            _GRAPH[key] = None
    _GRAPH.update(signature=None, geometry=None, batches=None)


CLASSES = (
    DKR_OT_ai_from_curve,
    DKR_OT_ai_add_branch,
    DKR_OT_ai_tag_destinations,
    DKR_OT_ai_link_nodes,
    DKR_OT_ai_unlink_nodes,
    DKR_OT_ai_link_weapon_nodes,
    DKR_OT_ai_select_balloon,
)
