"""Exercise the Mode Checklist's buttons and the arena AI tools inside Blender.

The pure rules are checked by ``test_mode_checklist.py``; this proves the
buttons that act on them work in a scene: adding per-racer chests, flags and
Head-For Points where the start ring says each racer's go, tagging AI nodes
for weapons and banks, splicing a branch without flattening the graph, and a
hub exit that leads to another custom track reaching the package as a link.

    blender --background --python tools/blender/tests/test_blender_modes.py
"""

from __future__ import annotations

import json
import math
import os
import shutil
import struct
import sys
import tempfile
import traceback
import zlib

import bpy

_HERE = os.path.dirname(os.path.abspath(__file__))
for argument in sys.argv:
    if argument.endswith("test_blender_modes.py"):
        _HERE = os.path.dirname(os.path.abspath(argument))
        break

sys.path.insert(0, os.path.abspath(os.path.join(_HERE, "..")))
sys.path.insert(0, _HERE)

import track_lab  # noqa: E402
from track_lab import level_links, mode_checklist as mc, prefs, scene  # noqa: E402
from track_lab.operators import mode_checklist as checklist_ops  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def fresh(level_type, challenge="BATTLE"):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    settings = bpy.context.scene.dkr
    settings.level_type = level_type
    if level_type == "CHALLENGE":
        settings.challenge_type = challenge


def of_type(object_id):
    return [o for o in scene.iter_dkr_objects(bpy.context)
            if str(o.get(scene.PROP_ID)) == object_id]


def rows():
    return {row.rid: row for row in checklist_ops.checklist(bpy.context)}


def circle_curve(radius, points=16, name="line"):
    data = bpy.data.curves.new(name, type="CURVE")
    data.dimensions = "3D"
    spline = data.splines.new("POLY")
    spline.points.add(points - 1)
    for i, point in enumerate(spline.points):
        angle = 2.0 * math.pi * i / points
        point.co = (radius * math.cos(angle), radius * math.sin(angle), 0.0, 1.0)
    spline.use_cyclic_u = True
    curve = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(curve)
    bpy.context.view_layer.objects.active = curve
    return curve


def run(operator, **kwargs):
    """An operator's result, with the RuntimeError an ERROR report raises."""
    try:
        return operator(**kwargs)
    except RuntimeError as error:
        return {"ERROR: %s" % error}


# ---------------------------------------------------------------------------

def test_registration():
    print("registration")
    for name in ("add_racer_set", "set_exit_destination", "ai_tag_destinations"):
        check(hasattr(bpy.ops.dkr, name), "dkr.%s is registered" % name)
    check("DKR_PT_mode_checklist" in dir(bpy.types), "the Mode Checklist panel is registered")


