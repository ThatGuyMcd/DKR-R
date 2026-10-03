"""Exercise the addon's operators inside Blender.

The round-trip tests prove the data survives. This proves the buttons work:
registration, placing an object, sampling a curve into an AI line, validation
and writing a ``.dkrmap``. It is a smoke test - it checks that each operator
runs and leaves the scene in the state it claims, not that the UI looks right.

    blender --background --python tools/blender/tests/test_blender_operators.py
"""

from __future__ import annotations

import json
import math
import os
import shutil
import struct
import sys
import traceback
import tempfile
import types

import bmesh
import bpy

_HERE = os.path.dirname(os.path.abspath(__file__))
for argument in sys.argv:
    if argument.endswith("test_blender_operators.py"):
        _HERE = os.path.dirname(os.path.abspath(argument))
        break

sys.path.insert(0, os.path.abspath(os.path.join(_HERE, "..")))
sys.path.insert(0, _HERE)

import dkr_track_editor  # noqa: E402
from dkr_track_editor import ai_graph, catalog as catalog_module, gltf_io, scene  # noqa: E402

from test_roundtrip import find_object_maps  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def fresh(level_type="RACE"):
    """A factory-default scene, with a Level Type chosen unless told otherwise.

    Chosen by default because the export refuses a track that has not said what
    it is, and most of these tests are about something else.
    """
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if level_type:
        bpy.context.scene.dkr.level_type = level_type


# ---------------------------------------------------------------------------

def test_registration():
    print("registration")
    for name in ("import_object_map", "export_object_map", "place_object",
                 "ai_from_curve", "ai_add_branch", "validate", "export_dkrmap",
                 "set_enum_field", "reset_field", "select_by_type",
                 "import_geometry", "drop_to_surface", "toggle_walls",
                 "edit_geometry", "check_geometry",
                 "header_defaults", "set_header_choice", "clear_header_choice",
                 "set_surface_type", "resegment", "track_from_mesh",
                 "track_from_mesh_blank", "pick_texture", "apply_texture",
                 "clear_texture", "sync_uvs", "select_by_texture",
                 "refresh_artwork", "set_slot",
                 "set_level_type", "use_imported_level_type", "toggle_vehicle",
                 "set_default_vehicle", "generate_start_grid",
                 "select_grid_children", "step_music", "play_music",
                 "pick_skybox", "show_skybox", "minimap_draw_edge",
                 "minimap_from_ai", "minimap_close_edges", "minimap_clear",
                 "minimap_save_png", "minimap_use_png", "minimap_forget_png",
                 "make_convertible", "ai_copy_difficulty",
                 "set_face_transparency", "set_texture_transparency",
                 "restore_custom_textures",
                 "add_water", "select_water", "remove_water", "wave_preset"):
        check(hasattr(bpy.ops.dkr, name), "operator dkr.%s exists" % name)
    check(hasattr(bpy.types.Scene, "dkr"), "scene settings registered")


def test_place():
    print("placing objects")
    fresh()
    bpy.context.scene.cursor.location = (100.0, 200.0, 300.0)
    result = bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")
    check(result == {"FINISHED"}, "place returns FINISHED")

    placed = scene.iter_dkr_objects(bpy.context)
    check(len(placed) == 1, "one object in the scene")
    if not placed:
        return
    empty = placed[0]
    check(str(empty[scene.PROP_ID]) == "ASSET_OBJECT_GROUNDZIPPER", "carries its type")
    check("scale" in empty and "angleY" in empty, "carries its fields")

    catalog = catalog_module.load()
    exported = scene.export_object_map(bpy.context, catalog)
    check(len(exported.objects) == 1, "exports one object")
    obj = exported.objects[0]
    # The cursor was set in Blender space; it must come back as map space.
    check(obj.translation == [100.0, 300.0, -200.0],
          "position converts Z-up to Y-up (got %r)" % (obj.translation,))
    check(isinstance(obj.fields["scale"], int), "int field stays an int")
    check(isinstance(obj.fields["angleY"], float), "angle field stays a float")

    # Blender turns an operator's ERROR report into an exception when it is
    # called from Python, so refusal shows up as a RuntimeError rather than a
    # CANCELLED return.
    try:
        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_NOT_REAL")
        check(False, "unknown type is refused")
    except RuntimeError as error:
        check("unknown object type" in str(error), "unknown type is refused")
    check(len(scene.iter_dkr_objects(bpy.context)) == 1,
          "the refused object was not added")


def test_ai_from_curve():
    print("AI line from a curve")
    fresh()

    curve_data = bpy.data.curves.new("racing-line", type="CURVE")
    curve_data.dimensions = "3D"
    spline = curve_data.splines.new("POLY")
    corners = [(-1000, -1000, 0), (1000, -1000, 0), (1000, 1000, 0), (-1000, 1000, 0)]
    spline.points.add(len(corners) - 1)
    for point, corner in zip(spline.points, corners):
        point.co = (corner[0], corner[1], corner[2], 1.0)
    spline.use_cyclic_u = True
    curve_object = bpy.data.objects.new("racing-line", curve_data)
    bpy.context.scene.collection.objects.link(curve_object)
    bpy.context.view_layer.objects.active = curve_object

    result = bpy.ops.dkr.ai_from_curve(spacing=400.0, closed="AUTO")
    check(result == {"FINISHED"}, "ai_from_curve returns FINISHED")

    nodes = [
        o for o in scene.iter_dkr_objects(bpy.context)
        if str(o[scene.PROP_ID]) == "ASSET_OBJECT_AINODE"
    ]
    check(len(nodes) >= 4, "sampled %d nodes from an 8000-unit lap" % len(nodes))

    ids = sorted(int(n["nodeID"]) for n in nodes)
    check(ids == list(range(len(nodes))), "node ids are 0..n-1 with no gaps")

    links = {int(n["nodeID"]): [a for a in n["adjacent"] if a != 255] for n in nodes}
    check(all(len(v) <= 4 for v in links.values()), "no node exceeds four neighbours")
    check(
        all(node in links.get(other, []) for node, nbrs in links.items() for other in nbrs),
        "every link is reciprocal, as retail data is",
    )
    check(all(len(v) == 2 for v in links.values()),
          "a closed loop gives every node exactly two neighbours")

    # The graph must survive a round trip through the file format.
    catalog = catalog_module.load()
    exported = scene.export_object_map(bpy.context, catalog)
    rebuilt = gltf_io.parse(json.loads(gltf_io.dumps(exported)))
    check(len(rebuilt.by_id("ASSET_OBJECT_AINODE")) == len(nodes),
          "AI nodes survive a write and read")
    adjacency = rebuilt.by_id("ASSET_OBJECT_AINODE")[0].fields["adjacent"]
    check(isinstance(adjacency, list) and len(adjacency) == 4,
          "adjacent stays a four-element list")


def test_ai_limits():
    print("AI graph limits")
    line = [(float(i) * 10.0, 0.0, 0.0) for i in range(600)]
    try:
        ai_graph.build_from_path(line, 1.0, closed=False)
        check(False, "over-long line is refused")
    except ai_graph.AiGraphError as error:
        check(str(ai_graph.MAX_NODES) in str(error),
              "over-long line is refused with the real limit, which is the %d "
              "the game keeps rather than the 255 a u8 could hold"
              % ai_graph.MAX_NODES)

    graph = ai_graph.AiGraph()
    hub = graph.add((0.0, 0.0, 0.0))
    spokes = [graph.add((float(i + 1), 0.0, 0.0)) for i in range(5)]
    for spoke in spokes[:4]:
        graph.link(hub, spoke)
    try:
        graph.link(hub, spokes[4])
        check(False, "a fifth neighbour is refused")
    except ai_graph.AiGraphError:
        check(True, "a fifth neighbour is refused")


def run_validate():
    """Call the operator, tolerating the exception Blender raises on ERROR."""
    try:
        bpy.ops.dkr.validate()
    except RuntimeError:
        pass
    return list(bpy.context.scene.dkr.results)


def test_validation():
    print("validation")
    fresh(level_type=None)
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")
    errors = [r for r in run_validate() if r.severity == "error"]
    check(len(errors) == 1 and "level type" in errors[0].message,
          "without a level type, choosing one is the only error")

    bpy.ops.dkr.set_level_type(mode="RACE")
    results = run_validate()
    check(bool(results), "validate fills in the results list")
    check(any(r.severity == "error" and "start positions" in r.message
              for r in results),
          "a race with no start line is an error")

    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
    check(any(r.severity == "error" and "racerIndex 1" in r.message
              for r in run_validate()),
          "one start position is not a race grid: racers 1-7 would start at "
          "the map origin")

    bpy.ops.dkr.generate_start_grid()
    errors = [r for r in run_validate() if r.severity == "error"]
    check(not errors, "a generated grid clears it (%s)"
          % [e.message for e in errors])

    # A cutscene spawns nobody, so it is not required to have a start line.
    fresh(level_type=None)
    bpy.ops.dkr.set_level_type(mode="SPECIAL", sub="CUTSCENE")
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")
    errors = [r for r in run_validate() if r.severity == "error"]
    check(not errors, "a cutscene is not required to have a start line")

    # An object the level type does not use is a warning, never an error.
    fresh(level_type="HUB")
    bpy.ops.dkr.generate_start_grid()
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")
    results = run_validate()
    check(any(r.severity == "warning" and "not used in a Hub" in r.message
              for r in results),
          "a zipper in a hub is flagged as unused there")
    check(not [r for r in results if r.severity == "error"],
          "and only as a warning")


def test_level_type_flow():
    print("level type")
    from dkr_track_editor import level_types
    from dkr_track_editor.ui import panels

    fresh(level_type=None)
    settings = bpy.context.scene.dkr
    check(settings.level_type == "NONE", "a new scene has no level type")
    check(not panels.DKR_PT_track.poll(bpy.context)
          and not panels.DKR_PT_export.poll(bpy.context),
          "only the Level Type card shows before a choice")

    bpy.ops.dkr.set_level_type(mode="BOSS")
    check(panels.DKR_PT_track.poll(bpy.context), "the rest appears once chosen")
    settings.boss = "BOSS_RACE_BLUEY1"
    overrides = level_types.settings_overrides(settings)
    check(overrides.get("/race-type") == "RACETYPE_BOSS"
          and overrides.get("/boss-race-id") == "BOSS_RACE_BLUEY1",
          "a boss race writes its race type and boss to the header")
    check(overrides.get("/avaliable-vehicles") == ["VEHICLE_HOVERCRAFT"],
          "and is raced in the boss's vehicle")

    bpy.ops.dkr.set_level_type(mode="CHALLENGE", sub="EGGS")
    check(level_types.current_key(settings) == "EGGS", "the challenge kind is kept")
    bpy.ops.dkr.toggle_vehicle(vehicle="VEHICLE_PLANE")
    check(set(settings.vehicles) == {"VEHICLE_PLANE"},
          "a challenge allows exactly one vehicle")

    bpy.ops.dkr.set_level_type(mode="RACE")
    bpy.ops.dkr.toggle_vehicle(vehicle="VEHICLE_CAR")
    check(set(settings.vehicles) == {"VEHICLE_PLANE", "VEHICLE_CAR"},
          "a race allows several")
    bpy.ops.dkr.toggle_vehicle(vehicle="VEHICLE_CAR")
    bpy.ops.dkr.toggle_vehicle(vehicle="VEHICLE_PLANE")
    check(set(settings.vehicles) == {"VEHICLE_PLANE"},
          "the last vehicle cannot be removed")


def _grid_children(root):
    return [c for c in root.children if scene.is_dkr_object(c)]


def test_start_grid():
    print("start grid")
    for mode, sub, count in (("RACE", "", 8), ("BOSS", "", 2),
                             ("CHALLENGE", "BATTLE", 4), ("HUB", "", 1)):
        fresh(level_type=None)
        bpy.ops.dkr.set_level_type(mode=mode, sub=sub)
        bpy.ops.dkr.generate_start_grid()
        roots = scene.grid_roots(bpy.context)
        check(len(roots) == 1, "%s: one grid root" % mode)
        if not roots:
            continue
        children = _grid_children(roots[0])
        check(len(children) == count, "%s: %d start positions (got %d)"
              % (mode, count, len(children)))
        check(sorted(int(c["racerIndex"]) for c in children) == list(range(count)),
              "%s: racerIndex 0-%d filled in" % (mode, count - 1))

    fresh(level_type=None)
    bpy.ops.dkr.set_level_type(mode="SPECIAL", sub="CUTSCENE")
    check(not bpy.ops.dkr.generate_start_grid.poll(),
          "a cutscene spawns nobody, so it offers no start grid")

    # The export reads each start position's world position and angle, so
    # turning the root turns the grid.
    fresh()
    bpy.context.scene.cursor.location = (1000.0, 0.0, 0.0)
    bpy.ops.dkr.generate_start_grid()
    root = scene.grid_roots(bpy.context)[0]
    catalog = catalog_module.load()

    def exported():
        return {o.fields["racerIndex"]: o
                for o in scene.export_object_map(bpy.context, catalog).objects}

    before = exported()
    root.rotation_euler.z = math.radians(90.0)
    after = exported()
    check(all(abs(after[i].fields["angleY"] - before[i].fields["angleY"] - 90.0) < 1e-6
              for i in range(8)),
          "turning the root 90 degrees adds 90 to every angleY")

    def reach(obj):
        x, _y, z = obj.translation
        return math.hypot(x - 1000.0, z)

    check(after[0].translation != before[0].translation
          and abs(reach(after[0]) - reach(before[0])) < 1e-3,
          "and swings every position around the root")

    root.location = (500.0, 500.0, 0.0)
    bpy.ops.dkr.set_level_type(mode="BOSS")
    roots = scene.grid_roots(bpy.context)
    check(len(roots) == 1 and len(_grid_children(roots[0])) == 2,
          "changing to a boss race rebuilds the grid with two")
    check(tuple(roots[0].location)[:2] == (500.0, 500.0)
          and roots[0].get(scene.PROP_GRID_KEY) == "BOSS",
          "where the old one stood")

    fresh(level_type="HUB")
    bpy.ops.dkr.generate_start_grid()
    bpy.ops.dkr.generate_start_grid()
    entrances = sorted(int(r[scene.PROP_GRID_ENTRANCE])
                       for r in scene.grid_roots(bpy.context))
    check(entrances == [0, 1], "a hub adds one entrance at a time (%r)" % entrances)


def test_presets_and_tooltips():
    print("balloon presets and tooltips")
    from dkr_track_editor.operators.edit import DKR_OT_place_object

    fresh()
    catalog = catalog_module.load()
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_WEAPONBALLOON", preset="GREEN")
    green = bpy.context.active_object
    check(green.get("balloonType") == "BALLOON_TYPE_TRAP", "the green balloon is a trap")
    check(green.get(scene.PROP_NODE_NAME)
          == catalog.get("ASSET_OBJECT_WEAPONBALLOON").node_name,
          "the exported node name stays the type's own")
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_WEAPONBALLOON", preset="RAINBOW")
    check(bpy.context.active_object.get("balloonType") == "BALLOON_TYPE_MAGNET",
          "the coloured balloon is the rainbow magnet")

    eggs = DKR_OT_place_object.description(
        bpy.context, types.SimpleNamespace(object_id="ASSET_OBJECT_EGGCREATOR", preset=""))
    zipper = DKR_OT_place_object.description(
        bpy.context, types.SimpleNamespace(object_id="ASSET_OBJECT_GROUNDZIPPER", preset=""))
    check(eggs != zipper, "each Place button has its own tooltip")
    check("Not used in a Race level" in eggs,
          "and says when the level type does not use it")

    # A panel draws in a region the background test never has, so an icon
    # Blender does not know would only surface as a traceback in the sidebar.
    from dkr_track_editor import level_types
    icons = {item.identifier for item in
             bpy.types.UILayout.bl_rna.functions["label"].parameters["icon"].enum_items}
    unknown = [p.icon for p in level_types.PRESETS if p.icon not in icons]
    check(not unknown, "every balloon preset's icon exists in this Blender (%r)" % unknown)

    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
    check(int(bpy.context.active_object["racerIndex"]) == 1,
          "a hand-placed start position takes the next free racerIndex")


def test_refresh_keeps_grids():
    print("refresh artwork keeps objects and grids")
    from dkr_track_editor import prefs

    fresh()
    bpy.ops.dkr.generate_start_grid()
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")
    before = len(scene.iter_dkr_objects(bpy.context))
    if prefs.resolve(bpy.context) is None:
        print("  skip: no decomp assets")
        return
    bpy.ops.dkr.refresh_artwork()
    after = scene.iter_dkr_objects(bpy.context)
    check(len(after) == before,
          "every object survives a refresh (%d of %d)" % (len(after), before))
    root = scene.grid_roots(bpy.context)[0]
    check(sum(1 for o in after if o.parent == root) == 8,
          "and the start positions stay in their grid")


def test_import_sets_level_type():
    print("importing a retail track sets its level type")
    from dkr_track_editor import level_types, prefs

    fresh(level_type=None)
    tree = prefs.resolve(bpy.context)
    # By label, since an extraction may suffix the name with its revision.
    names = {level.label: level.name for level in tree.levels()} if tree else {}
    if "Bluey1" not in names or "Horseshoe Gulch" not in names:
        print("  skip: no decomp assets")
        return
    settings = bpy.context.scene.dkr
    bpy.ops.dkr.import_level(level=names["Bluey1"], with_geometry=False)
    check(level_types.current_key(settings) == "BOSS"
          and settings.boss == "BOSS_RACE_BLUEY1",
          "Bluey 1 comes in as a boss race against Bluey")
    from dkr_track_editor.operators import header as header_ops
    check(bpy.context.scene.get(header_ops.key_for(header_ops.SKYBOX)) == "ASSET_OBJECT_DOME1"
          and bpy.context.scene.get(header_ops.key_for(header_ops.MUSIC)) == 57,
          "and brings its sky and music, which a remix can change")
    bpy.ops.dkr.import_level(level=names["Horseshoe Gulch"], with_geometry=False)
    check(level_types.current_key(settings) == "TEST_RACE",
          "Horseshoe Gulch comes in as Special > Test Race")


def test_race_ai():
    print("race AI: the bots' line and the header's AI bytes")
    from dkr_track_editor import level_header, level_header_template as template
    from dkr_track_editor import prefs, race_ai
    from dkr_track_editor.operators import header as header_ops
    from dkr_track_editor.operators import race_ai as race_ai_ops

    fresh()
    context = bpy.context
    check(race_ai_ops.is_drawing(), "the bot lines' draw handler is registered")

    catalog = catalog_module.load()
    object_type = catalog.get(race_ai.CHECKPOINT)
    root = scene.ensure_root(context)
    corners = [(0.0, 0.0), (2000.0, 0.0), (2000.0, 2000.0), (0.0, 2000.0)]
    for number, (x, z) in enumerate(corners):
        fields = object_type.fresh_fields()
        fields.update(index=number * 2, vehicleType=0, isAltCheckpoint=0)
        placed = gltf_io.MapObject(object_id=race_ai.CHECKPOINT, name="Checkpoint",
                                   translation=[x, 0.0, z], fields=fields)
        scene.create_empty(context, placed, catalog, root)["dkr_order"] = number

    route, _objects = race_ai_ops.read_route(context)
    check(len(route.main) == 4, "four placed checkpoints make a four-gate route (%d)"
          % len(route.main))
    found = race_ai_ops.geometry(route)
    check(all(found["lanes"]) and all(len(p) == 4 for p in found["points"]),
          "every lane has a line and a point on every gate")

    first = race_ai_ops.checkpoint_empties(context)[0]
    before = race_ai_ops.geometry(race_ai_ops.read_route(context)[0])["points"][0][0]
    first.rotation_euler.z = math.radians(90.0)
    after = race_ai_ops.geometry(race_ai_ops.read_route(context)[0])["points"][0][0]
    check(before != after, "turning a checkpoint in the viewport turns its lanes")

    context.scene.dkr.show_ai_lines = True
    try:
        race_ai_ops._draw()  # no GPU in the background: it must not raise
        survived = True
    except Exception:  # noqa: BLE001
        survived = False
    check(survived, "the draw handler never raises, GPU or not")

    ai = context.scene.dkr_ai
    check(ai.skill_0 == 2 and ai.adv1_0 == 0 and not header_ops.overrides(context),
          "unanswered, the panel shows the survey's defaults and writes nothing")
    ai.adv1_1 = 4
    ai.skill_3 = 0
    ai.trophy_9 = 4
    ai.set_2 = 1
    answers = header_ops.overrides(context)
    check(answers.get("/ai-levels/adv1/silver-coins") == 4
          and answers.get("/unknown/unkC/3") == 0
          and answers.get("/unknown/unk16/9") == 4
          and answers.get("/unknown/unk4F/2") == 1,
          "an edit in the panel is a header answer")
    document = template.document(header_ops.effective_overrides(context))
    payload = level_header.encode(document, catalog.raw.get("enumValues", {}))
    check(payload[0x21] == 4 and payload[0x0C + 3] == 0
          and payload[0x16 + 9] == 4 and payload[0x4F + 2] == 1,
          "and lands on the bytes the game reads")
    context.scene.dkr.ai_line_vehicle = "VEHICLE_PLANE"
    check(not race_ai_ops.read_route(context)[0].main,
          "the plane now loads set 1, which this scene leaves empty")

    tree = prefs.resolve(context)
    names = {level.label: level.name for level in tree.levels()} if tree else {}
    if "Ancient Lake" not in names:
        print("  skip: no decomp assets")
        return
    bpy.ops.dkr.import_level(level=names["Ancient Lake"], with_geometry=False)
    ai = bpy.context.scene.dkr_ai
    check(ai.skill_2 == 1 and ai.adv1_4 == 6 and ai.set_2 == 1,
          "importing Ancient Lake brings its difficulty and its plane's set")
    check(header_ops.inherited_overrides(bpy.context).get("/unknown/unkC/2") == 1,
          "and a remix lays it back over the inherited header")
    bpy.context.scene.dkr.ai_line_vehicle = "VEHICLE_CAR"
    lake = race_ai_ops.read_route(bpy.context)[0]
    check(len(lake.main) > 10 and not lake.duplicates,
          "Ancient Lake's car route loads (%d gates)" % len(lake.main))

    fresh()
    bpy.ops.dkr.ai_copy_difficulty(level=names["Ancient Lake"])
    copied = header_ops.overrides(bpy.context)
    check(bpy.context.scene.dkr_ai.adv2_4 == 7 and "/unknown/unk4F/2" not in copied,
          "Copy Difficulty takes the levels and skills, not the checkpoint sets")


def test_skybox():
    print("skybox")
    from dkr_track_editor import prefs, skyboxes
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import header as header_ops
    from dkr_track_editor.operators import skybox as skybox_ops

    fresh()
    tree = prefs.resolve(bpy.context)
    if tree is None:
        print("  skip: no decomp assets")
        return
    domes = skyboxes.catalogue(tree)
    check(len(domes) == 18, "the gallery offers the 18 domes (%d)" % len(domes))

    key = header_ops.key_for(header_ops.SKYBOX)
    check(not bpy.ops.dkr.show_skybox.poll(), "nothing to show before a sky is picked")
    bpy.ops.dkr.pick_skybox(asset_id="ASSET_OBJECT_DOME13")
    check(bpy.context.scene.get(key) == "ASSET_OBJECT_DOME13",
          "picking a dome answers the header's skybox")

    bpy.ops.dkr.generate_start_grid()
    bpy.ops.dkr.show_skybox()
    sky = skybox_ops.preview_object(bpy.context)
    check(sky is not None and sky.type == "MESH" and len(sky.data.polygons) > 0,
          "the chosen dome is shown in the viewport")
    if sky is None:
        return
    check(sky not in geometry_ops.unusable_meshes(bpy.context)
          and not scene.is_dkr_object(sky),
          "and is never taken for track geometry or exported")

    # Every retail 3D texture's PNG was turned right way up on extraction, and
    # a model's UVs count from the ROM's first row, so the preview draws a copy
    # turned back: its bottom row is the PNG's top row.
    from dkr_track_editor import preview
    rom = [node.image for material in sky.data.materials
           if material is not None and material.node_tree
           for node in material.node_tree.nodes
           if node.type == "TEX_IMAGE" and node.image is not None
           and node.image.name.endswith(preview.ROM_ROWS_SUFFIX)]
    check(bool(rom), "the dome is drawn with its pictures in the ROM's row order")
    if rom:
        copy = rom[0]
        source = bpy.data.images.get(copy.name[:-len(preview.ROM_ROWS_SUFFIX)])
        width, height = copy.size
        top = list(source.pixels[(height - 1) * width * 4:(height - 1) * width * 4 + 4])
        bottom = list(copy.pixels[0:4])
        check(max(abs(a - b) for a, b in zip(top, bottom)) < 1.5 / 255.0,
              "the copy's bottom row is the picture's top row")

    bpy.ops.dkr.pick_skybox(asset_id="ASSET_OBJECT_DOME8")
    sky = skybox_ops.preview_object(bpy.context)
    check(sky is not None and sky.get(skybox_ops.PROP_SKY) == "ASSET_OBJECT_DOME8",
          "picking another dome swaps the preview")

    bpy.ops.dkr.pick_skybox(asset_id="ASSET_OBJECT_DOME8")
    check(key not in bpy.context.scene and skybox_ops.preview_object(bpy.context) is None,
          "picking it again means no skybox, and the preview goes with it")

    icon = skybox_ops.build_icon(tree, domes[0])
    check(isinstance(icon, int), "a thumbnail is made from the dome")


