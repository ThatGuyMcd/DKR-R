"""Gate for the layout builder: adding and removing geometry.

Phase 2 step 3 of docs/BLENDER_ADDON_PLAN.md. Steps 1 and 2 kept every array
where it was found, which is what made them cheap and safe. This is the step
that gives that up, so it needs its own gate.

Retail's own layout is not reproducible by rule - the padding between arrays
runs 0, 4, 8, 10, 12 and on to 770 bytes with no pattern - so "rebuild and
compare to the file" is not available here the way it was for step 1. What is
available is stronger than it sounds:

* **The re-batcher is the identity on untouched data.** Pulling a segment apart
  into loose faces and putting it back must give the same batches, the same
  vertex order and the same triangles - not merely an equivalent segment. All
  2292 retail segments, both revisions. That is what says the decomposition
  loses nothing, and it is only true because a face remembers which batch it
  came from; grouping on the rendering fields alone merges the 411 segments
  that hold two batches agreeing on all of them.
* **A rebuilt layout round trips its content.** Rebuild, encode, decode, and
  every value has to come back - vertices, colours, triangles, UVs, batches,
  boxes, the BSP and the PVS. Only the offsets are allowed to differ.
* **The window invariant is a postcondition.** The Blender importer resolves a
  face to its batch through the batch windows, so a gap or an overlap
  mis-assigns render flags with nothing raised. Checked on retail and on
  everything this module builds.
* **Every triangle collides inside itself and nowhere else.** The collision
  facets are run through a transcription of the loader that turns them into
  planes (``track_init_collision``) and of the test that uses them
  (``resolve_collisions``): each triangle's centre has to collide, and a point
  60 units past each of its edges must not. A double-sided sign and a flipped
  face are built from nothing and held to the same.

The four retail models holding a degenerate triangle go through this path
untouched, because nothing here involves Blender - which is the point of
gating the encoder separately from the addon.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_level_model_layout.py
"""

from __future__ import annotations

import glob
import math
import os
import struct
import sys
from collections import Counter

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import (  # noqa: E402
    level_model, level_model_encoder, level_model_layout,
)

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")

#: Models carrying a triangle that names the same vertex twice. Blender cannot
#: hold one, so the addon round trip differs by exactly these; this path does
#: not go through Blender and must carry them.
DEGENERATE_MODELS = ("smokey", "darkmoon_caverns", "bluey", "temple_track")


def find_level_models():
    pattern = os.path.join(VANILLA, "*", "levels", "models", "*", "*.bin")
    return sorted(glob.glob(pattern))


def load(path):
    with open(path, "rb") as handle:
        return level_model.parse(level_model.decompress(handle.read()))


def segment_snapshot(segment):
    """Everything about a segment that is not its position in the file."""
    return (
        list(segment.vertices), list(segment.colours), list(segment.triangles),
        list(segment.uvs), segment.opaque_batches,
        [(b.texture_index, b.flags, b.misc, b.texture_offset, b.vertex_override,
          b.vertex_offset, b.face_offset, b.vertex_count, b.face_count)
         for b in segment.batches],
    )


def model_snapshot(model):
    return (
        [segment_snapshot(s) for s in model.segments],
        [tuple(b) for b in model.bounding_boxes],
        list(model.bsp),
        [(t.texture_id, t.raw_width, t.raw_height, t.format, t.surface_type)
         for t in model.textures],
        tuple(model.bounds),
        model.pvs,
    )


# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------

def check_rebatch_identity(path):
    """Decomposing and rebatching an untouched segment must change nothing."""
    model = load(path)
    for segment in model.segments:
        before = segment_snapshot(segment)
        faces, positions, colours = level_model_layout.decompose(segment)
        level_model_layout.rebatch_segment(segment, faces, positions, colours)
        if segment_snapshot(segment) != before:
            after = segment_snapshot(segment)
            return (
                "segment %d changed: %d batches and %d vertices became %d and %d"
                % (segment.index, len(before[5]), len(before[0]),
                   len(after[5]), len(after[0]))
            )
    return None


def check_windows_on_retail(path):
    """Retail already satisfies the invariant the re-batcher has to maintain."""
    problems = level_model_layout.check_windows(load(path))
    return problems[0] if problems else None


