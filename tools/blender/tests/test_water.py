"""Gate for water that moves: the wave grid, as the game builds it.

**The simulation of the game's grid is the game's.** ``func_800BBF78`` places
every segment on a grid sized by the reference water; :func:`water.simulate`
transcribes it, and every retail wave track has to come back the way it ships:
one segment per wave tile, nothing else on one, a single reference, and
nothing :func:`water.problems` objects to - except ``ocean_track.bin``, which
no header loads and which really is broken.

**Cutting a track into the grid keeps the track.** Every retail wave track is
cut again, and has to come out valid, keep its wave tiles where they were, keep
every triangle's ground covered, and survive the file.

**Water laid from nothing works.** A synthetic track with a pit gets waves over
the pit and nowhere else, one tile a segment, with a reference sized exactly
like a tile.

**The presets are retail's.** Each is compared with the header it was read out
of.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_water.py
"""

from __future__ import annotations

import glob
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import (  # noqa: E402
    assets, level_model, level_model_encoder, level_model_layout as layout,
    water,
)

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")

#: The one retail model whose waves are not laid out as the game needs.
BROKEN = {"ocean_track.bin"}

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)
    return condition


def wave_models():
    found = []
    for version in sorted(glob.glob(os.path.join(VANILLA, "*"))):
        tree = assets.AssetTree.find(version)
        if tree is None:
            continue
        for path in sorted(glob.glob(os.path.join(tree.root, "levels", "models",
                                                  "*", "*.bin"))):
            model = level_model.load(path)
            if water.has_waves(model):
                found.append((os.path.basename(tree.root), path, model))
    return found


def ground_area(model):
    """Twice the XZ area of every drawn, non-wave triangle."""
    total = 0
    for segment in model.segments:
        for batch in segment.batches:
            if water.is_wavy(batch.flags):
                continue
            for face in range(batch.face_offset, batch.face_offset + batch.face_count):
                _f, a, b, c = segment.triangles[face][:4]
                base = batch.vertex_offset
                (ax, _ay, az), (bx, _by, bz), (cx, _cy, cz) = (
                    segment.vertices[base + a], segment.vertices[base + b],
                    segment.vertices[base + c])
                total += abs((bx - ax) * (cz - az) - (cx - ax) * (bz - az))
    return total


def wave_cells(model, grid):
    """The wave tiles that hold wave water.

    Retail has four more, switched on over no water at all - Pirate Lagoon's
    15, 75 and 78 and one in the Bubbler cutscene - which a re-cut track
    drops, having nothing to draw there.
    """
    return sorted({grid.tiles[i] for i, wavy in enumerate(grid.wavy)
                   if wavy and any(water.is_wavy(b.flags)
                                   for b in model.segments[i].batches)})


# ---------------------------------------------------------------------------

def test_retail_grids():
    print("the game's grid, over every retail wave track")
    models = wave_models()
    if not models:
        print("  skip: no extracted assets")
        return
    for version, path, model in models:
        name = os.path.basename(path)
        label = "%s %s" % (version, name)
        found = water.problems(model)
        if name in BROKEN:
            check(found, "%s is known to be broken, and is found so" % label)
            continue
        check(not found, "%s: nothing wrong (%s)" % (label, found[:1]))
        grid = water.simulate(model)
        references = sum(1 for s in model.segments for b in s.batches
                         if water.is_reference(b.flags))
        # Whale Bay has two; the game takes the first.
        check(references >= 1, "%s: a reference (%d)" % (label, references))
        tiles = [grid.tiles[i] for i, w in enumerate(grid.wavy) if w]
        check(len(tiles) == len(set(tiles)),
              "%s: one segment per wave tile" % label)
        check(all(0 <= c < water.MAX_COLUMNS and 0 <= r < water.MAX_ROWS
                  for c, r in tiles), "%s: every tile is one the game can mark"
              % label)


