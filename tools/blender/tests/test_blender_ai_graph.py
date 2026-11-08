"""Exercise the arena AI graph tools inside Blender.

The pure rules are checked by ``test_ai_graph.py``; this proves the Blender
end: a weapon balloon placed in Battle brings its weapon node, parented to it
and linked into the graph when there is one; the node survives the graph being
redrawn and Refresh Artwork; Link and Unlink write both nodes; the export
writes a parented node at its world position with its links; and the overlay
builds its geometry in a background Blender, where there is no GPU to draw
it with.

    blender --background --python tools/blender/tests/test_blender_ai_graph.py
"""

from __future__ import annotations

import math
import os
import sys
import tempfile
import traceback

import bpy

_HERE = os.path.dirname(os.path.abspath(__file__))
for argument in sys.argv:
    if argument.endswith("test_blender_ai_graph.py"):
        _HERE = os.path.dirname(os.path.abspath(argument))
        break

sys.path.insert(0, os.path.abspath(os.path.join(_HERE, "..")))
sys.path.insert(0, _HERE)

import track_lab  # noqa: E402
from track_lab import ai_graph, gltf_io, mode_checklist as mc, prefs, scene  # noqa: E402
from track_lab.operators import ai as ai_ops  # noqa: E402
from track_lab.operators import mode_checklist as checklist_ops  # noqa: E402

FAILURES = []
E = ai_graph.NO_NEIGHBOUR


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def fresh(level_type="CHALLENGE", challenge="BATTLE"):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    settings = bpy.context.scene.dkr
    settings.level_type = level_type
    if level_type == "CHALLENGE":
        settings.challenge_type = challenge


def nodes():
    return ai_ops.node_empties(bpy.context)


def by_id():
    return {int(n["nodeID"]): n for n in nodes()}


def slots(obj):
    return [int(v) for v in obj["adjacent"]]


def linked_to(obj):
    return sorted(v for v in slots(obj) if v != E)


def place_balloon(where, preset="RED"):
    bpy.context.scene.cursor.location = where
    result = bpy.ops.dkr.place_object(object_id=mc.BALLOON, preset=preset)
    return result, bpy.context.active_object


def circle_curve(radius, points=16):
    data = bpy.data.curves.new("line", type="CURVE")
    data.dimensions = "3D"
    spline = data.splines.new("POLY")
    spline.points.add(points - 1)
    for i, point in enumerate(spline.points):
        angle = 2.0 * math.pi * i / points
        point.co = (radius * math.cos(angle), radius * math.sin(angle), 0.0, 1.0)
    spline.use_cyclic_u = True
    curve = bpy.data.objects.new("line", data)
    bpy.context.scene.collection.objects.link(curve)
    bpy.context.view_layer.objects.active = curve
    return curve


def select(*objects):
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[-1] if objects else None


def reciprocal():
    found = {node_id: linked_to(n) for node_id, n in by_id().items()}
    return all(a in found.get(b, []) for a, near in found.items() for b in near)


# ---------------------------------------------------------------------------

def test_registration():
    print("registration")
    for name in ("ai_link_nodes", "ai_unlink_nodes", "ai_link_weapon_nodes",
                 "ai_select_balloon"):
        check(hasattr(bpy.ops.dkr, name), "dkr.%s is registered" % name)
    settings = bpy.context.scene.dkr
    check(settings.auto_weapon_node and not settings.show_ai_graph,
          "Auto Weapon Node starts on, the overlay off")