def test_dkrmap_export():
    print("dkrmap package")
    fresh()
    bpy.ops.dkr.generate_start_grid()
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")

    settings = bpy.context.scene.dkr
    settings.track_name = "Ancient Lake Remix"
    settings.track_id = "ancient-lake-remix"
    settings.track_author = "test"

    temporary = tempfile.mkdtemp(prefix="dkr-track-")
    try:
        target = os.path.join(temporary, "ancient-lake-remix.dkrmap")
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=True)
        check(result == {"FINISHED"}, "export_dkrmap returns FINISHED")
        check(os.path.isdir(target), "package directory created")

        manifest_path = os.path.join(target, "manifest.json")
        check(os.path.isfile(manifest_path), "manifest written")
        if os.path.isfile(manifest_path):
            with open(manifest_path, "r", encoding="utf-8") as handle:
                manifest = json.load(handle)
            check(manifest["id"] == "ancient-lake-remix", "manifest carries the id")
            check(manifest["schemaVersion"] == 1, "manifest declares its schema")

            # The manifest must claim exactly what is on disk: a promised
            # payload that is not there fails at load time, and one that is
            # there but unclaimed is silently ignored.
            claimed = {entry["file"] for entry in manifest["adds"]}
            present = {
                name for name in os.listdir(target)
                if name.endswith(".bin")
            }
            check(claimed == present,
                  "manifest claims exactly the payloads present "
                  "(claims %s, has %s)" % (sorted(claimed), sorted(present)))

            from dkr_track_editor import prefs
            if prefs.resolve(bpy.context) is not None:
                check("objects_structure.bin" in present,
                      "the structure map was compiled into the package")
                # A level has two object maps and the runtime patches a
                # different header field from each, so an entry without a slot
                # is refused rather than guessed.
                maps = [e for e in manifest["adds"]
                        if e["section"] == "LEVEL_OBJECT_MAPS"]
                check(maps, "the manifest names the object-map section")
                check(all("slot" in e for e in maps),
                      "every object-map entry carries its slot")
                check(all(e["slot"] in ("structure", "collectables")
                          for e in maps),
                      "slots are named as the runtime expects")

        source = os.path.join(target, "source", "objects_structure.gltf")
        check(os.path.isfile(source), "asset-tool source glTF written per slot")
        if os.path.isfile(source):
            written = gltf_io.load(source)
            check(len(written.objects) == 9,
                  "the grid and the zipper went to the structure map by default")
        check(os.path.isfile(os.path.join(target, "source",
                                          "objects_structure.json")),
              "sidecar written")
        check(os.path.isfile(os.path.join(target, "HOW-TO-BUILD.md")),
              "build instructions written")

        # A payload the addon cannot produce must survive a re-export, or the
        # instruction to drop one in is a trap.
        with open(os.path.join(target, "header.bin"), "wb") as handle:
            handle.write(b"\x00" * 200)
        bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(os.path.isfile(os.path.join(target, "header.bin")),
              "a supplied header.bin survives re-export")
        with open(manifest_path, "r", encoding="utf-8") as handle:
            again = json.load(handle)
        check(any(e["file"] == "header.bin" for e in again["adds"]),
              "and the manifest picks it up")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def test_import_export_operators():
    print("import and export operators")
    paths = find_object_maps()
    if not paths:
        print("  skip: no extracted object maps")
        return
    source = paths[len(paths) // 2]

    fresh()
    result = bpy.ops.dkr.import_object_map(filepath=source, frame_view=False)
    check(result == {"FINISHED"}, "import returns FINISHED")
    imported = len(scene.iter_dkr_objects(bpy.context))
    check(imported == len(gltf_io.load(source).objects), "every object imported")
    check(bpy.context.scene.dkr.source_path == source, "source path remembered")

    temporary = tempfile.mkdtemp(prefix="dkr-io-")
    try:
        target = os.path.join(temporary, "objects.gltf")
        result = bpy.ops.dkr.export_object_map(filepath=target, write_sidecar=True)
        check(result == {"FINISHED"}, "export returns FINISHED")
        with open(source, "rb") as a, open(target, "rb") as b:
            check(a.read() == b.read(), "exported file matches the imported one byte for byte")
        check(os.path.isfile(os.path.join(temporary, "objects.json")), "sidecar written")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def test_place_shows_artwork():
    """The plain flow: fresh scene, press a button, see the object.

    This is what the addon is for. It has to work with no import beforehand and
    no path configured, which means the asset tree must be discovered rather
    than only remembered from a previous import.
    """
    print("artwork on a freshly placed object")
    from dkr_track_editor import prefs

    fresh()
    tree = prefs.resolve(bpy.context)
    if tree is None:
        print("  skip: no extracted decomp assets to draw from")
        return
    check(True, "asset tree discovered without configuration (%s)" % tree.label)

    expectations = [
        ("ASSET_OBJECT_COIN", "sprite", "banana"),
        ("ASSET_OBJECT_SILVERCOIN", "sprite", "silver_coin"),
        ("ASSET_OBJECT_PALMTREETOP", "sprite", "palm_tree_top"),
        ("ASSET_OBJECT_FROG", "mesh", None),
        ("ASSET_OBJECT_AIRZIPPERS", "mesh", None),
    ]
    for object_id, expected_kind, texture in expectations:
        bpy.ops.dkr.place_object(object_id=object_id)
        obj = bpy.context.active_object
        kind = str(obj.get("dkr_preview", "marker"))
        label = object_id.replace("ASSET_OBJECT_", "")

        check(kind == expected_kind,
              "%s is drawn as a %s (got %s)" % (label, expected_kind, kind))
        check(obj.type == "MESH" and obj.data is not None,
              "%s has real geometry" % label)
        if obj.type == "MESH":
            check(len(obj.data.polygons) > 0,
                  "%s has faces (%d)" % (label, len(obj.data.polygons)))
        if texture:
            check(texture in _texture_names(obj),
                  "%s is textured with %s (got %s)"
                  % (label, texture, _texture_names(obj)))

    # The data must be untouched by any of it.
    catalog = catalog_module.load()
    exported = scene.export_object_map(bpy.context, catalog)
    check(len(exported.objects) == len(expectations),
          "every placed object still exports")
    check(all(o.object_id.startswith("ASSET_OBJECT_") for o in exported.objects),
          "exported objects keep their type")


def _texture_names(obj):
    names = []
    for material in obj.data.materials if obj.type == "MESH" else []:
        if material is None or not material.node_tree:
            continue
        for node in material.node_tree.nodes:
            if node.type == "TEX_IMAGE" and node.image:
                names.append(node.image.name)
    return ", ".join(names) or "<none>"


def test_artwork_at_game_size():
    """Sprites show every tile, at the size the game draws them, and scale.

    A palm top is five strips of texture and a balloon three; drawing only the
    first left a sliver. The size comes from the header scale, the game's pixel
    to world ratio, and the object's own size byte - which the Blender scale
    now carries both ways.
    """
    print("artwork at game size")
    from dkr_track_editor import prefs, preview

    fresh()
    tree = prefs.resolve(bpy.context)
    if tree is None:
        print("  skip: no extracted decomp assets to draw from")
        return
    catalog = catalog_module.load()

    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_PALMTREETOP")
    palm = bpy.context.active_object
    check(len(palm.data.polygons) == 5,
          "a palm top draws its five strips (got %d)" % len(palm.data.polygons))
    frame = tree.sprite_frame(tree.model_id_for("ASSET_OBJECT_PALMTREETOP"))
    left, right, _bottom, _top = frame.bounds()
    expected = (right - left) * preview.SPRITE_UNITS_PER_PIXEL * 3.0
    xs = [v.co.x for v in palm.data.vertices]
    check(abs((max(xs) - min(xs)) - expected) < 1e-3,
          "a palm top is %.1f units across (got %.1f)" % (expected, max(xs) - min(xs)))
    check(tuple(palm.scale) == (1.0, 1.0, 1.0),
          "a fresh palm top's radius 64 is the header size (got %r)" % (tuple(palm.scale),))

    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_WEAPONBALLOON")
    balloon = bpy.context.active_object
    check(len(balloon.data.polygons) == 3,
          "a balloon draws its three strips (got %d)" % len(balloon.data.polygons))
    check(min(v.co.z for v in balloon.data.vertices) >= 0.0,
          "a balloon hangs above its anchor, string at the bottom")

    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")
    zipper = bpy.context.active_object
    check(zipper.get("dkr_preview") == "decal",
          "a ground zipper is drawn as its arrow decal (got %s)"
          % zipper.get("dkr_preview"))
    if zipper.type == "MESH":
        check("ground_zipper" in _texture_names(zipper),
              "the decal shows the zipper arrow (got %s)" % _texture_names(zipper))
        xs = [v.co.x for v in zipper.data.vertices]
        check(abs((max(xs) - min(xs)) - 2 * preview.SHADOW_HALF_WIDTH * 4.5) < 1e-3,
              "the arrow is %.1f units across (got %.1f)"
              % (2 * preview.SHADOW_HALF_WIDTH * 4.5, max(xs) - min(xs)))
        check(all(abs(v.co.z - preview.SHADOW_LIFT) < 1e-6 for v in zipper.data.vertices),
              "the arrow lies flat on the ground")

    # Scaling in the viewport is what writes the size byte; untouched objects
    # keep theirs exactly.
    palm.scale = (2.0, 2.0, 2.0)
    balloon.scale = (1.5, 1.5, 1.5)
    bpy.context.view_layer.update()
    exported = {o.object_id: o for o in scene.export_object_map(bpy.context, catalog).objects}
    check(exported["ASSET_OBJECT_PALMTREETOP"].fields.get("radius") == 128,
          "a palm top scaled 2x writes radius 128 (got %r)"
          % exported["ASSET_OBJECT_PALMTREETOP"].fields.get("radius"))
    check(exported["ASSET_OBJECT_WEAPONBALLOON"].fields.get("scale") == 1.5,
          "a balloon scaled 1.5x writes scale 1.5 (got %r)"
          % exported["ASSET_OBJECT_WEAPONBALLOON"].fields.get("scale"))
    zipper_type = catalog.get("ASSET_OBJECT_GROUNDZIPPER")
    check(exported["ASSET_OBJECT_GROUNDZIPPER"].fields.get("scale")
          == zipper_type.field("scale").fresh(),
          "an unscaled zipper keeps its fresh scale byte")

    # And the byte comes back as the scale on the way in.
    placed = scene.read_object(palm, catalog)
    root = scene.ensure_root(bpy.context)
    again = scene.create_empty(bpy.context, placed, catalog, root, tree)
    check(abs(again.scale.x - 2.0) < 1e-9,
          "radius 128 imports as a 2x scale (got %r)" % again.scale.x)


def test_renumber_checkpoints():
    """Renumber a set by naming the gates in order, from one that is right.

    The click session hands the operator exactly this: the gate to start from
    and the gates clicked after it. Each becomes the next number; the gates not
    clicked follow in their order; the ones before keep theirs.
    """
    print("renumber checkpoints")
    from dkr_track_editor import prefs, race_ai, validate
    from dkr_track_editor.operators import race_ai as race_ai_ops

    fresh()
    check(race_ai_ops._LABEL_HANDLE is not None,
          "the checkpoint numbers' draw handler is registered")
    tree = prefs.resolve(bpy.context)
    if tree is None:
        print("  skip: no extracted decomp assets")
        return
    lake = next((l for l in tree.levels() if l.label == "Ancient Lake"), None)
    if lake is None:
        print("  skip: no Ancient Lake")
        return
    bpy.ops.dkr.import_level(level=lake.name, with_geometry=False)
    catalog = catalog_module.load()

    def car_set():
        gates = [o for o in race_ai_ops.checkpoint_empties(bpy.context)
                 if int(o.get("vehicleType", 0)) == 0 and not int(o.get("isAltCheckpoint", 0))]
        return sorted(gates, key=lambda o: int(o["index"]))

    def unpaired():
        exported = scene.export_object_map(bpy.context, catalog)
        return len(race_ai.build_route(exported.objects, 0).unpaired)

    bpy.context.view_layer.objects.active = None
    check(not bpy.ops.dkr.renumber_checkpoints.poll(),
          "it needs a checkpoint selected to start from")

    gates = car_set()
    paired_before = unpaired()
    anchor, after = gates[5], gates[6]
    before_numbers = {o.name: int(o["index"]) for o in gates[:6]}
    bpy.context.scene.cursor.location = (anchor.matrix_world.translation
                                         + after.matrix_world.translation) / 2
    bpy.ops.dkr.place_object(object_id=race_ai.CHECKPOINT)
    new = bpy.context.active_object
    check(car_set().index(new) != 6,
          "the Place button's index puts it elsewhere in the lap (%d)" % int(new["index"]))

    bpy.context.view_layer.objects.active = anchor
    check(bpy.ops.dkr.renumber_checkpoints.poll(), "a selected checkpoint can start it")
    base = int(anchor["index"])
    bpy.ops.dkr.renumber_checkpoints(anchor=anchor.name, sequence=new.name)
    order = [o.name for o in car_set()]
    check(int(new["index"]) == base + 1,
          "the checkpoint clicked after %d becomes %d (got %d)"
          % (base, base + 1, int(new["index"])))
    check(order[order.index(new.name) + 1] == after.name and int(after["index"]) == base + 2,
          "the one that followed the start comes next, as %d (got %d)"
          % (base + 2, int(after["index"])))
    check({o.name: int(o["index"]) for o in car_set()[:6]} == before_numbers,
          "the checkpoints before the start keep their numbers")
    check(len({int(o["index"]) for o in car_set()}) == len(car_set()),
          "no number is used twice")

    exported = scene.export_object_map(bpy.context, catalog)
    issues = [i for i in validate.validate(exported) if i.severity == validate.ERROR
              and i.object_id == race_ai.CHECKPOINT]
    check(not issues, "no checkpoint errors after renumbering (%s)"
          % [i.message for i in issues])
    check(unpaired() == paired_before,
          "alternate checkpoints keep their pairs (%d unpaired, %d before)"
          % (unpaired(), paired_before))

    # Iterating: the next named goes after the last one, not after the start.
    second, third = car_set()[9], car_set()[8]
    bpy.ops.dkr.renumber_checkpoints(anchor=new.name,
                                     sequence="%s,%s" % (second.name, third.name))
    check((int(second["index"]), int(third["index"])) == (base + 2, base + 3),
          "each one named goes after the last (%d, %d)"
          % (int(second["index"]), int(third["index"])))

    # A gate on the alternate route cannot start a pass; its main gate does.
    alternate = next((o for o in race_ai_ops.checkpoint_empties(bpy.context)
                      if int(o.get("isAltCheckpoint", 0))), None)
    if alternate is not None:
        result = bpy.ops.dkr.renumber_checkpoints(anchor=alternate.name, sequence=new.name)
        check(result == {"CANCELLED"}, "an alternate checkpoint is refused as the start")


def test_balloon_variants():
    """A balloon's type has to pick the matching sprite, not a neighbouring one.

    The header lists its five sprites in enum declaration order, so the index
    must come from there. Taking it from the catalogue's observed values, which
    are sorted alphabetically, draws a missile balloon as a trap.
    """
    print("balloon type picks its own sprite")
    from dkr_track_editor import prefs, preview

    tree = prefs.resolve(bpy.context)
    catalog = catalog_module.load()
    if tree is None or "BalloonType" not in catalog.enums:
        print("  skip: no assets or no BalloonType enum")
        return

    for balloon_type in catalog.enums["BalloonType"]:
        variant = preview.variant_for(
            "ASSET_OBJECT_WEAPONBALLOON", {"balloonType": balloon_type}, catalog
        )
        _kind, path, _header = tree.preview_for("ASSET_OBJECT_WEAPONBALLOON", variant)
        name = os.path.basename(path).lower() if path else ""
        stem = balloon_type.replace("BALLOON_TYPE_", "").lower()
        # The asset spells missile "missle"; match on the shared prefix.
        check(stem[:5] in name,
              "%s uses %s (got %s)" % (balloon_type, stem, name or "<none>"))


def find_ancient_lake():
    from test_roundtrip import VANILLA
    import glob
    hits = sorted(glob.glob(os.path.join(
        VANILLA, "*", "levels", "models", "dino_domain", "ancient_lake.bin"
    )))
    return hits[0] if hits else None


def test_slots():
    """A level has two object maps and an object must go back to its own.

    The split is not semantic - 39 of the 85 object types appear in both maps
    across retail tracks, many near half and half - so it is remembered per
    object at import, never inferred from the type.
    """
    print("object map slots")
    path = find_ancient_lake()
    if path is None:
        print("  skip: no extracted level models")
        return

    from dkr_track_editor import prefs
    fresh()
    tree = prefs.resolve(bpy.context)
    if tree is None:
        print("  skip: no decomp assets")
        return
    lake = next((l for l in tree.levels() if l.label == "Ancient Lake"), None)
    if lake is None:
        print("  skip: Ancient Lake not in the index")
        return

    bpy.ops.dkr.import_level(level=lake.name)
    counts = scene.slot_counts(bpy.context)
    check(counts[scene.SLOT_STRUCTURE] > 0 and counts[scene.SLOT_COLLECTABLES] > 0,
          "both maps loaded (%d structure, %d collectables)"
          % (counts[scene.SLOT_STRUCTURE], counts[scene.SLOT_COLLECTABLES]))

    catalog = catalog_module.load()
    total = len(scene.export_object_map(bpy.context, catalog).objects)
    parts = {
        slot: len(scene.export_object_map(bpy.context, catalog, slot=slot).objects)
        for slot in scene.SLOTS
    }
    check(sum(parts.values()) == total,
          "the two maps partition the scene (%d + %d = %d)"
          % (parts[scene.SLOT_STRUCTURE], parts[scene.SLOT_COLLECTABLES], total))
    check(parts == counts, "export agrees with the scene's own count")

    # Each map has to match what it was loaded from, object for object.
    from dkr_track_editor import gltf_io as gio
    original = {
        scene.SLOT_STRUCTURE: gio.load(lake.objects_path),
        scene.SLOT_COLLECTABLES: gio.load(lake.collectables_path),
    }
    for slot, source in original.items():
        rebuilt = scene.export_object_map(bpy.context, catalog, slot=slot)
        check(gio.dumps(rebuilt) == gio.dumps(source),
              "the %s map round-trips byte-exactly on its own" % slot)

    # Moving an object between maps must move exactly one.
    obj = [o for o in scene.iter_dkr_objects(bpy.context)
           if scene.slot_of(o) == scene.SLOT_STRUCTURE][0]
    for other in bpy.context.selected_objects:
        other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.dkr.set_slot(slot="collectables")
    moved = scene.slot_counts(bpy.context)
    check(moved[scene.SLOT_STRUCTURE] == counts[scene.SLOT_STRUCTURE] - 1
          and moved[scene.SLOT_COLLECTABLES] == counts[scene.SLOT_COLLECTABLES] + 1,
          "set_slot moves exactly one object")


def test_partial_export_is_safe():
    """A header must never ship without a payload for both object-map slots.

    The header leaves 0x36 and 0xBA at zero for the runtime to patch, and zero
    is a valid index rather than an absence: the game clamps anything out of
    range to 0 and loads object map 0. So a header shipped with only one slot
    points the other at some other level's objects, which hangs on load.

    The fix is not to refuse - a track with no pickups is a legitimate thing to
    author - but to ship an **empty** map for the empty slot, which is what
    retail does in 16 of its own maps.
    """
    print("partial export")
    import struct
    import zlib

    from dkr_track_editor import prefs

    tree = prefs.resolve(bpy.context)
    if tree is None:
        print("  skip: no decomp assets")
        return
    lake = next((l for l in tree.levels() if l.label == "Ancient Lake"), None)
    if lake is None:
        print("  skip: Ancient Lake not in the index")
        return

    fresh()
    bpy.ops.dkr.import_level(level=lake.name, with_collectables=False)
    counts = scene.slot_counts(bpy.context)
    check(counts[scene.SLOT_COLLECTABLES] == 0,
          "the scene really has an empty collectables slot")

    temporary = tempfile.mkdtemp(prefix="dkr-partial-")
    try:
        target = os.path.join(temporary, "Untitled")
        bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        target += ".dkrmap"

        with open(os.path.join(target, "manifest.json"), "r", encoding="utf-8") as h:
            manifest = json.load(h)
        sections = [e["section"] for e in manifest["adds"]]
        slots = sorted(e["slot"] for e in manifest["adds"]
                       if e["section"] == "LEVEL_OBJECT_MAPS")

        if "LEVEL_HEADERS" in sections:
            check(slots == ["collectables", "structure"],
                  "a header ships with both slots (got %s)" % slots)

        empty = os.path.join(target, "objects_collectables.bin")
        check(os.path.isfile(empty), "the empty slot still gets a payload")
        if os.path.isfile(empty):
            with open(empty, "rb") as handle:
                blob = zlib.decompress(handle.read()[5:], -15)
            check(struct.unpack_from(">I", blob, 0)[0] == 0,
                  "and it is an empty map, fileSize 0")
            check(len(blob) == 16, "which is 16 bytes")

        # The id must follow the folder, not a stale name from an import.
        check(manifest["id"] == "untitled",
              "the id comes from the folder being exported to (got %r)"
              % manifest["id"])
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def _import_lake(**options):
    """A fresh scene holding Ancient Lake's geometry, and the mesh object."""
    path = find_ancient_lake()
    if path is None:
        return None, None
    fresh()
    bpy.ops.dkr.import_geometry(filepath=path, **options)
    from dkr_track_editor.operators import geometry as geometry_ops
    objects = geometry_ops.geometry_objects(bpy.context)
    return path, (objects[0] if objects else None)


def _identity(mesh):
    """``[(segment, vertex), ...]`` indexed by Blender vertex, unbiased.

    Stored biased by one, so that a vertex conjured from nothing - which
    Blender fills with zeroes - reads as "no source" rather than as segment 0
    vertex 0.
    """
    count = len(mesh.vertices)
    segments = [0] * count
    indices = [0] * count
    mesh.attributes["dkr_segment"].data.foreach_get("value", segments)
    mesh.attributes["dkr_vertex"].data.foreach_get("value", indices)
    return [(s - 1, v - 1) for s, v in zip(segments, indices)]


def _edit_mesh(obj):
    """Open the geometry for editing and hand back its bmesh."""
    import bmesh
    obj.hide_select = False
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bm = bmesh.from_edit_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    bm.verts.ensure_lookup_table()
    for face in bm.faces:
        face.select = False
    for edge in bm.edges:
        edge.select = False
    for vertex in bm.verts:
        vertex.select = False
    return bm


def _face_batches(mesh, model):
    """``[(segment, batch), ...]`` indexed by face, mirroring the exporter."""
    from dkr_track_editor.operators import geometry as geometry_ops

    owners = geometry_ops.batch_of_vertex(model)
    vertex_batch = [None] * len(mesh.vertices)
    for at, (segment, vertex) in enumerate(_identity(mesh)):
        owner = owners[segment][vertex] if vertex < len(owners[segment]) else -1
        if owner >= 0:
            vertex_batch[at] = (segment, owner)
    return [
        geometry_ops.batch_of_polygon(polygon.vertices, vertex_batch)
        for polygon in mesh.polygons
    ]


def _read_flags(mesh):
    values = [0] * len(mesh.polygons)
    mesh.attributes["dkr_flags"].data.foreach_get("value", values)
    return values


def test_geometry_import():
    """One mesh, every file vertex in it exactly once, and nothing duplicated.

    This is the contract the exporter rests on: an edit is addressed as
    ``(segment, vertex index)`` in the file, so a Blender vertex that appears
    twice - which is what the old three-mesh import produced for any segment
    holding batches of more than one kind - leaves "which one did the author
    move" with no answer.
    """
    print("geometry import")
    from dkr_track_editor import level_model
    from dkr_track_editor.operators import geometry as geometry_ops

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return
    check(obj is not None, "a geometry object was built")
    if obj is None:
        return

    objects = geometry_ops.geometry_objects(bpy.context)
    check(len(objects) == 1,
          "the track is one mesh, not one per kind (got %d)" % len(objects))
    check(str(obj[geometry_ops.PROP_GEOMETRY]) == geometry_ops.GEOMETRY_KIND,
          "it is marked as track geometry")
    check(str(obj.get(geometry_ops.PROP_MODEL_PATH, "")) == path,
          "the object records the .bin it was decoded from")

    mesh = obj.data
    model = level_model.load(path)

    check(len(mesh.vertices) == model.vertex_count,
          "every file vertex is in the mesh exactly once (%d vs %d)"
          % (len(mesh.vertices), model.vertex_count))
    check(len(mesh.polygons) > 100,
          "the track has real geometry (%d faces)" % len(mesh.polygons))

    for name, domain, kind in (
        (geometry_ops.ATTR_SEGMENT, "POINT", "INT"),
        (geometry_ops.ATTR_VERTEX, "POINT", "INT"),
        (geometry_ops.ATTR_COLOUR, "POINT", "INT"),
        (geometry_ops.ATTR_FLAGS, "FACE", "INT"),
        (geometry_ops.ATTR_SERIAL, "FACE", "INT"),
        (geometry_ops.ATTR_TRI_FLAGS, "FACE", "INT"),
        (geometry_ops.ATTR_TEXTURE, "FACE", "INT"),
        (geometry_ops.ATTR_OPAQUE, "FACE", "BOOLEAN"),
        (geometry_ops.ATTR_UV, "CORNER", "INT32_2D"),
    ):
        attribute = mesh.attributes.get(name)
        check(attribute is not None and attribute.domain == domain
              and attribute.data_type == kind,
              "%s is a %s attribute on the %s domain" % (name, kind, domain))

    raw = [0] * len(mesh.vertices)
    mesh.attributes["dkr_segment"].data.foreach_get("value", raw)
    check(min(raw) >= 1,
          "identities are stored biased by one, so zero can mean \"no source\"")
    check(int(mesh[geometry_ops.PROP_SCHEMA]) == geometry_ops.SCHEMA,
          "the mesh stamps the schema its attributes are written to")

    identity = _identity(mesh)
    check(len(set(identity)) == len(identity),
          "no two vertices claim the same (segment, vertex) pair")
    expected = {
        (index, at)
        for index, segment in enumerate(model.segments)
        for at in range(len(segment.vertices))
    }
    check(set(identity) == expected,
          "the identities are exactly the file's own, none missing or invented")

    # A segment holding batches of more than one kind is what used to duplicate.
    mixed = sum(
        1 for s in model.segments
        if len({geometry_ops.category_of(b.flags) for b in s.batches}) > 1
    )
    check(mixed > 0,
          "Ancient Lake really does mix kinds within a segment (%d segments), "
          "so this is the case that used to duplicate" % mixed)

    check(geometry_ops.COLOUR_ATTRIBUTE in [a.name for a in mesh.color_attributes],
          "baked vertex lighting imported as a colour attribute")

    # The three-way split survives as material slots rather than as objects.
    categories = set(geometry_ops.slot_categories(obj))
    check(geometry_ops.SURFACE in categories, "surface faces have their own slot")
    check(geometry_ops.INVISIBLE_WALLS in categories,
          "invisible walls have their own slot")
    check(None not in categories, "every slot names the kind it draws")

    check(obj.hide_select,
          "geometry is locked against selection so it is not picked by mistake")

    # Geometry must never be mistaken for a placed object.
    check(len(scene.iter_dkr_objects(bpy.context)) == 0,
          "geometry is not collected as an exportable object")
    check(bpy.context.scene.dkr.geometry_path == path, "geometry path remembered")


def test_wall_visibility():
    """Walls stay in the mesh and out of the way.

    They cannot be a separate object any more, and they cannot simply be left
    out either - their vertices are file vertices and have to be exportable. So
    they are masked, which is only sound because batch vertex windows never
    overlap: a wall vertex is never also a surface vertex.
    """
    print("invisible walls")
    from dkr_track_editor.operators import geometry as geometry_ops

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return
    if obj is None or geometry_ops.WALL_GROUP not in obj.vertex_groups:
        print("  skip: this track has no invisible walls")
        return

    modifier = obj.modifiers.get(geometry_ops.WALL_MASK)
    check(modifier is not None and modifier.type == "MASK",
          "a mask modifier hides the walls without deleting them")
    check(modifier is not None and modifier.show_viewport,
          "invisible walls start hidden")

    before = len(obj.data.vertices)
    bpy.ops.dkr.toggle_walls()
    check(not obj.modifiers[geometry_ops.WALL_MASK].show_viewport, "toggle shows them")
    bpy.ops.dkr.toggle_walls()
    check(obj.modifiers[geometry_ops.WALL_MASK].show_viewport, "toggle hides them again")
    check(len(obj.data.vertices) == before,
          "and the mesh itself never loses a vertex either way")


def test_geometry_roundtrip():
    """Import, change nothing, export: the bytes must be the ones that came in.

    Compared inflated rather than as containers: our DEFLATE stream will differ
    from Rare's and that says nothing about the model.
    """
    print("geometry round trip")
    from dkr_track_editor import level_model, level_model_encoder
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit is not None, "the exporter found the geometry")
    if edit is None:
        return
    check(not edit.edited,
          "an untouched import reports no changes (got %r)" % edit.describe())
    check(not edit.notes, "and nothing worth warning about (%r)" % (edit.notes,))

    with open(path, "rb") as handle:
        base = level_model.decompress(handle.read())
    written = level_model.decompress(level_model_encoder.pack(edit.model))
    check(written == base,
          "the re-encoded model is byte-identical to the shipped one "
          "(%d vs %d bytes)" % (len(written), len(base)))

    # And the operator that reports it agrees.
    check(bpy.ops.dkr.check_geometry() == {"FINISHED"},
          "check_geometry runs on an untouched import")


