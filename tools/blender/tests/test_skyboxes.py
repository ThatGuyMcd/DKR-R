"""Check the skybox gallery against the retail skies.

Every dome a retail header names has to be offered, and each one's panorama
has to be a picture of a sky rather than an empty frame.

    python tools/blender/tests/test_skyboxes.py
"""

from __future__ import annotations

import json
import math
import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from track_lab import assets, object_model, skyboxes  # noqa: E402

from test_roundtrip import REPO_ROOT  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def retail_skies(tree):
    found = {}
    for level in tree.levels():
        with open(level.header_path, "r", encoding="utf-8") as handle:
            header = json.load(handle)
        sky = ((header.get("background") or {}).get("skybox") or {}).get("id")
        if sky:
            found.setdefault(sky, []).append(level.label)
    return found


def test_catalogue(tree):
    print("the gallery")
    domes = skyboxes.catalogue(tree)
    ids = [d.asset_id for d in domes]
    check(len(domes) == 18 and ids[0] == "ASSET_OBJECT_DOME",
          "18 domes, in asset order (%d)" % len(domes))
    used = retail_skies(tree)
    check(set(used) <= set(ids),
          "every sky a retail header names is offered (%s missing)"
          % sorted(set(used) - set(ids)))
    thirteen = skyboxes.find(tree, "ASSET_OBJECT_DOME13")
    check(thirteen is not None and thirteen.used_by == ["Ancient Lake"]
          and thirteen.label == "Dome 13",
          "each dome knows which retail tracks use it")
    check(skyboxes.find(tree, "ASSET_OBJECT_DOME1").scale == 2.5,
          "and the scale its object header draws it at")
    check("No retail track uses it" in skyboxes.find(tree, "ASSET_OBJECT_DOME3").description,
          "an unused dome says so")


def _brightness(pixels, width, height, rows):
    total = count = 0
    for row in rows:
        for column in range(width):
            at = (row * width + column) * 4
            total += pixels[at] + pixels[at + 1] + pixels[at + 2]
            count += 3
    return total / count


def test_panoramas(tree):
    print("panoramas")
    width, height = 96, 48
    sampler = skyboxes.PngSampler(tree)
    background = skyboxes.BACKGROUND

    def bare(pixels, rows):
        return sum(
            1 for row in rows for column in range(width)
            if all(abs(pixels[(row * width + column) * 4 + c] - background[c]) < 1e-6
                   for c in range(3))
        )

    started = time.time()
    pictures = {}
    for dome in skyboxes.catalogue(tree):
        model = object_model.load(dome.model_path)
        pixels = skyboxes.panorama(model, sampler, width, height)
        pictures[dome.asset_id] = pixels
        if len(pixels) != width * height * 4 or not all(
                math.isfinite(p) and -1e-6 <= p <= 1.0 + 1e-6 for p in pixels):
            check(False, "%s gives %d well-formed floats" % (dome.asset_id, width * height * 4))
            continue
        if max(v[1] for v in model.vertices) <= 0:
            continue  # a cloud layer under the horizon, not a sky
        share = 1.0 - bare(pixels, range(height)) / float(width * height)
        check(share > 0.9, "%s fills its picture (%d%%)"
              % (dome.asset_id, round(share * 100)))
    elapsed = time.time() - started
    check(elapsed < 20.0, "all %d made in %.1f s" % (len(pictures), elapsed))

    check(bare(pictures["ASSET_OBJECT_DOME8"], range(height - 5, height)) == 0,
          "a cap that closes at the zenith draws without teeth (Walrus Cove)")
    check(skyboxes.window(object_model.load(
        skyboxes.find(tree, "ASSET_OBJECT_DOME13").model_path))[1] < 40.0,
          "an open dome's picture stops at its own edge (Ancient Lake, 38 degrees)")

    upper = range(height // 2, height)
    day = _brightness(pictures["ASSET_OBJECT_DOME13"], width, height, upper)
    night = _brightness(pictures["ASSET_OBJECT_DOME8"], width, height, upper)
    check(day > night, "Ancient Lake's day sky is brighter than Walrus Cove's "
          "snowy night (%.2f vs %.2f)" % (day, night))
    check(pictures["ASSET_OBJECT_DOME13"] == skyboxes.panorama(
        object_model.load(skyboxes.find(tree, "ASSET_OBJECT_DOME13").model_path),
        sampler, width, height), "the same dome gives the same picture")


def test_orientation(tree):
    print("orientation")
    # Ancient Lake's dome carries a band of desert mesas whose PNG has the rock
    # at the bottom; the band's lower edge is at 2 degrees, its upper at 15.
    # Read in the ROM's row order the rock stands on the horizon with sky above
    # it, as in game. Read in the PNG's, it hangs from the sky instead.
    model = object_model.load(skyboxes.find(tree, "ASSET_OBJECT_DOME13").model_path)
    sampler = skyboxes.PngSampler(tree)
    rock_low = sky_high = 0
    faces = 0
    for batch in model.batches:
        texture = model.texture_for(batch)
        png = tree.texture_3d_png(texture.texture_id) if texture else None
        if not png or "dome_desert" not in png:
            continue
        check(tree.texture_3d_flipped(texture.texture_id),
              "%s is marked flipped-image" % os.path.basename(png))
        sample = sampler(texture)
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            _flags, *corners = model.triangles[face]
            uvs = model.face_uvs(face, texture)
            centre = [sum(uv[axis] for uv in uvs) / 3.0 for axis in range(2)]
            seen = []
            for corner, index in enumerate(corners):
                elevation = skyboxes._direction(model.vertices[batch.vertex_offset + index])[1]
                # A little inside the triangle: a corner's UV sits on the
                # texture's edge, where a clamped texture has no inside.
                u, v = [uvs[corner][axis] * 0.9 + centre[axis] * 0.1 for axis in range(2)]
                seen.append((elevation, sample(u, v)))
            seen.sort(key=lambda item: item[0])
            faces += 1
            rock_low += seen[0][1][0] > seen[0][1][2]      # red over blue: rock
            sky_high += seen[-1][1][2] > seen[-1][1][0]    # blue over red: sky
    check(faces and rock_low == faces and sky_high == faces,
          "Ancient Lake's mesas stand on the horizon with the sky above them "
          "(%d of %d rock below, %d of %d sky above)"
          % (rock_low, faces, sky_high, faces))


def test_seam_and_pole():
    print("seam and pole")
    # A triangle across the azimuth seam is drawn the short way round, and one
    # touching the zenith fills up to the top edge rather than to a point.
    a = (0.95, 10.0, False, (0, 0, 1, 1, 1, 1))
    b = (0.05, 10.0, False, (0, 0, 1, 1, 1, 1))
    pole = (0.0, 90.0, True, (0, 0, 1, 1, 1, 1))
    polygon = skyboxes._polygon([a, b, pole])
    us = [p[0] for p in polygon]
    check(max(us) - min(us) < 0.2 + 1e-9, "the seam is crossed the short way (%r)" % us)
    check(len(polygon) == 4 and sum(1 for p in polygon if p[1] == 90.0) == 2,
          "the pole becomes an edge along the top")
    check(skyboxes._direction((1.0, 100.0, 0.0))[2],
          "a vertex a degree off the zenith counts as the pole")


def main():
    test_seam_and_pole()
    tree = assets.AssetTree.discover(REPO_ROOT)
    if tree is None:
        print("SKIP: no extracted asset tree, so the retail checks did not run")
    else:
        test_catalogue(tree)
        test_orientation(tree)
        test_panoramas(tree)
    print()
    if FAILURES:
        print("%d FAILURE(S)" % len(FAILURES))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
