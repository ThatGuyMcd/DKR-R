"""Gate for :mod:`minimap`: the game's dot formula, the picture and its payloads.

The minimap is only worth drawing if the dots land on it, so the checks here
hold the module to retail rather than to taste:

* **The formula is the game's.** For every retail level that ships a minimap,
  the computer racers' own line - two lanes of it, every set - is run through
  the retail header numbers and the retail picture, and has to land on the
  drawn road. This is the proof that ``minimap_marker_pos`` and the sprite
  placement were understood.
* **Mirrored play follows.** Retail's hand-set Adventure 2 offsets are the
  rule :func:`minimap.mirror_offsets` computes, to within two pixels.
* **A picture made here lines up by construction.** Edges from a retail AI
  path, fitted and filled, hold every lane of that path.
* **The payloads are what the loaders read**: the texture header retail
  minimaps carry, odd rows swapped for ``gDPLoadTextureBlockS``, the 16-byte
  sprite, and the placeholder ids the runtime substitutes.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_minimap.py
"""

from __future__ import annotations

import math
import os
import struct
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from dkr_track_editor import (  # noqa: E402
    assets, gltf_io, level_model, level_model_encoder, minimap, race_ai,
    textures,
)

from test_roundtrip import VANILLA  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def _tree():
    for version in ("us.v77", "us.v80"):
        root = os.path.join(VANILLA, version)
        if os.path.isfile(os.path.join(root, "asset_sprites.meta.json")):
            return assets.AssetTree(root)
    return None


def _alpha(width, height, rgba, u, v):
    column, row = int(math.floor(u)), int(math.floor(v))
    if 0 <= column < width and 0 <= row < height:
        return rgba[(row * width + column) * 4 + 3]
    return 0


def _lane_points(object_map, lanes=(1, 2), samples=6):
    points = []
    for vehicle_set in race_ai.vehicle_sets(object_map.objects):
        route = race_ai.build_route(object_map.objects, vehicle_set)
        for lane in lanes:
            points += race_ai.lane_line(route, lane, samples)
    return points


def _retail_levels(tree):
    """``(level, model, retail minimap, picture)`` for every level with one."""
    found = []
    for level in tree.levels():
        if not level.model_path or not level.objects_path:
            continue
        model = level_model.load(level.model_path)
        if not minimap.shows_minimap(model):
            continue
        sprite = minimap.retail_minimap(tree.root, model.minimap_sprite_index)
        if sprite is None:
            continue
        width, height, rgba = textures.read_png(sprite.png)
        found.append((level, model, sprite, (width, height, rgba)))
    return found


# ---------------------------------------------------------------------------

def test_retail_formula(levels):
    print("the dot formula puts retail's AI line on retail's road")
    rates = []
    for level, model, sprite, (width, height, rgba) in levels:
        placement = minimap.placement_from_model(model, sprite.anchor,
                                                 sprite.sprite_pos, (width, height))
        points = _lane_points(gltf_io.load(level.objects_path))
        if len(points) < 100:
            continue  # arenas and cutscenes: too few gates to say anything
        hits = sum(1 for x, _y, z in points
                   if _alpha(width, height, rgba,
                             *minimap.texel(placement, x, z)) > 60)
        rate = hits / float(len(points))
        rates.append(rate)
        check(rate >= 0.8, "%s: %d of %d on the road (%.0f%%)"
              % (level.label, hits, len(points), rate * 100))
    mean = sum(rates) / len(rates) if rates else 0.0
    check(len(rates) >= 25, "%d retail levels with a race line were checked"
          % len(rates))
    check(mean >= 0.93, "on average %.1f%% of the line lands on the road"
          % (mean * 100))


def test_retail_mirror(levels):
    print("Adventure 2 offsets are the mirror rule")
    checked = 0
    for level, model, sprite, (width, height, rgba) in levels:
        if (model.minimap_offset_x_adv2, model.minimap_offset_y_adv2) == (0, 0):
            continue
        rule = minimap.mirror_offsets(model.minimap_offset_x_adv1,
                                      model.minimap_offset_y_adv1, sprite.anchor[0])
        off = (model.minimap_offset_x_adv2 - rule[0],
               model.minimap_offset_y_adv2 - rule[1])
        checked += 1
        # Retail nudged a few Y offsets by hand - Whale Bay's by four - but
        # never X by more than two, and X is the one mirroring changes.
        if abs(off[0]) > 2 or abs(off[1]) > 4:
            check(False, "%s: retail Adventure 2 offsets are %d,%d from the "
                  "rule" % ((level.label,) + off))
    check(checked >= 25, "%d levels' hand-set mirror X offsets are within two "
          "pixels of anchor - offset + 2" % checked)