def test_retail_regrid():
    print("cutting every retail wave track into its grid again")
    models = wave_models()
    if not models:
        print("  skip: no extracted assets")
        return
    for version, path, model in models:
        name = os.path.basename(path)
        if version != "us.v77":
            continue
        before_grid = water.simulate(model)
        before_cells = wave_cells(model, before_grid)
        before_area = ground_area(model)
        try:
            count = layout.resegment(model)
        except layout.LayoutError as error:
            check(False, "%s cuts into its grid (%s)" % (name, error))
            continue
        again = level_model.parse(level_model.decompress(
            level_model_encoder.pack(model)))
        found = water.problems(again)
        check(not found, "%s: %d segments, nothing wrong (%s)"
              % (name, count, found[:1]))
        grid = water.simulate(again)
        if name not in BROKEN:
            check(wave_cells(again, grid) == before_cells,
                  "%s: the wave tiles stay where they were (%d)"
                  % (name, len(before_cells)))
            check((grid.tile_w, grid.tile_h)
                  == (before_grid.tile_w, before_grid.tile_h),
                  "%s: and the same size" % name)
        area = ground_area(again)
        check(abs(area - before_area) <= max(64, before_area // 500),
              "%s: the ground covers the same area (%d vs %d)"
              % (name, area, before_area))
        spilled = [i for i, box in enumerate(again.bounding_boxes)
                   if not grid.wavy[i]
                   and (box[3] - box[0] > grid.tile_w + water.CORNER_NUDGE
                        or box[5] - box[2] > grid.tile_h + water.CORNER_NUDGE)]
        check(not spilled or name in BROKEN,
              "%s: no dry segment is larger than a tile (%s)"
              % (name, spilled[:4]))


def test_cutting():
    print("cutting faces along the grid")
    corner = lambda x, z, s, t, c=(255, 255, 255, 255): ((x, 0, z), (s, t), c)
    big = [corner(0, 0, 0, 0), corner(0, 300, 0, 300), corner(300, 0, 300, 0)]
    pieces = layout._cut_face(big, [0, 100, 200, 300], [0, 100, 200, 300])
    area = 0.0
    for polygon in pieces:
        for i in range(1, len(polygon) - 1):
            (ax, _, az), (bx, _, bz), (cx, _, cz) = (polygon[0][0], polygon[i][0],
                                                     polygon[i + 1][0])
            area += abs((bx - ax) * (cz - az) - (cx - ax) * (bz - az)) / 2.0
    check(abs(area - 45000.0) < 1.0, "the pieces cover the triangle (%g)" % area)
    check(all(max(c[0][0] for c in p) - min(c[0][0] for c in p) <= 100
              and max(c[0][2] for c in p) - min(c[0][2] for c in p) <= 100
              for p in pieces), "and none crosses a grid line")
    check(all(c[1] == (c[0][0], c[0][2]) for p in pieces for c in p),
          "UVs are carried linearly to every cut")

    # Two triangles sharing an edge cut it at the same point.
    left = [corner(0, 0, 0, 0), corner(37, 250, 5, 9), corner(0, 250, 1, 1)]
    right = [corner(37, 250, 99, 99), corner(0, 0, 50, 50), corner(60, 0, 3, 3)]
    cuts_left = {c[0] for p in layout._cut_face(left, [], [100, 200]) for c in p}
    cuts_right = {c[0] for p in layout._cut_face(right, [], [100, 200]) for c in p}
    shared = {point for point in cuts_left
              if point[2] in (100, 200) and point[0] > 0}
    check(shared and shared <= cuts_right,
          "a shared edge is cut at the same integer point from both sides")


def pit_model(depth=-200, squares=6, pit=(2, 3)):
    """A ground ``squares`` x ``squares`` thousand units wide at height 0,
    with a square pit over squares ``pit[0]`` to ``pit[1]``."""
    faces, positions, colours = [], [], []
    key = layout.BatchKey(0, 0, 0, 0, 0, True, None)
    step = 1000
    for gx in range(squares):
        for gz in range(squares):
            inside = pit[0] <= gx <= pit[1] and pit[0] <= gz <= pit[1]
            pit_here = inside
            height = depth if pit_here else 0
            base = len(positions)
            x, z = gx * step, gz * step
            positions += [(x, height, z), (x + step, height, z),
                          (x + step, height, z + step), (x, height, z + step)]
            colours += [(200, 200, 200, 255)] * 4
            for a, b, c in ((0, 3, 2), (0, 2, 1)):
                faces.append(layout.Face(key, (base + a, base + b, base + c),
                                         ((0, 0), (0, 0), (0, 0))))
    from track_lab import level_model as lm
    model = layout.blank_model([lm.TextureRef(5, 32, 32, 1, 0)])
    layout.rebatch_segment(model.segments[0], faces, positions, colours)
    layout.resegment(model)
    return model


class _Texture:
    index = 7
    width = height = 16
    format = 0
    frames = 51
    translucent = True
    name = "water"


def test_water_from_nothing():
    print("waves laid over a pit")
    model = pit_model()
    check(not water.has_waves(model), "the pit starts dry")

    ground = water.Ground(model)
    check(ground.height(500, 500) == 0, "the ground is found at 0")
    check(ground.height(2500, 2500) == -200, "and the pit's floor at -200")
    check(ground.dry(0, 0, 1000, 1000, -50), "a ground tile is dry at -50")
    check(not ground.dry(2000, 2000, 3000, 3000, -50),
          "and a pit tile is not")
    check(ground.height(-500, 500) is None, "nothing is found off the track")


def main():
    test_retail_grids()
    test_retail_regrid()
    test_cutting()
    test_water_from_nothing()
    test_add_water()
    test_dry_squares_join()
    test_tile_choice()
    test_presets()
    test_header_problems()
    print()
    if FAILURES:
        print("FAIL: %d check(s)" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: water")
    return 0


def test_add_water():
    print("laying water over a pit")
    model = pit_model()
    ground_before = ground_area(model)
    laid = water.lay_water(model, True, -50, (0, 0, 6000, 6000), _Texture,
                           tile=1000)
    check(laid.tiles == 4, "four squares of the pit got waves (%d)" % laid.tiles)
    check(laid.dry == 32, "and the 32 of solid ground were skipped (%d)"
          % laid.dry)
    again = level_model.parse(level_model.decompress(
        level_model_encoder.pack(model)))
    found = water.problems(again)
    check(not found, "the result is a valid wave grid (%s)" % found[:1])
    grid = water.simulate(again)
    check((grid.tile_w, grid.tile_h) == (1000, 1000),
          "the reference sizes the tiles at 1000")
    tiles = sorted(grid.tiles[i] for i, w in enumerate(grid.wavy) if w)
    check(tiles == [(2, 2), (2, 3), (3, 2), (3, 3)],
          "the wave tiles are the pit's (%s)" % tiles)
    check(all(grid.wavy[:4]) and not any(grid.wavy[4:]),
          "and they are the first four segments")
    check(all(height == -50 for height, wavy in zip(grid.heights, grid.wavy)
              if wavy), "each at the water level")
    check(len(again.segments) == 36, "one segment a square (%d)"
          % len(again.segments))
    check(abs(ground_area(again) - ground_before) <= 64,
          "and the ground is all still there")
    flags = {b.flags for s in again.segments for b in s.batches
             if water.is_water(b.flags)}
    check(flags == {0x412205, 0x1412205},
          "the water carries retail's flags, one batch the reference (%s)"
          % sorted(hex(f) for f in flags))
    check(again.animated_texture_count > 0,
          "and the animation gate is open for the animated water")
    entry = again.textures[laid.texture_index]
    check((entry.texture_id, entry.width, entry.format) == (7, 16, 0),
          "the water's table entry names its texture")
    water_batches = [(s, i, b) for s in again.segments
                     for i, b in enumerate(s.batches) if water.is_water(b.flags)]
    check(all(i >= s.opaque_batches for s, i, _b in water_batches),
          "water is drawn in the second pass")

    # More water on the same track keeps the grid.
    try:
        water.lay_water(model, True, -50, (0, 0, 6000, 6000), _Texture,
                        tile=2000)
        check(False, "a second tile size is refused")
    except water.WaterError as error:
        check("same size" in str(error), "a second tile size is refused")
    try:
        water.lay_water(model, True, -50, (2000, 2000, 4000, 4000), _Texture)
        check(False, "water where there is water already finds nowhere to go")
    except water.WaterError as error:
        check("already hold water" in str(error),
              "water where there is water already finds nowhere to go")

    calm = pit_model()
    laid = water.lay_water(calm, False, 50, (0, 0, 6000, 6000), _Texture,
                           tile=2000)
    check(laid.tiles == 9 and not water.has_waves(calm),
          "calm water covers the track without switching waves on (%d)"
          % laid.tiles)
    flags = {b.flags for s in calm.segments for b in s.batches
             if water.is_water(b.flags)}
    check(flags == {0x12205}, "with retail's calm flags (%s)"
          % sorted(hex(f) for f in flags))


def test_dry_squares_join():
    print("a track larger than 127 squares joins its dry ones")
    model = pit_model(squares=16, pit=(7, 8))
    laid = water.lay_water(model, True, -50, (7000, 7000, 9000, 9000),
                           _Texture, tile=1000)
    check(laid.tiles == 4, "the pit got its four wave squares (%d)" % laid.tiles)
    check(len(model.segments) <= layout.MAX_SEGMENTS,
          "256 squares fit in %d segments" % len(model.segments))
    found = water.problems(model)
    check(not found, "and the waves still work (%s)" % found[:1])
    grid = water.simulate(model)
    check(sum(grid.wavy) == 4 and all(grid.wavy[:4]),
          "one segment for each wave square, first")
    check(abs(ground_area(model) - 2 * 16000 * 16000) <= 64,
          "and all the ground is there")


def test_tile_choice():
    print("choosing a tile size")
    model = pit_model()
    corners = water.triangles_of(model)
    tile, cells = water.choose_tile(corners, (0, 0, 6000, 6000), 115)
    check(tile == 1024, "a small track takes the smallest tile (%d, %d cells)"
          % (tile, cells))
    tile, cells = water.choose_tile(corners, (0, 0, 6000, 6000), 10)
    check(tile == 2048, "a tighter limit takes a larger one (%d, %d cells)"
          % (tile, cells))
    # 64000 units is 33.3 columns of 1920 and 31.25 of 2048.
    wide = [[(-32000, 0, 0), (32000, 0, 0), (0, 0, 10)]]
    tile, _cells = water.choose_tile(wide, (0, 0, 10, 10), 115)
    check(tile == 2048, "a track 64000 wide never gets more than 32 columns "
          "(%d)" % tile)


def test_presets():
    print("the presets are retail's")
    headers = None
    for version in sorted(glob.glob(os.path.join(VANILLA, "*"))):
        folder = os.path.join(version, "levels", "headers")
        if os.path.isdir(folder):
            headers = folder
            break
    if headers is None:
        print("  skip: no extracted headers")
        return
    for key, label, stem, _text in water.PRESETS:
        path = os.path.join(headers, stem + ".json")
        with open(path, "r", encoding="utf-8") as handle:
            waves = json.load(handle)["waves"]
        mine = water.PRESET_VALUES[key]
        differ = {name: (value, waves.get(name)) for name, value in mine.items()
                  if waves.get(name) != value}
        check(not differ, "%s matches %s.json (%s)" % (key, stem, differ))
        check(set(water.preset_answers(key)) <= set(water.HEADER_POINTERS),
              "%s only names bytes the Water panel draws" % key)
    covered = {field.pointer for field in water.HEADER_FIELDS}
    check(covered == {"/waves/" + name for name in water.PRESET_VALUES["LAKE"]},
          "every wave byte but the texture has a field, and nothing else does")


def test_header_problems():
    print("header values that would break the waves")
    check(not water.header_problems(water.preset_answers("WHALE")),
          "a retail preset is fine")
    check(water.header_problems({"/waves/seed-size": 0}),
          "a pattern of length 0 is refused")
    check(water.header_problems({"/waves/subdivisions": 9}),
          "and detail past 8")


if __name__ == "__main__":
    sys.exit(main())