def check_rebuilt_round_trip(path):
    """A rebuilt layout must carry every value through encode and decode."""
    model = load(path)
    before = model_snapshot(model)

    level_model_layout.rebuild(model)
    problems = level_model_layout.check_windows(model)
    if problems:
        return "rebuilding broke the window invariant: %s" % problems[0]

    payload = level_model_encoder.encode(model)
    if len(payload) != model.blob_size:
        return "encoded %d bytes, layout said %d" % (len(payload), model.blob_size)
    if model.model_size != model.blob_size:
        return ("modelSize is %d but the blob is %d; the loader allocates its "
                "scratch from modelSize and would write over the model"
                % (model.model_size, model.blob_size))

    again = level_model.parse(payload)
    after = model_snapshot(again)
    if after == before:
        return None

    for index, (was, now) in enumerate(zip(before[0], after[0])):
        if was != now:
            names = ("vertices", "colours", "triangles", "uvs",
                     "opaque_batches", "batches")
            for part, name in enumerate(names):
                if was[part] != now[part]:
                    return "segment %d %s did not survive the rebuild" % (index, name)
            return "segment %d changed" % index
    for part, name in enumerate(("segments", "bounding boxes", "bsp", "textures",
                                 "bounds", "pvs")):
        if before[part] != after[part]:
            return "%s did not survive the rebuild" % name
    return "the model changed but no differing part was found"


def check_facets_match_retail(path):
    """The generated collision facets are mostly the ones retail ships.

    They are authored adjacency the loader reads, not scratch it fills, so a
    rebuilt layout has to write them. The rule refuses the pairs the loader
    mishandles - a neighbour running the edge the same way round, or folded
    back onto the triangle (see ``collision_facets``) - so it no longer
    reproduces retail exactly: 96.7% of all facets match, and the lowest
    model, the unused Temple track, is at 87.9%. Whether what it writes is
    *right* is :func:`check_facets_bound_their_triangles`'s question; this one
    keeps it from drifting from retail. Eighty-five per cent per model is the
    floor.
    """
    with open(path, "rb") as handle:
        blob = level_model.decompress(handle.read())
    model = level_model.parse(blob)
    same = total = 0
    for segment in model.segments:
        mine = level_model_layout.collision_facets(segment)
        for face, triangle in enumerate(segment.triangles):
            if triangle[0] & level_model_layout.TRI_FLAG_NO_COLLISION:
                continue
            at = segment.collision_facets_ptr + face * level_model_layout.FACET_SIZE
            total += 1
            same += blob[at:at + 8] == mine[face * 8:face * 8 + 8]
    if total and same < 0.85 * total:
        return "only %d of %d facets match retail" % (same, total)
    return None


# ---------------------------------------------------------------------------
# The game's collision, transcribed
# ---------------------------------------------------------------------------

def _plane(p1, p2, p3):
    """The normalised plane through three points, as ``track_init_collision``
    computes it - a zero normal stays zero."""
    (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = p1, p2, p3
    nx = y1 * (z2 - z3) + y2 * (z3 - z1) + y3 * (z1 - z2)
    ny = z1 * (x2 - x3) + z2 * (x3 - x1) + z3 * (x1 - x2)
    nz = x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2)
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length > 0.0:
        nx, ny, nz = nx / length, ny / length, nz / length
    return [nx, ny, nz, -(x1 * nx + y1 * ny + z1 * nz)]


def game_collision(segment, facets_bytes):
    """``(planes, facets, corners)`` as ``track_init_collision``
    (``tracks.c:3064``) leaves them: a plane per collidable triangle, then one
    per edge, leaning by the two normals' sum - made once per shared edge and
    handed to the neighbour negated (``| 0x8000``)."""
    planes, corners = [], {}
    for batch in segment.batches:
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            flags, a, b, c = segment.triangles[face][:4]
            if flags & level_model_layout.TRI_FLAG_NO_COLLISION:
                continue
            points = [tuple(float(v) for v in segment.vertices[batch.vertex_offset + i][:3])
                      for i in (a, b, c)]
            planes.append(_plane(*points))
            corners[face] = points
    count = len(planes)
    facets = [list(struct.unpack_from(">4H", facets_bytes, face * 8))
              for face in range(len(segment.triangles))]
    face_of_plane = {facets[face][0]: face for face in corners}
    for batch in segment.batches:
        if not batch.collidable:
            continue
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face not in corners:
                continue
            base = facets[face][0]
            n = planes[base]
            for side in range(3):
                other = facets[face][1 + side]
                if other >= count:
                    continue
                lean = [n[k] + planes[other][k] for k in range(3)]
                p1, p2 = corners[face][side], corners[face][(side + 1) % 3]
                p3 = tuple(lean[k] * 10.0 + p1[k] for k in range(3))
                if other != base and other in face_of_plane:
                    row = facets[face_of_plane[other]]
                    for j in range(3):
                        if row[1 + j] == base:
                            row[1 + j] = len(planes) | 0x8000
                facets[face][1 + side] = len(planes)
                planes.append(_plane(p1, p2, p3))
    return planes, facets, corners