def test_fit_roundtrip(tree):
    print("a picture made here holds its own AI line")
    for name in ("AncientLake", "FossilCanyon", "JungleFalls", "CrescentIsland",
                 "SpaceportAlpha"):
        level = tree.level(name)
        if level is None:
            continue
        model = level_model.load(level.model_path)
        object_map = gltf_io.load(level.objects_path)
        route = race_ai.build_route(object_map.objects, 0)
        edges = minimap.edges_from_route(route)
        placement = minimap.fit(edges, model.bounds)
        rgba = minimap.picture(edges, placement)
        width, height = placement.width, placement.height
        points = [p for lane in range(race_ai.LANES)
                  for p in race_ai.lane_line(route, lane, 6)]
        hits = sum(1 for x, _y, z in points
                   if _alpha(width, height, rgba,
                             *minimap.texel(placement, x, z)) > 60)
        check(hits >= 0.97 * len(points), "%s: %d of %d lane points on the "
              "road it drew (%r)" % (level.label, hits, len(points), placement))
        check(width % minimap.WIDTH_STEP == 0 and width * height <= minimap.MAX_TEXELS
              and max(width, height) <= minimap.MAX_SIZE,
              "%s: %dx%d fits texture memory" % (level.label, width, height))
        mirrored = [minimap.texel(placement, x, z, mirrored=True)
                    for x, _y, z in points[:40]]
        normal = [minimap.texel(placement, x, z) for x, _y, z in points[:40]]
        check(all(abs(a[0] - b[0]) < 1e-6 and abs(a[1] - b[1]) < 1e-6
                  for a, b in zip(mirrored, normal)),
              "%s: mirrored dots land on the same texels" % level.label)


def test_fit_limits():
    print("fit respects the size it is given")
    bounds = (0, 8000, 0, 100, 0, 8000)
    ring = [[(1000, 0, 1000), (7000, 0, 1000), (7000, 0, 7000), (1000, 0, 7000)],
            [(2000, 0, 2000), (6000, 0, 2000), (6000, 0, 6000), (2000, 0, 6000)]]
    for size in (48, 56, 64, 72, 80):
        placement = minimap.fit(ring, bounds, size=size, rotation=0)
        check(placement.height <= size and placement.width <= size + 7
              and placement.width * placement.height <= minimap.MAX_TEXELS,
              "size %d gives %dx%d" % (size, placement.width, placement.height))
    placement = minimap.fit(ring, bounds, size=64, rotation=0)
    check(placement.anchor_x == minimap.SPRITE_X + placement.width - 2
          and placement.anchor_y == minimap.SPRITE_Y + placement.height,
          "the anchor is the picture's bottom-right corner, as retail's are")
    check((placement.offset_x2, placement.offset_y2)
          == minimap.mirror_offsets(placement.offset_x, placement.offset_y,
                                    placement.anchor_x),
          "the Adventure 2 offsets are calculated")


def test_auto_rotation():
    print("auto rotation stands the road upright")
    bounds = (0, 20000, 0, 100, 0, 2000)
    long_x = [[(0, 0, 0), (20000, 0, 0), (20000, 0, 2000), (0, 0, 2000)]]
    turned = minimap.auto_rotation(minimap._points(long_x), bounds)
    check(turned in (90, 270), "a track long along X is turned a quarter "
          "(%d)" % turned)
    placement = minimap.fit(long_x, bounds)
    check(placement.height >= placement.width, "and comes out taller than wide "
          "(%dx%d)" % (placement.width, placement.height))
    tall = [[(0, 0, 0), (2000, 0, 0), (2000, 0, 20000), (0, 0, 20000)]]
    check(minimap.auto_rotation(minimap._points(tall), (0, 2000, 0, 1, 0, 20000)) == 0,
          "a track already upright is not turned")