def test_geometry_vertex_edit():
    """Move one vertex: exactly that one moves, and its box follows.

    The bounding box matters more than it looks. The game culls a segment
    against it before drawing, so a vertex moved outside a stale box comes out
    as scenery that vanishes rather than as a bounding box bug.
    """
    print("moving a vertex")
    from dkr_track_editor import level_model, level_model_encoder
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    mesh = obj.data
    base_model = level_model.load(path)
    moved_at = 7
    segment_index, vertex_index = _identity(mesh)[moved_at]
    # Blender Z is the file's Y, so lifting it here raises it there.
    mesh.vertices[moved_at].co.z += 4000.0

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit.summary.moved == 1,
          "exactly one vertex is reported moved (got %d)" % edit.summary.moved)
    check(edit.summary.bounds > 0,
          "and the derived bounds were brought back in line")

    differing = [
        (s, v)
        for s, segment in enumerate(base_model.segments)
        for v in range(len(segment.vertices))
        if segment.vertices[v] != edit.model.segments[s].vertices[v]
    ]
    check(differing == [(segment_index, vertex_index)],
          "exactly the moved vertex differs in the file (got %r)" % (differing,))

    was = base_model.segments[segment_index].vertices[vertex_index]
    now = edit.model.segments[segment_index].vertices[vertex_index]
    check(now[1] == was[1] + 4000 and now[0] == was[0] and now[2] == was[2],
          "it moved 4000 up the file's Y and nowhere else (%r -> %r)" % (was, now))

    box = edit.model.bounding_boxes[segment_index]
    check(box[4] >= now[1],
          "its segment's bounding box followed it (upper Y %d, vertex Y %d)"
          % (box[4], now[1]))
    check(tuple(box) != tuple(base_model.bounding_boxes[segment_index]),
          "which means the box actually changed")

    # And it survives the file.
    reparsed = level_model.parse(
        level_model.decompress(level_model_encoder.pack(edit.model))
    )
    check(reparsed.segments[segment_index].vertices[vertex_index] == now,
          "the moved vertex reads back from the encoded model")
    check(tuple(reparsed.bounding_boxes[segment_index]) == tuple(box),
          "so does the recomputed box")
    check(reparsed.bounds == edit.model.bounds,
          "and the header bounds it recomputed")


def test_geometry_colour_edit():
    """Repaint one vertex: the colour reaches the file without a rebuild.

    The baked lighting multiplies into the texture, so losing it is not a
    subtle shading difference - it draws the track black. The in-place patch
    used to write positions, flags and surface types and quietly drop colour,
    which meant an author could repaint, export, see every other edit land, and
    have no way to tell the lighting had not.
    """
    print("repainting a vertex")
    from dkr_track_editor import level_model, level_model_encoder
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    mesh = obj.data
    base_model = level_model.load(path)
    painted_at = 11
    segment_index, vertex_index = _identity(mesh)[painted_at]
    was = base_model.segments[segment_index].colours[vertex_index]
    now = tuple(0 if channel == 255 else 255 for channel in was)

    for channel, name in enumerate(geometry_ops.ATTR_COLOUR_CHANNELS):
        attribute = mesh.attributes[name]
        values = [0] * len(attribute.data)
        attribute.data.foreach_get("value", values)
        values[painted_at] = now[channel]
        attribute.data.foreach_set("value", values)

    edit = geometry_export.build_edited_model(bpy.context)
    check(not edit.rebuilt,
          "nothing was added or removed, so the file keeps its layout")
    check(edit.summary.painted == 1,
          "exactly one vertex is reported repainted (got %d)" % edit.summary.painted)

    differing = [
        (s, v)
        for s, segment in enumerate(base_model.segments)
        for v in range(len(segment.colours))
        if tuple(segment.colours[v]) != tuple(edit.model.segments[s].colours[v])
    ]
    check(differing == [(segment_index, vertex_index)],
          "exactly the repainted vertex differs in the file (got %r)" % (differing,))
    check(tuple(edit.model.segments[segment_index].colours[vertex_index]) == now,
          "and it carries the colour that was painted (%r -> %r)" % (was, now))

    reparsed = level_model.parse(
        level_model.decompress(level_model_encoder.pack(edit.model))
    )
    check(tuple(reparsed.segments[segment_index].colours[vertex_index]) == now,
          "which reads back from the encoded model")


def test_unpainted_colour_layer():
    """An all-zero colour layer is not a black track, and it says so.

    Blender starts a new colour attribute white, so an all-zero layer does not
    come from Blender - it comes from carrying already-black vertices through
    a mesh. Testing only that a layer exists let one through and built a track
    black everywhere, with nothing anywhere saying why. Testing whether the
    layer carries anything keeps the fallback and leaves a deliberate black
    alone.
    """
    print("reading an all-zero colour layer")
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import new_track

    def mesh_with(layers):
        fresh()
        bpy.ops.mesh.primitive_plane_add()
        mesh = bpy.context.active_object.data
        for name, domain, colour in layers:
            layer = mesh.color_attributes.new(name, "FLOAT_COLOR", domain)
            if colour is not None:
                layer.data.foreach_set(
                    "color", list(colour) * len(layer.data))
        return mesh

    white = (255, 255, 255, 255)
    name = geometry_ops.COLOUR_ATTRIBUTE

    stats = {}
    mesh = mesh_with([(name, "POINT", (0.0, 0.0, 0.0, 0.0))])
    colours = new_track._read_colours(mesh, stats)
    check(set(colours) == {white},
          "an all-zero layer builds white, not black (got %r)" % (set(colours),))
    check(stats.get("colour_pristine") == name,
          "and the reason is recorded for the author (got %r)"
          % (stats.get("colour_pristine"),))

    # Black with an opaque alpha is a choice, and it survives.
    stats = {}
    mesh = mesh_with([(name, "POINT", (0.0, 0.0, 0.0, 1.0))])
    colours = new_track._read_colours(mesh, stats)
    check(set(colours) == {(0, 0, 0, 255)},
          "black painted on purpose is kept (got %r)" % (set(colours),))
    check("colour_pristine" not in stats,
          "and is not reported as unpainted")

    # Paint on one channel only is still paint.
    stats = {}
    mesh = mesh_with([(name, "POINT", (0.0, 0.0, 1.0, 0.0))])
    colours = new_track._read_colours(mesh, stats)
    check(set(colours) == {(0, 0, 255, 0)},
          "a single painted channel counts as painted (got %r)" % (set(colours),))

    # The layer the author did paint is named rather than guessed at.
    stats = {}
    mesh = mesh_with([(name, "POINT", (0.0, 0.0, 0.0, 0.0)),
                      ("Color", "CORNER", (1.0, 0.5, 0.25, 1.0))])
    colours = new_track._read_colours(mesh, stats)
    check(set(colours) == {white}, "an all-zero layer still builds white")
    check(stats.get("colour_elsewhere") == ["Color"],
          "and the painted layer is named, not chosen (got %r)"
          % (stats.get("colour_elsewhere"),))

    # A layer Blender just created is white, which is real lighting.
    stats = {}
    mesh = mesh_with([(name, "POINT", None)])
    colours = new_track._read_colours(mesh, stats)
    check(set(colours) == {white},
          "a freshly created layer is white and is kept (got %r)" % (set(colours),))
    check("colour_pristine" not in stats,
          "and is not reported as unpainted, because Blender starts it white")

    # A baked layer on the wrong domain is reported as such.
    stats = {}
    mesh = mesh_with([(name, "CORNER", (1.0, 1.0, 1.0, 1.0))])
    colours = new_track._read_colours(mesh, stats)
    check(set(colours) == {white}, "a CORNER baked layer builds white")
    check(str(stats.get("colour_domain")) == "CORNER",
          "and its domain is reported (got %r)" % (stats.get("colour_domain"),))


def test_geometry_refuses_orphans():
    """A vertex with no segment and no face has nowhere to go, and says so.

    A duplicated identity is no longer an error: Blender gives an extruded
    vertex the identity of the one it was pulled from, which is how it says
    "this came from that", so the same pair legitimately appears twice. What
    still has no answer is a vertex conjured from nothing and attached to
    nothing - it names no segment and no face names it.
    """
    print("refusing an orphaned vertex")
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    mesh = obj.data
    before = len(mesh.vertices)
    bm = _edit_mesh(obj)
    bm.verts.new((0.0, 0.0, 9000.0))
    import bmesh
    bmesh.update_edit_mesh(mesh)
    bpy.ops.object.mode_set(mode="OBJECT")
    check(len(mesh.vertices) == before + 1, "a loose vertex really was added")

    try:
        geometry_export.build_edited_model(bpy.context)
        check(False, "an orphaned vertex is refused")
    except geometry_export.GeometryExportError as error:
        message = str(error)
        check("not part of any face" in message,
              "an orphaned vertex is refused, saying why (%s)" % message[:70])
        check(str(before) in message, "and names which vertex it means")
        # An index alone is no use: Blender gives an author no way to jump to
        # vertex 1671, so the message has to name the command that finds it.
        check("Loose Geometry" in message,
              "and names the Blender command that selects them")

    # A new island that does have faces is a different mistake with a different
    # fix, and used to be reported as "belongs to no face", which was false.
    path, obj = _import_lake(include_hidden=True)
    mesh = obj.data
    bm = _edit_mesh(obj)
    made = [bm.verts.new((float(i) * 100.0, 0.0, 9000.0)) for i in range(3)]
    bm.faces.new(made)
    bmesh.update_edit_mesh(mesh)
    bpy.ops.object.mode_set(mode="OBJECT")
    try:
        geometry_export.build_edited_model(bpy.context)
        check(False, "a detached island is refused")
    except geometry_export.GeometryExportError as error:
        message = str(error)
        check("no face joins to the track" in message,
              "a detached island is refused as detached, not as loose (%s)"
              % message[:70])
        check("not part of any face" not in message,
              "and is not described as having no faces, because it has one")


def test_geometry_reaches_chained_new_faces():
    """New geometry several faces out from the road still finds its segment.

    A vertex built from nothing inherits nothing, so only the faces around it
    can say where it belongs - and that answer has to spread outwards. Reading
    it once would place only what touches the existing track and refuse a ramp
    built two faces further on, which is an ordinary thing to model.
    """
    print("new geometry chained away from the track")
    from mathutils import Vector

    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    mesh = obj.data
    bm = _edit_mesh(obj)
    anchor = bm.faces[8]
    first, second = list(anchor.verts)[:2]
    one = bm.verts.new(first.co + Vector((0.0, 0.0, 300.0)))
    two = bm.verts.new(second.co + Vector((0.0, 0.0, 300.0)))
    three = bm.verts.new(one.co + Vector((0.0, 0.0, 300.0)))
    bm.faces.new((first, second, one))   # touches the track, so names `one`
    bm.faces.new((second, one, two))     # names `two`, but only once `one` is known
    bm.faces.new((one, two, three))      # names `three`, one step further out
    bmesh.update_edit_mesh(mesh)
    bpy.ops.object.mode_set(mode="OBJECT")

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit.rebuilt, "it exports through the rebuilding path")
    check(edit.summary.faces_added == 3,
          "all three new triangles reached the model (got %d)"
          % edit.summary.faces_added)


def test_geometry_schema_guard():
    """A mesh from an older addon is refused, not read one slot off.

    The identities went biased by one, so an older mesh would not fail to
    read - it would name a segment that exists and place every vertex wrong.
    Silent and wrong is why this is a refusal.
    """
    print("schema guard")
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    del obj.data[geometry_ops.PROP_SCHEMA]
    try:
        geometry_export.build_edited_model(bpy.context)
        check(False, "an unstamped mesh is refused")
    except geometry_export.GeometryExportError as error:
        check("import the track again" in str(error),
              "an unstamped mesh is refused, telling the author to re-import")

    obj.data[geometry_ops.PROP_SCHEMA] = geometry_ops.SCHEMA - 1
    try:
        geometry_export.build_edited_model(bpy.context)
        check(False, "an older schema is refused")
    except geometry_export.GeometryExportError as error:
        check("schema" in str(error), "an older schema is refused by number")


def test_geometry_batch_flags():
    """Render flags are per batch, so all of a batch's faces have to agree."""
    print("batch render flags")
    from dkr_track_editor import level_model
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    mesh = obj.data
    model = level_model.load(path)
    batches = _face_batches(mesh, model)

    counts = {}
    for key in batches:
        if key is not None:
            counts[key] = counts.get(key, 0) + 1
    target = next(
        (key for key, count in sorted(counts.items())
         if count > 1
         and not (model.segments[key[0]].batches[key[1]].flags
                  & level_model.RENDER_HIDDEN)),
        None,
    )
    if target is None:
        print("  skip: no visible batch with more than one face")
        return

    was = model.segments[target[0]].batches[target[1]].flags
    now = was | level_model.RENDER_HIDDEN

    values = _read_flags(mesh)
    faces = [i for i, key in enumerate(batches) if key == target]
    for index in faces:
        values[index] = geometry_ops.to_signed32(now)
    mesh.attributes[geometry_ops.ATTR_FLAGS].data.foreach_set("value", values)

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit.summary.flags == 1,
          "one batch is reported reflagged (got %d)" % edit.summary.flags)
    check(edit.summary.moved == 0, "and nothing moved")
    check(edit.model.segments[target[0]].batches[target[1]].flags == now,
          "the batch carries its new flags")
    check(geometry_ops.category_of(now) == geometry_ops.INVISIBLE_WALLS,
          "setting RENDER_HIDDEN with collision left on makes an invisible wall")

    # The whole u32 has to survive, not just the two bits that name the kind.
    check(edit.model.segments[target[0]].batches[target[1]].flags & ~0x300
          == was & ~0x300,
          "every other flag bit came through untouched")

    check(not edit.rebuilt,
          "reflagging a whole batch keeps the in-place path, so the rest of "
          "the file stays byte-identical")

    # One face flagged differently from the rest of its batch used to be
    # refused, because a batch is one draw call and the layout could not be
    # changed. It can now: the odd face gets a batch of its own.
    values[faces[0]] = geometry_ops.to_signed32(was)
    mesh.attributes[geometry_ops.ATTR_FLAGS].data.foreach_set("value", values)
    split = geometry_export.build_edited_model(bpy.context)
    check(split.rebuilt,
          "a batch flagged two ways takes the rebuilding path instead")
    kept = [b for b in split.model.segments[target[0]].batches if b.flags == was]
    made = [b for b in split.model.segments[target[0]].batches if b.flags == now]
    check(kept and made,
          "and the segment ends up with both flag values in it (%d and %d "
          "batches)" % (len(kept), len(made)))


def test_geometry_in_package():
    """A .dkrmap ships a model payload when, and only when, geometry changed.

    A track that only reworks objects must ship none: its header then keeps
    pointing at the base track's geometry, and the package stays small and
    stays correct.
    """
    print("geometry in the package")
    from dkr_track_editor import level_model, prefs

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return
    if prefs.resolve(bpy.context) is None:
        print("  skip: no decomp assets")
        return

    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
    settings = bpy.context.scene.dkr
    settings.track_name = "Ancient Lake Reshaped"
    settings.track_id = "ancient-lake-reshaped"

    temporary = tempfile.mkdtemp(prefix="dkr-geometry-")
    try:
        target = os.path.join(temporary, "ancient-lake-reshaped.dkrmap")
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "export with untouched geometry succeeds")

        model_bin = os.path.join(target, "model.bin")
        check(not os.path.isfile(model_bin),
              "unchanged geometry ships no model payload")
        with open(os.path.join(target, "manifest.json"), "r", encoding="utf-8") as h:
            manifest = json.load(h)
        check(not any(e["section"] == "LEVEL_MODELS" for e in manifest["adds"]),
              "and the manifest does not claim one")

        # Now reshape it, and the payload has to appear.
        obj.data.vertices[11].co.z += 2500.0
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "export with edited geometry succeeds")
        check(os.path.isfile(model_bin), "edited geometry ships model.bin")

        with open(os.path.join(target, "manifest.json"), "r", encoding="utf-8") as h:
            manifest = json.load(h)
        entries = [e for e in manifest["adds"] if e["section"] == "LEVEL_MODELS"]
        check(len(entries) == 1 and entries[0]["file"] == "model.bin",
              "the manifest claims it under LEVEL_MODELS")

        claimed = {entry["file"] for entry in manifest["adds"]}
        present = {n for n in os.listdir(target) if n.endswith(".bin")}
        check(claimed == present,
              "the manifest still claims exactly what is on disk "
              "(claims %s, has %s)" % (sorted(claimed), sorted(present)))

        if os.path.isfile(model_bin):
            with open(model_bin, "rb") as handle:
                shipped = level_model.parse(level_model.decompress(handle.read()))
            source = level_model.load(path)
            differing = [
                (s, v)
                for s, segment in enumerate(source.segments)
                for v in range(len(segment.vertices))
                if segment.vertices[v] != shipped.segments[s].vertices[v]
            ]
            check(len(differing) == 1,
                  "the shipped model differs from the base by exactly the one "
                  "vertex that moved (got %r)" % (differing,))
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def test_stale_object_maps():
    """An export that could not compile the maps must not pass for success.

    The addon writes ``objects_*.bin`` itself, and the only reason it cannot is
    that the decomp assets are unreachable. A file left from an earlier export
    then no longer matches the scene. Dropping it would be worse than keeping
    it - a header whose slot has no payload points at another level's objects
    and hangs - so it ships, and the package has to say so.
    """
    print("stale object maps")
    from dkr_track_editor import prefs

    if prefs.resolve(bpy.context) is None:
        print("  skip: no decomp assets")
        return

    fresh()
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
    settings = bpy.context.scene.dkr
    settings.track_name = "Stale Maps"
    settings.track_id = "stale-maps"

    temporary = tempfile.mkdtemp(prefix="dkr-stale-")
    original = prefs.resolve
    try:
        target = os.path.join(temporary, "stale-maps.dkrmap")
        bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        compiled = os.path.join(target, "objects_structure.bin")
        check(os.path.isfile(compiled), "the first export compiled the maps")
        if not os.path.isfile(compiled):
            return
        with open(compiled, "rb") as handle:
            first = handle.read()

        # Change the scene, then export with the asset tree out of reach.
        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")
        prefs.resolve = lambda context=None: None
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "the export still finishes")

        with open(compiled, "rb") as handle:
            second = handle.read()
        check(second == first,
              "the payload is the earlier one, because nothing recompiled it")

        source = gltf_io.load(os.path.join(target, "source",
                                           "objects_structure.gltf"))
        check(len(source.objects) == 2,
              "while source/ did get the object that was added (%d)"
              % len(source.objects))

        # That mismatch is the whole problem, so the package has to record it.
        with open(os.path.join(target, "HOW-TO-BUILD.md"), "r",
                  encoding="utf-8") as handle:
            notes = handle.read()
        check("could not be compiled this time" in notes,
              "HOW-TO-BUILD.md says the shipped maps are from an earlier export")
        check("objects_structure.bin" in notes.split("could not be compiled")[-1],
              "and names which payload it means")
    finally:
        prefs.resolve = original
        shutil.rmtree(temporary, ignore_errors=True)


def test_memory_budget():
    """The load budget has to be visible before an export, not after a crash.

    The game reserves a fixed arena for a level model and the heaviest retail
    track already sits at 68% of it, so an author adding geometry needs the
    ceiling in front of them. The figures come from level_model_layout so the
    panel cannot drift from what the encoder believes.
    """
    print("memory budget")
    from dkr_track_editor import level_model, level_model_layout as layout
    from dkr_track_editor.operators import geometry as geometry_ops

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    model = level_model.load(path)
    check(int(obj[geometry_ops.PROP_RUNTIME_SIZE]) == layout.runtime_size(model),
          "the object records what the layout module says it costs")
    check(int(obj[geometry_ops.PROP_HEADROOM]) == layout.headroom_triangles(model),
          "and how many more triangles fit")

    budget = geometry_ops.budget_of(obj)
    check(budget is not None, "the panel can read it back")
    if budget is None:
        return
    fraction, headroom = budget
    check(0.0 < fraction < 1.0,
          "Ancient Lake fits the budget, at %d%%" % round(fraction * 100))
    check(headroom > 0, "with room to spare (%d triangles)" % headroom)

    # The quieter ceiling, and the more dangerous one: a memory overflow at
    # least writes a debug print, while crowding the collision candidate list
    # produces no diagnostic at all and shows up as falling through the floor
    # somewhere other than the cause.
    pressure = geometry_ops.collision_pressure(obj)
    check(pressure is not None, "the collision pressure is recorded too")
    if pressure is not None:
        count, tolerated, candidates = pressure
        check(count == len(layout.oversized_segments(model)),
              "and matches what the layout module counts")
        check(count <= tolerated,
              "Ancient Lake does not crowd the %d collision slots (%d oversized)"
              % (candidates, count))
        check(not layout.check_collision_pressure(model),
              "so it raises no collision warning")

    # The invariant the face-to-batch lookup rests on, checked where it is used.
    check(not layout.check_windows(model),
          "the batch windows tile every segment, which is what the face kinds "
          "and the wall mask are resolved through")