def collides_at(planes, facet, point):
    """``resolve_collisions``' test that a point on the triangle's plane is
    inside it: under four units past every edge plane (``collision.c:345``)."""
    for side in range(3):
        index = facet[1 + side]
        a, b, c, d = planes[index & 0x7FFF]
        distance = point[0] * a + point[1] * b + point[2] * c + d
        if index & 0x8000:
            distance = -distance
        if distance >= 4.0:
            return False
    return True


def collision_faults(segment, facets_bytes, reach=60.0):
    """``(holes, leaks)``: triangles whose centre does not collide, and edges
    that collide ``reach`` units past themselves on the triangle's plane."""
    planes, facets, corners = game_collision(segment, facets_bytes)
    holes, leaks = [], []
    for batch in segment.batches:
        if not batch.collidable:
            continue
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            points = corners.get(face)
            if points is None:
                continue
            normal = planes[facets[face][0]][:3]
            if normal == [0.0, 0.0, 0.0]:
                continue  # degenerate: no plane to collide with
            centre = tuple(sum(p[k] for p in points) / 3.0 for k in range(3))
            if not collides_at(planes, facets[face], centre):
                holes.append(face)
            for side in range(3):
                p1, p2 = points[side], points[(side + 1) % 3]
                mid = tuple((p1[k] + p2[k]) / 2.0 for k in range(3))
                edge = tuple(p2[k] - p1[k] for k in range(3))
                away = (edge[1] * normal[2] - edge[2] * normal[1],
                        edge[2] * normal[0] - edge[0] * normal[2],
                        edge[0] * normal[1] - edge[1] * normal[0])
                length = math.sqrt(sum(c * c for c in away))
                if length == 0.0:
                    continue
                third = points[(side + 2) % 3]
                if sum((third[k] - mid[k]) * away[k] for k in range(3)) > 0:
                    length = -length
                probe = tuple(mid[k] + away[k] / length * reach for k in range(3))
                if collides_at(planes, facets[face], probe):
                    leaks.append((face, side))
    return holes, leaks


def check_facets_bound_their_triangles(path):
    """Under the game's own collision, every triangle collides inside itself
    and not 60 units past its edges. Retail's own facets fail this 113 and
    195 times over the 55 US models; the game's rule for object models,
    applied to level models, 277 and 358."""
    model = load(path)
    holes = leaks = 0
    example = None
    for segment in model.segments:
        found = collision_faults(segment, level_model_layout.collision_facets(segment))
        holes += len(found[0])
        leaks += len(found[1])
        if example is None and (found[0] or found[1]):
            example = (segment.index, (found[0] or [f for f, _s in found[1]])[0])
    if holes or leaks:
        return ("%d triangle(s) do not collide inside themselves and %d edge(s) "
                "collide past themselves (first: segment %d triangle %d)"
                % (holes, leaks, example[0], example[1]))
    return None


def _segment(points, triangles):
    """A one-batch collidable segment: ``points`` and ``(a, b, c)`` faces."""
    segment = level_model.Segment(0)
    segment.vertices = [tuple(p) for p in points]
    segment.triangles = [(0, a, b, c) for a, b, c in triangles]
    batch = level_model.Batch(0, 0, 0, 0)
    batch.vertex_count = len(points)
    batch.face_count = len(triangles)
    segment.batches = [batch]
    return segment


def check_double_sided_sign(_path=None):
    """A sign standing by the road, both sides modelled: the same corners
    again, wound backwards. Pairing each face with its own back gives the edge
    a zero plane, and the sign collides over its whole plane - across the
    road. Taken from the custom track that found it."""
    sign = [(-266, 46, -1458), (-336, 46, -1458), (-336, 116, -1458), (-266, 116, -1458)]
    front = [(0, 1, 2), (0, 2, 3)]
    back = [(3, 2, 1), (3, 1, 0)]
    segment = _segment(sign, front + back)
    facets = level_model_layout.collision_facets(segment)
    holes, leaks = collision_faults(segment, facets)
    if holes or leaks:
        return ("the sign leaves %d hole(s) and %d edge(s) colliding past "
                "themselves" % (len(holes), len(leaks)))
    planes, rows, _corners = game_collision(segment, facets)
    road = (-1000.0, 11.0, -1458.0)  # on the sign's plane, far down the road
    if any(collides_at(planes, rows[face], road) for face in range(4)):
        return "the sign still collides 700 units away on its own plane"
    return None


def check_flipped_neighbour(_path=None):
    """Two floor triangles sharing an edge, one wound the wrong way. Handing
    the second the first's plane negated puts it on the wrong side of its own
    edge; the edge has to be a wall of its own instead."""
    floor = [(0, 0, 0), (100, 0, 0), (100, 0, 100), (0, 0, 100)]
    segment = _segment(floor, [(0, 2, 1), (0, 2, 3)])
    holes, leaks = collision_faults(segment, level_model_layout.collision_facets(segment))
    if holes or leaks:
        return ("a flipped neighbour leaves %d hole(s) and %d edge(s) "
                "colliding past themselves" % (len(holes), len(leaks)))
    return None