def test_even_odd():
    print("the road is what an odd number of edges enclose")
    bounds = (0, 1000, 0, 10, 0, 1000)
    outer = [(100, 0, 100), (900, 0, 100), (900, 0, 900), (100, 0, 900)]
    inner = [(300, 0, 300), (700, 0, 300), (700, 0, 700), (300, 0, 700)]
    placement = minimap.fit([outer, inner], bounds, size=64, rotation=0)
    coverage = minimap.rasterize([outer, inner], placement)
    width = placement.width

    def at(x, z):
        u, v = minimap.texel(placement, x, z)
        return coverage[int(v) * width + int(u)]

    check(at(500, 500) == 0.0, "the middle of a ring is a hole")
    check(at(200, 500) == 1.0, "the band between the edges is road")
    check(all(coverage[row * width] == 0.0 for row in range(placement.height))
          and all(coverage[row * width + width - 1] == 0.0
                  for row in range(placement.height)),
          "outside the outer edge is not: the margin stays clear")
    soft = [value for value in coverage if 0.0 < value < 1.0]
    check(bool(soft), "the edge is soft: %d texels part covered" % len(soft))
    hard = minimap.soften(coverage, width, placement.height, 0)
    check(all(value in (0.0, 1.0) for value in hard), "Soft Edge 0 is hard")


def test_hd_picture():
    print("the high-resolution copy covers what the texture does")
    bounds = (0, 1000, 0, 10, 0, 1000)
    outer = [(100, 0, 100), (900, 0, 150), (850, 0, 900), (120, 0, 820)]
    inner = [(300, 0, 300), (700, 0, 330), (680, 0, 700), (310, 0, 650)]
    edges = [outer, inner]
    placement = minimap.fit(edges, bounds, size=64, rotation=0)
    scale = minimap.HD_SCALE
    flag = (placement.width // 2 - 2, 3)
    for soft in (0, 1, 2):
        native = minimap.picture(edges, placement, soft, flag)
        width, height, hd = minimap.hd_picture(edges, placement, soft, flag)
        check((width, height) == (placement.width * scale, placement.height * scale),
              "soft %d: %d times the size (%dx%d)" % (soft, scale, width, height))
        worst = total = 0.0
        for v in range(placement.height):
            for u in range(placement.width):
                if flag[0] <= u < flag[0] + minimap.FLAG_SIZE and \
                        flag[1] <= v < flag[1] + minimap.FLAG_SIZE:
                    continue
                mean = sum(hd[((v * scale + y) * width + u * scale + x) * 4 + 3]
                           for y in range(scale) for x in range(scale)) / scale ** 2
                gap = abs(mean - native[(v * placement.width + u) * 4 + 3])
                total += gap
                worst = max(worst, gap)
        average = total / (placement.width * placement.height)
        # A hard edge is a staircase in the texture and a line in the copy, so
        # it can differ by a whole texel there; on average they agree.
        check(average < (6.0 if soft == 0 else 2.0) and
              (soft == 0 or worst < 64),
              "soft %d: reduced back, it is the texture (mean %.2f, worst %.0f)"
              % (soft, average, worst))

    coverage = minimap.rasterize(edges, placement, scale=scale)
    check(len(coverage) == width * height, "rasterize draws at a scale")
    _w, _h, hd = minimap.hd_picture(edges, placement, 1, flag)
    greys = set()
    for dy in range(minimap.FLAG_SIZE):
        for dx in range(minimap.FLAG_SIZE):
            cx = (flag[0] + dx) * scale + scale // 2
            cy = (flag[1] + dy) * scale + scale // 2
            at = (cy * width + cx) * 4
            if coverage[cy * width + cx] >= 0.35:
                greys.add((hd[at], (dx + dy) % 2))
    check(greys and all(grey == (0 if parity == 0 else 255)
                        for grey, parity in greys),
          "the flag is the same checkerboard, a square a texel (%r)" % greys)

    rgba = minimap.picture(edges, placement, 1, flag)
    payload = minimap.texture_payload(rgba, placement.width, placement.height)
    identity = minimap.texture_identity(payload)
    check(identity.endswith("#3#1"), "the texture is named as IA8 (%s)" % identity)
    rgba[3] = 255 - rgba[3]
    other = minimap.texture_payload(rgba, placement.width, placement.height)
    check(minimap.texture_identity(other) != identity,
          "a different picture has a different name")


def test_payloads():
    print("the payloads are what the loaders read")
    width, height = 16, 3
    rgba = bytearray()
    for row in range(height):
        for column in range(width):
            rgba += bytes((255, 255, 255, (row * width + column) * 5 % 256))
    payload = minimap.texture_payload(rgba, width, height)
    check(len(payload) % 16 == 0 and len(payload) >= 40,
          "the texture is padded to 16 bytes (%d)" % len(payload))
    check(payload[0] == width and payload[1] == height, "width and height")
    check(payload[2] == 0x05, "IA8, render mode TRANSPARENT")
    check(payload[3] == minimap.SPRITE_X and payload[4] == minimap.SPRITE_Y,
          "sprite-x and sprite-y where posX and posY are")
    check(struct.unpack_from(">h", payload, 6)[0] == 0x4D0,
          "cutout, clamped both ways and line-swapped, as retail's flags are")
    check(payload[0x12] == 1, "one frame")
    check(struct.unpack_from(">h", payload, 0x16)[0]
          == textures.align16(32 + width * height), "textureSize")
    plain = textures.encode_texels(rgba, width, height, minimap.IA8)
    texels = payload[32:32 + width * height]
    check(texels[:width] == plain[:width], "even rows are as they were")
    row = plain[width:2 * width]
    check(texels[width:2 * width] == row[4:8] + row[0:4] + row[12:16] + row[8:12],
          "odd rows trade each pair of words")

    sprite = minimap.sprite_payload(61, 62)
    check(len(sprite) == 16, "the sprite is 16 bytes")
    read = minimap.read_sprite_payload(sprite)
    check(read == {"base": minimap.CUSTOM_TEXTURE_ID_BASE, "frames": 1,
                   "anchor": (61, 62), "offsets": [0, 1]},
          "base texture placeholder, one frame, the anchor: %r" % read)

    try:
        minimap.texture_payload(bytearray(12 * 4 * 4), 12, 4)
        check(False, "a width that is not a whole TMEM line is refused")
    except minimap.MinimapError:
        check(True, "a width that is not a whole TMEM line is refused")


def test_png(tree):
    print("pictures go out as PNG and come back")
    rgba = bytearray()
    for index in range(8 * 5):
        rgba += bytes((index * 6, 255 - index, 7, index * 5))
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "map.png")
        minimap.write_png(path, 8, 5, rgba)
        width, height, back = textures.read_png(path)
        check((width, height, bytes(back)) == (8, 5, bytes(rgba)),
              "a written PNG reads back texel for texel")
        check(bytes(minimap.read_own_png(path, 8, 5)) == bytes(rgba),
              "Use My PNG takes a picture of the same size")
        try:
            minimap.read_own_png(path, 16, 5)
            check(False, "and refuses one of another size")
        except minimap.MinimapError as error:
            check("16x5" in str(error), "and refuses one of another size: %s" % error)