def test_geometry_add_geometry():
    """Extruding a face adds geometry, which means the blob is laid out afresh.

    Extrude is how anyone actually adds to a mesh, and it produces quads out of
    the sides it sweeps - so the export has to fan them into triangles rather
    than refuse, or the commonest modelling operation there is would be
    unusable.
    """
    print("adding geometry")
    from dkr_track_editor import level_model, level_model_layout as layout
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    import bmesh
    mesh = obj.data
    before_vertices, before_faces = len(mesh.vertices), len(mesh.polygons)
    bm = _edit_mesh(obj)
    bm.faces[8].select = True
    bmesh.update_edit_mesh(mesh)
    bpy.ops.mesh.extrude_region_move(
        TRANSFORM_OT_translate={"value": (0.0, 0.0, 400.0)})
    bpy.ops.object.mode_set(mode="OBJECT")

    check(len(mesh.vertices) > before_vertices, "the extrude added vertices")
    quads = sum(1 for p in mesh.polygons if len(p.vertices) > 3)
    check(quads > 0, "and it made quads, which the file cannot store (%d)" % quads)

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit.rebuilt, "so the export lays the model out afresh")
    check(edit.ships, "and has something to ship")

    base = level_model.load(path)
    check(edit.model.vertex_count > base.vertex_count,
          "the model gained vertices (%d -> %d)"
          % (base.vertex_count, edit.model.vertex_count))
    check(edit.model.triangle_count == base.triangle_count + 7,
          "and gained 7 triangles: one new cap plus three quads fanned into "
          "two each (got %d)" % (edit.model.triangle_count - base.triangle_count))

    # The rebuilt model has to be a model, not merely bytes.
    from dkr_track_editor import level_model_encoder
    blob = level_model.decompress(level_model_encoder.pack(edit.model))
    reparsed = level_model.parse(blob)
    check(reparsed.vertex_count == edit.model.vertex_count,
          "it re-encodes and re-parses with the same vertex count")
    check(reparsed.model_size == len(blob),
          "modelSize matches the blob it declares (%d vs %d)"
          % (reparsed.model_size, len(blob)))
    check(not layout.check_windows(reparsed),
          "and the batch windows still tile every segment, which the importer "
          "and the wall mask both rely on")

    # Every original vertex still has to be somewhere in the rebuilt model.
    kept = {v for segment in reparsed.segments for v in segment.vertices}
    missing = [v for segment in base.segments for v in segment.vertices
               if v not in kept]
    check(not missing,
          "no original vertex position was lost in the rebuild (%d missing)"
          % len(missing))


def test_geometry_across_segments():
    """A face built across the join between two segments is written, not refused.

    Merging, filling or bridging over a segment boundary is ordinary modelling,
    and the first thing an author tidying up a converted track does. The face
    goes to one segment and the corners the other owns are copied into it.
    """
    print("geometry across segments")
    from dkr_track_editor import level_model, level_model_encoder
    from dkr_track_editor import level_model_layout as layout
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    mesh = obj.data
    owner = [d.value for d in mesh.attributes[geometry_ops.ATTR_SEGMENT].data]
    positions = [v.co.copy() for v in mesh.vertices]
    first = next(at for at, value in enumerate(owner) if value == 1)
    others = sorted((at for at, value in enumerate(owner) if value == 2),
                    key=lambda at: (positions[at] - positions[first]).length)
    near, further = others[0], others[1]

    bm = _edit_mesh(obj)
    bm.verts.ensure_lookup_table()
    # Held, not re-indexed: adding a vertex outdates bmesh's lookup table.
    a, b, c = bm.verts[first], bm.verts[near], bm.verts[further]
    bm.faces.new((a, b, c))
    # And a vertex made from nothing between the same two segments.
    middle = bm.verts.new((positions[first] + positions[near]) / 2.0)
    bm.faces.new((a, middle, b))
    import bmesh
    bmesh.update_edit_mesh(mesh)
    bpy.ops.object.mode_set(mode="OBJECT")

    base = level_model.load(path)
    try:
        edit = geometry_export.build_edited_model(bpy.context)
        refused = None
    except geometry_export.GeometryExportError as error:
        edit, refused = None, str(error)
    check(refused is None, "a face across two segments exports (%s)" % refused)
    if edit is None:
        return
    check(edit.model.triangle_count == base.triangle_count + 2,
          "both new triangles are in the model (+%d)"
          % (edit.model.triangle_count - base.triangle_count))
    check(any("crossed between segments" in note for note in edit.notes),
          "and the export says it copied corners across")
    blob = level_model.decompress(level_model_encoder.pack(edit.model))
    reparsed = level_model.parse(blob)
    check(reparsed.triangle_count == edit.model.triangle_count
          and not layout.check_windows(reparsed),
          "it re-encodes, re-parses and every batch window still tiles")
    check(not layout.bsp_problems(reparsed), "and the BSP still walks")


def test_geometry_merge_by_distance():
    """Merge by Distance over a whole track exports, and exports it intact.

    The first thing an author tidying a converted track reaches for, and it
    welds every boundary between segments. Blender averages integer attributes
    as it welds, so it invents segment ids - which stretched six segment boxes
    across Ancient Lake - and garbles the packed vertex colour. Both have to
    come out right.
    """
    print("merge by distance")
    from dkr_track_editor import level_model_layout as layout
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    mesh = obj.data
    raw = [d.value for d in mesh.attributes[geometry_ops.ATTR_COLOUR].data]
    before = {}
    for vertex, value in zip(mesh.vertices, raw):
        where = tuple(int(round(c)) for c in scene.to_map(obj.matrix_world @ vertex.co))
        before.setdefault(where, []).append(geometry_ops.unpack_colour(value))
    count = len(mesh.vertices)

    _edit_mesh(obj)
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=0.5)
    bpy.ops.object.mode_set(mode="OBJECT")
    check(len(obj.data.vertices) < count,
          "the merge welded the segment boundaries (%d -> %d vertices)"
          % (count, len(obj.data.vertices)))

    edit = geometry_export.build_edited_model(bpy.context)
    check(not layout.oversized_segments(edit.model),
          "no segment box stretches across the track (%s)"
          % layout.oversized_segments(edit.model))

    outside = 0
    for segment in edit.model.segments:
        for position, colour in zip(segment.vertices, segment.colours):
            there = before.get(tuple(position))
            if not there:
                continue
            for channel in range(4):
                low = min(c[channel] for c in there) - 1
                high = max(c[channel] for c in there) + 1
                if not low <= colour[channel] <= high:
                    outside += 1
                    break
    check(not outside,
          "every vertex colour lies between the colours welded into it "
          "(%d do not)" % outside)


def test_geometry_remove_geometry():
    """Deleting faces has to shrink the track rather than be ignored."""
    print("removing geometry")
    from dkr_track_editor import level_model
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    import bmesh
    mesh = obj.data
    before = len(mesh.polygons)
    bm = _edit_mesh(obj)
    for face in bm.faces[:3]:
        face.select = True
    bmesh.update_edit_mesh(mesh)
    bpy.ops.mesh.delete(type="ONLY_FACE")
    bpy.ops.object.mode_set(mode="OBJECT")
    check(len(mesh.polygons) == before - 3, "three faces really were deleted")

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit.rebuilt, "the export rebuilds the layout")
    base = level_model.load(path)
    check(edit.model.triangle_count == base.triangle_count - 3,
          "and the model lost exactly those three triangles (%d -> %d)"
          % (base.triangle_count, edit.model.triangle_count))
    check(edit.summary.faces_removed == 3,
          "which is what it reports (%r)" % edit.describe())


def test_geometry_rebuild_needs_the_whole_track():
    """A partial import must never be allowed to rebuild the model.

    Importing without the invisible walls leaves their faces out of the mesh.
    Reshaping is still safe - the file keeps its own triangles - but rebuilding
    a segment from that mesh would delete 986 walls from the retail set without
    anyone asking. So the rebuilding path refuses what the reshaping path
    allows.
    """
    print("rebuild refuses a partial import")
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=False)
    if path is None:
        print("  skip: no extracted level models")
        return
    omitted = int(obj.get(geometry_ops.PROP_OMITTED, 0) or 0)
    if not omitted:
        print("  skip: this track has no hidden faces to leave out")
        return
    check(omitted > 0, "the import recorded the faces it left out (%d)" % omitted)

    # Reshaping is still fine, because the file keeps its own triangles.
    obj.data.vertices[7].co.z += 100.0
    edit = geometry_export.build_edited_model(bpy.context)
    check(not edit.rebuilt and edit.summary.moved == 1,
          "moving a vertex still takes the in-place path")

    # Adding geometry is not.
    import bmesh
    mesh = obj.data
    bm = _edit_mesh(obj)
    bm.faces[8].select = True
    bmesh.update_edit_mesh(mesh)
    bpy.ops.mesh.extrude_region_move(
        TRANSFORM_OT_translate={"value": (0.0, 0.0, 400.0)})
    bpy.ops.object.mode_set(mode="OBJECT")
    try:
        geometry_export.build_edited_model(bpy.context)
        check(False, "a rebuild from a partial import is refused")
    except geometry_export.GeometryExportError as error:
        check("Include Invisible Walls" in str(error),
              "a rebuild from a partial import is refused, naming the option "
              "to turn on (%s)" % str(error)[:80])


def test_unusable_mesh_is_named():
    """A mesh the author modelled has to be named, not skipped in silence.

    It is the first thing anyone building a track from scratch will do, and the
    export cannot use it - a vertex has to name the segment it belongs to and a
    Blender mesh names none. Saying nothing leaves them with an empty package
    and no reason for it.
    """
    print("unusable meshes")
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    bpy.ops.mesh.primitive_cube_add(size=500.0)
    cube = bpy.context.active_object
    check(geometry_ops.unusable_meshes(bpy.context) == [cube],
          "a mesh the author modelled is spotted")

    # The addon's own objects are not attempts at geometry and must not be
    # reported: a placed object drawn with its artwork is a mesh too.
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_COIN")
    check(geometry_ops.unusable_meshes(bpy.context) == [cube],
          "placed objects are not reported, whatever they are drawn with")

    path = find_ancient_lake()
    if path is not None:
        bpy.ops.dkr.import_geometry(filepath=path, replace_existing=True)
        check(cube in geometry_ops.unusable_meshes(bpy.context),
              "and imported track geometry is not reported either")
        check(all(geometry_ops.PROP_GEOMETRY not in o
                  for o in geometry_ops.unusable_meshes(bpy.context)),
              "only the author's own mesh is")


def test_identified_fields_are_visible():
    """A field the decomp has identified must not stay hidden as a raw byte.

    The catalogue keeps ``unkB`` as the name, because that name is the custom
    property key an existing .blend already holds and renaming it would drop the
    author's value in silence. The meaning rides alongside as a label - and the
    label is what has to bring the field out from behind Show Raw Bytes, or a
    checkpoint's per-lane offsets stay buried under a name that says nothing.
    """
    print("identified fields")
    from dkr_track_editor.ui import panels

    catalog = catalog_module.load()
    checkpoint = catalog.get("ASSET_OBJECT_CHECKPOINT")
    if checkpoint is None:
        print("  skip: no checkpoint in the catalogue")
        return

    labelled = [f for f in checkpoint.fields if f.label != f.name]
    check(len(labelled) >= 12,
          "the checkpoint's per-lane fields carry labels (%d)" % len(labelled))
    check(all(f.is_raw for f in labelled),
          "and they are all still named unk*, so the raw filter would catch them")
    check(not any(panels.is_hidden_raw(f, False) for f in labelled),
          "yet none is hidden with Show Raw Bytes off")

    unlabelled = [f for f in checkpoint.fields if f.is_raw and f.label == f.name]
    check(all(panels.is_hidden_raw(f, False) for f in unlabelled),
          "while %d genuinely unidentified byte(s) stay hidden" % len(unlabelled))
    check(not any(panels.is_hidden_raw(f, True) for f in unlabelled),
          "and Show Raw Bytes still reveals those")


def _pick(subject):
    """Any valid member of an enum, so a test can answer a choice."""
    catalog = catalog_module.load()
    members = sorted(catalog.raw.get("enumValues", {}).get(subject, {}))
    return members[0] if members else None


def test_header_from_scratch():
    """A track with no ancestor can now produce a header, and must answer first.

    A remix inherits two hundred bytes from the track it is built on. A track
    modelled from scratch inherits nothing, and the header is what points at the
    geometry - so without one the package has nothing to load even once the
    geometry exists.
    """
    print("level header from scratch")
    from dkr_track_editor import level_header_template as template
    from dkr_track_editor.operators import header as header_ops

    fresh(level_type=None)
    check(not header_ops.overrides(bpy.context),
          "a fresh scene answers nothing")
    check(header_ops.unanswered(bpy.context) == ["/race-type"],
          "only the race type remains outstanding (%r)"
          % (header_ops.unanswered(bpy.context),))

    check(bpy.ops.dkr.header_defaults() == {"FINISHED"}, "Fill Defaults runs")
    filled = header_ops.overrides(bpy.context)
    check(len(filled) >= 10, "it answers most of the form (%d)" % len(filled))
    check(header_ops.unanswered(bpy.context) == ["/race-type"],
          "the race type still needs an answer")
    check(filled.get("/world") == "WORLD_CUSTOM_TRACKS",
          "Fill Defaults selects Custom Tracks")
    check(header_ops.key_for("/race-type") not in bpy.context.scene,
          "and leaves what the Level Type owns to the Level Type")

    bpy.ops.dkr.set_level_type(mode="RACE")
    check(not header_ops.unanswered(bpy.context),
          "choosing the level type completes the default header")
    check(header_ops.inherited_overrides(bpy.context)["/world"] == "WORLD_CUSTOM_TRACKS",
          "remixes also default to Custom Tracks")

    world = _pick("World")
    if not world:
        print("  skip: the catalogue has no World enum")
        return
    bpy.context.scene[header_ops.key_for("/world")] = world
    check(header_ops.inherited_overrides(bpy.context)["/world"] == world,
          "an explicit world is respected for remixes")
    check(not header_ops.unanswered(bpy.context),
          "answering the world completes the header")

    # The template has to accept what the panel collected, unchanged.
    document = template.document(header_ops.effective_overrides(bpy.context))
    check(document.get("world") == world,
          "the answer reaches the document (%r)" % document.get("world"))
    check(document.get("race-type") == "RACETYPE_DEFAULT",
          "and so does the Level Type's race type")


def test_header_reaches_the_package():
    """The authored header has to end up in the .dkrmap, and a partial one must not."""
    print("authored header in the package")
    from dkr_track_editor import prefs
    from dkr_track_editor.operators import header as header_ops

    if prefs.resolve(bpy.context) is None:
        print("  skip: no decomp assets")
        return
    world = _pick("World")
    if not world:
        print("  skip: the catalogue has no World enum")
        return

    fresh()
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
    settings = bpy.context.scene.dkr
    settings.track_name = "Scratch Track"
    settings.track_id = "scratch-track"

    temporary = tempfile.mkdtemp(prefix="dkr-header-")
    try:
        target = os.path.join(temporary, "scratch-track.dkrmap")

        # A new race exports directly into Custom Tracks without choosing a world.
        bpy.ops.dkr.header_defaults()
        check(bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False) == {"FINISHED"},
              "the default world exports without another answer")
        with open(os.path.join(target, "header.bin"), "rb") as handle:
            check(handle.read(1) == bytes([6]), "the package targets Custom Tracks")

        bpy.context.scene[header_ops.key_for("/world")] = world
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "a complete header exports")

        header_bin = os.path.join(target, "header.bin")
        check(os.path.isfile(header_bin), "header.bin is written")
        if os.path.isfile(header_bin):
            with open(header_bin, "rb") as handle:
                payload = handle.read()
            check(len(payload) > 0, "and it holds %d bytes" % len(payload))

        with open(os.path.join(target, "manifest.json"), "r", encoding="utf-8") as h:
            manifest = json.load(h)
        check(any(e["section"] == "LEVEL_HEADERS" for e in manifest["adds"]),
              "the manifest claims it")
        # A header authored from nothing points at no geometry: the template
        # leaves /model for the runtime to patch from a LEVEL_MODELS payload,
        # and a scratch track has none. The package is well formed and would
        # still not load, so the export has to say so.
        check(not any(e["section"] == "LEVEL_MODELS" for e in manifest["adds"]),
              "and the package really does ship without geometry, which is the "
              "case the warning is for")
        claimed = {e["file"] for e in manifest["adds"]}
        present = {n for n in os.listdir(target) if n.endswith(".bin")}
        check(claimed == present,
              "and still claims exactly what is on disk (%s vs %s)"
              % (sorted(claimed), sorted(present)))
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def test_material_opacity():
    """A material's opacity: the viewport follows it, and the export writes it.

    The game has one way to fade a face whatever its texture is: the loader
    reads a vertex coloured ``(1, 1, b)`` as alpha ``b``. So the slider lives on
    the material, the preview multiplies it into the picture's alpha, and the
    export gives every vertex of the material's batches that colour.
    """
    print("material opacity")
    from dkr_track_editor import level_model
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    material = next(
        (m for m in obj.data.materials
         if m is not None and m.get(geometry_ops.PROP_CATEGORY) == geometry_ops.SURFACE
         and int(m.get(geometry_ops.PROP_TEXTURE_INDEX, -1)) >= 0
         and any(n.type == "TEX_IMAGE" for n in m.node_tree.nodes)), None)
    check(material is not None, "a textured surface material to fade")
    if material is None:
        return
    check(hasattr(material, geometry_ops.PROP_OPACITY)
          and geometry_ops.opacity_of(material) == 1.0,
          "every material starts fully opaque")
    check(geometry_ops.OPACITY_NODE not in material.node_tree.nodes,
          "and its preview has no fade in it")

    material.dkr_opacity = 0.5
    node = material.node_tree.nodes.get(geometry_ops.OPACITY_NODE)
    check(node is not None and abs(node.inputs[1].default_value - 0.5) < 1e-6,
          "setting it puts the fade in the viewport")
    check(node is not None and node.outputs[0].is_linked,
          "and the fade reaches the shader")
    material.dkr_opacity = 0.25
    again = material.node_tree.nodes.get(geometry_ops.OPACITY_NODE)
    check(again is not None and again.as_pointer() == node.as_pointer()
          and abs(node.inputs[1].default_value - 0.25) < 1e-6,
          "dragging it changes the one value rather than rebuilding the tree")

    edit = geometry_export.build_edited_model(bpy.context)
    index = int(material[geometry_ops.PROP_TEXTURE_INDEX])
    faded = kept = wrong = 0
    for segment in edit.model.segments:
        for batch in segment.batches:
            window = segment.colours[batch.vertex_offset:
                                     batch.vertex_offset + batch.vertex_count]
            ours = (batch.texture_index == index
                    and geometry_ops.category_of(batch.flags) == geometry_ops.SURFACE)
            if ours:
                faded += 1
                wrong += sum(1 for c in window if tuple(c) != (1, 1, 64, 255))
            else:
                kept += 1
    check(faded and not wrong,
          "every vertex of the material's %d batch(es) says alpha 64 to the "
          "loader (%d do not)" % (faded, wrong))
    check(edit.ships, "and the package would ship it")
    check(any("opacity" in note for note in edit.notes),
          "the export says what fading costs")
    base = level_model.load(path)
    same = all(
        tuple(a) == tuple(b)
        for s_new, s_old in zip(edit.model.segments, base.segments)
        for batch in s_new.batches
        if not (batch.texture_index == index
                and geometry_ops.category_of(batch.flags) == geometry_ops.SURFACE)
        for a, b in zip(s_new.colours[batch.vertex_offset:batch.vertex_offset + batch.vertex_count],
                        s_old.colours[batch.vertex_offset:batch.vertex_offset + batch.vertex_count]))
    check(same, "the other %d batches keep their baked light" % kept)

    material.dkr_opacity = 1.0
    check(geometry_ops.OPACITY_NODE not in material.node_tree.nodes,
          "back to full, the fade leaves the viewport")
    edit = geometry_export.build_edited_model(bpy.context)
    check(not edit.edited, "and the export is untouched again (%r)" % edit.describe())


def _faded_batches(model, obj):
    """``[(segment, index, batch), ...]`` for every batch a material fades."""
    from dkr_track_editor.operators import geometry as geometry_ops

    alpha_for = geometry_ops.batch_alpha(geometry_ops.opacities(obj))
    return [(segment, index, batch)
            for segment in model.segments
            for index, batch in enumerate(segment.batches)
            if alpha_for(batch) is not None]


def test_faded_water_is_drawn_last():
    """Faded calm water over a ROM texture is flagged ``RENDER_WATER``.

    A faded face left in the first pass is drawn before what is behind it and
    then hides it - a lake showing the sky instead of its bed - so the export
    moves it to the second. A ROM texture's render mode cannot change, so for
    one of those the only way is the flag retail's own water carries, and it is
    only given to water.
    """
    print("faded water is drawn in the second pass")
    from dkr_track_editor import level_model, transparency as looks
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    translucency = geometry_export.texture_translucency(bpy.context, obj)
    material = next(
        (m for m in obj.data.materials
         if m is not None
         and m.get(geometry_ops.PROP_CATEGORY) == geometry_ops.SURFACE
         and translucency.get(int(m.get(geometry_ops.PROP_TEXTURE_INDEX, -1)))
         is False), None)
    check(material is not None, "a surface material over an opaque ROM texture")
    if material is None:
        return
    index = int(material[geometry_ops.PROP_TEXTURE_INDEX])

    material.dkr_opacity = 0.4
    edit = geometry_export.build_edited_model(bpy.context)
    faded = _faded_batches(edit.model, obj)
    stuck = [batch for segment, at, batch in faded
             if at < segment.opaque_batches]
    check(faded and len(stuck) == len(faded),
          "faded, ground over an opaque ROM texture cannot leave the first pass "
          "(%d of %d stay)" % (len(stuck), len(faded)))
    check(any("still drawn with the solid track" in note for note in edit.notes),
          "and the export says so")

    for other in obj.data.materials:
        if (other is not None
                and int(other.get(geometry_ops.PROP_TEXTURE_INDEX, -1)) == index):
            other[geometry_ops.PROP_SURFACE] = looks.SURFACE_WATER_CALM
    edit = geometry_export.build_edited_model(bpy.context)
    faded = _faded_batches(edit.model, obj)
    moved = [batch for segment, at, batch in faded
             if at >= segment.opaque_batches and batch.flags & looks.RENDER_WATER]
    check(faded and len(moved) == len(faded),
          "as calm water, every faded draw call is flagged RENDER_WATER and "
          "drawn in the second pass (%d of %d)" % (len(moved), len(faded)))
    check(any("RENDER_WATER" in note for note in edit.notes)
          and not any("still drawn with the solid track" in note
                      for note in edit.notes),
          "the export says what it did, and nothing is left behind")
    ours = {id(batch) for _segment, _at, batch in faded}
    base = level_model.load(path)
    before = sum(1 for segment in base.segments for batch in segment.batches
                 if batch.flags & looks.RENDER_WATER)
    after = sum(1 for segment in edit.model.segments
                for batch in segment.batches
                if batch.flags & looks.RENDER_WATER and id(batch) not in ours)
    check(after == before,
          "and no other draw call gained the flag (%d water before, %d besides "
          "ours after)" % (before, after))

    # The panel says it before the export has to: over dry ground the fade
    # would hide what is behind it, over calm water it would not.
    from dkr_track_editor.ui import panels
    settings = bpy.context.scene.dkr
    settings.texture_id = geometry_ops.texture_table(obj)[index]["id"]
    settings.texture_opacity = 0.4
    warning = ("label", "In game, faded faces over this texture")
    settings.texture_surface = "0"
    check(warning in _draw_panel(panels.DKR_PT_textures),
          "the Textures panel warns about fading this texture on dry ground")
    settings.texture_surface = str(looks.SURFACE_WATER_CALM)
    check(warning not in _draw_panel(panels.DKR_PT_textures),
          "and not as calm water")