def check_rebuilt_facets(path):
    """A rebuilt layout writes the facets rather than leaving them zeroed."""
    model = load(path)
    level_model_layout.rebuild(model)
    payload = level_model_encoder.encode(model)
    for segment in level_model.parse(payload).segments:
        start = segment.collision_facets_ptr
        written = payload[start:start + len(segment.triangles) * 8]
        if written != level_model_layout.collision_facets(segment):
            return "segment %d's collision facets were not written" % segment.index
    return None


def check_added_triangle(path):
    """Adding a face must be refused in place and accepted by the builder."""
    model = load(path)
    index = next((i for i, s in enumerate(model.segments)
                  if s.batches and len(s.triangles) > 1), None)
    if index is None:
        return None
    segment = model.segments[index]

    faces, positions, colours = level_model_layout.decompose(segment)
    faces.append(level_model_layout.Face(
        faces[0].key, faces[0].vertices, faces[0].uvs, faces[0].flags
    ))
    level_model_layout.rebatch_segment(segment, faces, positions, colours)

    if len(segment.triangles) != len(faces):
        return ("after adding a face the segment holds %d triangles, expected %d"
                % (len(segment.triangles), len(faces)))

    # The in-place encoder must refuse this, or step 1's guarantee is hollow.
    stale = load(path)
    stale.segments[index].triangles.append((0, 0, 1, 2))
    if not level_model_encoder.check_layout(stale):
        return "the in-place encoder accepted a segment whose triangle count grew"

    level_model_layout.rebuild(model)
    payload = level_model_encoder.encode(model)
    again = level_model.parse(payload)
    if len(again.segments[index].triangles) != len(faces):
        return "the added face did not survive encode and decode"
    problems = level_model_layout.check_windows(again)
    if problems:
        return "adding a face broke the window invariant: %s" % problems[0]
    return None


def check_budget(path):
    """Every retail model fits, and reports headroom rather than a negative."""
    model = load(path)
    size = level_model_layout.runtime_size(model)
    if size > level_model_layout.BUDGET:
        return ("needs %d bytes of the %d budget at load; the formula or the "
                "budget is wrong" % (size, level_model_layout.BUDGET))
    if level_model_layout.headroom_triangles(model) <= 0:
        return "reports no headroom at all, which no retail track should"
    return None


def check_degenerate_carried(path):
    """The four models Blender cannot represent must still go through here."""
    stem = os.path.splitext(os.path.basename(path))[0]
    if not any(stem.startswith(name) for name in DEGENERATE_MODELS):
        return None
    model = load(path)
    degenerate = [
        (s.index, i) for s in model.segments
        for i, t in enumerate(s.triangles)
        if t[1] == t[2] or t[2] == t[3] or t[1] == t[3]
    ]
    if not degenerate:
        return None
    level_model_layout.rebuild(model)
    again = level_model.parse(level_model_encoder.encode(model))
    for segment_index, face in degenerate:
        triangle = again.segments[segment_index].triangles[face]
        if not (triangle[1] == triangle[2] or triangle[2] == triangle[3]
                or triangle[1] == triangle[3]):
            return ("the degenerate triangle at segment %d face %d did not "
                    "survive the rebuild" % (segment_index, face))
    return None


def check_range_refusals(path):
    """A coordinate or UV the format cannot hold must say so in track terms."""
    model = load(path)
    segment = next((s for s in model.segments if s.vertices and s.uvs), None)
    if segment is None:
        return None

    original = segment.uvs[0]
    segment.uvs[0] = ((40000, 0), (0, 0), (0, 0))
    try:
        level_model_encoder.encode(model)
    except level_model_encoder.LevelModelEncodeError as error:
        if "texel" not in str(error):
            return "a UV past s16 was refused without explaining the ceiling"
    except Exception as error:  # noqa: BLE001
        return ("a UV past s16 raised %s rather than a LevelModelEncodeError"
                % type(error).__name__)
    else:
        return "a UV past s16 was accepted"
    segment.uvs[0] = original

    segment.vertices[0] = (40000, 0, 0)
    try:
        level_model_encoder.encode(model)
    except level_model_encoder.LevelModelEncodeError:
        pass
    except Exception as error:  # noqa: BLE001
        return ("a vertex past s16 raised %s rather than a "
                "LevelModelEncodeError" % type(error).__name__)
    else:
        return "a vertex past s16 was accepted"
    return None