def test_model_placeholder(tree):
    print("the model names the track's own sprite where the runtime finds it")
    level = tree.level("AncientLake")
    if level is None:
        print("  skip: no Ancient Lake")
        return
    model = level_model.load(level.model_path)
    placement = minimap.Placement(model.bounds, 245, 1.09, 1.09, 38, -68,
                                  colour=0x3C8080, width=48, height=60)
    minimap.apply_to_model(model, placement)
    payload = level_model_encoder.pack(model)
    at = level_model_encoder.STORED_PREFIX_AT + 0x20
    check(struct.unpack_from(">i", payload, at)[0] == minimap.CUSTOM_SPRITE_ID_BASE,
          "minimapSpriteIndex is the placeholder, uncompressed at +%d" % at)
    back = level_model.parse(level_model.decompress(payload))
    check(minimap.has_own_minimap(back), "and decodes as the track's own")
    again = minimap.placement_from_model(back, (placement.anchor_x, placement.anchor_y))
    check((again.rotation, round(again.x_scale, 4), again.offset_x, again.offset_y,
           again.offset_x2, again.colour)
          == (245, 1.09, 38, -68, placement.offset_x2, 0x3C8080),
          "every number survives the encoder")


def test_inverse():
    print("the picture maps back onto the ground")
    placement = minimap.Placement((-5000, 7000, 0, 10, -3000, 9000), 245, 0.87,
                                  0.87, 40, -60, width=56, height=60)
    worst = 0.0
    for x, z in ((-4000, -2000), (0, 0), (6500, 8800), (1234, -2999)):
        u, v = minimap.texel(placement, x, z)
        back = minimap.world_of(placement, u, v)
        worst = max(worst, abs(back[0] - x), abs(back[1] - z))
    check(worst < 1e-6, "texel and world_of undo each other (%.2g)" % worst)


def main():
    tree = _tree()
    test_fit_limits()
    test_auto_rotation()
    test_even_odd()
    test_hd_picture()
    test_payloads()
    test_inverse()
    if tree is None:
        print("SKIP: no extracted assets under %s; the retail checks need them"
              % VANILLA)
    else:
        levels = _retail_levels(tree)
        test_retail_formula(levels)
        test_retail_mirror(levels)
        test_fit_roundtrip(tree)
        test_png(tree)
        test_model_placeholder(tree)
    print()
    if FAILURES:
        print("%d failure(s)" % len(FAILURES))
        return 1
    print("all minimap checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
