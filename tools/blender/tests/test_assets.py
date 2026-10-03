"""Resolve every object type to its artwork, and decode every object model.

The preview system is only as good as the name chain behind it, so this walks
that chain for all 85 object types that appear in retail tracks and reports how
many resolve to a sprite, a mesh, or nothing. It also decodes all 390 extracted
object models and checks each is internally consistent.

Runs on any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_assets.py
"""

from __future__ import annotations

import collections
import glob
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import (  # noqa: E402
    assets, catalog as catalog_module, object_model,
)

from test_roundtrip import VANILLA  # noqa: E402

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def find_tree():
    for version in sorted(glob.glob(os.path.join(VANILLA, "*"))):
        tree = assets.AssetTree.find(version)
        if tree is not None:
            return tree
    return None


def test_object_models(tree):
    paths = sorted(glob.glob(os.path.join(tree.root, "objects", "models", "*.bin")))
    if not paths:
        print("  skip: no extracted object models")
        return
    decoded = vertices = triangles = 0
    for path in paths:
        try:
            model = object_model.load(path)
        except Exception as error:  # noqa: BLE001
            FAILURES.append("%s: %s" % (os.path.basename(path), error))
            continue
        decoded += 1
        vertices += len(model.vertices)
        triangles += len(model.triangles)
        for index, batch in enumerate(model.batches):
            if batch.vertex_count < 0 or batch.face_count < 0:
                FAILURES.append("%s batch %d has a negative span"
                                % (os.path.basename(path), index))
            if batch.vertex_count > 256:
                FAILURES.append(
                    "%s batch %d spans %d vertices; the index is a u8"
                    % (os.path.basename(path), index, batch.vertex_count)
                )
        # Every face must land inside the vertex array once resolved.
        for indices, _batch in model.faces():
            if max(indices) >= len(model.vertices):
                FAILURES.append("%s has a face past its vertices"
                                % os.path.basename(path))
                break
    print("  object models : %d of %d decoded, %d vertices, %d triangles"
          % (decoded, len(paths), vertices, triangles))
    check(decoded == len(paths), "every object model decodes")


def test_texture_resolution(tree):
    """Every textured batch of every object model must find its PNG.

    A model's texture table stores an index into the global 3D texture list, and
    an animated texture has no file at its own stem - only numbered frames. So a
    naive lookup silently leaves things like the zipper untextured.
    """
    paths = sorted(glob.glob(os.path.join(tree.root, "objects", "models", "*.bin")))
    if not paths:
        return
    total = resolved = untextured = 0
    unresolved = collections.Counter()
    for path in paths:
        try:
            model = object_model.load(path)
        except Exception:  # noqa: BLE001
            continue
        for batch in model.batches:
            total += 1
            texture = model.texture_for(batch)
            if texture is None:
                untextured += 1
                continue
            if tree.texture_3d_png(texture.texture_id):
                resolved += 1
            else:
                unresolved[texture.texture_id] += 1

    print("  batches       : %d total, %d textured and resolved, %d untextured"
          % (total, resolved, untextured))
    check(not unresolved,
          "every textured batch resolves to a PNG (%d ids did not: %s)"
          % (len(unresolved), list(unresolved)[:6]))