def test_faded_own_texture_is_drawn_last():
    """A faded texture of the track's own is written see-through.

    That is what puts the faded faces in the second pass, and it is the
    package's business alone: the look an author sees stays what they picked,
    and the pixels - which name the HD replacement - do not change.
    """
    print("a faded texture of the track's own is written see-through")
    from dkr_track_editor import level_model, textures as texture_module
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-faded-own-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary,
                                                          "faded.blend"))
        picture = _write_probe_image(temporary, "lake", 64, 32)
        bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=3,
                                        y_subdivisions=3)
        bpy.ops.dkr.track_from_mesh_blank(keep_source=False)
        built = geometry_ops.geometry_objects(bpy.context)
        check(len(built) == 1, "the track built")
        if not built:
            return
        obj = built[0]

        bpy.ops.dkr.add_custom_texture(
            filepath=picture, size="64x32",
            texture_format=str(texture_module.FORMAT_CODES["RGBA16"]))
        settings = bpy.context.scene.dkr
        _select_faces(obj.data, [polygon.index for polygon in obj.data.polygons])
        settings.texture_id = texture_module.custom_id(0)
        settings.texture_mapping = "PROJECT"
        bpy.ops.dkr.apply_texture()
        entry = custom_ops.entries(bpy.context)[0]
        check(not entry.translucent,
              "the picture is opaque to start with (%s)" % entry.transparency)

        table = geometry_ops.texture_table(obj)
        material = next(
            (m for m in obj.data.materials
             if m is not None and 0 <= int(m.get(geometry_ops.PROP_TEXTURE_INDEX,
                                                 -1)) < len(table)
             and table[int(m[geometry_ops.PROP_TEXTURE_INDEX])]["id"]
             == texture_module.custom_id(0)), None)
        check(material is not None, "a material draws it")
        if material is None:
            return
        material.dkr_opacity = 0.4

        exported = custom_ops.exported(bpy.context)[0]
        check(exported.render_mode == "TRANSPARENT" and exported.translucent,
              "faded, the export writes it see-through")
        look = custom_ops.entries(bpy.context)[0]
        check(look.render_mode == entry.render_mode
              and look.transparency == entry.transparency,
              "while everywhere else it is still what its look says")
        check(exported.texels() == entry.texels(),
              "and its pixels do not change")

        settings.track_name = "Faded"
        settings.track_id = "faded"
        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
        target = os.path.join(temporary, "faded.dkrmap")
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "the package exports (%r)" % (result,))
        if result != {"FINISHED"}:
            return
        with open(os.path.join(target, "textures", "0.bin"), "rb") as handle:
            payload = handle.read()
        check(payload[2] == 0x01,
              "the package's texture says TRANSPARENT RGBA16 (0x%02x)"
              % payload[2])
        model = level_model.load(os.path.join(target, "model.bin"))
        faded = _faded_batches(model, obj)
        check(faded and all(at >= segment.opaque_batches
                            for segment, at, _batch in faded),
              "and its %d faded draw call(s) are drawn in the second pass"
              % len(faded))

        material.dkr_opacity = 1.0
        check(not custom_ops.exported(bpy.context)[0].translucent,
              "back to full opacity, it is written opaque again")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_surface_types():
    """What the ground behaves like rides on the texture table entry.

    Not on the triangle and not on the texture file - so two entries can show
    one image and behave differently, which is how the game gets a picture that
    is grass in one place and road in another. A material keyed by image would
    merge exactly the entries the format keeps apart, so they are keyed by entry.
    """
    print("surface types")
    from dkr_track_editor import level_model
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    model = level_model.load(path)
    found = geometry_ops.surface_types(obj)
    check(bool(found), "the materials carry surface types (%d)" % len(found))

    wrong = [
        (i, v) for i, v in found.items()
        if i >= len(model.textures) or model.textures[i].surface_type != v
    ]
    check(not wrong,
          "and every one matches its texture table entry (%r)" % (wrong[:3],))

    # Keyed by kind AND entry, so one entry drawn as both surface and
    # decoration is two materials - but they must agree about the entry, since
    # the surface type belongs to it rather than to either of them.
    pairs = [(m.get(geometry_ops.PROP_CATEGORY),
              int(m.get(geometry_ops.PROP_TEXTURE_INDEX, -1)))
             for m in obj.data.materials if m is not None]
    real = [p for p in pairs if p[1] >= 0]
    check(len(real) == len(set(real)),
          "no two materials share a (kind, texture entry) pair")
    check(not geometry_ops.surface_conflicts(obj),
          "and an import never disagrees with itself")

    # Untouched, it must report nothing - this runs on every export now, so a
    # mistake here would break the byte-identical round trip everywhere.
    edit = geometry_export.build_edited_model(bpy.context)
    check(not edit.edited and edit.summary.surfaces == 0,
          "an untouched import reports no surface change (%r)" % edit.describe())

    # Now change one and it has to reach the model.
    target = sorted(found)[0]
    material = next(
        m for m in obj.data.materials
        if m is not None and int(m.get(geometry_ops.PROP_TEXTURE_INDEX, -1)) == target
    )
    was = int(material[geometry_ops.PROP_SURFACE])
    now = 1 if was != 1 else 13
    material[geometry_ops.PROP_SURFACE] = now

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit.summary.surfaces == 1,
          "changing one reports one (%r)" % edit.describe())
    check(edit.ships, "and the package would ship it")
    check(edit.model.textures[target].surface_type == now,
          "the texture table entry carries the new type")
    check(not edit.rebuilt,
          "and it stays on the in-place path, because a surface type changes "
          "no counts")

    # It has to survive the file, since the byte is written on the entry.
    from dkr_track_editor import level_model_encoder
    reparsed = level_model.parse(
        level_model.decompress(level_model_encoder.pack(edit.model))
    )
    check(reparsed.textures[target].surface_type == now,
          "and reads back from the encoded model")
    check(geometry_ops.surface_name(now) != "surface %d" % now,
          "the picker can name it (%s)" % geometry_ops.surface_name(now))

    # Two materials drawing one entry must not be allowed to disagree. Ancient
    # Lake has no entry drawn as two kinds, so the state is built rather than
    # found - leaving the refusal unexercised because no retail track happens to
    # reach it is how a guard rots.
    twin = next(
        (m for m in obj.data.materials
         if m is not None and m is not material),
        None,
    )
    if twin is None:
        print("  skip: only one material on this mesh")
        return
    twin[geometry_ops.PROP_TEXTURE_INDEX] = target
    twin[geometry_ops.PROP_SURFACE] = was
    try:
        geometry_export.build_edited_model(bpy.context)
        check(False, "a disagreement about one entry is refused")
    except geometry_export.GeometryExportError as error:
        check("surface types" in str(error),
              "a disagreement about one entry is refused (%s)" % str(error)[:80])


def test_resegment_makes_the_track_its_own_base():
    """Re-segmenting renumbers every vertex, so the shipped .bin stops being the base.

    That is not a side effect to tidy up afterwards - an export against the old
    file would place geometry by indices that now mean something else. Writing
    the re-segmented model out beside the .blend is what makes this a checkpoint
    rather than a one-way door: reshaping afterwards is back on the in-place
    path and byte-exact against the new file.
    """
    print("re-segmenting")
    from dkr_track_editor import level_model, level_model_layout as layout
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    # It refuses without somewhere to write the new base.
    try:
        bpy.ops.dkr.resegment()
        check(False, "an unsaved .blend is refused")
    except RuntimeError as error:
        check("save the .blend first" in str(error),
              "an unsaved .blend is refused, saying why")

    temporary = tempfile.mkdtemp(prefix="dkr-reseg-")
    try:
        blend = os.path.join(temporary, "track.blend")
        bpy.ops.wm.save_as_mainfile(filepath=blend)

        before_segments = len(level_model.load(path).segments)
        result = bpy.ops.dkr.resegment()
        check(result == {"FINISHED"}, "resegment returns FINISHED")

        written = os.path.join(temporary, "track-geometry.bin")
        check(os.path.isfile(written),
              "the re-segmented model is written beside the .blend")
        if not os.path.isfile(written):
            return

        rebuilt = geometry_ops.geometry_objects(bpy.context)
        check(len(rebuilt) == 1, "the mesh was rebuilt as one object")
        check(str(rebuilt[0].get(geometry_ops.PROP_MODEL_PATH, "")) == written,
              "and now points at the new base rather than the shipped .bin")
        check(bpy.context.scene.dkr.geometry_path == written,
              "so does the scene")

        model = level_model.load(written)
        check(len(model.segments) != before_segments,
              "the segmentation really changed (%d -> %d)"
              % (before_segments, len(model.segments)))
        check(not layout.check_windows(model),
              "batch windows still tile every segment")
        check(not layout.check_collision_pressure(model),
              "and nothing crowds the collision candidate list")

        # The checkpoint property: an untouched export is byte-exact against
        # the file just written, on the in-place path.
        edit = geometry_export.build_edited_model(bpy.context)
        check(edit is not None and not edit.rebuilt,
              "a following export takes the in-place path")
        if edit is not None:
            check(not edit.edited,
                  "and reports no change (%r)" % edit.describe())
            from dkr_track_editor import level_model_encoder
            with open(written, "rb") as handle:
                base = level_model.decompress(handle.read())
            again = level_model.decompress(level_model_encoder.pack(edit.model))
            check(again == base,
                  "re-encoding it reproduces the new base byte for byte")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_track_from_mesh():
    """A mesh an author modelled becomes real track geometry.

    The last piece of the from-scratch path. Everything before it started from a
    track the game ships; this one starts from nothing, so the model is built
    blank, filled with the author's faces, and then partitioned into segments
    with the boxes, BSP and PVS to match.
    """
    print("track from a mesh")
    from dkr_track_editor import level_model, level_model_layout as layout
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export
    from dkr_track_editor.operators import new_track as new_track_ops

    donor = find_ancient_lake()
    if donor is None:
        print("  skip: no extracted level models to take a texture table from")
        return

    fresh()
    bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=4, y_subdivisions=4)
    source = bpy.context.active_object
    quads = len(source.data.polygons)
    check(new_track_ops.convertible(bpy.context) == [source],
          "the author's mesh is offered for conversion")

    temporary = tempfile.mkdtemp(prefix="dkr-scratch-")
    try:
        blend = os.path.join(temporary, "mytrack.blend")
        bpy.ops.wm.save_as_mainfile(filepath=blend)

        result = bpy.ops.dkr.track_from_mesh(filepath=donor)
        check(result == {"FINISHED"}, "track_from_mesh returns FINISHED")

        written = os.path.join(temporary, "mytrack-geometry.bin")
        check(os.path.isfile(written), "a level model was written beside the .blend")
        if not os.path.isfile(written):
            return

        model = level_model.load(written)
        check(model.triangle_count == quads * 2,
              "every quad was fanned into two triangles (%d from %d quads)"
              % (model.triangle_count, quads))
        check(len(model.segments) >= 1,
              "it was partitioned into %d segment(s)" % len(model.segments))
        check(not layout.check_windows(model), "the batch windows tile")
        check(len(model.bounding_boxes) == len(model.segments)
              and len(model.bsp) == len(model.segments),
              "boxes and BSP nodes match the segment count")
        check(model.textures, "it carries the donor's texture table (%d)"
              % len(model.textures))
        check(model.bounds != (0, 0, 0, 0, 0, 0),
              "and bounds derived from the geometry (%r)" % (model.bounds,))

        # The author's own mesh is kept, and stops being reported as unusable.
        check(new_track_ops.PROP_CONVERTED in source,
              "the source mesh is marked as converted")
        check(source not in geometry_ops.unusable_meshes(bpy.context),
              "so the export no longer reports it as geometry it cannot use")

        # And the result is ordinary editable geometry.
        built = geometry_ops.geometry_objects(bpy.context)
        check(len(built) == 1, "the converted track was imported back")
        if built:
            mesh = built[0].data
            check(mesh.attributes.get(geometry_ops.ATTR_SEGMENT) is not None,
                  "with the identity attributes every other operator needs")
            check(str(built[0].get(geometry_ops.PROP_MODEL_PATH, "")) == written,
                  "and its own file as the base")

        edit = geometry_export.build_edited_model(bpy.context)
        check(edit is not None and not edit.rebuilt and not edit.edited,
              "an export straight afterwards reports no change (%r)"
              % (edit.describe() if edit else None))
        if edit is not None:
            from dkr_track_editor import level_model_encoder
            with open(written, "rb") as handle:
                base = level_model.decompress(handle.read())
            again = level_model.decompress(level_model_encoder.pack(edit.model))
            check(again == base, "and reproduces the file byte for byte")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_track_from_mesh_refuses_a_giant():
    """A mesh larger than s16 is refused, not wrapped around the world."""
    print("track from a mesh that is too big")
    from dkr_track_editor.operators import new_track as new_track_ops

    donor = find_ancient_lake()
    if donor is None:
        print("  skip: no extracted level models")
        return

    fresh()
    bpy.ops.mesh.primitive_grid_add(size=200000.0)
    source = bpy.context.active_object
    bpy.context.view_layer.update()
    from dkr_track_editor import level_model
    textures = level_model.load(donor).textures
    try:
        new_track_ops.read_source_mesh(source, textures)
        check(False, "a mesh outside s16 is refused")
    except ValueError as error:
        check("outside the s16" in str(error),
              "a mesh outside s16 is refused (%s)" % str(error)[:70])
        check("Scale the mesh down" in str(error),
              "and says what to do about it")
    fresh()


def test_scratch_track_ships_its_geometry():
    """A track built from a mesh must ship its model, unchanged or not.

    "Nothing changed, so send no geometry" is right for a remix - the header
    goes on naming the track the game already has. It is wrong the moment the
    base is the author's own file, because then the only copy of the geometry is
    that file and leaving it out ships a header pointing at nothing. The whole
    package looked correct when this was wrong: manifest honest, header present,
    export reporting success, and no track in it.
    """
    print("a scratch track ships its geometry")
    from dkr_track_editor import prefs
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import header as header_ops

    donor = find_ancient_lake()
    if donor is None or prefs.resolve(bpy.context) is None:
        print("  skip: no extracted assets")
        return
    world, race = _pick("World"), _pick("RaceType")
    if not world or not race:
        print("  skip: the catalogue has no World/RaceType enum")
        return

    fresh()
    bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=4, y_subdivisions=4)
    temporary = tempfile.mkdtemp(prefix="dkr-e2e-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary, "mytrack.blend"))
        bpy.ops.dkr.track_from_mesh(filepath=donor)

        built = geometry_ops.geometry_objects(bpy.context)
        check(built and built[0].get(geometry_ops.PROP_AUTHORED_BASE),
              "the converted geometry knows its base is the author's own")

        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
        bpy.ops.dkr.header_defaults()
        bpy.context.scene[header_ops.key_for("/world")] = world
        bpy.context.scene[header_ops.key_for("/race-type")] = race
        settings = bpy.context.scene.dkr
        settings.track_name = "Scratch"
        settings.track_id = "scratch"

        target = os.path.join(temporary, "scratch.dkrmap")
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "it exports")

        with open(os.path.join(target, "manifest.json"), "r", encoding="utf-8") as h:
            manifest = json.load(h)
        sections = sorted(e["section"] for e in manifest["adds"])
        check("LEVEL_MODELS" in sections,
              "and ships its geometry even though nothing was edited after the "
              "conversion (got %r)" % sections)
        check("LEVEL_HEADERS" in sections, "along with the header it authored")
        check(os.path.isfile(os.path.join(target, "model.bin")),
              "model.bin is on disk")
        claimed = {e["file"] for e in manifest["adds"]}
        present = {n for n in os.listdir(target) if n.endswith(".bin")}
        check(claimed == present,
              "and the manifest claims exactly what is there (%s vs %s)"
              % (sorted(claimed), sorted(present)))
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_drop_to_surface():
    print("drop to surface")
    from dkr_track_editor.operators import geometry as geometry_ops

    path, obj = _import_lake(include_hidden=False)
    if path is None:
        print("  skip: no extracted level models")
        return
    if obj is None:
        print("  skip: no geometry built")
        return

    # Surface, decoration and walls share one mesh now, so the face to aim at
    # has to be picked by its kind rather than by which object it is in.
    mesh = obj.data
    categories = geometry_ops.slot_categories(obj)
    polygon = next(
        (p for p in mesh.polygons
         if 0 <= p.material_index < len(categories)
         and categories[p.material_index] == geometry_ops.SURFACE),
        None,
    )
    if polygon is None:
        print("  skip: no drivable surface in this track")
        return

    centre = obj.matrix_world @ polygon.center
    bpy.context.scene.cursor.location = (centre.x, centre.y, centre.z + 5000.0)
    bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_GROUNDZIPPER")

    zipper = [o for o in scene.iter_dkr_objects(bpy.context)][0]
    before = zipper.location.z
    for other in bpy.context.selected_objects:
        other.select_set(False)
    zipper.select_set(True)
    bpy.context.view_layer.objects.active = zipper

    result = bpy.ops.dkr.drop_to_surface(offset=0.0)
    check(result == {"FINISHED"}, "drop_to_surface returns FINISHED")
    check(zipper.location.z < before, "the object fell (%0.1f -> %0.1f)"
          % (before, zipper.location.z))
    check(abs(zipper.location.z - centre.z) < 1.0,
          "it landed on the face it was above (%0.2f vs %0.2f)"
          % (zipper.location.z, centre.z))


def test_click_to_place():
    """Where a click in the viewport puts an object.

    The modal session itself needs a window and a mouse, which a background
    run has neither of; what it does with a click is this ray, tested here.
    """
    print("click to place")
    from mathutils import Matrix, Vector
    from dkr_track_editor.operators import snap

    fresh()
    context = bpy.context
    context.scene.cursor.location = (0.0, 0.0, -50.0)
    down = Vector((0.0, 0.0, -1.0))

    at = snap.click_location(context, Vector((10.0, 20.0, 100.0)), down)
    check(at is not None and (at - Vector((10.0, 20.0, -50.0))).length < 1e-4,
          "with no track, a click lands on the plane through the cursor (%r)"
          % (at,))
    check(snap.click_location(context, Vector((0.0, 0.0, 100.0)),
                              Vector((0.0, 0.0, 1.0))) is None,
          "a click at the sky places nothing")

    road = _flat_road(context)

    slanted = Vector((1.0, 0.0, -1.0)).normalized()
    at = snap.click_location(context, Vector((-100.0, 0.0, 100.0)), slanted)
    check(at is not None and at.length < 1e-3,
          "a slanted click lands where the ray meets the track (%r)" % (at,))

    # The bug this replaces: a click past the edge of the track fell through
    # to the cursor plane, which near the horizon is thousands of units off.
    at = snap.click_location(context, Vector((2000.0, 0.0, 100.0)), down)
    check(at is None, "a click that misses the track places nothing (%r)" % (at,))

    road.hide_set(True)
    at = snap.click_location(context, Vector((0.0, 0.0, 100.0)), down)
    check(at is not None and abs(at.z + 50.0) < 1e-4,
          "a hidden track is not clicked on (%r)" % (at,))
    road.hide_set(False)

    # A view looking straight down from z=100, drawing only depths 1..50 in
    # front of the eye: the road, 100 below, is past its far clip.
    near, far = 1.0, 50.0
    looking_down = Matrix((
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, -(far + near) / (far - near), -2.0 * far * near / (far - near)),
        (0.0, 0.0, -1.0, 0.0),
    )) @ Matrix.Translation((0.0, 0.0, -100.0))
    at = snap.click_location(context, Vector((0.0, 0.0, 100.0)), down,
                             matrix=looking_down)
    check(at is None, "track past the far clip, not drawn, is not landed on")
    far = 500.0
    looking_down = Matrix((
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, -(far + near) / (far - near), -2.0 * far * near / (far - near)),
        (0.0, 0.0, -1.0, 0.0),
    )) @ Matrix.Translation((0.0, 0.0, -100.0))
    at = snap.click_location(context, Vector((0.0, 0.0, 100.0)), down,
                             matrix=looking_down)
    check(at is not None and at.length < 1e-3,
          "with the far clip past it, the track is landed on (%r)" % (at,))

    # No window to click in: invoking falls back to one object at the cursor.
    for _ in range(2):
        result = bpy.ops.dkr.place_object("INVOKE_DEFAULT",
                                          object_id="ASSET_OBJECT_CHECKPOINT")
        check(result == {"FINISHED"}, "invoked without a window, it places")
    indices = sorted(o.get("index") for o in scene.iter_dkr_objects(context))
    check(indices == [0, 1], "each checkpoint takes the next index (%r)"
          % (indices,))
    from dkr_track_editor.operators.edit import DKR_OT_place_object
    check(DKR_OT_place_object.placing() is None,
          "no placing session is left running")


def _flat_road(context, location=(0.0, 0.0, 0.0)):
    """A 1000-unit square of track at z=0, as the importer would mark it."""
    from dkr_track_editor.operators import geometry as geometry_ops

    mesh = bpy.data.meshes.new("road")
    mesh.from_pydata([(-500.0, -500.0, 0.0), (500.0, -500.0, 0.0),
                      (500.0, 500.0, 0.0), (-500.0, 500.0, 0.0)],
                     [], [(0, 1, 2, 3)])
    road = bpy.data.objects.new("road", mesh)
    road.location = location
    road[geometry_ops.PROP_GEOMETRY] = geometry_ops.GEOMETRY_KIND
    context.scene.collection.objects.link(road)
    context.view_layer.update()
    return road


def test_snapping_to_elements():
    """The screen-space snaps, against a matrix instead of a window."""
    print("snapping to elements")
    import numpy as np
    from mathutils import Matrix, Vector
    from dkr_track_editor.operators import snap

    # Top down, one unit to a pixel: world (-500..500) fills 1000 pixels.
    flat = Matrix((
        (1.0 / 500.0, 0.0, 0.0, 0.0),
        (0.0, 1.0 / 500.0, 0.0, 0.0),
        (0.0, 0.0, -0.001, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    ))
    size = (1000, 1000)
    corners = np.array([(-500.0, -500.0, 0.0), (500.0, 500.0, 0.0)])
    found = snap.nearest_points(corners, flat, size, (990.0, 995.0), 20.0)
    check(len(found) == 1 and (found[0][1] - Vector((500.0, 500.0, 0.0))).length < 1e-6,
          "the vertex under the mouse is caught (%r)" % (found,))
    check(snap.nearest_points(corners, flat, size, (900.0, 900.0), 20.0) == [],
          "a vertex further than the threshold is not")

    # An edge running away from the eye: w = z. Halfway along it on screen
    # is a third of the way along it in the world.
    receding = Matrix((
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, 0.5, 0.0),
        (0.0, 0.0, 1.0, 0.0),
    ))
    vertices = np.array([(-1.0, 0.0, 1.0), (1.0, 0.0, 3.0)])
    edges = np.array([(0, 1)])
    found = snap.nearest_on_edges(vertices, edges, receding, size,
                                  (400.0, 510.0), 20.0)
    check(len(found) == 1, "the edge under the mouse is caught")
    if found:
        pixels, _w, _drawn = snap.project([found[0][1]], receding, size)
        check(abs(pixels[0][0] - 400.0) < 1e-3 and abs(pixels[0][1] - 500.0) < 1e-3,
              "the point on the edge is the one drawn under the mouse (%r)"
              % (pixels[0],))
        check((found[0][1] - Vector((-1.0 / 3.0, 0.0, 5.0 / 3.0))).length < 1e-6,
              "undoing the perspective divide (%r)" % (found[0][1],))

    # One unit a pixel: a grid of 1 is too fine to see, 10 still is, 100 is not.
    step = snap.grid_step(Vector((0.0, 0.0, 0.0)), flat, size, 1.0, 10)
    check(step == 100.0, "the grid step follows the zoom (%r)" % (step,))

    fresh()
    road = _flat_road(bpy.context, location=(10.0, 0.0, 5.0))
    vertices, edges, faces = snap.Snapper()._read(
        road, bpy.context.evaluated_depsgraph_get())
    check(vertices.shape == (4, 3) and edges.shape == (4, 2) and faces.shape == (1, 3),
          "the track is read as vertices, edges and face centres")
    check(abs(vertices[:, 0].min() + 490.0) < 1e-4 and abs(vertices[0, 2] - 5.0) < 1e-4,
          "in world space (%r)" % (vertices[0],))
    check((Vector(faces[0]) - Vector((10.0, 0.0, 5.0))).length < 1e-4,
          "the face centre too (%r)" % (faces[0],))


def _texture_catalogue(context):
    """The ROM's 3D textures as the addon sees them, or ``[]``."""
    from dkr_track_editor import prefs, textures as texture_catalogue

    return texture_catalogue.catalogue(prefs.resolve(context))


def _read_textures(mesh):
    values = [0] * len(mesh.polygons)
    mesh.attributes["dkr_texture"].data.foreach_get("value", values)
    return values


def _select_faces(mesh, faces):
    wanted = set(faces)
    for polygon in mesh.polygons:
        polygon.select = polygon.index in wanted