def test_weapon_node_before_the_graph():
    print("a balloon placed before the graph, then the graph drawn")
    fresh()
    result, balloon = place_balloon((850.0, 0.0, 0.0))
    check(result == {"FINISHED"}, "the balloon is placed")
    found = ai_ops.weapon_nodes_of(balloon)
    check(len(found) == 1, "it brought one AI node with it")
    node = found[0]
    check(int(node["unk8"]) == ai_graph.CLASS_WEAPON, "of destination class 1")
    check(node.name.startswith("Weapon Node · Red Balloon"),
          "named for its balloon (%s)" % node.name)
    check(scene.slot_of(node) == scene.SLOT_STRUCTURE, "in the structure map")
    check(linked_to(node) == [], "unlinked, since there is no graph yet")
    bpy.context.view_layer.update()
    gap = (node.matrix_world.translation - balloon.matrix_world.translation).length
    check(gap < 1e-4, "sitting on the balloon (%.4f)" % gap)
    check(bpy.context.active_object == balloon, "the balloon stays the active object")
    loose = {row.rid: row for row in checklist_ops.checklist(bpy.context)}
    check("ai_graph" in loose and loose["ai_graph"].status == mc.MISSING,
          "the checklist still asks for a graph")

    circle_curve(1200.0)
    check(bpy.ops.dkr.ai_from_curve(spacing=300.0) == {"FINISHED"},
          "AI Line From Curve samples the arena")
    found = ai_ops.weapon_nodes_of(balloon)
    check(len(found) == 1 and found[0] == node, "the weapon node survived the rewrite")
    ids = sorted(by_id())
    check(ids == list(range(len(ids))), "with an id after the new nodes, none shared (%r)"
          % ids)
    check(len(linked_to(node)) == ai_graph.AUTO_LINK_COUNT,
          "and linked to the %d nearest (%r)" % (ai_graph.AUTO_LINK_COUNT, linked_to(node)))
    check(reciprocal(), "every link is written both ways")

    # Splicing a branch rewrites every node; the weapon node must come back
    # on its balloon.
    branch = bpy.data.curves.new("branch", type="CURVE")
    branch.dimensions = "3D"
    spline = branch.splines.new("POLY")
    spline.points.add(2)
    for point, co in zip(spline.points, ((1200, 0, 0), (0, 300, 0), (-1200, 0, 0))):
        point.co = (co[0], co[1], co[2], 1.0)
    curve = bpy.data.objects.new("branch", branch)
    bpy.context.scene.collection.objects.link(curve)
    bpy.context.view_layer.objects.active = curve
    check(bpy.ops.dkr.ai_add_branch(spacing=300.0) == {"FINISHED"}, "a branch is added")
    found = ai_ops.weapon_nodes_of(balloon)
    check(len(found) == 1 and found[0].name.startswith("Weapon Node · Red Balloon")
          and int(found[0]["unk8"]) == 1,
          "and the weapon node is still on its balloon, by name and class")


def test_weapon_node_into_a_graph():
    print("a balloon placed beside an existing graph")
    fresh()
    circle_curve(1200.0)
    bpy.ops.dkr.ai_from_curve(spacing=300.0)
    before = len(nodes())
    result, balloon = place_balloon((0.0, 1150.0, 0.0), preset="BLUE")
    node = ai_ops.weapon_nodes_of(balloon)[0]
    check(len(nodes()) == before + 1 and int(node["nodeID"]) == before,
          "a weapon node with the next free id (%d)" % int(node["nodeID"]))
    check(node.name.startswith("Weapon Node · Blue Balloon"), "named Blue")
    near = linked_to(node)
    check(len(near) == 2, "linked to two nodes (%r)" % near)
    graph_nodes = by_id()
    nearest = sorted(graph_nodes, key=lambda i: (
        graph_nodes[i].matrix_world.translation - node.matrix_world.translation).length)
    check(set(near) == set(nearest[1:3]), "the two nearest (%r, %r)" % (near, nearest[1:3]))
    check(reciprocal(), "both ways")

    # The export: a parented node at its world position, with its links.
    balloon.location.x += 40.0
    bpy.context.view_layer.update()
    gap = (node.matrix_world.translation - balloon.matrix_world.translation).length
    check(gap < 1e-4, "moving the balloon carries its node")
    temporary = tempfile.mkdtemp(prefix="dkr-ai-")
    target = os.path.join(temporary, "objects.gltf")
    check(bpy.ops.dkr.export_object_map(filepath=target) == {"FINISHED"},
          "the object map exports")
    written = gltf_io.load(target)
    exported = [o for o in written.objects if o.object_id == mc.AINODE
                and o.fields.get("nodeID") == int(node["nodeID"])]
    check(len(exported) == 1, "the weapon node is in it")
    if exported:
        want = scene.to_map(balloon.matrix_world.translation)
        got = exported[0].translation
        check(all(abs(a - b) < 1e-3 for a, b in zip(got, want)),
              "at the balloon's world position, not its local one (%r vs %r)"
              % (got, want))
        check(sorted(a for a in exported[0].fields["adjacent"] if a != E) == near,
              "with its links")
        check(exported[0].fields.get("unk8") == 1, "and destination class 1")
        check(exported[0].name == "ai-node" or "Weapon" not in exported[0].name,
              "under the type's node name, not the outliner label (%s)"
              % exported[0].name)

    # Ctrl+Z inside a placing session goes through _remove_last, which takes
    # the balloon's node with it.
    from track_lab.operators.edit import DKR_OT_place_object  # noqa: PLC0415

    class Session:
        _placed = [balloon.name]
        report = staticmethod(lambda *_args: None)

    count = len(nodes())
    DKR_OT_place_object._remove_last(Session, bpy.context)
    check(len(nodes()) == count - 1, "taking the balloon back takes its node too")

    bpy.context.scene.dkr.auto_weapon_node = False
    place_balloon((0.0, -1150.0, 0.0))
    check(not ai_ops.weapon_nodes_of(bpy.context.active_object),
          "with Auto Weapon Node off, a balloon comes alone")
    bpy.context.scene.dkr.auto_weapon_node = True

    fresh("CHALLENGE", "EGGS")
    place_balloon((0.0, 0.0, 0.0))
    check(not nodes(), "an egg arena gets no weapon node: egg bots use no graph")
    fresh("RACE")
    place_balloon((0.0, 0.0, 0.0))
    check(not nodes(), "nor does a race")