def test_uvs(tree):
    """UVs must come out at the right scale once taken out of fixed point.

    Coordinates outside 0..1 are legitimate - N64 textures tile, and a face can
    span many repeats - so an outlier proves nothing. What does prove the
    divisor is right is the bulk of them: with the wrong scale every value would
    be off by a factor of 32 or of the texture size, and the median would move
    with it.
    """
    paths = sorted(glob.glob(os.path.join(tree.root, "objects", "models", "*.bin")))
    if not paths:
        print("  skip: no object models")
        return

    values = []
    faces = 0
    for path in paths[:80]:
        try:
            model = object_model.load(path)
        except Exception:  # noqa: BLE001
            continue
        check(len(model.uvs) == len(model.triangles) or not model.triangles,
              "%s pairs UVs with triangles" % os.path.basename(path))
        for batch in model.batches:
            texture = model.texture_for(batch)
            if texture is None:
                continue
            for face in range(batch.face_offset, batch.face_offset + batch.face_count):
                coordinates = model.face_uvs(face, texture)
                if coordinates is None:
                    continue
                faces += 1
                for u, v in coordinates:
                    values.append(abs(u))
                    values.append(abs(v))

    if not values:
        FAILURES.append("no textured faces had UVs at all")
        return
    values.sort()
    median = values[len(values) // 2]
    inside = sum(1 for value in values if value <= 2.0) / float(len(values))
    print("  UVs           : %d faces, median |uv| %.3f, %.0f%% within two tiles"
          % (faces, median, inside * 100))
    check(median <= 2.0,
          "the median UV is within a couple of tiles (got %.3f); a wrong divisor "
          "would move this by a factor of 32" % median)
    check(inside >= 0.8,
          "most UVs sit within two tiles (got %.0f%%)" % (inside * 100))


def test_resolution(tree):
    """Every catalogued object type should resolve to something, or nothing on purpose."""
    catalog = catalog_module.load()
    kinds = collections.Counter()
    missing = []

    for object_id in sorted(catalog.types):
        header = tree.object_header(object_id)
        if header is None:
            missing.append(object_id)
            kinds["no header"] += 1
            continue
        kind, path, _header = tree.preview_for(object_id)
        kinds[kind] += 1
        if kind != "none":
            check(path and os.path.isfile(path),
                  "%s resolves to a file that exists (%s)" % (object_id, path))

    print("  object types  : %s"
          % ", ".join("%s %d" % (k, n) for k, n in sorted(kinds.items())))
    if missing:
        print("  no header     : %s" % ", ".join(m.replace("ASSET_OBJECT_", "")
                                                  for m in missing[:8]))

    drawable = kinds["sprite"] + kinds["mesh"]
    check(drawable >= 60,
          "most object types resolve to artwork (%d of %d)" % (drawable, len(catalog.types)))


def test_levels(tree):
    """Every level header must resolve to a model and both of its object maps.

    A track's objects live in two maps - ``map-2`` for the track and
    ``map-collectables`` for the pickups - and all 65 retail headers carry both.
    Loading only the first leaves a track with no coins and no balloons.
    """
    levels = tree.levels()
    check(len(levels) >= 60, "the level index found the headers (%d)" % len(levels))
    if not levels:
        return

    incomplete = [entry.name for entry in levels if not entry.is_complete]
    check(not incomplete,
          "every level resolves to a model and an object map (%d did not: %s)"
          % (len(incomplete), incomplete[:6]))

    without_pickups = [entry.name for entry in levels if not entry.collectables_path]
    check(not without_pickups,
          "every level resolves its collectables map (%d did not: %s)"
          % (len(without_pickups), without_pickups[:6]))

    for entry in levels:
        for path in [entry.model_path] + entry.object_maps:
            if path and not os.path.isfile(path):
                FAILURES.append("%s points at a missing file: %s" % (entry.name, path))
                break

    lake = next((e for e in levels if e.label == "Ancient Lake"), None)
    check(lake is not None, "Ancient Lake is in the index")
    if lake:
        check(lake.world_label == "Dino Domain",
              "Ancient Lake is in Dino Domain (got %s)" % lake.world_label)
        check(os.path.basename(lake.model_path) == "ancient_lake.bin",
              "Ancient Lake resolves its model (got %s)"
              % os.path.basename(lake.model_path or ""))
        check(lake.objects_path != lake.collectables_path,
              "its two object maps are different files")
        print("  Ancient Lake  : %s + %s + %s"
              % (os.path.basename(lake.model_path),
                 os.path.basename(lake.objects_path),
                 os.path.basename(lake.collectables_path)))


def test_known_objects(tree):
    """Spot-check the chain against types whose look is known."""
    expectations = [
        ("ASSET_OBJECT_PALMTREETOP", "sprite", "palm_tree_top"),
        ("ASSET_OBJECT_BEACHTREE", "sprite", "beach_tree"),
        ("ASSET_OBJECT_WEAPONBALLOON", "sprite", "balloon_boost"),
        ("ASSET_OBJECT_AIRZIPPERS", "mesh", "AirZippers"),
    ]
    for object_id, expected_kind, expected_stem in expectations:
        kind, path, _header = tree.preview_for(object_id)
        name = os.path.basename(path) if path else "<none>"
        check(kind == expected_kind,
              "%s is a %s (got %s)" % (object_id, expected_kind, kind))
        check(expected_stem.lower() in name.lower(),
              "%s resolves to %s (got %s)" % (object_id, expected_stem, name))
        print("  %-32s %-7s %s" % (object_id.replace("ASSET_OBJECT_", ""), kind, name))


def test_balloon_variants(tree):
    """A weapon balloon's five sprites are selected by its balloonType field."""
    header = tree.object_header("ASSET_OBJECT_WEAPONBALLOON")
    if header is None:
        print("  skip: no weapon balloon header")
        return
    check(len(header.models) == 5,
          "the balloon header lists five sprites (got %d)" % len(header.models))
    seen = set()
    for variant in range(len(header.models)):
        _kind, path, _h = tree.preview_for("ASSET_OBJECT_WEAPONBALLOON", variant)
        if path:
            seen.add(os.path.basename(path))
    check(len(seen) == len(header.models),
          "each balloon type gets its own sprite (%d distinct)" % len(seen))
    print("  balloon sprites: %s" % ", ".join(sorted(seen)))


def test_sprite_frames(tree):
    """A sprite frame is every tile of it, placed where the game puts it.

    The N64 holds 4 KB of texture at a time, so a tree is cut into strips: a
    palm top is five, a balloon three. Each strip's quad is one pixel short of
    its texture each way and the next starts on its last row, so strips that
    are drawn the way ``sprite_init_frame`` draws them meet exactly.
    """
    expectations = [
        ("ASSET_OBJECT_PALMTREETOP", 5),
        ("ASSET_OBJECT_WEAPONBALLOON", 3),
        ("ASSET_OBJECT_GOLDENBALLOON", 3),
        ("ASSET_OBJECT_BEACHTREE", 3),
        ("ASSET_OBJECT_BLUEBERRYBUSH", 6),
        ("ASSET_OBJECT_COIN", 1),
    ]
    for object_id, tiles in expectations:
        model_id = tree.model_id_for(object_id)
        frame = tree.sprite_frame(model_id) if model_id else None
        label = object_id.replace("ASSET_OBJECT_", "")
        if not check(frame is not None, "%s resolves a sprite frame" % label):
            continue
        check(len(frame.tiles) == tiles,
              "%s draws %d tiles (got %d)" % (label, tiles, len(frame.tiles)))
        left, right, bottom, top = frame.bounds()
        check(left < 0 < right and bottom < top,
              "%s's anchor sits inside its width (%r)" % (label, frame.bounds()))
        print("  %-16s %d tile(s), %d x %d px, anchor %d,%d"
              % (label, len(frame.tiles), right - left, top - bottom,
                 frame.anchor_x, frame.anchor_y))

    # Palm strips stack straight down: each top is the previous bottom.
    frame = tree.sprite_frame(tree.model_id_for("ASSET_OBJECT_PALMTREETOP"))
    if frame:
        quads = [t.quad(frame.anchor_x, frame.anchor_y) for t in frame.tiles]
        seams = [(quads[i][2], quads[i + 1][3]) for i in range(len(quads) - 1)]
        check(all(a == b for a, b in seams),
              "palm strips meet without a gap or an overlap (%r)" % seams)

    # A balloon hangs from the end of its string, so the anchor is below it.
    frame = tree.sprite_frame(tree.model_id_for("ASSET_OBJECT_WEAPONBALLOON"))
    if frame:
        check(frame.bounds()[2] >= 0,
              "a balloon is drawn above its anchor (%r)" % (frame.bounds(),))


def test_ground_zipper_decal(tree):
    """A ground zipper is drawn as its shadow, the arrow on the road."""
    header = tree.object_header("ASSET_OBJECT_GROUNDZIPPER")
    if not check(header is not None, "the ground zipper has a header"):
        return
    shadow = tree.shadow_png(header)
    check(shadow is not None and "ground_zipper" in os.path.basename(shadow).lower(),
          "the ground zipper's shadow is its arrow (got %s)" % shadow)
    check(abs(header.shadow_scale - 4.5) < 1e-6,
          "its shadow scale is 4.5 (got %r)" % header.shadow_scale)
    # Shadow group 0 loads no texture, whatever the header word says.
    check(tree.shadow_png(tree.object_header("ASSET_OBJECT_PALMTREETOP")) is None,
          "a palm top has no shadow texture to draw")


def test_scale_rules():
    """A size byte scales an object exactly as its ``obj_init_*`` does."""
    catalog = catalog_module.load()
    expected = {
        "ASSET_OBJECT_PALMTREETOP": "radius",
        "ASSET_OBJECT_WEAPONBALLOON": "scale",
        "ASSET_OBJECT_GOLDENBALLOON": "scale",
        "ASSET_OBJECT_GROUNDZIPPER": "scale",
        "ASSET_OBJECT_AIRZIPPERS": "radius",
        "ASSET_OBJECT_TTDOOR": "scale",
    }
    for object_id, name in expected.items():
        object_type = catalog.get(object_id)
        rule = object_type.scale_rule if object_type else None
        if not check(rule is not None, "%s has a scale rule" % object_id):
            continue
        check(rule.field.name == name,
              "%s is sized by %s (got %s)" % (object_id, name, rule.field.name))

    for object_id in ("ASSET_OBJECT_CHECKPOINT", "ASSET_OBJECT_TRIGGER",
                      "ASSET_OBJECT_EXIT", "ASSET_OBJECT_MIDIFADE"):
        object_type = catalog.get(object_id)
        check(object_type is None or object_type.scale_rule is None,
              "%s's size byte is a gate, not a drawing scale" % object_id)

    rule = catalog.get("ASSET_OBJECT_PALMTREETOP").scale_rule
    check(rule.factor(64) == 1.0 and rule.factor(128) == 2.0,
          "radius 64 is the header size and 128 twice it")
    check(rule.factor(0) == 10 / 64.0, "a radius under 10 is drawn as 10")
    check(rule.value_for(rule.factor(0), 0) == 0,
          "a byte that already gives the scale is kept, even below the floor")
    check(rule.value_for(1.5, 64) == 96, "scaling by 1.5 writes 96")
    check(rule.value_for(9.0, 64) == 255, "a scale past the byte clamps to 255")
    check(rule.value_for(0.01, 64) == 10, "a scale under the floor writes the floor")
    # Every retail value survives the trip through a factor and back.
    check(all(rule.value_for(rule.factor(v), v) == v for v in range(256)),
          "every byte survives factor and back")

    # A balloon's byte is catalogued already divided by 64, as 1.0 for 64.
    rule = catalog.get("ASSET_OBJECT_WEAPONBALLOON").scale_rule
    check(rule.factor(1.0) == 1.0 and rule.factor(2.0) == 2.0,
          "a balloon's scale 1.0 is the header size (got %r)" % rule.factor(1.0))
    check(rule.value_for(1.5, 1.0) == 1.5, "scaling a balloon by 1.5 writes 1.5")
    check(all(rule.value_for(rule.factor(v / 64.0), v / 64.0) == v / 64.0
              for v in range(256)),
          "every balloon byte survives factor and back")


def main():
    tree = find_tree()
    if tree is None:
        print("SKIP: no extracted asset tree found under %s" % VANILLA)
        return 0
    print("asset tree: %s" % tree.root)
    print()

    test_object_models(tree)
    test_texture_resolution(tree)
    test_uvs(tree)
    test_resolution(tree)
    test_levels(tree)
    print()
    test_known_objects(tree)
    test_balloon_variants(tree)
    print()
    test_sprite_frames(tree)
    test_ground_zipper_decal(tree)
    test_scale_rules()

    print()
    if FAILURES:
        print("FAIL: %d" % len(FAILURES))
        for line in FAILURES[:15]:
            print("  " + line)
        return 1
    print("PASS: object artwork resolves and every model decodes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