def test_bananas_arena():
    print("a banana arena from the checklist's buttons")
    fresh("CHALLENGE", "BANANAS")
    check(not bpy.ops.dkr.add_racer_set.poll(),
          "Add Chests waits for the start ring, which says where each goes")
    check(rows()["chests"].status == mc.MISSING, "chests start missing")

    bpy.ops.dkr.generate_start_grid()
    check(rows()["start"].ok, "Build Start Ring fixes the start row")
    check(bpy.ops.dkr.add_racer_set(kind="CHESTS") == {"FINISHED"}, "Add Chests runs")
    chests = of_type(mc.CHEST)
    check(sorted(int(c["playerID"]) for c in chests) == [1, 2, 3, 4],
          "four chests, playerID 1-4")
    starts = checklist_ops.racer_starts(bpy.context)
    nearest_ok = True
    for chest in chests:
        racer = int(chest["playerID"]) - 1
        where = chest.matrix_world.translation
        closest = min(starts, key=lambda r: (starts[r].matrix_world.translation - where).length)
        nearest_ok &= closest == racer
    check(nearest_ok, "each chest is nearest its own racer's start")
    check(rows()["chests"].ok, "the chests row is met")
    check(bpy.ops.dkr.add_racer_set(kind="CHESTS") == {"CANCELLED"},
          "pressing it again adds nothing")

    bpy.data.objects.remove(chests[0], do_unlink=True)
    bpy.ops.dkr.add_racer_set(kind="CHESTS")
    check(len(of_type(mc.CHEST)) == 4, "a deleted chest is put back alone")

    bpy.ops.dkr.add_racer_set(kind="FLAGS")
    flags = of_type(mc.FLAG)
    check(sorted(int(f["playerIndex"]) for f in flags) == [0, 1, 2, 3],
          "four flags, playerIndex 0-3")
    over = True
    for flag in flags:
        chest = next(c for c in of_type(mc.CHEST)
                     if int(c["playerID"]) == int(flag["playerIndex"]) + 1)
        offset = flag.matrix_world.translation - chest.matrix_world.translation
        over &= abs(offset.x) < 1e-3 and abs(offset.y) < 1e-3 and offset.z > 100
    check(over, "each flag stands over its racer's chest")

    bpy.context.scene.cursor.location = (0.0, 0.0, 0.0)
    bpy.ops.dkr.place_object(object_id=mc.COIN_CREATOR)
    # On the diagonals, away from the chests, which win a node a balloon
    # also wants.
    for x in (-850.0, 850.0):
        bpy.context.scene.cursor.location = (x, x, 0.0)
        bpy.ops.dkr.place_object(object_id=mc.BALLOON, preset="RED")
    check(rows()["ai_graph"].status == mc.MISSING, "the AI graph is still missing")

    circle_curve(1200.0)
    check(bpy.ops.dkr.ai_from_curve(spacing=300.0) == {"FINISHED"},
          "AI Line From Curve samples the arena")
    check(rows()["ai_graph"].ok, "and meets the graph row")
    check(rows()["bank_nodes"].status == mc.MISSING, "bank nodes are not tagged yet")
    check(bpy.ops.dkr.ai_tag_destinations() == {"FINISHED"}, "Tag Destinations runs")
    classes = sorted(int(n["unk8"]) for n in of_type(mc.AINODE) if int(n["unk8"]))
    check(classes == [1, 1, 4, 5, 6, 7],
          "two weapon nodes and one bank node per racer (%r)" % classes)
    current = rows()
    check(all(row.ok or row.status == mc.NOTE for row in current.values()),
          "every row is met (%s)" % [r for r, row in current.items() if not row.ok])

    # A branch must not flatten the classes just set.
    branch = bpy.data.curves.new("branch", type="CURVE")
    branch.dimensions = "3D"
    spline = branch.splines.new("POLY")
    spline.points.add(2)
    for point, co in zip(spline.points, ((1200, 0, 0), (0, 300, 0), (-1200, 0, 0))):
        point.co = (co[0], co[1], co[2], 1.0)
    curve = bpy.data.objects.new("branch", branch)
    bpy.context.scene.collection.objects.link(curve)
    bpy.context.view_layer.objects.active = curve
    before = len(of_type(mc.AINODE))
    check(bpy.ops.dkr.ai_add_branch(spacing=300.0, unk8=0) == {"FINISHED"},
          "Add Branch splices a second curve on")
    after = of_type(mc.AINODE)
    kept = sorted(int(n["unk8"]) for n in after if int(n["unk8"]))
    check(len(after) > before and kept == classes,
          "and every node keeps its destination class (%r)" % kept)
    links = {int(n["nodeID"]): [a for a in n["adjacent"] if a != 255] for n in after}
    check(all(a in links.get(b, []) for a, near in links.items() for b in near),
          "every link is still reciprocal")

    result = run(bpy.ops.dkr.validate)
    errors = [r.message for r in bpy.context.scene.dkr.results if r.severity == "error"]
    check(not errors, "the finished arena validates without errors (%s)" % errors[:2])


def test_eggs_arena():
    print("an egg arena")
    fresh("CHALLENGE", "EGGS")
    bpy.ops.dkr.generate_start_grid()
    check(bpy.ops.dkr.add_racer_set(kind="HEAD_FOR") == {"FINISHED"},
          "Add Head-For Points runs")
    points = of_type(mc.HEAD_FOR)
    pairs = sorted((int(p["unk8"]), int(p["unk9"])) for p in points)
    check(pairs == sorted([(k, r) for k in (0, 2) for r in range(4)]),
          "one kind-0 and one kind-2 point per racer (%r)" % pairs)
    bpy.ops.dkr.add_racer_set(kind="FLAGS")
    flags_ok = True
    for flag in of_type(mc.FLAG):
        drop = next(p for p in points if int(p["unk8"]) == 0
                    and int(p["unk9"]) == int(flag["playerIndex"]))
        offset = flag.matrix_world.translation - drop.matrix_world.translation
        flags_ok &= abs(offset.x) < 1e-3 and abs(offset.y) < 1e-3
    check(flags_ok, "each flag stands over its racer's nest point")
    current = rows()
    check(current["nests"].status == mc.UNKNOWN,
          "with no geometry the nest floors are unknown")
    check(current["egg_creator"].status == mc.MISSING
          and current["balloons"].severity == mc.ERROR,
          "the egg creator and a balloon are still missing")
    run(bpy.ops.dkr.validate)
    errors = [r.message for r in bpy.context.scene.dkr.results if r.severity == "error"]
    check(any("Egg Creator" in m for m in errors), "validate names the egg creator")
    check(not bpy.types.DKR_PT_ai.poll(bpy.context),
          "the AI Node Graph panel stays out of an egg arena")


def test_ai_panel_visibility():
    print("where the AI Node Graph panel shows")
    for level_type, challenge, shown in (("CHALLENGE", "BATTLE", True),
                                         ("CHALLENGE", "BANANAS", True),
                                         ("HUB", "BATTLE", True),
                                         ("RACE", "BATTLE", False),
                                         ("BOSS", "BATTLE", False)):
        fresh(level_type, challenge)
        check(bool(bpy.types.DKR_PT_ai.poll(bpy.context)) == shown,
              "%s %s" % ("shown in" if shown else "hidden in",
                         challenge if level_type == "CHALLENGE" else level_type))
    fresh("RACE")
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_MODECHANGE", preset="LOOP_ENTRY")
    check(bpy.types.DKR_PT_ai.poll(bpy.context),
          "shown in a race once it has a loop, the one thing there that reads it")