def test_refresh_keeps_weapon_nodes():
    print("refresh artwork keeps a weapon node on its balloon")
    if prefs.resolve(bpy.context) is None:
        print("  skip: no decomp assets")
        return
    fresh()
    _result, balloon = place_balloon((300.0, 0.0, 0.0), preset="GREEN")
    bpy.ops.dkr.refresh_artwork()
    balloons = [o for o in scene.iter_dkr_objects(bpy.context) if ai_ops.is_balloon(o)]
    node = nodes()[0]
    check(len(balloons) == 1 and ai_ops.balloon_of(node) == balloons[0],
          "the node is parented to the rebuilt balloon")
    bpy.context.view_layer.update()
    gap = (node.matrix_world.translation - balloons[0].matrix_world.translation).length
    check(gap < 1e-3, "and still sits on it (%.4f)" % gap)
    check(node.name.startswith("Weapon Node · Green Balloon"), "keeping its name")


def test_link_and_unlink():
    print("linking two nodes by hand")
    fresh()
    circle_curve(1200.0)
    bpy.ops.dkr.ai_from_curve(spacing=300.0)
    graph = by_id()
    a, b = graph[0], graph[5]
    select(a)
    check(not bpy.ops.dkr.ai_link_nodes.poll(), "one node is not enough")
    select(a, b)
    check(bpy.ops.dkr.ai_link_nodes.poll(), "two unlinked nodes can be linked")
    check(not bpy.ops.dkr.ai_unlink_nodes.poll(), "and cannot be unlinked")
    check(bpy.ops.dkr.ai_link_nodes() == {"FINISHED"}, "Link Nodes runs")
    check(5 in linked_to(a) and 0 in linked_to(b), "each holds the other")
    check(not bpy.ops.dkr.ai_link_nodes.poll(), "linking them again is refused")
    check(reciprocal(), "every link is reciprocal")
    check(bpy.ops.dkr.ai_unlink_nodes() == {"FINISHED"}, "Unlink Nodes runs")
    check(5 not in linked_to(a) and 0 not in linked_to(b), "and removes both sides")
    check(slots(a)[:2] != [E, E] and slots(a)[-1] == E, "the slots stay packed")

    full = graph[8]
    full["adjacent"] = [7, 9, 1, 2]
    select(full, graph[12])
    check(not bpy.ops.dkr.ai_link_nodes.poll(), "a node with four neighbours is refused")