def check_collision_pressure(path):
    """Retail must be quiet, and a track of giant boxes must not be."""
    model = load(path)
    if level_model_layout.check_collision_pressure(model):
        return ("retail trips the collision pressure warning, so the threshold "
                "is wrong: %d oversized segments"
                % len(level_model_layout.oversized_segments(model)))

    if len(model.bounding_boxes) <= level_model_layout.OVERSIZED_BUDGET:
        return None
    # Stretch every box across the track and the warning has to appear.
    lower = (model.bounds[0], model.bounds[2], model.bounds[4])
    upper = (model.bounds[1], model.bounds[3], model.bounds[5])
    model.bounding_boxes = [lower + upper for _ in model.bounding_boxes]
    if not level_model_layout.check_collision_pressure(model):
        return "a model whose every segment spans the track raised no warning"
    return None


def _has_waves(model):
    from dkr_track_editor import water

    return water.has_waves(model)


def check_resegment_batch_ceiling(path):
    """A new segment's opaque-batch count must fit its byte.

    ``numberofOpaqueBatches`` is a byte. A resegment that carried every source
    batch into whichever new segment its faces landed in pushed one wave tile
    of an authored track to 335 built from 20 distinct (texture, flags) pairs,
    and the export died inside ``struct.pack`` saying only that a ``B`` wanted
    0 to 255 - naming neither batches nor the segment. Merging what draws alike
    is what keeps a crowded segment in range.
    """
    model = load(path)
    if not any(len(segment.triangles) for segment in model.segments):
        return None

    level_model_layout.resegment(model)
    over = [index for index, segment in enumerate(model.segments)
            if segment.opaque_batches > 255]
    if over:
        return ("segments %r came out with more opaque batches than a u8 counts"
                % over[:4])
    try:
        level_model_encoder.pack(model)
    except Exception as error:  # noqa: BLE001 - any failure here is the finding
        return "the resegmented model does not encode: %s" % error
    return None