def test_hub_exit_link():
    print("a hub exit to another custom track")
    fresh("HUB")
    bpy.ops.dkr.generate_start_grid()
    bpy.context.scene.cursor.location = (500.0, 0.0, 0.0)
    bpy.ops.dkr.place_object(object_id=level_links.EXIT)
    exit_object = of_type(level_links.EXIT)[0]
    bpy.context.view_layer.objects.active = exit_object
    check(rows()["destinations"].status == mc.MISSING, "a new exit leads nowhere")
    check(checklist_ops.describe_destination(exit_object) == "",
          "and the panel says so")

    result = run(bpy.ops.dkr.set_exit_destination, kind="CUSTOM", track="Frosty Ridge")
    check(result != {"FINISHED"}, "a track name that is not an id is refused")
    check(bpy.ops.dkr.set_exit_destination(kind="CUSTOM", track="frosty-ridge")
          == {"FINISHED"}, "a custom track id is accepted")
    check(scene.destination_track(exit_object) == "frosty-ridge", "and stored on the exit")
    check(rows()["destinations"].ok, "the destination row is met")
    exported = scene.read_object(exit_object, __import__(
        "track_lab.catalog", fromlist=["load"]).load())
    check(exported.fields.get(level_links.TRACK_FIELD) == "frosty-ridge"
          and exported.fields.get("destinationMapId") == level_links.PLACEHOLDER_LEVEL,
          "the exported exit names the track and holds the placeholder")

    if prefs.resolve(bpy.context) is not None:
        settings = bpy.context.scene.dkr
        settings.track_id = "test-hub"
        temporary = tempfile.mkdtemp(prefix="dkr-hub-")
        try:
            target = os.path.join(temporary, "test-hub.dkrmap")
            result = run(bpy.ops.dkr.export_dkrmap, filepath=target, validate_first=True)
            check(result == {"FINISHED"}, "the hub exports (%s)" % result)
            with open(os.path.join(target, "manifest.json"), encoding="utf-8") as handle:
                manifest = json.load(handle)
            links = manifest.get("levelLinks", [])
            check(len(links) == 1 and links[0]["track"] == "frosty-ridge",
                  "the manifest carries the link (%r)" % links)
            if links:
                slot = links[0]["slot"]
                with open(os.path.join(target, "objects_%s.bin" % slot), "rb") as handle:
                    packed = handle.read()
                blob = zlib.decompress(packed[5:], -15)
                offset = links[0]["offset"]
                check(blob[offset] == 0, "byte %d of the inflated %s map is the "
                      "placeholder level 0" % (offset, slot))
                entries = struct.unpack(">I", blob[:4])[0]
                check(16 <= offset < 16 + entries, "inside the map's entries")
            source = os.path.join(target, "source", "objects_%s.gltf"
                                  % scene.slot_of(exit_object))
            with open(source, encoding="utf-8") as handle:
                document = json.load(handle)
            carried = [n["extras"] for n in document["nodes"][1:]
                       if n["extras"].get("id") == level_links.EXIT]
            check(carried and carried[0].get(level_links.TRACK_FIELD) == "frosty-ridge",
                  "the source glTF keeps the destination track")

            # Back into a scene: the link survives an import, which replaces
            # the scene's objects.
            bpy.ops.dkr.import_object_map(filepath=source)
            check(any(scene.destination_track(o) == "frosty-ridge"
                      for o in of_type(level_links.EXIT)),
                  "an imported exit comes back pointing at the track")
        finally:
            shutil.rmtree(temporary, ignore_errors=True)
    else:
        print("  skip: no asset tree, so no package to compile")

    exit_object = of_type(level_links.EXIT)[0]
    bpy.context.view_layer.objects.active = exit_object
    check(bpy.ops.dkr.set_exit_destination(kind="RETAIL", level="ASSET_LEVEL_FOSSILCANYON")
          == {"FINISHED"}, "switching back to a retail level works")
    check(not scene.destination_track(exit_object)
          and exit_object["destinationMapId"] == "ASSET_LEVEL_FOSSILCANYON",
          "and drops the link")


def main():
    track_lab.register()
    try:
        test_registration()
        test_bananas_arena()
        test_eggs_arena()
        test_ai_panel_visibility()
        test_hub_exit_link()
    finally:
        track_lab.unregister()
    print()
    if FAILURES:
        print("FAIL: %d check(s)" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: all mode checks")
    return 0


if __name__ == "__main__":
    try:
        _code = main()
    except BaseException:  # noqa: BLE001 - an uncaught error must not read as a pass
        traceback.print_exc()
        _code = 1
    sys.exit(_code)