def test_link_weapon_nodes():
    print("Link Weapon Nodes, for nodes left loose")
    fresh()
    circle_curve(1200.0)
    bpy.ops.dkr.ai_from_curve(spacing=300.0)
    _result, balloon = place_balloon((1150.0, 0.0, 0.0))
    node = ai_ops.weapon_nodes_of(balloon)[0]
    for other in list(linked_to(node)):
        ai_ops.unlink(node, by_id()[other])
    rows = {row.rid: row for row in checklist_ops.checklist(bpy.context)}
    check(rows.get("weapon_links") is not None
          and rows["weapon_links"].status == mc.MISSING,
          "the checklist reports the loose weapon node")
    check(bpy.ops.dkr.ai_link_weapon_nodes.poll(), "and offers Link Weapon Nodes")
    check(bpy.ops.dkr.ai_link_weapon_nodes() == {"FINISHED"}, "which runs")
    check(len(linked_to(node)) == 2 and reciprocal(), "and links it both ways")
    check(not bpy.ops.dkr.ai_link_weapon_nodes.poll(), "then has nothing left to do")
    rows = {row.rid: row for row in checklist_ops.checklist(bpy.context)}
    check("weapon_links" not in rows and rows["weapon_reach"].ok,
          "and the checklist is satisfied")

    # A weapon node moved off its balloon is advice in the checklist.
    node.location.x += 400.0
    bpy.context.view_layer.update()
    rows = {row.rid: row for row in checklist_ops.checklist(bpy.context)}
    check(rows["weapon_reach"].status == mc.ADVISE,
          "a weapon node dragged off its balloon is flagged")


def test_tag_warning():
    print("Tag Destinations warns about a balloon far from every node")
    fresh()
    circle_curve(1200.0)
    bpy.ops.dkr.ai_from_curve(spacing=300.0)
    bpy.context.scene.dkr.auto_weapon_node = False
    place_balloon((0.0, 0.0, 0.0))
    reports = []
    try:
        result = bpy.ops.dkr.ai_tag_destinations()
    except RuntimeError as error:
        result = None
        reports.append(str(error))
    check(result == {"FINISHED"}, "it still tags the nearest node")
    check(any(int(n["unk8"]) == 1 for n in nodes()), "a node became class 1")


def test_overlay_geometry():
    print("the overlay's geometry, without a GPU")
    fresh()
    circle_curve(1200.0)
    bpy.ops.dkr.ai_from_curve(spacing=300.0)
    _result, balloon = place_balloon((1150.0, 0.0, 0.0))
    settings = bpy.context.scene.dkr
    settings.show_ai_graph = True
    settings.ai_graph_show_ids = True
    plain = ai_ops.graph_geometry(bpy.context)
    count = len(nodes())
    check(sum(len(p) for _s, p, _c in plain["points"]) >= count,
          "a dot for each of the %d nodes" % count)
    check(plain["reach"] and all(plain["reach"].values()),
          "the weapon node has its balloon in reach")
    check(len(plain["labels"]) == count, "and an id label each")
    start = by_id()[4]
    select(start)
    lit = ai_ops.graph_geometry(bpy.context)
    route = lit["route_ids"]
    weapon = int(ai_ops.weapon_nodes_of(balloon)[0]["nodeID"])
    check(route and route[0] == 4 and route[-1] == weapon,
          "from node 4 the route ends at the weapon node (%r)" % route)
    check(lit is not plain, "selecting a node rebuilt the geometry")
    check(ai_ops.graph_geometry(bpy.context) is lit, "and nothing changed: cached")
    ai_ops._draw_graph()  # no GPU context here: must not raise
    ai_ops._draw_graph_labels()
    check(True, "the draw handlers return quietly in the background")
    ok = bpy.types.DKR_PT_ai.poll(bpy.context)
    check(ok, "the AI Node Graph panel shows in Battle")


def main():
    track_lab.register()
    try:
        test_registration()
        test_weapon_node_before_the_graph()
        test_weapon_node_into_a_graph()
        test_refresh_keeps_weapon_nodes()
        test_link_and_unlink()
        test_link_weapon_nodes()
        test_tag_warning()
        test_overlay_geometry()
    finally:
        track_lab.unregister()
    print()
    if FAILURES:
        print("FAIL: %d check(s)" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: all AI graph checks")
    return 0


if __name__ == "__main__":
    try:
        _code = main()
    except BaseException:  # noqa: BLE001 - an uncaught error must not read as a pass
        traceback.print_exc()
        _code = 1
    sys.exit(_code)