def check_crowded_wave_batches(_path=None):
    """Source batches must merge when a wave tile gathers their geometry.

    Retail never reaches the opaque-batch byte's limit through resegment.
    These two source segments each fit, but together carry over 255 opaque
    batches into one wave tile. Rendering fields differ one at a time so the
    test also catches merging batches that do not draw alike.
    """
    from dkr_track_editor import water

    layout = level_model_layout
    model = layout.blank_model([
        level_model.TextureRef(5, 32, 32, 1, 0),
        level_model.TextureRef(6, 32, 32, 1, 0),
    ])
    keys = [(0, 0, 0, 0, 0, True),
            (1, 0, 0, 0, 0, True),
            (0, water.RENDER_ANTI_ALIASING, 0, 0, 0, True),
            (0, 0, 1, 0, 0, True),
            (0, 0, 0, 1, 0, True),
            (0, 0, 0, 0, 1, True),
            (0, 0, 0, 0, 0, False)]
    model.segments = []
    for source in range(2):
        segment = level_model.Segment(source)
        faces, positions, colours = [], [], []
        for serial in range(160):
            number = source * 160 + serial
            x, z = 10 + (number % 20) * 40, 10 + (number // 20) * 40
            base = len(positions)
            positions += [(x, -20, z), (x + 10, -20, z), (x, -20, z + 10)]
            colours += [(number % 256, 80, 120, 255)] * 3
            faces.append(layout.Face(
                layout.BatchKey(*keys[serial % len(keys)], serial=serial),
                (base, base + 1, base + 2),
                ((serial, 0), (serial + 32, 0), (serial, 32)),
                layout.TRI_FLAG_NO_COLLISION if serial % 2 else 0,
            ))
        if source == 0:
            base = len(positions)
            positions += [(0, 0, 0), (1000, 0, 0),
                          (1000, 0, 1000), (0, 0, 1000)]
            colours += [water.WATER_COLOUR] * 4
            key = layout.BatchKey(0, water.WAVY_FLAGS | water.RENDER_WAVE_REFERENCE,
                                  0, 0, 0, False, 160)
            for corners in ((0, 1, 2), (0, 2, 3)):
                faces.append(layout.Face(key, [base + i for i in corners],
                                         ((0, 0), (1024, 0), (1024, 1024))))
        layout.rebatch_segment(segment, faces, positions, colours)
        model.segments.append(segment)

    def content(model):
        result = Counter()
        for segment in model.segments:
            faces, positions, colours = layout.decompose(segment)
            for face in faces:
                result[(face.key[:6], face.flags, face.uvs,
                        tuple((positions[i], colours[i]) for i in face.vertices))] += 1
        return result

    if (any(s.opaque_batches > 255 for s in model.segments)
            or sum(s.opaque_batches for s in model.segments) <= 255):
        return "fixture must fit per source segment and overflow when gathered"
    before = content(model)
    count = layout.resegment(model)
    if count != 1:
        return "the crowded geometry did not gather into one wave tile"
    segment = model.segments[0]
    if segment.opaque_batches > 255:
        return ("the wave tile inherited %d opaque batches (limit 255)"
                % segment.opaque_batches)
    problems = layout.check_windows(model) + water.problems(model)
    if problems:
        return problems[0]
    again = level_model.parse(level_model.decompress(level_model_encoder.pack(model)))
    if content(again) != before:
        return "merging changed geometry, colours, UVs, flags or rendering fields"
    return None


def check_draw_triangle_count(_path=None):
    """A shared-vertex grid fits the vertex window but overflows gSPPolygon."""
    layout = level_model_layout
    model = layout.blank_model()
    positions = [(x * 100, 0, z * 100) for z in range(4) for x in range(5)]
    colours = [(255, 255, 255, 255)] * len(positions)
    key = layout.BatchKey(255, 0, 0, 0, 0, True)
    faces = []
    for z in range(3):
        for x in range(4):
            a = z * 5 + x
            for corners in ((a, a + 5, a + 6), (a, a + 6, a + 1)):
                faces.append(layout.Face(key, corners,
                                         [positions[i][::2] for i in corners]))
    segment = model.segments[0]
    layout.rebatch_segment(segment, faces, positions, colours)
    drawn = 0
    for batch in segment.batches:
        # The command in f3ddkr.h and its decoder in f3ddkr_rt64.cpp.
        command = (((batch.face_count - 1) << 4 | 1) & 0xFF) << 16
        drawn += ((command >> 20) & 0xF) + 1
    if drawn != len(faces):
        return "the renderer draws %d of %d triangles" % (drawn, len(faces))
    layout.rebuild(model)
    again = level_model.parse(level_model_encoder.encode(model))
    rebuilt, pool, _colours = layout.decompose(again.segments[0])
    expected = Counter((tuple(positions[i] for i in f.vertices), f.uvs)
                       for f in faces)
    actual = Counter((tuple(pool[i] for i in f.vertices), f.uvs) for f in rebuilt)
    if actual != expected:
        return "splitting draw calls changed geometry or UVs"
    # Refuse a previously authored oversized batch rather than ship holes.
    again.segments[0].batches[0].face_count = 17
    try:
        level_model_encoder.encode(again)
    except level_model_encoder.LevelModelEncodeError as error:
        if "16 per batch" not in str(error):
            return "oversized draw count was refused without explaining the limit"
    else:
        return "the encoder accepted a batch the game cannot draw"
    return None


def check_resegment(path):
    """Re-segmenting must keep every triangle and fix the crowding.

    A model with waves is cut into the wave grid instead, which cuts
    triangles in two by design; ``test_water.py`` holds that path to what it
    promises.
    """
    blob, model = None, load(path)
    if _has_waves(model):
        return None
    before_triangles = sum(len(s.triangles) for s in model.segments)
    before_positions = {tuple(v) for s in model.segments for v in s.vertices}
    if before_triangles == 0:
        return None

    count = level_model_layout.resegment(model)
    if count != len(model.segments):
        return "resegment reported %d segments but built %d" % (
            count, len(model.segments)
        )

    after_triangles = sum(len(s.triangles) for s in model.segments)
    if after_triangles != before_triangles:
        return "%d triangles became %d" % (before_triangles, after_triangles)
    after_positions = {tuple(v) for s in model.segments for v in s.vertices}
    if after_positions != before_positions:
        lost = len(before_positions - after_positions)
        gained = len(after_positions - before_positions)
        return "vertex positions changed: %d lost, %d invented" % (lost, gained)

    problems = level_model_layout.check_windows(model)
    if problems:
        return "batch windows broken after resegment: %s" % problems[0]

    # Collision pressure is the harm, and it is what has to be gone. "No
    # oversized segment at all" is the wrong bar: a box is measured as a
    # fraction of the whole model, so on a small model every piece is a large
    # fraction of it however finely it is cut, and a segment made large by one
    # large triangle cannot be split away from itself.
    pressure = level_model_layout.check_collision_pressure(model)
    if pressure:
        return "resegment left collision pressure: %s" % pressure[0]

    if len(model.pvs) != level_model.pvs_size(len(model.segments)):
        return "the PVS was not resized with the segment count"

    payload = level_model_encoder.encode(model)
    again = level_model.parse(payload)
    if sum(len(s.triangles) for s in again.segments) != before_triangles:
        return "triangles were lost through encode and decode"
    return None


def check_resegment_bsp(path):
    """The generated tree must be one the game can walk.

    Walked the way ``traverse_segments_bsp_tree`` walks it, it has to draw
    every segment exactly once. Resembling retail node by node is not enough:
    the first tree this module built named each segment from its own node,
    passed a structural check, and drew segment 255 of a 127-segment track.
    """
    model = load(path)
    if sum(len(s.triangles) for s in model.segments) == 0:
        return None
    level_model_layout.resegment(model)

    if len(model.bsp) != len(model.segments):
        return "the BSP holds %d nodes for %d segments" % (
            len(model.bsp), len(model.segments)
        )
    problems = level_model_layout.bsp_problems(model)
    if problems:
        return "the game cannot walk the generated BSP: %s" % "; ".join(problems)

    # The shape that crashed: every node naming its own segment, segment 0
    # holding a left subtree, so a run goes below zero.
    crashing = [(1, -1, 0, 0, 0), (-1, 2, 0, 1, 0), (-1, -1, 0, 2, 0)]
    if level_model_layout.draw_order(crashing, 3).count(255) != 1:
        return "draw_order does not reproduce the game's segment 255"
    return None


def check_retail_bsp_walks(path):
    """Retail is the ground truth for the walk: every tree draws every segment
    once, which is what makes bsp_problems safe to act on at export."""
    problems = level_model_layout.bsp_problems(load(path))
    return "; ".join(problems) if problems else None


def check_built_bsp_on_retail_boxes(path):
    """build_bsp over retail's own boxes and order walks too."""
    model = load(path)
    model.bsp = level_model_layout.build_bsp(model.bounding_boxes)
    problems = level_model_layout.bsp_problems(model)
    return "; ".join(problems) if problems else None


def check_segment_ceiling(path):
    """However fine the split is asked to be, the game's 127 holds."""
    model = load(path)
    triangles = sum(len(s.triangles) for s in model.segments)
    if triangles <= level_model_layout.MAX_SEGMENTS or _has_waves(model):
        return None
    count = level_model_layout.resegment(model, target=1)
    if count > level_model_layout.MAX_SEGMENTS:
        return "%d segments from %d triangles" % (count, triangles)
    if sum(len(s.triangles) for s in model.segments) != triangles:
        return "loosening the split lost triangles"
    problems = level_model_layout.bsp_problems(model)
    return "; ".join(problems) if problems else None


def check_resegment_relieves_pressure(path):
    """The case re-segmenting exists for: geometry grown out of its segment.

    Whole batches are moved a long way, which is what extruding does - each
    triangle stays its own size, but the segment it belongs to now spans the
    map. That is the shape that crowds the ten-slot collision candidate list,
    and re-segmenting has to be what removes it. Building the failure here
    rather than only checking retail is what makes this a demonstration instead
    of a regression guard.
    """
    model = load(path)
    if len(model.segments) < 8:
        return None
    stretched = [i for i in range(0, len(model.segments), 3)][:7]
    moved = False
    for index in stretched:
        segment = model.segments[index]
        for batch in segment.batches[:max(1, len(segment.batches) // 2)]:
            for slot in range(batch.vertex_offset,
                              batch.vertex_offset + batch.vertex_count):
                if slot >= len(segment.vertices):
                    break
                x, y, z = segment.vertices[slot]
                segment.vertices[slot] = (
                    max(-32768, min(32767, x + 7000)), y,
                    max(-32768, min(32767, z - 7000)),
                )
                moved = True
    if not moved:
        return None

    from dkr_track_editor import level_model_edit
    level_model_edit.recompute_bounds(model)
    if not level_model_layout.check_collision_pressure(model):
        return None  # this model did not end up crowded; nothing to relieve

    before = sum(len(s.triangles) for s in model.segments)
    waves = _has_waves(model)
    level_model_layout.resegment(model)
    if waves:
        # Cut into the wave grid: triangles are split, not lost, and a wave
        # track stretched past 127 squares joins its dry ones instead of
        # failing. What matters is that the waves still work.
        from dkr_track_editor import water

        found = water.problems(model)
        if found:
            return "the stretched wave track came out broken: %s" % found[0]
        level_model_encoder.encode(model)
        return None
    if level_model_layout.check_collision_pressure(model):
        return ("re-segmenting left %d oversized segments on geometry it is "
                "meant to fix" % len(level_model_layout.oversized_segments(model)))
    if sum(len(s.triangles) for s in model.segments) != before:
        return "triangles were lost while relieving pressure"
    if level_model_layout.check_windows(model):
        return "batch windows broken while relieving pressure"
    level_model_encoder.encode(model)
    return None


def check_blank_model(_path=None):
    """A model built from nothing must be a valid model.

    Everything else here starts from a decoded retail model, so any field
    nobody sets was inherited. This is the one path with no ancestor, and it is
    what arbitrary-mesh import stands on.
    """
    textures = [level_model.TextureRef(12, 32, 32, 17, 1)]
    model = level_model_layout.blank_model(textures)
    if len(model.segments) != 1:
        return "a blank model came out with %d segments" % len(model.segments)
    if model.model_size != model.blob_size:
        return "modelSize %d does not match the blob %d" % (
            model.model_size, model.blob_size
        )

    key = level_model_layout.BatchKey(0, 0, 0, 0, 0, True, 0)
    positions = [(0, 0, 0), (1000, 0, 0), (1000, 0, 1000), (0, 0, 1000)]
    colours = [(255, 255, 255, 255)] * 4
    uvs = ((0, 0), (1024, 0), (1024, 1024))
    faces = [level_model_layout.Face(key, (0, 1, 2), uvs, 0),
             level_model_layout.Face(key, (0, 2, 3), uvs, 0)]
    level_model_layout.rebatch_segment(model.segments[0], faces, positions,
                                       colours)
    level_model_layout.resegment(model)

    again = level_model.parse(level_model_encoder.encode(model))
    if sum(len(s.triangles) for s in again.segments) != 2:
        return "the two triangles did not survive encode and decode"
    if level_model_layout.check_windows(again):
        return "a blank model produced broken batch windows"
    if len(again.pvs) != level_model.pvs_size(len(again.segments)):
        return "the PVS is not one bitmask per segment"
    if len(again.bsp) != len(again.segments):
        return "the BSP does not hold one node per segment"
    if tuple(again.bounds) != (0, 1000, 0, 0, 0, 1000):
        return "bounds came out %r, not the geometry's own" % (again.bounds,)
    if again.textures[0].surface_type != 1:
        return "the texture's surface type was not carried through"

    segment = again.segments[0]
    if segment.has_waves != 0:
        return ("hasWaves is authored, not scratch, and a new track has no "
                "water; it came out %d" % segment.has_waves)
    return None


CASES = (
    ("rebatch is the identity", check_rebatch_identity),
    ("retail satisfies the window rule", check_windows_on_retail),
    ("a rebuilt layout round trips", check_rebuilt_round_trip),
    ("an added face is built, not patched", check_added_triangle),
    ("every model fits the memory budget", check_budget),
    ("collision facets follow retail's rule", check_facets_match_retail),
    ("collision stays inside each triangle", check_facets_bound_their_triangles),
    ("a rebuilt layout writes the facets", check_rebuilt_facets),
    ("degenerate triangles survive", check_degenerate_carried),
    ("out-of-range values are refused", check_range_refusals),
    ("collision pressure is calibrated", check_collision_pressure),
    ("resegment keeps every triangle", check_resegment),
    ("resegment stays inside the u8 batch count", check_resegment_batch_ceiling),
    ("the generated BSP walks", check_resegment_bsp),
    ("every retail BSP walks", check_retail_bsp_walks),
    ("build_bsp walks on retail boxes", check_built_bsp_on_retail_boxes),
    ("resegment stays under 127", check_segment_ceiling),
    ("resegment relieves crowding", check_resegment_relieves_pressure),
)


def main():
    failures = []
    for label, case in (
        ("a model builds from nothing", check_blank_model),
        ("crowded wave batches merge", check_crowded_wave_batches),
        ("every triangle reaches the renderer", check_draw_triangle_count),
        ("a double-sided sign is no wall", check_double_sided_sign),
        ("a flipped neighbour is no hole", check_flipped_neighbour),
    ):
        problem = case()
        print("%-38s %s" % (label, "FAIL" if problem else "PASS"))
        if problem:
            print("    %s" % problem)
            failures.append((label, problem))

    models = find_level_models()
    if not models:
        print("no extracted level models found under %s" % VANILLA)
        return 1 if failures else 0

    for label, case in CASES:
        bad = []
        for path in models:
            problem = case(path)
            if problem:
                bad.append((os.path.basename(path), problem))
        print("%-38s %s" % (label, "PASS" if not bad else "FAIL (%d)" % len(bad)))
        for name, problem in bad[:5]:
            print("    %s: %s" % (name, problem))
        failures += bad

    worst = max(
        (level_model_layout.runtime_size(load(p)) for p in models), default=0
    )
    print("")
    print("level models checked: %d" % len(models))
    print("worst runtime footprint: %d of %d budget (%.1f%%)"
          % (worst, level_model_layout.BUDGET,
             100.0 * worst / level_model_layout.BUDGET))
    if failures:
        print("%d case failure(s)" % len(failures))
        return 1
    print("the layout builder reproduces every model it takes apart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