def test_apply_texture():
    """A texture the track never shipped with reaches the file it exports.

    The whole point of the feature: a level model's table names entries of the
    ROM's 3D texture list, so a custom track is not stuck with the images its
    base model carried. This walks the whole path - pick, apply, export - and
    checks the far end rather than the operator's own bookkeeping.
    """
    print("applying a texture the track does not have")
    from dkr_track_editor import level_model, level_model_encoder
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    catalogue = _texture_catalogue(bpy.context)
    if not catalogue:
        print("  skip: no extracted textures")
        return

    mesh = obj.data
    base = level_model.load(path)
    used = {texture.texture_id for texture in base.textures}
    chosen = next((e for e in catalogue if e.index not in used and not e.animated),
                  None)
    if chosen is None:
        print("  skip: this track already uses every still texture in the ROM")
        return

    # Faces that are drawn, so the material and the UVs both mean something.
    flags = _read_flags(mesh)
    drawn = [
        index for index, value in enumerate(flags)
        if geometry_ops.category_of(geometry_ops.to_unsigned32(value))
        == geometry_ops.SURFACE
    ][:12]
    check(len(drawn) > 1, "there are drivable faces to retexture (%d)" % len(drawn))
    if not drawn:
        return
    _select_faces(mesh, drawn)

    settings = bpy.context.scene.dkr
    settings.texture_id = chosen.index
    settings.texture_mapping = "KEEP"
    settings.texture_surface = "1"

    before = len(base.textures)
    result = bpy.ops.dkr.apply_texture()
    check(result == {"FINISHED"}, "apply_texture returns FINISHED (%r)" % (result,))

    extras = geometry_ops.extra_textures(obj)
    check(len(extras) == 1,
          "one entry was added to the track's table (%d)" % len(extras))
    if not extras:
        return
    check(extras[0]["id"] == chosen.index, "and it names the texture that was picked")
    check(extras[0]["surface"] == 1,
          "with the surface type the panel asked for (%d)" % extras[0]["surface"])

    written = _read_textures(mesh)
    check(all(written[index] == before for index in drawn),
          "every selected face draws the new table entry %d" % before)
    check(any(written[index] != before
              for index in range(len(mesh.polygons)) if index not in set(drawn)),
          "and the faces that were not selected were left alone")

    slot = mesh.polygons[drawn[0]].material_index
    material = mesh.materials[slot] if slot < len(mesh.materials) else None
    check(material is not None
          and material.get(geometry_ops.PROP_TEXTURE_INDEX) == before,
          "the faces carry a material for the new entry")
    check(material is not None and material.get(geometry_ops.PROP_SURFACE) == 1,
          "which knows what the ground now behaves like")

    # And out the other end.
    edit = geometry_export.build_edited_model(bpy.context)
    check(edit is not None, "the geometry still exports")
    if edit is None:
        return
    check(edit.rebuilt,
          "the layout is rebuilt rather than patched, because the table grew")
    check(len(edit.model.textures) == before + 1,
          "the exported model carries %d textures (%d before)"
          % (len(edit.model.textures), before))
    if len(edit.model.textures) != before + 1:
        return

    added = edit.model.textures[before]
    check(added.texture_id == chosen.index,
          "the new table entry names texture %d" % chosen.index)
    check(added.surface_type == 1, "and behaves as the surface type chosen")

    payload = level_model_encoder.pack(edit.model)
    again = level_model.parse(level_model.decompress(payload))
    check(len(again.textures) == before + 1,
          "the compiled model.bin holds the new entry")
    drawing = sum(
        batch.face_count for segment in again.segments
        for batch in segment.batches if batch.texture_index == before
    )
    check(drawing == len(drawn),
          "and %d triangle(s) in the file draw it (%d were selected)"
          % (drawing, len(drawn)))
    fresh()


def test_apply_animated_texture():
    """An animated texture flags the batches drawing it, or it renders frozen."""
    print("applying an animated texture")
    from dkr_track_editor import level_model
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return

    catalogue = _texture_catalogue(bpy.context)
    animated = next((e for e in catalogue if e.animated), None)
    if animated is None:
        print("  skip: no animated textures in the extraction")
        return

    mesh = obj.data
    flags = _read_flags(mesh)
    drawn = [
        index for index, value in enumerate(flags)
        if geometry_ops.category_of(geometry_ops.to_unsigned32(value))
        == geometry_ops.SURFACE
        and not (geometry_ops.to_unsigned32(value) & level_model.RENDER_TEX_ANIM)
    ][:4]
    if not drawn:
        print("  skip: no still drivable faces")
        return
    _select_faces(mesh, drawn)

    settings = bpy.context.scene.dkr
    settings.texture_id = animated.index
    settings.texture_mapping = "KEEP"
    settings.texture_surface = "0"
    bpy.ops.dkr.apply_texture()

    after = _read_flags(mesh)
    check(all(geometry_ops.to_unsigned32(after[i]) & level_model.RENDER_TEX_ANIM
              for i in drawn),
          "the retextured faces are flagged for animation")

    edit = geometry_export.build_edited_model(bpy.context)
    check(edit is not None and edit.model.animated_texture_count > 0,
          "and the model declares animation, which is the gate the renderer "
          "tests before advancing any of them")

    # Putting a still texture back has to clear the bit again: it says what the
    # artwork is, not what the author last did.
    still = next((e for e in catalogue if not e.animated), None)
    if still is not None:
        _select_faces(mesh, drawn)
        settings.texture_id = still.index
        bpy.ops.dkr.apply_texture()
        cleared = _read_flags(mesh)
        check(not any(geometry_ops.to_unsigned32(cleared[i])
                      & level_model.RENDER_TEX_ANIM for i in drawn),
              "and a still texture clears it again")
    fresh()


def test_project_texture_onto_new_geometry():
    """New geometry inherits meaningless UVs; projecting is what fixes them."""
    print("projecting a texture onto new geometry")
    from dkr_track_editor import textures as texture_catalogue
    from dkr_track_editor.operators import geometry as geometry_ops

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return
    catalogue = _texture_catalogue(bpy.context)
    if not catalogue:
        print("  skip: no extracted textures")
        return

    mesh = obj.data
    faces = list(range(min(6, len(mesh.polygons))))
    _select_faces(mesh, faces)

    # Flatten their UVs first, which is the state a face conjured from nothing
    # arrives in and the one an author actually needs rescuing from.
    raw = [0] * (len(mesh.loops) * 2)
    mesh.attributes[geometry_ops.ATTR_UV].data.foreach_get("value", raw)
    for index in faces:
        for corner in mesh.polygons[index].loop_indices:
            raw[corner * 2] = 0
            raw[corner * 2 + 1] = 0
    mesh.attributes[geometry_ops.ATTR_UV].data.foreach_set("value", raw)

    settings = bpy.context.scene.dkr
    settings.texture_id = catalogue[0].index
    settings.texture_mapping = "PROJECT"
    settings.texture_scale = texture_catalogue.DEFAULT_PROJECTION_SCALE
    result = bpy.ops.dkr.apply_texture()
    check(result == {"FINISHED"}, "apply_texture projects (%r)" % (result,))

    after = [0] * (len(mesh.loops) * 2)
    mesh.attributes[geometry_ops.ATTR_UV].data.foreach_get("value", after)
    spread = 0
    for index in faces:
        corners = list(mesh.polygons[index].loop_indices)
        values = {(after[c * 2], after[c * 2 + 1]) for c in corners}
        spread += 1 if len(values) > 1 else 0
    check(spread == len(faces),
          "every projected face got UVs that span it rather than one texel "
          "(%d of %d)" % (spread, len(faces)))
    check(all(-32768 <= value <= 32767 for value in after),
          "and all of them fit the s16 the file stores a UV in")
    fresh()


def test_track_from_mesh_with_its_own_textures():
    """A track built from nothing, textured from the ROM, end to end.

    The path a custom track actually takes now: model a mesh, convert it with no
    donor at all, and give it whatever textures it should have. Nothing in it
    came from a shipped track, which is the case the old donor requirement made
    impossible - and it is the one that has to work, because it is the only one
    where every texture in the table was chosen rather than inherited.
    """
    print("a track from a mesh, textured from the ROM")
    from dkr_track_editor import level_model, level_model_encoder
    from dkr_track_editor import textures as texture_catalogue
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export
    from dkr_track_editor.operators import new_track as new_track_ops

    catalogue = _texture_catalogue(bpy.context)
    if not catalogue:
        print("  skip: no extracted textures")
        return

    fresh()
    bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=3, y_subdivisions=3)
    source = bpy.context.active_object
    quads = len(source.data.polygons)

    temporary = tempfile.mkdtemp(prefix="dkr-texture-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary, "mine.blend"))
        result = bpy.ops.dkr.track_from_mesh_blank()
        check(result == {"FINISHED"},
              "track_from_mesh_blank returns FINISHED (%r)" % (result,))

        written = os.path.join(temporary, "mine-geometry.bin")
        check(os.path.isfile(written), "a level model was written beside the .blend")
        if not os.path.isfile(written):
            return

        blank = level_model.load(written)
        check(not blank.textures,
              "it starts with no texture table at all (%d entries)"
              % len(blank.textures))
        check(blank.triangle_count == quads * 2,
              "and holds the mesh's %d triangles" % blank.triangle_count)

        built = geometry_ops.geometry_objects(bpy.context)
        check(len(built) == 1, "the converted track was imported back")
        if not built:
            return
        obj = built[0]
        mesh = obj.data
        check(all(value == level_model.NO_TEXTURE for value in _read_textures(mesh)),
              "every face is untextured until the author picks one")

        # Now give it a look. Nothing here came from a shipped track.
        picks = [entry for entry in catalogue if not entry.animated][:2]
        if len(picks) < 2:
            return
        half = len(mesh.polygons) // 2
        settings = bpy.context.scene.dkr
        settings.texture_mapping = "PROJECT"
        settings.texture_scale = texture_catalogue.DEFAULT_PROJECTION_SCALE

        _select_faces(mesh, range(half))
        settings.texture_id = picks[0].index
        settings.texture_surface = "0"
        check(bpy.ops.dkr.apply_texture() == {"FINISHED"},
              "the first texture goes on")

        _select_faces(mesh, range(half, len(mesh.polygons)))
        settings.texture_id = picks[1].index
        settings.texture_surface = "1"
        check(bpy.ops.dkr.apply_texture() == {"FINISHED"},
              "and so does a second one, with its own surface type")

        edit = geometry_export.build_edited_model(bpy.context)
        check(edit is not None, "the track exports")
        if edit is None:
            return
        check(len(edit.model.textures) == 2,
              "the table holds exactly the two textures that were chosen (%d)"
              % len(edit.model.textures))

        again = level_model.parse(
            level_model.decompress(level_model_encoder.pack(edit.model))
        )
        check([t.texture_id for t in again.textures]
              == [picks[0].index, picks[1].index],
              "the compiled file names them in the order they were added (%r)"
              % ([t.texture_id for t in again.textures],))
        check([t.surface_type for t in again.textures] == [0, 1],
              "each with the surface type it was given")
        untextured = sum(
            batch.face_count for segment in again.segments
            for batch in segment.batches
            if batch.texture_index == level_model.NO_TEXTURE
        )
        check(untextured == 0,
              "and no triangle is left untextured (%d are)" % untextured)
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_texture_browser_pieces():
    """The parts of the Textures panel that can fail without a screen.

    A background Blender has no region to draw into, so the panel body cannot be
    exercised directly. What can break in it and nowhere else is checked here
    instead: the thumbnails, which reach for a preview collection and a PNG on
    disk; the two enum callbacks, which Blender calls while drawing and which
    must never raise; and the tooltip, which is a classmethod given operator
    properties rather than a string.
    """
    print("the texture browser's moving parts")
    from dkr_track_editor import props, textures as texture_catalogue
    from dkr_track_editor.operators import textures as texture_ops

    fresh()
    catalogue = _texture_catalogue(bpy.context)
    if not catalogue:
        print("  skip: no extracted textures")
        return

    groups = props.texture_group_items(None, bpy.context)
    check(len(groups) > 1,
          "the folder filter offers the extraction's sets (%d)" % len(groups))
    check(groups[0][0] == "ALL", "with everything first")
    named = {item[0] for item in groups}
    check(all(group in named for group in texture_catalogue.groups(catalogue)),
          "and names every folder the catalogue found")

    surfaces = props.texture_surface_items(None, bpy.context)
    check(len(surfaces) > 1,
          "the surface picker offers the SurfaceType enum (%d)" % len(surfaces))
    check(all(item[0].lstrip("-").isdigit() for item in surfaces),
          "identified by value, which is what the table entry stores")

    # A background Blender has no icon manager, so ``icon_id`` is 0 however well
    # the load went - the thing worth checking is that the PNG was found and
    # read, which ``image_size`` reports and a failed load would not.
    icon = texture_ops.icon_for(catalogue[0])
    check(isinstance(icon, int), "asking for a thumbnail returns an icon id")
    held = texture_ops._collection().get(texture_ops.preview_key(catalogue[0]))
    check(held is not None, "and the preview is held for %s" % catalogue[0].name)
    check(held is not None
          and tuple(held.image_size) == (catalogue[0].width, catalogue[0].height),
          "loaded from the texture's own PNG (%r)"
          % (tuple(held.image_size) if held else None,))
    check(texture_ops.icon_for(catalogue[0]) == icon,
          "and asking again gives the same one rather than reloading it")
    texture_ops.teardown()
    check(texture_ops._previews["collection"] is None,
          "unregistering releases the previews, or a re-enable cannot make them")

    bpy.context.scene.dkr.texture_id = catalogue[0].index
    check(texture_ops.picked(bpy.context) is not None,
          "the scene remembers which texture is chosen")
    bpy.context.scene.dkr.texture_id = -1
    check(texture_ops.picked(bpy.context) is None, "and that nothing is")

    # Blender calls this while building a tooltip, with whatever the button set.
    class _Props:
        index = catalogue[0].index

    text = texture_ops.DKR_OT_pick_texture.description(bpy.context, _Props())
    check(catalogue[0].name in text, "the tooltip names the texture (%r)"
          % text.splitlines()[0])

    class _Missing:
        index = 999999

    check(isinstance(
        texture_ops.DKR_OT_pick_texture.description(bpy.context, _Missing()), str),
        "and an index the extraction does not have still gives a tooltip")
    fresh()


def _read_raw_uvs(mesh):
    values = [0] * (len(mesh.loops) * 2)
    mesh.attributes["dkr_uv"].data.foreach_get("value", values)
    return values


def test_texture_side_operators():
    """Select by texture, remove a texture, and push UV editing into the file.

    Three small operators that all write to the mesh's record of the file rather
    than to what the viewport shows, which is exactly where they could look
    right and do nothing.
    """
    print("selecting, clearing and syncing UVs")
    from dkr_track_editor import level_model, textures as texture_catalogue
    from dkr_track_editor.operators import geometry as geometry_ops

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return
    catalogue = _texture_catalogue(bpy.context)
    if not catalogue:
        print("  skip: no extracted textures")
        return

    mesh = obj.data
    flags = _read_flags(mesh)
    drawn = [
        index for index, value in enumerate(flags)
        if geometry_ops.category_of(geometry_ops.to_unsigned32(value))
        == geometry_ops.SURFACE
    ][:5]
    if len(drawn) < 2:
        print("  skip: not enough drivable faces")
        return

    used = {t.texture_id for t in level_model.load(path).textures}
    chosen = next((e for e in catalogue if e.index not in used and not e.animated),
                  None)
    if chosen is None:
        return

    _select_faces(mesh, drawn)
    settings = bpy.context.scene.dkr
    settings.texture_id = chosen.index
    settings.texture_mapping = "PROJECT"
    settings.texture_scale = texture_catalogue.DEFAULT_PROJECTION_SCALE
    settings.texture_surface = "0"
    bpy.ops.dkr.apply_texture()

    # Select by texture: nothing selected, then ask for it back.
    _select_faces(mesh, [])
    check(bpy.ops.dkr.select_by_texture() == {"FINISHED"},
          "select_by_texture returns FINISHED")
    check(sorted(p.index for p in mesh.polygons if p.select) == sorted(drawn),
          "and selects exactly the faces drawing that texture")

    # Push a UV edit through. The UVMap is what an author unwraps; the file's
    # own raw values are what ships, and they only meet here.
    uv_layer = mesh.uv_layers.active
    check(uv_layer is not None, "the mesh has a UV map to edit")
    if uv_layer is None:
        return
    corners = list(mesh.polygons[drawn[0]].loop_indices)
    for corner in corners:
        uv_layer.data[corner].uv = (0.25, 0.75)
    _select_faces(mesh, [drawn[0]])
    check(bpy.ops.dkr.sync_uvs() == {"FINISHED"}, "sync_uvs returns FINISHED")

    raw = _read_raw_uvs(mesh)
    want_s = round(0.25 * level_model.UV_FRACTIONAL_BITS * chosen.width)
    want_t = round((1.0 - 0.75) * level_model.UV_FRACTIONAL_BITS * chosen.height)
    got = {(raw[c * 2], raw[c * 2 + 1]) for c in corners}
    check(got == {(want_s, want_t)},
          "and the file's own UVs now say what the UV editor showed "
          "(%r, wanted %r)" % (got, (want_s, want_t)))

    # Removing a texture leaves the face on its baked colours.
    _select_faces(mesh, [drawn[1]])
    check(bpy.ops.dkr.clear_texture() == {"FINISHED"},
          "clear_texture returns FINISHED")
    check(_read_textures(mesh)[drawn[1]] == level_model.NO_TEXTURE,
          "and the face now names no texture at all")
    check(_read_textures(mesh)[drawn[0]] != level_model.NO_TEXTURE,
          "while the faces beside it keep theirs")
    fresh()


def _refused(call):
    """The message an operator cancelled with, or ``None`` if it went through.

    ``bpy.ops`` turns a CANCELLED-with-an-error into a ``RuntimeError`` when it
    is driven from a script, so a refusal has to be caught rather than read off
    the return value.
    """
    try:
        call()
    except RuntimeError as error:
        return str(error)
    return None


def _write_probe_image(directory, name, width, height, file_format="PNG"):
    """A picture on disk to import, made without leaving Blender.

    Deliberately not a power of two and deliberately not 2:1, so the import has
    to resample it and choose a shape rather than pass it through. A JPEG takes
    the other road through the import: a PNG original is copied as it is, and
    anything else is written out through Blender.
    """
    image = bpy.data.images.new(name, width, height, alpha=True)
    pixels = [0.0] * (width * height * 4)
    for row in range(height):
        for column in range(width):
            at = (row * width + column) * 4
            pixels[at] = column / max(1, width - 1)
            pixels[at + 1] = row / max(1, height - 1)
            pixels[at + 2] = 0.25
            pixels[at + 3] = 1.0
    image.pixels.foreach_set(pixels)
    path = os.path.join(directory,
                        name + (".jpg" if file_format == "JPEG" else ".png"))
    image.file_format = file_format
    image.filepath_raw = path
    image.save()
    bpy.data.images.remove(image)
    return path


def _image_material(name, path):
    """A material drawing ``path`` the way an author's does: image, BSDF, output.

    It also carries a second image node nothing is linked to, added first, as
    real materials often do - a leftover, a roughness map. The picture that
    counts is the one that reaches the surface.
    """
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    tree = material.node_tree
    output = next((n for n in tree.nodes if n.type == "OUTPUT_MATERIAL"), None)
    if output is None:
        output = tree.nodes.new("ShaderNodeOutputMaterial")
    shader = next((n for n in tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if shader is None:
        shader = tree.nodes.new("ShaderNodeBsdfPrincipled")
        tree.links.new(shader.outputs[0], output.inputs["Surface"])
    stray = tree.nodes.new("ShaderNodeTexImage")
    stray.image = bpy.data.images.new("%s leftover" % name, 8, 8)
    node = tree.nodes.new("ShaderNodeTexImage")
    node.image = bpy.data.images.load(path)
    tree.links.new(node.outputs["Color"], shader.inputs["Base Color"])
    return material


def _map_key(co):
    """A Blender position as the whole map units a level model stores it in."""
    return tuple(int(round(float(c))) for c in scene.to_map(co))


def test_track_from_mesh_keeps_material_textures():
    """The flow the HD texture plan exists for, end to end.

    Make the track with textured materials, convert it with one click, export
    it: the textures are still there - in the model file, on screen, in the
    package - and the export leaves the pack that gives DKR-R the originals.
    Each link is checked at its far end: every corner's UV in the written file
    against the author's unwrap, the picture each viewport material shows, and
    the pack's names against the identity of the payload bytes it ships with.
    """
    print("a textured mesh keeps its textures through Track From Mesh")
    import zipfile

    from dkr_track_editor import (level_model, rice_identity, rice_pack,
                                  textures as texture_module)
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-keep-tex-")
    try:
        bpy.ops.wm.save_as_mainfile(
            filepath=os.path.join(temporary, "keeping.blend")
        )
        road = _write_probe_image(temporary, "road", 200, 120)
        # A JPEG, because that is what a photograph usually is - and its
        # original cannot simply be copied into the pack the way a PNG's is.
        grass = _write_probe_image(temporary, "grass", 120, 200,
                                   file_format="JPEG")

        bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=4,
                                        y_subdivisions=4)
        source = bpy.context.active_object
        mesh = source.data
        mesh.materials.append(_image_material("Road", road))
        mesh.materials.append(_image_material("Grass", grass))
        half = len(mesh.polygons) // 2
        for polygon in mesh.polygons:
            polygon.material_index = 0 if polygon.index < half else 1

        # A grid's unwrap is continuous, so each position has one UV.
        unwrap = {}
        layer = mesh.uv_layers.active
        for loop in mesh.loops:
            position = source.matrix_world @ mesh.vertices[loop.vertex_index].co
            unwrap[_map_key(position)] = tuple(layer.data[loop.index].uv)

        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=False)
        check(result == {"FINISHED"},
              "the textured mesh converts with one click (%r)" % (result,))

        settings = bpy.context.scene.dkr
        own = custom_ops.entries(bpy.context)
        check([(e.width, e.height) for e in own] == [(64, 32), (32, 64)],
              "each picture became one of the track's own textures, in the "
              "shape closest to it (%r)" % [(e.width, e.height) for e in own])
        check([texture_module.png_size(e.original) for e in own]
              == [(200, 120), (120, 200)],
              "and each keeps its original at the size it was made (%r)"
              % [texture_module.png_size(e.original) for e in own])
        check(bool(own) and custom_ops.same_source(own[0].source, road),
              "the picture taken is the one reaching the surface, not a "
              "leftover node (%r)" % (own[0].source if own else None))
        if len(own) != 2:
            return

        written = os.path.join(temporary, "keeping-geometry.bin")
        model = level_model.load(written)
        check([t.texture_id for t in model.textures]
              == [texture_module.custom_id(0), texture_module.custom_id(1)],
              "the model file's table names both, by the ids the runtime "
              "rewrites (%r)" % [t.texture_id for t in model.textures])
        untextured = sum(batch.face_count for segment in model.segments
                         for batch in segment.batches
                         if batch.texture_index == level_model.NO_TEXTURE)
        check(untextured == 0,
              "and no triangle is left untextured (%d are)" % untextured)

        built = geometry_ops.geometry_objects(bpy.context)
        check(len(built) == 1, "the track was imported back")
        if not built:
            return
        obj = built[0]
        tri = obj.data
        table = geometry_ops.texture_table(obj)
        face_textures = _read_textures(tri)
        raw = [0] * (len(tri.loops) * 2)
        tri.attributes[geometry_ops.ATTR_UV].data.foreach_get("value", raw)
        compared = wrong = 0
        for polygon in tri.polygons:
            entry = table[face_textures[polygon.index]]
            for loop in polygon.loop_indices:
                vertex = tri.vertices[tri.loops[loop].vertex_index]
                uv = unwrap.get(_map_key(obj.matrix_world @ vertex.co))
                if uv is None:
                    continue
                want = (int(round(uv[0] * 32 * entry["w"])),
                        int(round((1.0 - uv[1]) * 32 * entry["h"])))
                compared += 1
                if (raw[loop * 2], raw[loop * 2 + 1]) != want:
                    wrong += 1
        check(compared >= len(tri.polygons) * 3 and wrong == 0,
              "every corner's UV in the file is the author's unwrap (%d of %d "
              "differ)" % (wrong, compared))

        shown = {}
        for material in tri.materials:
            index = int(material.get(geometry_ops.PROP_TEXTURE_INDEX, -1))
            node = custom_ops.image_node(material)
            if index >= 0 and node is not None:
                shown[index] = os.path.normcase(os.path.normpath(
                    bpy.path.abspath(node.image.filepath)))
        wanted = {
            index: os.path.normcase(os.path.normpath(
                own[texture_module.custom_ordinal(entry["id"])].png))
            for index, entry in enumerate(table)
        }
        check(shown == wanted,
              "and the viewport shows each one's picture rather than an empty "
              "material (%r)" % shown)

        # -- export ------------------------------------------------------
        settings.track_name = "Keeping"
        settings.track_id = "keeping"
        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
        target = os.path.join(temporary, "keeping.dkrmap")
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "the package exports (%r)" % (result,))

        pack = os.path.join(temporary, "keeping-hd.zip")
        check(os.path.isfile(pack),
              "and the high-resolution pack is written beside it")
        check(not any(name.endswith(".zip") for name in os.listdir(target)),
              "not inside it, where the runtime would carry it unread")
        if not os.path.isfile(pack):
            return

        payloads = []
        identities = []
        for ordinal, entry in enumerate(own):
            with open(os.path.join(target, "textures", "%d.bin" % ordinal),
                      "rb") as handle:
                payload = handle.read()
            payloads.append(payload)
            texels = payload[32:32 + texture_module.texel_bytes(
                entry.width, entry.height, entry.format)]
            identities.append(rice_identity.rice_identity(
                texels, entry.width, entry.height, entry.format))
        with zipfile.ZipFile(pack) as archive:
            names = set(archive.namelist())
            check(names == ({rice_pack.entry_name(i) for i in identities}
                            | {rice_pack.STAMP_NAME}),
                  "the pack names each original by the identity of the payload "
                  "the package ships (%r)" % sorted(names))
            sizes = [struct.unpack(">II", archive.read(
                         rice_pack.entry_name(i))[16:24])
                     for i in identities if rice_pack.entry_name(i) in names]
            check(sizes == [(200, 120), (120, 200)],
                  "and holds them at full size (%r)" % sizes)

        with open(os.path.join(target, "manifest.json"), encoding="utf-8") as h:
            manifest = json.load(h)
        stamp = rice_pack.read_stamp(pack) or {}
        digest = rice_pack.texture_digest(payloads)
        check(manifest.get("hdTexturePack", {}).get("textureDigest") == digest
              and stamp.get("textureDigest") == digest,
              "the manifest and the pack carry one digest, of these payloads")
        with open(os.path.join(target, "HOW-TO-BUILD.md"), encoding="utf-8") as h:
            check("keeping-hd.zip" in h.read(),
                  "and HOW-TO-BUILD.md says how to import it")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def _shown_pictures(obj):
    """``{table index: the file its material draws, or None}``."""
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops

    shown = {}
    for material in obj.data.materials:
        index = int(material.get(geometry_ops.PROP_TEXTURE_INDEX, -1))
        if index < 0:
            continue
        node = custom_ops.image_node(material)
        shown[index] = (os.path.normcase(os.path.normpath(
            bpy.path.abspath(node.image.filepath))) if node else None)
    return shown


def test_moved_blend_rebuilds_its_textures():
    """A .blend moved without its dkr_textures folder converts again correctly.

    The case that shipped: a track converted in one folder, the .blend moved
    to another without the folder beside it, and converted again after an
    edit that left one material with no faces - which moves every later
    texture table entry up. The materials are reused by entry, and with no PNG
    to load each went on showing what its entry held before: the picture one
    place along, all over the track, while the file itself was right.

    The pictures are packed and their files deleted, as a glTF import leaves
    them, so the .blend is the only thing left to rebuild them from.
    """
    print("a moved .blend rebuilds its own textures and shows them in place")
    from dkr_track_editor import textures as texture_module
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    first = tempfile.mkdtemp(prefix="dkr-moved-from-")
    second = tempfile.mkdtemp(prefix="dkr-moved-to-")
    third = tempfile.mkdtemp(prefix="dkr-moved-again-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(first, "moving.blend"))
        bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=4,
                                        y_subdivisions=4)
        source = bpy.context.active_object
        mesh = source.data
        for index, name in enumerate(("red", "green", "blue")):
            path = _write_probe_image(first, name, 90 + 30 * index, 50)
            material = _image_material(name, path)
            custom_ops.image_of(material).pack()
            os.remove(path)
            mesh.materials.append(material)
        for polygon in mesh.polygons:
            polygon.material_index = polygon.index % 3

        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=True)
        check(result == {"FINISHED"}, "the packed pictures convert (%r)" % (result,))
        own = custom_ops.entries(bpy.context)
        check(len(own) == 3
              and all(e.source.startswith(custom_ops.PACKED) for e in own),
              "each is recorded as a picture packed in the .blend (%r)"
              % [e.source for e in own])
        if len(own) != 3:
            return
        before = {e.name: texture_module.read_png(e.png) for e in own}

        # The edit: red's faces become green, so red's material draws nothing
        # and green and blue each move one table entry up.
        bpy.ops.dkr.make_convertible(object_name=source.name)
        for polygon in mesh.polygons:
            if polygon.material_index == 0:
                polygon.material_index = 1

        # The move. The old folder stays where it was, as it did on the
        # author's desktop, so the old materials still find their pictures.
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(second, "moving.blend"))
        check(len(custom_ops.missing(bpy.context)) == 3,
              "moved without its folder, the scene knows all three are missing")

        bpy.context.view_layer.objects.active = source
        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=True)
        check(result == {"FINISHED"}, "the moved scene converts again (%r)" % (result,))
        own = custom_ops.entries(bpy.context)
        check(len(own) == 3, "reusing its textures rather than adding them again")
        folder = custom_ops.folder(bpy.context)
        check(os.path.samefile(os.path.dirname(folder), second)
              and all(os.path.isfile(e.png) and os.path.isfile(e.original)
                      and os.path.samefile(os.path.dirname(e.png), folder)
                      for e in own),
              "each PNG and its original were rebuilt beside the moved .blend")
        check(all(texture_module.read_png(e.png) == before[e.name] for e in own),
              "with the texels they had, so the HD pack keeps its names")

        obj = geometry_ops.geometry_objects(bpy.context)[0]
        table = geometry_ops.texture_table(obj)
        ordinals = [texture_module.custom_ordinal(r["id"]) for r in table]
        check(ordinals == [1, 2],
              "the table moved up one, as the empty material asks (%r)" % ordinals)
        wanted = {index: os.path.normcase(os.path.normpath(own[ordinal].png))
                  for index, ordinal in enumerate(ordinals)}
        shown = _shown_pictures(obj)
        check(shown == wanted,
              "and each material shows its own entry's picture, not the one "
              "the entry held before the move (%r)" % shown)

        # Moved again, and exported without converting: the export rebuilds.
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(third, "moving.blend"))
        settings = bpy.context.scene.dkr
        settings.track_name = "Moving"
        settings.track_id = "moving"
        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
        result = bpy.ops.dkr.export_dkrmap(
            filepath=os.path.join(third, "moving.dkrmap"), validate_first=False)
        check(result == {"FINISHED"},
              "a scene moved again exports, rebuilding what it left (%r)" % (result,))
        check(not custom_ops.missing(bpy.context),
              "and nothing is missing afterwards")

        # Nothing left to rebuild from: the entry shows no picture, not a stale one.
        shutil.rmtree(custom_ops.folder(bpy.context))
        bpy.data.images.remove(bpy.data.images["blue.png"])
        check(bpy.ops.dkr.restore_custom_textures() == {"FINISHED"},
              "the panel's button rebuilds what it can")
        lost = [e.name for e in custom_ops.missing(bpy.context)]
        check(lost == ["blue"],
              "and leaves missing only the one whose picture is gone (%r)" % lost)
        shown = _shown_pictures(obj)
        check(shown.get(0) is not None and shown.get(1) is None,
              "whose material then shows no picture rather than its old one (%r)"
              % shown)
    finally:
        fresh()
        for directory in (first, second, third):
            shutil.rmtree(directory, ignore_errors=True)


def test_track_from_mesh_survives_ctrl_j():
    """Pieces joined with Ctrl+J keep their mapping, whatever their maps were called.

    Joining matches UV maps by name, so pieces whose maps were named apart come
    out with one map per name and each piece mapped in only one of them.
    Reading the active map alone would flatten the other piece - every face one
    texel - and say nothing.
    """
    print("a mesh joined from differently mapped pieces")
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-ctrl-j-")
    try:
        bpy.ops.wm.save_as_mainfile(
            filepath=os.path.join(temporary, "joined.blend")
        )
        left_image = _write_probe_image(temporary, "left", 200, 120)
        right_image = _write_probe_image(temporary, "right", 120, 200)

        bpy.ops.mesh.primitive_grid_add(size=2000.0, x_subdivisions=2,
                                        y_subdivisions=2,
                                        location=(-1500.0, 0.0, 0.0))
        left = bpy.context.active_object
        left.data.materials.append(_image_material("Left", left_image))
        bpy.ops.mesh.primitive_grid_add(size=2000.0, x_subdivisions=2,
                                        y_subdivisions=2,
                                        location=(1500.0, 0.0, 0.0))
        right = bpy.context.active_object
        right.data.materials.append(_image_material("Right", right_image))
        right.data.uv_layers.active.name = "Scanned UVs"

        for other in bpy.context.scene.objects:
            other.select_set(False)
        left.select_set(True)
        right.select_set(True)
        bpy.context.view_layer.objects.active = left
        bpy.ops.object.join()
        joined = bpy.context.active_object
        maps = [layer.name for layer in joined.data.uv_layers]
        print("  Ctrl+J left %d UV map(s): %s" % (len(maps), ", ".join(maps)))

        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=False)
        check(result == {"FINISHED"}, "the joined mesh converts (%r)" % (result,))
        check(len(custom_ops.entries(bpy.context)) == 2,
              "both pieces' pictures came along")

        built = geometry_ops.geometry_objects(bpy.context)
        check(len(built) == 1, "the track was imported back")
        if not built:
            return
        tri = built[0].data
        raw = [0] * (len(tri.loops) * 2)
        tri.attributes[geometry_ops.ATTR_UV].data.foreach_get("value", raw)
        flat = sum(
            1 for polygon in tri.polygons
            if len({(raw[l * 2], raw[l * 2 + 1])
                    for l in polygon.loop_indices}) == 1
        )
        check(flat == 0,
              "no face of either piece lost its mapping (%d came out as a "
              "single texel)" % flat)
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_custom_texture_reaches_the_package():
    """A picture the ROM never had comes out of the export as a loadable texture.

    This is the whole feature end to end, and it is checked at the far end -
    the bytes in the package - rather than at the operator's own bookkeeping.
    Three things have to line up and none of them is visible from any one of
    them alone: the mesh's table entry names the sentinel id, the manifest adds
    a ``TEXTURES_3D`` payload at that ordinal, and the payload is a
    ``TextureHeader`` the game's ``load_texture`` would accept.

    Nothing here needs an extraction. That is not incidental - a track can now
    be textured entirely with artwork its author brought, so the path has to
    work on a machine that has never seen a ROM.
    """
    print("a custom texture reaches the package")
    from dkr_track_editor import level_model, textures as texture_module
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-custom-tex-")
    try:
        bpy.ops.wm.save_as_mainfile(
            filepath=os.path.join(temporary, "ownart.blend")
        )
        source = _write_probe_image(temporary, "probe", 200, 120)

        bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=3,
                                        y_subdivisions=3)
        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=False)
        check(result == {"FINISHED"},
              "a track builds from a mesh with no textures (%r)" % (result,))

        built = geometry_ops.geometry_objects(bpy.context)
        check(len(built) == 1, "there is one piece of track geometry")
        if not built:
            return
        obj = built[0]

        # -- import ------------------------------------------------------
        settings = bpy.context.scene.dkr
        result = bpy.ops.dkr.add_custom_texture(
            filepath=source,
            texture_format=str(texture_module.FORMAT_CODES["RGBA16"]),
            size="",
        )
        check(result == {"FINISHED"}, "the image imports (%r)" % (result,))
        check(len(settings.custom_textures) == 1,
              "the scene holds one texture of its own")
        if not len(settings.custom_textures):
            return

        record = settings.custom_textures[0]
        check((record.width, record.height) == (64, 32),
              "200x120 was resampled to the largest colour texture that fits "
              "texture memory, in the shape closest to the picture's (got "
              "%dx%d)" % (record.width, record.height))
        check(not os.path.isabs(record.png),
              "the PNG is remembered relative to the .blend, so the scene and "
              "its pictures move together (%r)" % record.png)
        check(os.path.isfile(custom_ops.resolve(record.png)),
              "and it resolves to a file that is there")
        check(custom_ops.FOLDER in record.png.replace("\\", "/"),
              "in the folder the exporter looks in")

        own = custom_ops.entries(bpy.context)
        check(len(own) == 1 and own[0].index == texture_module.CUSTOM_ID_BASE,
              "and it is the track's texture number one, id 0x%04X"
              % texture_module.CUSTOM_ID_BASE)
        check(int(settings.texture_id) == texture_module.CUSTOM_ID_BASE,
              "the browser selected it, so the next click is Apply")

        # -- apply -------------------------------------------------------
        mesh = obj.data
        drawn = [polygon.index for polygon in mesh.polygons]
        _select_faces(mesh, drawn)
        settings.texture_mapping = "PROJECT"
        settings.texture_surface = "0"
        result = bpy.ops.dkr.apply_texture()
        check(result == {"FINISHED"}, "it applies to faces (%r)" % (result,))

        extras = geometry_ops.extra_textures(obj)
        check(len(extras) == 1, "one table entry was added (%d)" % len(extras))
        if not extras:
            return
        check(extras[0]["id"] == texture_module.CUSTOM_ID_BASE,
              "and it names the sentinel, not a ROM index (0x%X)"
              % extras[0]["id"])
        check((extras[0]["w"], extras[0]["h"]) == (64, 32),
              "with the size the texture actually is")

        # -- export ------------------------------------------------------
        settings.track_name = "Own Art"
        settings.track_id = "own-art"
        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
        target = os.path.join(temporary, "own-art.dkrmap")
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
        check(result == {"FINISHED"}, "the package exports (%r)" % (result,))

        manifest_path = os.path.join(target, "manifest.json")
        check(os.path.isfile(manifest_path), "there is a manifest")
        if not os.path.isfile(manifest_path):
            return
        with open(manifest_path, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)

        textures_added = [entry for entry in manifest["adds"]
                          if entry["section"] == "TEXTURES_3D"]
        check(len(textures_added) == 1,
              "the manifest adds one TEXTURES_3D payload (%d)"
              % len(textures_added))
        if not textures_added:
            return
        check(textures_added[0]["file"] == "textures/0.bin",
              "named by its ordinal, which is what the runtime numbers by "
              "(%r)" % textures_added[0]["file"])

        payload_path = os.path.join(target, "textures", "0.bin")
        check(os.path.isfile(payload_path), "and the payload is on disk")
        if not os.path.isfile(payload_path):
            return
        with open(payload_path, "rb") as handle:
            payload = handle.read()

        # The header the game reads, checked as the game reads it.
        check(payload[0x00] == 64 and payload[0x01] == 32,
              "the payload declares 64x32 (%dx%d)" % (payload[0], payload[1]))
        check(payload[0x02] & 0xF == texture_module.FORMAT_CODES["RGBA16"],
              "in RGBA16 (format byte 0x%02X)" % payload[0x02])
        check(payload[0x05] == 1, "numberOfInstances is 1")
        check(struct.unpack_from(">H", payload, 0x12)[0] >> 8 == 1,
              "load_texture would read one frame")
        check(payload[0x1D] == 0, "and would not try to decompress it")
        check(len(payload) == 32 + 64 * 32 * 2,
              "the payload is header plus image (%d)" % len(payload))
        check(len(payload) % 16 == 0,
              "and 16-aligned, so the display list fits its allocation")

        # The model has to name the same texture the package ships.
        model_path = os.path.join(target, "model.bin")
        check(os.path.isfile(model_path), "the geometry shipped too")
        if not os.path.isfile(model_path):
            return
        model = level_model.load(model_path)
        ids = [texture.texture_id for texture in model.textures]
        check(texture_module.CUSTOM_ID_BASE in ids,
              "and its texture table names the sentinel the runtime rewrites "
              "(%r)" % ids)

        # A package that claims a payload it does not have fails at load with a
        # far worse message than this one.
        for entry in manifest["adds"]:
            check(os.path.isfile(os.path.join(target, entry["file"])),
                  "the manifest claims %s and it is there" % entry["file"])
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_custom_texture_refuses_what_the_hardware_cannot_draw():
    """The two sizes that produce a broken track are refused at import.

    Both fail silently in game - a texture too large for texture memory draws
    corrupt, and one too large to wrap stretches once across each face instead
    of tiling - so both have to be caught here, where there is still something
    to say about them.
    """
    print("a custom texture the RDP cannot load is refused")
    from dkr_track_editor import textures as texture_module

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-custom-tex-limits-")
    try:
        bpy.ops.wm.save_as_mainfile(
            filepath=os.path.join(temporary, "limits.blend")
        )
        source = _write_probe_image(temporary, "probe", 64, 64)
        colour = str(texture_module.FORMAT_CODES["RGBA16"])
        settings = bpy.context.scene.dkr

        refusal = _refused(lambda: bpy.ops.dkr.add_custom_texture(
            filepath=source, texture_format=colour, size="64x64"
        ))
        check(refusal is not None and "texture memory" in refusal,
              "64x64 in colour is 8KB into 4KB of texture memory (%r)"
              % refusal)
        check(len(settings.custom_textures) == 0,
              "and nothing was added to the track")

        refusal = _refused(lambda: bpy.ops.dkr.add_custom_texture(
            filepath=source, texture_format=colour, size="48x32"
        ))
        check(refusal is not None and "power of two" in refusal,
              "48 is not a power of two, so it would clamp (%r)" % refusal)

        refusal = _refused(lambda: bpy.ops.dkr.add_custom_texture(
            filepath=source, texture_format=colour, size="128x16"
        ))
        check(refusal is not None and "clamp" in refusal,
              "128 is past the largest side that can wrap (%r)" % refusal)

        # The same picture in an eight-bit format does fit at 64x64, which is
        # the trade the format menu exists to offer.
        result = bpy.ops.dkr.add_custom_texture(
            filepath=source,
            texture_format=str(texture_module.FORMAT_CODES["I8"]),
            size="64x64",
        )
        check(result == {"FINISHED"},
              "the same size in I8 is exactly texture memory (%r)" % (result,))
        check(len(settings.custom_textures) == 1,
              "and it is the only one that was added")

        # Nothing that failed may leave a PNG behind: the next import would
        # number around it and the folder would fill with dead pictures. That
        # goes for the full-resolution copies kept under original/ as well.
        folder = os.path.join(temporary, "dkr_textures")
        written = sorted(
            name for name in (os.listdir(folder) if os.path.isdir(folder) else [])
            if os.path.isfile(os.path.join(folder, name))
        )
        check(len(written) == 1,
              "a refused import leaves no file behind (%r)" % written)
        originals = os.path.join(folder, "original")
        kept = sorted(os.listdir(originals)) if os.path.isdir(originals) else []
        check(kept == written,
              "and keeps an original only for the one that was added (%r)" % kept)
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


def test_custom_texture_removal_keeps_the_numbering_honest():
    """Removing one moves the ids behind it, and stops if the table holds it.

    Both halves matter and neither is visible in the panel. Ids are positions,
    so a removal that did not renumber would repaint the track by one; and a
    removal that dropped a *table* entry would move every index after it, which
    nothing else in the addon is prepared for.
    """
    print("removing a custom texture")
    from dkr_track_editor import textures as texture_module
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-custom-tex-remove-")
    try:
        bpy.ops.wm.save_as_mainfile(
            filepath=os.path.join(temporary, "removing.blend")
        )
        colour = str(texture_module.FORMAT_CODES["RGBA16"])
        first = _write_probe_image(temporary, "first", 64, 32)
        second = _write_probe_image(temporary, "second", 64, 32)
        third = _write_probe_image(temporary, "third", 64, 32)

        bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=3,
                                        y_subdivisions=3)
        bpy.ops.dkr.track_from_mesh_blank(keep_source=False)
        built = geometry_ops.geometry_objects(bpy.context)
        check(len(built) == 1, "the track built")
        if not built:
            return
        obj = built[0]

        for path in (first, second, third):
            bpy.ops.dkr.add_custom_texture(filepath=path,
                                           texture_format=colour, size="64x32")
        settings = bpy.context.scene.dkr
        check(len(settings.custom_textures) == 3, "three textures were added")

        # Give the third one a table entry, so the removal of the first has to
        # move an id the mesh is already holding.
        _select_faces(obj.data, [polygon.index for polygon in obj.data.polygons])
        settings.texture_id = texture_module.custom_id(2)
        settings.texture_mapping = "PROJECT"
        bpy.ops.dkr.apply_texture()
        extras = geometry_ops.extra_textures(obj)
        check(len(extras) == 1 and extras[0]["id"] == texture_module.custom_id(2),
              "the third texture has the table entry (%r)"
              % [record.get("id") for record in extras])

        # Removing the one the table holds is refused, and says which mesh.
        settings.texture_id = texture_module.custom_id(2)
        refusal = _refused(bpy.ops.dkr.remove_custom_texture)
        check(refusal is not None and "place in" in refusal,
              "a texture the geometry has a table entry for cannot be removed "
              "(%r)" % refusal)
        check(len(settings.custom_textures) == 3, "and nothing was removed")

        # Removing the first is allowed, and the third becomes the second.
        settings.texture_id = texture_module.custom_id(0)
        result = bpy.ops.dkr.remove_custom_texture()
        check(result == {"FINISHED"}, "the first one goes (%r)" % (result,))
        check(len(settings.custom_textures) == 2, "two are left")

        extras = geometry_ops.extra_textures(obj)
        check(extras and extras[0]["id"] == texture_module.custom_id(1),
              "and the table entry moved down with it, from 2 to 1 (%r)"
              % [record.get("id") for record in extras])
        own = custom_ops.entries(bpy.context)
        check([entry.name for entry in own] == ["second", "third"],
              "the survivors kept their order (%r)"
              % [entry.name for entry in own])
        check(own[1].index == texture_module.custom_id(1),
              "and the one the mesh points at is the one it meant")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        fresh()


class _OperatorProperties:
    """Check properties assigned to the button returned by layout.operator."""

    def __init__(self, idname, rna):
        object.__setattr__(self, "_idname", idname)
        object.__setattr__(self, "_rna", rna)

    def __setattr__(self, name, value):
        if name not in self._rna.properties:
            raise AttributeError("%s has no property %r" % (self._idname, name))
        object.__setattr__(self, name, value)


class _Layout:
    """A stand-in for a panel's layout that checks what a panel asks it for.

    Blender draws nothing in the background, so a panel's ``draw`` is only
    reachable by calling it; this makes a missing property or operator fail
    the way it would in the sidebar, and records what was drawn.
    """

    def __init__(self, drawn=None):
        self.drawn = drawn if drawn is not None else []
        self.active = True
        self.alert = False
        self.enabled = True
        self.alignment = "EXPAND"
        self.scale_y = 1.0

    def _child(self, *args, **kwargs):
        return _Layout(self.drawn)

    row = column = box = split = grid_flow = column_flow = _child

    def label(self, text="", **kwargs):
        self.drawn.append(("label", text))

    def separator(self, **kwargs):
        pass

    def template_icon(self, **kwargs):
        pass

    def prop(self, owner, name, **kwargs):
        if not hasattr(owner, name):
            raise AttributeError("the panel draws %r, which %r does not have"
                                 % (name, owner))
        self.drawn.append(("prop", name))

    def _operator_type(self, idname):
        module, name = idname.split(".")
        operator = getattr(getattr(bpy.ops, module), name)
        return operator.get_rna_type()

    def operator(self, idname, **kwargs):
        rna = self._operator_type(idname)
        self.drawn.append(("operator", idname))
        return _OperatorProperties(idname, rna)

    def operator_menu_enum(self, idname, prop, **kwargs):
        rna = self._operator_type(idname)
        if prop not in rna.properties:
            raise AttributeError("%s has no property %r" % (idname, prop))
        if rna.properties[prop].type != "ENUM":
            raise TypeError("%s.%s is not an enum" % (idname, prop))
        self.drawn.append(("menu", idname))


def _draw_panel(panel):
    layout = _Layout()
    panel.draw(types.SimpleNamespace(layout=layout), bpy.context)
    return layout.drawn


def _write_alpha_image(directory, name, width, height, kind):
    """A picture with transparency: ``"holes"`` has hard holes, ``"soft"`` fades.

    The holes are a checker of solid and clear cells with a one-pixel
    half-covered rim on one side, the way an anti-aliased brush leaves them;
    the fade runs alpha across the whole width.
    """
    image = bpy.data.images.new(name, width, height, alpha=True)
    pixels = [0.0] * (width * height * 4)
    for row in range(height):
        for column in range(width):
            at = (row * width + column) * 4
            pixels[at] = 0.8
            pixels[at + 1] = 0.4
            pixels[at + 2] = 0.1
            if kind == "soft":
                alpha = column / max(1, width - 1)
            else:
                cell = ((column * 4) // width + (row * 4) // height) % 2
                alpha = 1.0 if cell == 0 else 0.0
                if cell == 1 and (column * 4) % width == 0:
                    alpha = 0.5
            pixels[at + 3] = alpha
    image.pixels.foreach_set(pixels)
    path = os.path.join(directory, name + ".png")
    image.file_format = "PNG"
    image.filepath_raw = path
    image.save()
    bpy.data.images.remove(image)
    return path


def _read_opaque(mesh):
    values = [False] * len(mesh.polygons)
    mesh.attributes["dkr_opaque"].data.foreach_get("value", values)
    return values


def _batches_of(model, texture_index):
    """``[(segment, batch index, batch), ...]`` drawing one table entry."""
    return [(segment, index, batch)
            for segment in model.segments
            for index, batch in enumerate(segment.batches)
            if batch.texture_index == texture_index]


def test_transparent_rom_texture_is_drawn():
    """A see-through texture from the ROM lands in the pass that draws it.

    The bug this closes: a road retextured with the ROM's water kept the opaque
    side of ``numberofOpaqueBatches``, and ``render_level_segment`` never draws
    a see-through batch there - nor, being outside its range, in the second
    pass. The face vanished in game.
    """
    print("a see-through ROM texture is drawn")
    from dkr_track_editor import level_model, level_model_encoder, transparency
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return
    catalogue = _texture_catalogue(bpy.context)
    if not catalogue:
        print("  skip: no extracted textures")
        return

    base = level_model.load(path)
    used = {texture.texture_id for texture in base.textures}
    glass = next((e for e in catalogue if e.translucent and not e.animated
                  and e.format == 0 and e.index not in used), None)
    solid = next((e for e in catalogue if not e.translucent and not e.animated
                  and e.format == 1 and e.index not in used), None)
    if glass is None or solid is None:
        print("  skip: no unused see-through and solid textures")
        return

    mesh = obj.data
    flags = _read_flags(mesh)
    opaque = _read_opaque(mesh)
    road = [index for index, value in enumerate(flags)
            if geometry_ops.category_of(geometry_ops.to_unsigned32(value))
            == geometry_ops.SURFACE and opaque[index]][:10]
    check(len(road) > 2, "there are solid road faces to retexture (%d)" % len(road))
    if len(road) < 3:
        return
    _select_faces(mesh, road)

    settings = bpy.context.scene.dkr
    settings.texture_id = glass.index
    settings.texture_mapping = "KEEP"
    settings.texture_transparency = "AUTO"
    result = bpy.ops.dkr.apply_texture()
    check(result == {"FINISHED"}, "the see-through texture applies (%r)" % (result,))

    opaque = _read_opaque(mesh)
    check(not any(opaque[index] for index in road),
          "the faces moved to the see-through side")
    flags = _read_flags(mesh)
    check(not any(geometry_ops.to_unsigned32(flags[index])
                  & transparency.RENDER_CUTOUT for index in road),
          "and blend rather than cut out, which is how the texture was made")
    slot = mesh.polygons[road[0]].material_index
    check(mesh.materials[slot].get(geometry_ops.PROP_LOOK) == transparency.BLEND,
          "the material shows the picture blended")

    added = len(base.textures)
    edit = geometry_export.build_edited_model(bpy.context)
    drawing = _batches_of(edit.model, added)
    check(drawing and all(index >= segment.opaque_batches
                          for segment, index, _b in drawing),
          "every batch drawing it is in the second pass (%d batches)"
          % len(drawing))
    payload = level_model_encoder.pack(edit.model)
    again = level_model.parse(level_model.decompress(payload))
    check(all(index >= segment.opaque_batches
              for segment, index, _b in _batches_of(again, added)),
          "and stays there through the file")

    # Cut out instead: the faces' own choice.
    result = bpy.ops.dkr.set_face_transparency(look="CUTOUT")
    check(result == {"FINISHED"}, "the faces can be cut out (%r)" % (result,))
    flags = _read_flags(mesh)
    check(all(geometry_ops.to_unsigned32(flags[index])
              & transparency.RENDER_CUTOUT for index in road),
          "and carry RENDER_CUTOUT")
    edit = geometry_export.build_edited_model(bpy.context)
    drawing = _batches_of(edit.model, added)
    check(drawing and all(batch.flags & transparency.RENDER_CUTOUT
                          and index >= segment.opaque_batches
                          for segment, index, batch in drawing),
          "a cut-out over a see-through texture is still drawn second, as "
          "retail's are")

    # A blend is not something a solid texture can be.
    settings.texture_id = solid.index
    settings.texture_transparency = "BLEND"
    settings.texture_mapping = "PROJECT"
    result = bpy.ops.dkr.apply_texture()
    check(result == {"FINISHED"}, "a solid texture applies over them (%r)" % (result,))
    opaque = _read_opaque(mesh)
    check(all(opaque[index] for index in road),
          "and the faces are back on the solid side")
    flags = _read_flags(mesh)
    check(not any(geometry_ops.to_unsigned32(flags[index])
                  & transparency.RENDER_CUTOUT for index in road),
          "asking a solid texture to blend leaves it solid, not cut out")
    refused = _refused(lambda: bpy.ops.dkr.set_face_transparency(look="BLEND"))
    check(refused is not None and "only be" in refused,
          "and making the faces blend is refused with the reason (%s)" % refused)
    edit = geometry_export.build_edited_model(bpy.context)
    solid_index = len(base.textures) + 1
    drawing = _batches_of(edit.model, solid_index)
    check(drawing and all(index < segment.opaque_batches
                          for segment, index, _b in drawing),
          "the solid texture's batches are drawn in the first pass")

    # Opacity rides on Apply, whatever the texture: a ROM one here.
    settings.texture_opacity = 0.5
    result = bpy.ops.dkr.apply_texture()
    check(result == {"FINISHED"}, "the texture applies half faded (%r)" % (result,))
    material = mesh.materials[mesh.polygons[road[0]].material_index]
    check(abs(geometry_ops.opacity_of(material) - 0.5) < 1e-6,
          "and its material carries the panel's opacity (%r)"
          % geometry_ops.opacity_of(material))
    check(geometry_ops.OPACITY_NODE in material.node_tree.nodes,
          "which the viewport shows")
    settings.texture_opacity = 0.3
    node = material.node_tree.nodes.get(geometry_ops.OPACITY_NODE)
    check(abs(geometry_ops.opacity_of(material) - 0.3) < 1e-6
          and node is not None and abs(node.inputs[1].default_value - 0.3) < 1e-6,
          "moving the slider fades the picked texture's faces live, no Apply")
    bpy.ops.dkr.pick_texture(index=glass.index)
    settings.texture_opacity = 1.0
    check(abs(geometry_ops.opacity_of(material) - 0.3) < 1e-6,
          "with another texture picked the slider leaves this one alone")
    bpy.ops.dkr.pick_texture(index=solid.index)
    check(abs(settings.texture_opacity - 0.3) < 1e-6,
          "picking a texture the track draws brings its opacity to the slider "
          "(%r)" % settings.texture_opacity)
    settings.texture_opacity = 0.5
    settings.texture_opacity = 1.0
    result = bpy.ops.dkr.apply_texture()
    check(result == {"FINISHED"}
          and geometry_ops.opacity_of(material) == 1.0
          and geometry_ops.OPACITY_NODE not in material.node_tree.nodes,
          "applying again at full takes the fade back off")
    fresh()


def test_custom_texture_transparency():
    """A picture with alpha keeps it, from the import to the bytes the game reads."""
    print("a custom texture with transparency")
    from dkr_track_editor import dkrmap, transparency
    from dkr_track_editor import textures as texture_module
    from dkr_track_editor.operators import custom_textures as custom_ops
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export
    from dkr_track_editor.operators import pack as pack_ops

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-alpha-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary, "alpha.blend"))
        holes = _write_alpha_image(temporary, "fence", 96, 48, "holes")
        soft = _write_alpha_image(temporary, "smoke", 40, 40, "soft")
        flat = _write_probe_image(temporary, "flat", 60, 30)

        bpy.ops.mesh.primitive_grid_add(size=4000.0, x_subdivisions=3,
                                        y_subdivisions=3)
        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=False)
        check(result == {"FINISHED"}, "a track builds from a plain mesh")
        obj = geometry_ops.geometry_objects(bpy.context)[0]
        settings = bpy.context.scene.dkr
        rgba16 = str(texture_module.FORMAT_CODES["RGBA16"])

        for source, expected in ((holes, "CUTOUT"), (soft, "BLEND"),
                                 (flat, "OPAQUE")):
            result = bpy.ops.dkr.add_custom_texture(
                filepath=source, texture_format=rgba16, size="",
                transparency="AUTO")
            check(result == {"FINISHED"}, "%s imports" % os.path.basename(source))
            record = settings.custom_textures[-1]
            check(record.transparency == expected,
                  "%s reads as %s (%s)" % (os.path.basename(source), expected,
                                           record.transparency))
        own = custom_ops.entries(bpy.context)
        check([entry.render_mode for entry in own]
              == ["TRANSPARENT", "TRANSPARENT", "OPAQUE"],
              "and each is written with the render mode its look needs")

        fence = own[0]
        settings.texture_id = fence.index
        settings.texture_transparency = "AUTO"
        settings.texture_mapping = "PROJECT"
        from dkr_track_editor.ui import panels
        drawn = _draw_panel(panels.DKR_PT_textures)
        check(("menu", "dkr.set_texture_transparency") in drawn
              and ("prop", "texture_transparency") in drawn
              and ("menu", "dkr.set_face_transparency") in drawn,
              "the Textures panel offers the transparency controls")
        check(("prop", "texture_opacity") in drawn
              and drawn.index(("prop", "texture_opacity")) + 1
              == drawn.index(("prop", "texture_transparency")),
              "with the opacity right above the transparency")
        check(("label", "Made cut out") in drawn,
              "and says how the chosen texture is made")
        check(any(kind == "operator" and text == "dkr.add_water"
                  for kind, text in _draw_panel(panels.DKR_PT_water)),
              "a track with no water still gets a Water panel with Add Water")
        mesh = obj.data
        _select_faces(mesh, range(len(mesh.polygons)))
        result = bpy.ops.dkr.apply_texture()
        check(result == {"FINISHED"}, "the fence applies (%r)" % (result,))
        flags = _read_flags(mesh)
        check(all(geometry_ops.to_unsigned32(value) & transparency.RENDER_CUTOUT
                  for value in flags), "every face is cut out")
        check(not any(_read_opaque(mesh)), "and drawn in the second pass")

        edit = geometry_export.build_edited_model(bpy.context)
        index = len(geometry_ops.base_textures(obj))
        drawing = _batches_of(edit.model, index)
        check(drawing and all(batch.flags & transparency.RENDER_CUTOUT
                              and position >= segment.opaque_batches
                              for segment, position, batch in drawing),
              "the exported batches are cut out and drawn second (%d)"
              % len(drawing))

        payload = fence.encode()
        check(payload[2] >> 4 == 0 and payload[2] & 0xF == 1,
              "the payload is TRANSPARENT RGBA16")
        alpha_bits = [payload[32 + i * 2 + 1] & 1
                      for i in range(fence.width * fence.height)]
        check(0 in alpha_bits and 1 in alpha_bits,
              "and keeps both holes and solid texels")

        # The HD copy is hardened at the same half the payload was.
        original = custom_ops.original_for(bpy.context, fence.ordinal)
        hd = pack_ops._hd_picture(types.SimpleNamespace(report=lambda *a: None),
                                  fence, original)
        check(hd != original and os.path.isfile(hd),
              "the HD pack carries a hardened copy of a cut-out's original")
        image = bpy.data.images.load(hd, check_existing=False)
        alphas = set(round(value, 3) for value in list(image.pixels)[3::4])
        bpy.data.images.remove(image)
        check(alphas <= {0.0, 1.0}, "whose alpha is all or nothing (%s)"
              % sorted(alphas)[:4])

        # The texture's own look changes, and its faces follow.
        result = bpy.ops.dkr.set_texture_transparency(look="OPAQUE")
        check(result == {"FINISHED"}, "the fence can be made opaque")
        check(not any(geometry_ops.to_unsigned32(value)
                      & transparency.RENDER_CUTOUT for value in _read_flags(mesh)),
              "its faces lose the cut-out")
        check(all(_read_opaque(mesh)), "and move to the first pass")
        check(custom_ops.entries(bpy.context)[0].encode()[2] >> 4 == 1,
              "and the payload is written OPAQUE")
        result = bpy.ops.dkr.set_texture_transparency(look="BLEND")
        check(result == {"FINISHED"} and not any(_read_opaque(mesh)),
              "made blended, its faces go back to the second pass")

        smoke = own[1]
        check(transparency.advice(smoke.transparency, smoke.format) is not None,
              "a soft picture in RGBA16 is told that its blend is all or nothing")

        # And out through the package: the manifest carries all three.
        package = dkrmap.TrackPackage(
            directory=os.path.join(temporary, "alpha.dkrmap"),
            track_id="alpha", name="Alpha")
        payloads = package.encode_textures(custom_ops.entries(bpy.context))
        check([p[2] >> 4 for p in payloads] == [0, 0, 1],
              "every payload's render mode matches its look")
    finally:
        fresh()
        shutil.rmtree(temporary, ignore_errors=True)


def test_track_from_mesh_keeps_alpha():
    """A mesh textured with a cut-out converts into cut-out track geometry."""
    print("track from mesh keeps a picture's alpha")
    from dkr_track_editor import level_model, transparency
    from dkr_track_editor.operators import geometry as geometry_ops

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-alpha-mesh-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary, "leaf.blend"))
        holes = _write_alpha_image(temporary, "leaves", 64, 64, "holes")
        bpy.ops.mesh.primitive_grid_add(size=3000.0, x_subdivisions=2,
                                        y_subdivisions=2)
        source = bpy.context.active_object
        source.data.materials.append(_image_material("leaves", holes))
        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=False,
                                                   keep_textures=True)
        check(result == {"FINISHED"}, "the mesh converts (%r)" % (result,))
        record = bpy.context.scene.dkr.custom_textures[0]
        check(record.transparency == "CUTOUT", "its picture reads as a cut-out")
        path = bpy.context.scene.dkr.geometry_path
        model = level_model.load(path)
        batches = [(segment, index, batch) for segment in model.segments
                   for index, batch in enumerate(segment.batches)]
        check(batches and all(batch.flags & transparency.RENDER_CUTOUT
                              and index >= segment.opaque_batches
                              for segment, index, batch in batches),
              "and every batch in the model it wrote is cut out and drawn "
              "second (%d)" % len(batches))
        obj = geometry_ops.geometry_objects(bpy.context)[0]
        check(not any(_read_opaque(obj.data)),
              "the imported mesh agrees")
    finally:
        fresh()
        shutil.rmtree(temporary, ignore_errors=True)


def test_track_from_mesh_keeps_opacity():
    """A material made see-through in Blender stays see-through on the track.

    The Principled BSDF's Alpha is a fade over the whole material - glass - and
    the track's material takes it as its opacity, which the export writes.
    """
    print("track from mesh keeps a material's opacity")
    from dkr_track_editor import transparency
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export

    fresh()
    temporary = tempfile.mkdtemp(prefix="dkr-glass-mesh-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary, "glass.blend"))
        solid = _write_alpha_image(temporary, "glass", 32, 32, "soft")
        bpy.ops.mesh.primitive_grid_add(size=3000.0, x_subdivisions=2,
                                        y_subdivisions=2)
        source = bpy.context.active_object
        material = _image_material("glass", solid)
        shader = next(n for n in material.node_tree.nodes
                      if n.type == "BSDF_PRINCIPLED")
        shader.inputs["Alpha"].default_value = 0.4
        source.data.materials.append(material)
        result = bpy.ops.dkr.track_from_mesh_blank(keep_source=False,
                                                   keep_textures=True)
        check(result == {"FINISHED"}, "the mesh converts (%r)" % (result,))
        obj = geometry_ops.geometry_objects(bpy.context)[0]
        faded = [m for m in obj.data.materials if m is not None
                 and abs(geometry_ops.opacity_of(m) - 0.4) < 1e-6]
        check(faded, "its material comes out at the Alpha it was given")
        check(all(geometry_ops.OPACITY_NODE in m.node_tree.nodes for m in faded),
              "and the viewport shows it faded")
        edit = geometry_export.build_edited_model(bpy.context)
        alpha = transparency.opacity_byte(0.4)
        colours = [tuple(c) for segment in edit.model.segments
                   for c in segment.colours]
        check(colours and all(c == (1, 1, alpha, 255) for c in colours),
              "and the export fades every vertex to %d" % alpha)
    finally:
        fresh()
        shutil.rmtree(temporary, ignore_errors=True)


def _faces_with(mesh, predicate):
    from dkr_track_editor.operators import geometry as geometry_ops

    return [index for index, value in enumerate(_read_flags(mesh))
            if predicate(geometry_ops.to_unsigned32(value))]


def _delete_faces(obj, faces):
    import bmesh

    edit = bmesh.new()
    try:
        edit.from_mesh(obj.data)
        edit.faces.ensure_lookup_table()
        bmesh.ops.delete(edit, geom=[edit.faces[i] for i in sorted(faces)],
                         context="FACES")
        edit.to_mesh(obj.data)
    finally:
        edit.free()
    obj.data.update()


def test_waves_on_ancient_lake():
    """Ancient Lake's still lake becomes a wavy one, and stays one.

    The whole path an author takes: remove the calm water, lay waves at its
    height, and then do the things that used to wipe them out - re-segment,
    and delete the water the waves are sized from.
    """
    print("waves on Ancient Lake")
    from dkr_track_editor import level_model, water
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import geometry_export
    from dkr_track_editor.operators import pack as pack_ops

    path, obj = _import_lake(include_hidden=True)
    if path is None:
        print("  skip: no extracted level models")
        return
    if not _texture_catalogue(bpy.context):
        print("  skip: no extracted textures")
        return
    temporary = tempfile.mkdtemp(prefix="dkr-waves-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary, "lake.blend"))
        calm = _faces_with(obj.data, lambda f: water.is_water(f))
        check(calm, "Ancient Lake has calm water (%d faces)" % len(calm))
        check(bpy.ops.dkr.remove_water(which="CALM") == {"FINISHED"},
              "the calm water is removed")
        check(not _faces_with(obj.data, lambda f: water.is_water(f)),
              "and none is left")

        result = bpy.ops.dkr.add_water(kind="WAVES", level=2.0, area="TRACK",
                                       tile=0, skip_dry=True)
        check(result == {"FINISHED"}, "waves are laid at the lake's height (%r)"
              % (result,))
        obj = geometry_ops.geometry_objects(bpy.context)[0]
        base = bpy.context.scene.dkr.geometry_path
        check(base.endswith("lake-geometry.bin"),
              "the track is its own base now (%s)" % os.path.basename(base))
        model = level_model.load(base)
        check(not water.problems(model), "the file is a valid wave grid (%s)"
              % water.problems(model)[:1])
        grid = water.simulate(model)
        tiles = sum(grid.wavy)
        check(tiles > 0, "with wave tiles (%d of %d segments)"
              % (tiles, len(model.segments)))
        check(len(model.segments) <= 127, "inside the segment limit")
        summary = geometry_ops.water_summary(obj)
        check(summary.get("tiles") == tiles and not summary.get("problems"),
              "the Water panel's summary agrees (%s)" % summary)
        from dkr_track_editor.ui import panels
        bpy.context.scene.dkr.show_wave_details = True
        drawn = _draw_panel(panels.DKR_PT_water)
        check(("menu", "dkr.wave_preset") in drawn
              and ("prop", "crest") in drawn and ("prop", "power") in drawn,
              "the Water panel draws the wave settings and the presets")
        check(any(kind == "label" and text.startswith("Waves: %d" % tiles)
                  for kind, text in drawn),
              "and says how many wave tiles there are")
        check(pack_ops.geometry_has_waves(bpy.context),
              "the export sees wave water on the mesh")

        edit = geometry_export.build_edited_model(bpy.context)
        check(not edit.rebuilt and edit.ships,
              "an unchanged export patches the new base and still ships it")
        check(not water.problems(edit.model), "and keeps the waves working")

        bpy.ops.dkr.wave_preset(preset="PIRATE")
        check(bpy.context.scene.dkr_water.power == 128,
              "a preset sets the waves' header bytes (%d)"
              % bpy.context.scene.dkr_water.power)
        bpy.context.scene.dkr_water.seed = 121
        check(bpy.context.scene.dkr_water.seed == 120,
              "the pattern length stays even")

        # Re-segmenting used to hand every segment hasWaves = 0.
        check(bpy.ops.dkr.resegment() == {"FINISHED"}, "the track re-segments")
        model = level_model.load(bpy.context.scene.dkr.geometry_path)
        check(sum(water.simulate(model).wavy) == tiles
              and not water.problems(model),
              "and keeps every wave tile (%d)" % sum(water.simulate(model).wavy))

        # Deleting the reference water leaves the tiles unsized.
        obj = geometry_ops.geometry_objects(bpy.context)[0]
        reference = _faces_with(obj.data, water.is_reference)
        check(reference, "the reference water is on the mesh (%d faces)"
              % len(reference))
        _delete_faces(obj, reference)
        edit = geometry_export.build_edited_model(bpy.context)
        check(any("grid again" in note for note in edit.notes),
              "the export notices and cuts the track again (%s)"
              % [n for n in edit.notes if "wave" in n][:1])
        check(not water.problems(edit.model)
              and water.simulate(edit.model).reference is not None,
              "and the waves have a new reference")
        check(sum(water.simulate(edit.model).wavy) == tiles - 1,
              "one tile fewer, since its water is gone")

        check(bpy.ops.dkr.remove_water(which="WAVES") == {"FINISHED"},
              "the waves are removed")
        edit = geometry_export.build_edited_model(bpy.context)
        check(not water.has_waves(edit.model),
              "and the export switches every tile's waves off")
    finally:
        fresh()
        shutil.rmtree(temporary, ignore_errors=True)


def test_waves_from_scratch():
    """A track modelled in Blender gets waves, and a header that can load them."""
    print("waves on a track from a mesh")
    from dkr_track_editor import level_header_template as template
    from dkr_track_editor import level_model, water
    from dkr_track_editor.operators import geometry as geometry_ops
    from dkr_track_editor.operators import header as header_ops
    from dkr_track_editor.operators import pack as pack_ops

    fresh()
    if not _texture_catalogue(bpy.context):
        print("  skip: no extracted textures")
        return
    temporary = tempfile.mkdtemp(prefix="dkr-waves-mesh-")
    try:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(temporary, "sea.blend"))
        bpy.ops.mesh.primitive_grid_add(size=6000.0, x_subdivisions=7,
                                        y_subdivisions=7)
        check(bpy.ops.dkr.track_from_mesh_blank(keep_source=False) == {"FINISHED"},
              "a flat track builds")
        refused = _refused(lambda: bpy.ops.dkr.add_water(
            kind="WAVES", level=-10.0, area="TRACK", tile=2000, skip_dry=True))
        check(refused is not None and "dry" in refused,
              "water under the ground is refused, and says why (%s)" % refused)

        result = bpy.ops.dkr.add_water(kind="WAVES", level=10.0, area="TRACK",
                                       tile=2000, skip_dry=True)
        check(result == {"FINISHED"}, "waves above the ground are laid (%r)"
              % (result,))
        model = level_model.load(bpy.context.scene.dkr.geometry_path)
        grid = water.simulate(model)
        check(sum(grid.wavy) == 9 and len(model.segments) == 9,
              "three by three tiles, each its own segment (%d wave, %d total)"
              % (sum(grid.wavy), len(model.segments)))
        check((grid.tile_w, grid.tile_h) == (2000, 2000),
              "sized as asked (%dx%d)" % (grid.tile_w, grid.tile_h))
        check(set(h for h, w in zip(grid.heights, grid.wavy) if w) == {10},
              "at the level asked for")
        water_entry = model.textures[-1]
        check(water_entry.width == 16 and water_entry.format & 0xF == 0,
              "drawn with retail's 16x16 RGBA32 water")

        refused = _refused(lambda: bpy.ops.dkr.add_water(
            kind="WAVES", level=10.0, area="TRACK", tile=1000))
        check(refused is not None and "same size" in refused,
              "a second tile size is refused (%s)" % refused)

        # The header a scratch track writes names the wave detail texture.
        bpy.ops.dkr.header_defaults()
        world = _pick("World")
        if world:
            bpy.context.scene[header_ops.key_for("/world")] = world
            document = pack_ops._authored_header(bpy.context)
            check(template.lookup(document, water.DETAIL_POINTER)
                  == water.DETAIL_TEXTURE,
                  "the authored header names the wave detail texture")
            check(template.lookup(document, "/waves/wave-power") == 256,
                  "and carries wave settings")
    finally:
        fresh()
        shutil.rmtree(temporary, ignore_errors=True)


def main():
    dkr_track_editor.register()
    try:
        test_registration()
        test_place()
        test_ai_from_curve()
        test_ai_limits()
        test_validation()
        test_level_type_flow()
        test_start_grid()
        test_presets_and_tooltips()
        test_refresh_keeps_grids()
        test_import_sets_level_type()
        test_race_ai()
        test_skybox()
        test_dkrmap_export()
        test_import_export_operators()
        test_geometry_import()
        test_wall_visibility()
        test_geometry_roundtrip()
        test_geometry_vertex_edit()
        test_geometry_refuses_orphans()
        test_geometry_reaches_chained_new_faces()
        test_geometry_schema_guard()
        test_geometry_batch_flags()
        test_geometry_add_geometry()
        test_geometry_across_segments()
        test_geometry_merge_by_distance()
        test_geometry_remove_geometry()
        test_geometry_rebuild_needs_the_whole_track()
        test_geometry_in_package()
        test_stale_object_maps()
        test_memory_budget()
        test_unusable_mesh_is_named()
        test_identified_fields_are_visible()
        test_header_from_scratch()
        test_header_reaches_the_package()
        test_surface_types()
        test_material_opacity()
        test_faded_water_is_drawn_last()
        test_faded_own_texture_is_drawn_last()
        test_resegment_makes_the_track_its_own_base()
        test_track_from_mesh()
        test_track_from_mesh_refuses_a_giant()
        test_apply_texture()
        test_apply_animated_texture()
        test_project_texture_onto_new_geometry()
        test_track_from_mesh_with_its_own_textures()
        test_track_from_mesh_keeps_material_textures()
        test_moved_blend_rebuilds_its_textures()
        test_track_from_mesh_survives_ctrl_j()
        test_texture_browser_pieces()
        test_texture_side_operators()
        test_custom_texture_reaches_the_package()
        test_custom_texture_refuses_what_the_hardware_cannot_draw()
        test_custom_texture_removal_keeps_the_numbering_honest()
        test_transparent_rom_texture_is_drawn()
        test_custom_texture_transparency()
        test_track_from_mesh_keeps_alpha()
        test_track_from_mesh_keeps_opacity()
        test_waves_on_ancient_lake()
        test_waves_from_scratch()
        test_scratch_track_ships_its_geometry()
        test_drop_to_surface()
        test_click_to_place()
        test_snapping_to_elements()
        test_place_shows_artwork()
        test_balloon_variants()
        test_artwork_at_game_size()
        test_renumber_checkpoints()
        test_slots()
        test_partial_export_is_safe()
        test_geometry_colour_edit()
        test_unpainted_colour_layer()
    finally:
        dkr_track_editor.unregister()

    print()
    if FAILURES:
        print("FAIL: %d check(s)" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: all operator checks")
    return 0


if __name__ == "__main__":
    # What vanishes under Blender is an **uncaught exception**, not sys.exit:
    # measured on 5.2, a bare sys.exit(3) exits 3 and so does sys.exit(main()),
    # while a NameError makes Blender print the traceback and still exit 0. So a
    # crashing test read as a pass, stopped the suite where it stood, and hid
    # every check after it - which is what this catch repairs.
    try:
        _code = main()
    except BaseException:  # noqa: BLE001 - the point is to report anything
        traceback.print_exc()
        _code = 1
    sys.exit(_code)
