"""Lay a level model out afresh, for edits that change how much of it there is.

Step 3 of Phase 2. Steps 1 and 2 kept every array at the offset it was read
from, which is what let an author reshape a track for free - no count changed,
so no offset moved. Adding or removing a triangle breaks that: everything after
it shifts, and the file has to be built rather than patched.

Two jobs live here, and they are separate on purpose.

:func:`rebatch_segment` turns a flat list of faces back into batches. A batch is
one draw call over a contiguous window of the segment's vertices, and the
windows partition the vertex array exactly - measured across all 2292 retail
segments, zero gaps and zero overlaps - so no vertex is shared between batches
and a re-batcher duplicates rather than shares. That is not an optimisation to
undo later; the triangle's vertex index is a ``u8`` counted from the batch's own
window, so a shared vertex is not expressible.

:func:`rebuild` assigns fresh offsets. Retail's own layout is not reproducible
by rule - the padding between arrays runs 0, 4, 8, 10, 12 ... 770 bytes with no
pattern - so this writes a *valid* layout rather than *the* layout, and the
section order it uses is retail's, which is universal across all 55 models:
header, textures, segments, then per segment batches, triangles and vertices,
then bounding boxes, the BSP, the PVS and the collision facet reservations.

Three things are measured rather than assumed, and the module docstrings say so
where it matters:

* ``modelSize`` at 0x48 is the inflated length. All 55 models.
* A batch terminator holds ``(vertex count, triangle count)``. All 1146
  segments.
* Which side of ``numberofOpaqueBatches`` a batch is on is **not** a free
  choice. No flag bit and no texture *format* separates the two sides, which is
  what this note used to conclude from; the texture's *render mode* does, as
  ``render_level_segment`` reads it. That rule, in :mod:`.transparency`, puts
  every textured retail batch on the side retail wrote. Here opacity still
  travels with the face - the caller derives it - and is never inferred.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import math
import struct
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from . import water
from .level_model import (
    BATCH_SIZE, BOUNDING_BOX_SIZE, BSP_NODE_SIZE, ENDIAN, HEADER_SIZE,
    SEGMENT_SIZE, TEXTURE_INFO_SIZE, TRIANGLE_SIZE, VERTEX_SIZE, Batch,
    LevelModel, Segment, RENDER_NO_COLLISION, pvs_size,
)

#: Every array is put on a 16 byte boundary. Retail is less consistent - most
#: arrays land on 16, some only on 8, and the collision facet reservations often
#: on 2 - but over-aligning is always safe, and the cost is a few bytes per
#: array against a half megabyte budget.
ALIGNMENT = 16

#: One ``CollisionFacetPlanes``: a base plane index and three edge bisectors,
#: all ``u16``. Authored adjacency, not scratch - see :func:`collision_facets`.
FACET_SIZE = 8

#: The format's ceiling, from the ``u8`` batch-local vertex index. Nothing in
#: retail comes close - the widest batch across all 110 models is 24 vertices
#: and the mean is about 9 - which is why :data:`BATCH_VERTEX_TARGET` aims at
#: retail's shape instead and this stays a hard backstop.
MAX_BATCH_VERTICES = 256

#: What a rebuilt batch aims for. Retail's ceiling, not the format's: batches
#: this size are what the RSP vertex buffer was sized around, and a re-batcher
#: that produced 200-vertex batches would be legal and nothing like the data the
#: renderer was tuned for.
BATCH_VERTEX_TARGET = 24

#: ``gSPPolygon`` packs ``triangle count - 1`` into four bits. The runtime
#: decodes those bits, so 17 triangles would draw only one even when every
#: vertex fits. Merging source batches must respect both command limits.
MAX_BATCH_TRIANGLES = 16

#: ``LEVEL_MODEL_MAX_SIZE`` in ``tracks.c``. Covers the inflated blob *and*
#: everything the loader allocates past ``modelSize``.
BUDGET = 0x82A00

#: ``Triangle.flags``: this triangle is skipped when collision is derived.
TRI_FLAG_NO_COLLISION = 0x80

#: ``TriangleBatchInfo.flags``: the batch is a water surface, which the loader
#: counts into ``unk32`` and gives scratch for.
BATCH_FLAG_WAVES = 0x2000


class LayoutError(Exception):
    pass


def align(offset: int, to: int = ALIGNMENT) -> int:
    return (offset + to - 1) // to * to


# ---------------------------------------------------------------------------
# Faces, detached from their batching
# ---------------------------------------------------------------------------

class BatchKey(tuple):
    """Which draw call a face belongs to.

    ``opaque`` is in here because it decides which side of
    ``numberofOpaqueBatches`` a batch lands on, and because it cannot be
    recovered from anything else - see the module docstring.

    ``serial`` is what keeps an untouched segment untouched. Retail routinely
    holds two batches that agree on every rendering field - 411 of the 1146
    segments do - so grouping on the fields alone merges them, and a segment
    nobody edited comes back with fewer batches and its vertices re-duplicated.
    Carrying the batch a face came from means a decompose and rebatch of an
    unedited segment is the identity. A face the author adds takes the serial of
    the batch its material maps to, so it joins that draw call; passing ``None``
    asks for the merging behaviour instead.
    """

    __slots__ = ()

    def __new__(cls, texture_index, flags, misc, texture_offset,
                vertex_override, opaque, serial=None):
        return tuple.__new__(cls, (int(texture_index), int(flags) & 0xFFFFFFFF,
                                   int(misc), int(texture_offset),
                                   int(vertex_override), bool(opaque),
                                   None if serial is None else int(serial)))

    texture_index = property(lambda self: self[0])
    flags = property(lambda self: self[1])
    misc = property(lambda self: self[2])
    texture_offset = property(lambda self: self[3])
    vertex_override = property(lambda self: self[4])
    opaque = property(lambda self: self[5])
    serial = property(lambda self: self[6])


class Face:
    """One triangle, holding pool vertex indices rather than batch-local ones."""

    __slots__ = ("key", "vertices", "uvs", "flags")

    def __init__(self, key: BatchKey, vertices, uvs, flags: int = 0):
        self.key = key
        #: Three indices into the segment's vertex pool.
        self.vertices = tuple(int(v) for v in vertices)
        #: ``((s0, t0), (s1, t1), (s2, t2))``, raw fixed point.
        self.uvs = tuple(tuple(int(c) for c in pair) for pair in uvs)
        self.flags = int(flags) & 0xFF


def decompose(segment: Segment) -> Tuple[List[Face], List, List]:
    """Pull a segment apart into faces and one flat vertex pool.

    The inverse of :func:`rebatch_segment`, and the reason the re-batcher can be
    tested without Blender anywhere near it: decomposing and rebatching an
    untouched segment has to give the same segment back.
    """
    faces: List[Face] = []
    for index, batch in enumerate(segment.batches):
        key = BatchKey(batch.texture_index, batch.flags, batch.misc,
                       batch.texture_offset, batch.vertex_override,
                       index < segment.opaque_batches, index)
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face >= len(segment.triangles):
                break
            flags, vi0, vi1, vi2 = segment.triangles[face]
            base = batch.vertex_offset
            uvs = segment.uvs[face] if face < len(segment.uvs) else ((0, 0),) * 3
            faces.append(Face(key, (base + vi0, base + vi1, base + vi2), uvs, flags))
    return faces, list(segment.vertices), list(segment.colours)


def rebatch_segment(segment: Segment, faces: Sequence[Face], positions: Sequence,
                    colours: Sequence,
                    target: int = BATCH_VERTEX_TARGET) -> None:
    """Rebuild a segment's batches, triangles and vertices from loose faces.

    Faces are grouped by :class:`BatchKey`, opaque groups first, and each group
    is cut into batches before exceeding ``target`` vertices or 16 triangles.
    Order within a group is the order the faces arrive in, so an untouched
    segment comes back unchanged rather than merely equivalent.
    """
    if target < 3 or target > MAX_BATCH_VERTICES:
        raise LayoutError(
            "a batch target of %d vertices cannot hold a triangle, or exceeds "
            "the %d the u8 index allows" % (target, MAX_BATCH_VERTICES)
        )

    groups: Dict[BatchKey, List[Face]] = {}
    order: List[BatchKey] = []
    for face in faces:
        if face.key not in groups:
            groups[face.key] = []
            order.append(face.key)
        groups[face.key].append(face)

    # Opaque first, because numberofOpaqueBatches is a split point and not a
    # label: render_level_segment draws [0, k) then [k, n).
    order.sort(key=lambda key: not key.opaque)

    new_batches: List[Batch] = []
    new_vertices: List = []
    new_colours: List = []
    new_triangles: List = []
    new_uvs: List = []
    opaque_count = 0

    for key in order:
        for chunk in _chunk(groups[key], target):
            batch = Batch(key.texture_index, len(new_vertices), len(new_triangles),
                          key.flags, key.vertex_override, key.misc,
                          key.texture_offset)
            local: Dict[int, int] = {}
            for face in chunk:
                indices = []
                for pool_index in face.vertices:
                    if pool_index not in local:
                        if not 0 <= pool_index < len(positions):
                            raise LayoutError(
                                "a face names vertex %d, which the pool of %d "
                                "does not hold" % (pool_index, len(positions))
                            )
                        local[pool_index] = len(local)
                        new_vertices.append(tuple(positions[pool_index]))
                        new_colours.append(tuple(
                            colours[pool_index] if pool_index < len(colours)
                            else (0, 0, 0, 0)
                        ))
                    indices.append(local[pool_index])
                new_triangles.append((face.flags, indices[0], indices[1], indices[2]))
                new_uvs.append(face.uvs)
            batch.vertex_count = len(new_vertices) - batch.vertex_offset
            batch.face_count = len(new_triangles) - batch.face_offset
            if batch.vertex_count > MAX_BATCH_VERTICES:
                raise LayoutError(
                    "a batch ended up addressing %d vertices; the triangle's "
                    "vertex index is a u8 counted from the batch, so %d is all "
                    "it can reach" % (batch.vertex_count, MAX_BATCH_VERTICES)
                )
            new_batches.append(batch)
            if key.opaque:
                opaque_count += 1

    segment.batches = new_batches
    segment.vertices = new_vertices
    segment.colours = new_colours
    segment.triangles = new_triangles
    segment.uvs = new_uvs
    segment.opaque_batches = opaque_count
    _sync_segment(segment)


def _chunk(faces: Sequence[Face], target: int) -> Iterable[List[Face]]:
    """Keep both the vertex window and the four-bit draw count representable."""
    current: List[Face] = []
    seen: set = set()
    for face in faces:
        fresh = {v for v in face.vertices if v not in seen}
        if current and (len(seen) + len(fresh) > target
                        or len(current) >= MAX_BATCH_TRIANGLES):
            yield current
            current, seen = [], set()
            fresh = set(face.vertices)
        current.append(face)
        seen |= fresh
    if current:
        yield current


# ---------------------------------------------------------------------------
# Layout
# ---------------------------------------------------------------------------

def _sync_segment(segment: Segment) -> None:
    """Bring a segment's count fields and terminator in line with its lists.

    The terminator holds the end offsets, so each batch's span is the difference
    to the next entry - which makes the terminator ``(vertex count, triangle
    count)``, true in all 1146 retail segments.
    """
    segment.vertex_count_field = len(segment.vertices)
    segment.triangle_count_field = len(segment.triangles)
    segment.batch_count_field = len(segment.batches)
    terminator = Batch(0xFF, len(segment.vertices), len(segment.triangles), 0)
    segment.batch_terminator = terminator


def check_windows(model: LevelModel) -> List[str]:
    """Every batch window must tile its segment exactly, with nothing shared.

    This is a postcondition of the re-batcher rather than an observation about
    retail. The Blender importer resolves a face to its batch through these
    windows, so a gap or an overlap silently mis-assigns render flags and
    mis-groups invisible walls, with nothing raised anywhere.
    """
    problems = []
    for segment in model.segments:
        if not segment.batches:
            continue
        cursor = 0
        for index, batch in enumerate(segment.batches):
            if batch.vertex_offset != cursor:
                problems.append(
                    "segment %d batch %d starts at vertex %d, expected %d; batch "
                    "windows have to tile the segment with no gap or overlap"
                    % (segment.index, index, batch.vertex_offset, cursor)
                )
            if batch.vertex_count > MAX_BATCH_VERTICES:
                problems.append(
                    "segment %d batch %d addresses %d vertices, past the %d a u8 "
                    "index reaches"
                    % (segment.index, index, batch.vertex_count, MAX_BATCH_VERTICES)
                )
            cursor = batch.vertex_offset + batch.vertex_count
        if cursor != len(segment.vertices):
            problems.append(
                "segment %d batches cover %d vertices but it holds %d"
                % (segment.index, cursor, len(segment.vertices))
            )

        cursor = 0
        for index, batch in enumerate(segment.batches):
            if batch.face_offset != cursor:
                problems.append(
                    "segment %d batch %d starts at face %d, expected %d"
                    % (segment.index, index, batch.face_offset, cursor)
                )
            cursor = batch.face_offset + batch.face_count
        if cursor != len(segment.triangles):
            problems.append(
                "segment %d batches cover %d faces but it holds %d"
                % (segment.index, cursor, len(segment.triangles))
            )

        if not 0 <= segment.opaque_batches <= len(segment.batches):
            problems.append(
                "segment %d says %d of its %d batches are opaque"
                % (segment.index, segment.opaque_batches, len(segment.batches))
            )
    return problems


#: How near two corners have to be to be one: within 3 units on every axis.
#: The game's own test, ``NEARBY`` in ``object_models.c:656``.
EDGE_TOLERANCE = 3

#: A triangle folded back onto its neighbour further than this - the dot of
#: their normals - is not its neighbour. The plane between two such triangles
#: lies almost in each of them, and the game's four-unit tolerance against it
#: reaches tens of units past the edge at -0.95 and without end at -1.
FOLD_LIMIT = -0.95


def collision_facets(segment: Segment) -> bytes:
    """The ``CollisionFacetPlanes`` array for one segment, as the file holds it.

    Not scratch, whatever the reservation looks like. ``track_init_collision``
    (``tracks.c:3064-3223``) derives one plane per triangle and then *reads*
    each facet: the triangle's ``basePlaneIndex``, and for each edge the plane
    of the triangle across it, from which it builds the plane bounding that
    edge - a bisector with the neighbour, or a wall straight up from an edge
    that names its own triangle. Left zeroed, every triangle points at the first
    one's plane and every edge at nothing useful, and a racer falls through a
    floor with no hole in it.

    Planes are numbered by triangle, skipping those flagged not to collide; an
    edge's neighbour is the first triangle, in a batch that collides, that runs
    the same edge **the other way round** - corners the same vertices or within
    :data:`EDGE_TOLERANCE` of them - and is not folded back onto it past
    :data:`FOLD_LIMIT`. An edge with none names its own triangle, which the
    loader turns into a wall straight up from the edge: always safe.

    That is stricter than the rule the game itself uses for object models
    (``object_models.c:559-676``), which takes either direction and any fold,
    and which this function followed until it put an invisible wall across a
    custom track. Two things in the loader make that rule unsafe:

    * The plane for a shared edge is made once, by whichever triangle comes
      first, and handed to the other **negated** (``| 0x8000``). Negating is
      right only when the two run the edge in opposite directions - when
      their normals agree. Across a flipped face it puts the triangle on the
      wrong side of its own edge: it stops colliding where it is, and collides
      across the far side instead.
    * The plane leans by the sum of the two normals. A double-sided face - the
      same corners again, wound backwards - sums to nothing, the plane comes
      out zero, and a zero plane passes every point: the triangle collides
      over its whole plane. A sign standing by the road becomes a wall across
      it.

    Transcribing the loader and ``resolve_collisions`` and holding every
    triangle to its own outline (``tests/test_level_model_layout.py``): retail's
    facets leave 113 triangles of the 55 US models that do not collide inside
    themselves and 195 edges that collide 60 units past themselves; the game's
    rule written by this function, 277 and 358; this one, none. It agrees with
    96.7% of retail's facets, the rest being pairs the loader mishandles.
    """
    ordinal: Dict[int, int] = {}
    faces = []
    for batch in segment.batches:
        collides = not (batch.flags & RENDER_NO_COLLISION)
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face >= len(segment.triangles):
                break
            flags, a, b, c = segment.triangles[face][:4]
            corners = [batch.vertex_offset + index for index in (a, b, c)]
            if max(corners) >= len(segment.vertices):
                continue
            faces.append((face, corners, collides))
            if not flags & TRI_FLAG_NO_COLLISION:
                ordinal[face] = len(ordinal)

    def at(index):
        return segment.vertices[index][:3]

    def normal(corners):
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = (at(i) for i in corners)
        nx = y1 * (z2 - z3) + y2 * (z3 - z1) + y3 * (z1 - z2)
        ny = z1 * (x2 - x3) + z2 * (x3 - x1) + z3 * (x1 - x2)
        nz = x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2)
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        return (nx / length, ny / length, nz / length) if length else (0.0, 0.0, 0.0)

    def cell(index):
        # Wider than twice the tolerance, so near corners share a cell or
        # sit in adjacent ones.
        return tuple(int(value) // 8 for value in at(index))

    def near(i, j):
        if i == j:
            return True
        a, b = at(i), at(j)
        return all(abs(a[axis] - b[axis]) <= EDGE_TOLERANCE for axis in range(3))

    # Every collidable edge, filed under the cell of each of its corners, so a
    # lookup around one corner finds it whichever way round it runs.
    edges: Dict[tuple, List[Tuple[int, int, int]]] = {}
    for face, corners, collides in faces:
        if face not in ordinal or not collides:
            continue
        for side in range(3):
            p, q = corners[side], corners[(side + 1) % 3]
            for corner in {cell(p), cell(q)}:
                edges.setdefault(corner, []).append((face, p, q))

    normals = {face: normal(corners) for face, corners, _c in faces if face in ordinal}

    out = bytearray(len(segment.triangles) * FACET_SIZE)
    for face, corners, _collides in faces:
        if face not in ordinal:
            continue  # derives no plane, and the loader skips its facet
        own = ordinal[face]
        mine = normals[face]
        row = [own]
        for side in range(3):
            a, b = corners[side], corners[(side + 1) % 3]
            cx, cy, cz = cell(a)
            best = None
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for other, p, q in edges.get((cx + dx, cy + dy, cz + dz), ()):
                            if other == face or (best is not None and other >= best):
                                continue
                            if not (near(a, q) and near(b, p)):
                                continue  # the same way round: a flipped face
                            theirs = normals[other]
                            if (mine[0] * theirs[0] + mine[1] * theirs[1]
                                    + mine[2] * theirs[2]) < FOLD_LIMIT:
                                continue  # folded back: a double-sided face
                            best = other
            row.append(ordinal[best] if best is not None else own)
        struct.pack_into(ENDIAN + "4H", out, face * FACET_SIZE, *row)
    return bytes(out)


def rebuild(model: LevelModel) -> int:
    """Give every array a fresh offset and resize the blob. Returns the size.

    Section order is retail's, which is the same in all 55 models: header,
    texture table, segment array, then each segment's batches, triangles and
    vertices, then the bounding boxes, the BSP, the PVS, and finally one
    collision facet reservation per segment.
    """
    problems = check_windows(model)
    if problems:
        raise LayoutError(problems[0])

    model.texture_count_field = len(model.textures)
    model.segment_count_field = len(model.segments)
    for segment in model.segments:
        _sync_segment(segment)

    offset = align(HEADER_SIZE)
    model.textures_ptr = offset
    offset = align(offset + len(model.textures) * TEXTURE_INFO_SIZE)
    model.segments_ptr = offset
    offset = align(offset + len(model.segments) * SEGMENT_SIZE)

    for segment in model.segments:
        segment.batches_ptr = offset
        offset = align(offset + (len(segment.batches) + 1) * BATCH_SIZE)
        segment.triangles_ptr = offset
        offset = align(offset + len(segment.triangles) * TRIANGLE_SIZE)
        segment.vertices_ptr = offset
        offset = align(offset + len(segment.vertices) * VERTEX_SIZE)

    model.bounding_boxes_ptr = offset
    offset = align(offset + len(model.bounding_boxes) * BOUNDING_BOX_SIZE)
    model.bsp_ptr = offset
    offset = align(offset + len(model.bsp) * BSP_NODE_SIZE)

    # The PVS is one bitmask per segment, so its size is fixed by the segment
    # count. A caller that changed that count and carried the old bitfield
    # forward would get a file that is internally consistent and that the game
    # reads past the end of, which is why this refuses rather than pads: the
    # symptom would be segments winking out with nothing to point at.
    pvs = model.pvs or b"\xff" * pvs_size(len(model.segments))
    if len(pvs) != pvs_size(len(model.segments)):
        raise LayoutError(
            "the PVS is %d bytes but %d segments need %d; regenerate it when "
            "the segment count changes"
            % (len(pvs), len(model.segments), pvs_size(len(model.segments)))
        )
    model.bitfields_ptr = offset
    offset = align(offset + len(pvs))
    model.unk_c_ptr = offset

    for segment in model.segments:
        segment.collision_facets_ptr = offset
        offset = align(offset + len(segment.triangles) * FACET_SIZE)

    model.blob_size = offset
    model.model_size = offset

    # Both are authored and have to be written: the PVS, and the collision
    # facets, which the loader reads rather than fills (collision_facets).
    model.gaps = []
    model.opaque = [(model.bitfields_ptr, pvs)]
    for segment in model.segments:
        model.opaque.append((segment.collision_facets_ptr, collision_facets(segment)))
    return offset


# ---------------------------------------------------------------------------
# Budget
# ---------------------------------------------------------------------------

def collidable_triangles(segment: Segment) -> int:
    """Triangles the loader will build collision planes for."""
    count = 0
    for batch in segment.batches:
        if batch.flags & RENDER_NO_COLLISION:
            continue
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face < len(segment.triangles):
                if not segment.triangles[face][0] & TRI_FLAG_NO_COLLISION:
                    count += 1
    return count


def runtime_size(model: LevelModel) -> int:
    """Upper bound on blob plus everything the loader allocates past it.

    The loader grows its arena from ``modelSize``, per segment: two bytes a
    triangle for ``unk10``, the collision planes, then two bytes per water batch
    for ``unk34``, each 16-aligned. ``track_init_collision`` returns
    ``counter * 0x10`` and shares edge bisector planes between neighbouring
    triangles, so four planes a triangle is the worst case and the true figure
    is lower - which is the safe direction for a warning.

    Calibrated against retail: nothing exceeds :data:`BUDGET`, Bluey the largest
    at 68% and the median track at 30%.
    """
    total = model.blob_size
    for segment in model.segments:
        total += align(2 * len(segment.triangles))
        total += 64 * collidable_triangles(segment)
        waves = sum(1 for b in segment.batches if b.flags & BATCH_FLAG_WAVES)
        total += align(2 * waves)
    return total


#: ``collision.c`` keeps ``LevelModelSegment *segments[10]`` and stops filling
#: it at ten. Candidates are chosen by bounding box overlap with the query
#: region, so a segment with an oversized box wins a slot from anywhere on the
#: track and holds it.
COLLISION_CANDIDATES = 10

#: A segment box spanning more than this fraction of the track's longest axis is
#: treated as oversized. Retail's median is 0.14 and its 90th percentile 0.27,
#: so this sits well clear of ordinary data.
OVERSIZED_BOX = 0.5

#: Oversized segments tolerated before it is worth saying something. Retail's
#: worst multi-segment model has two, and 40 of the 55 have none, so this is
#: more than double anything shipped and still half the candidate list.
OVERSIZED_BUDGET = 5


def oversized_segments(model: LevelModel) -> List[int]:
    """Segments whose bounding box spans most of the track.

    These are the ones that crowd collision. Candidate selection in
    ``collision.c`` takes the first ten segments whose box overlaps the query
    region and stops, so a segment stretched across the map overlaps every
    query and permanently holds a slot. A handful of them fills the list, and
    the geometry a racer is actually standing on stops being considered - the
    symptom is falling through the floor somewhere else entirely, which points
    nowhere near the cause.

    One giant polygon is enough to do this to its segment, which is why this
    only becomes reachable at step 3.
    """
    bounds = model.bounds
    if not bounds or len(bounds) < 6:
        return []
    track = max(bounds[1] - bounds[0], bounds[3] - bounds[2],
                bounds[5] - bounds[4])
    if track <= 0 or len(model.bounding_boxes) < 2:
        # A single-segment model spans itself; there is nothing to crowd.
        return []
    found = []
    for index, box in enumerate(model.bounding_boxes):
        span = max(box[3] - box[0], box[4] - box[1], box[5] - box[2])
        if span > track * OVERSIZED_BOX:
            found.append(index)
    return found


def check_collision_pressure(model: LevelModel) -> List[str]:
    """Warnings about geometry that will crowd the collision candidate list."""
    oversized = oversized_segments(model)
    if len(oversized) <= OVERSIZED_BUDGET:
        return []
    return [
        "%d segments have a bounding box spanning more than half the track "
        "(%s). Collision considers at most %d segments at a time, chosen by box "
        "overlap, so these hold slots everywhere and can push the geometry a "
        "racer is standing on out of the list. The symptom is falling through "
        "the floor far from these segments. No retail track has more than two"
        % (len(oversized), ", ".join(str(i) for i in oversized[:8]),
           COLLISION_CANDIDATES)
    ]


# ---------------------------------------------------------------------------
# Re-segmentation
# ---------------------------------------------------------------------------

#: Triangles a rebuilt segment aims for. Retail averages roughly 48 on Ancient
#: Lake and 15 on Pirate Lagoon, so this sits inside the range the game's own
#: data uses rather than above it.
SEGMENT_TRIANGLE_TARGET = 48

#: The most segments a model can hold. ``render_level_geometry_and_objects``
#: (``tracks.c``) lists the segments it draws in ``u8 segmentIds[128]`` and
#: clears ``objectsVisible[1..numberOfSegments]`` in an array of the same size,
#: so a 128th segment writes past it every frame. Retail's largest is 117.
MAX_SEGMENTS = 127

#: Splitting stops here however many triangles are left. A segment holding one
#: triangle costs a bounding box, a BSP node and a row of the PVS, and the PVS
#: grows with the square of the segment count.
MIN_SEGMENT_TRIANGLES = 8


def resegment(model: LevelModel,
              target: int = SEGMENT_TRIANGLE_TARGET,
              batch_target: int = BATCH_VERTEX_TARGET,
              wave_grid: Optional[Tuple[int, int, int, int]] = None) -> int:
    """Partition a model's geometry into fresh segments. Returns the count.

    A model holding wave water is cut into the wave grid instead - see
    :func:`resegment_grid` - because the game places *every* segment on that
    grid, and anything else draws waves where there is no water. ``wave_grid``
    is ``(tile width, tile depth, origin x, origin z)``; without it the grid is
    read off the model.

    This is what a track grown outside its inherited segmentation needs. The
    three steps before it edit a segmentation; none of them makes one, so a
    shape extruded a long way stays in the segment it grew from and stretches
    that segment's box across the map - which crowds the ten-slot collision
    candidate list and inherits visibility bits that say nothing about the new
    ground.

    Everything one-per-segment is regenerated together, because a changed
    segment count invalidates all of it: bounding boxes, the BSP, and the PVS,
    whose size is fixed by the count. The layout is rebuilt too, so a caller
    goes straight to :func:`level_model_encoder.encode` and must **not** call
    :func:`rebuild` afterwards.

    Two deliberate choices:

    * **The PVS comes out all-visible.** Computing real visibility needs a
      solution this module does not have; every bit set is conservative -
      nothing is culled that should be drawn - and costs draw calls rather
      than correctness.
    * **Identity is destroyed.** Which segment a triangle belongs to changes,
      so any ``(segment, vertex)`` mapping a caller holds is stale. That is why
      this is explicit rather than something an export does quietly.
    """
    # Batches that draw alike are merged rather than carried over: this call is
    # replacing the segmentation, so the source batches have no claim on the
    # result, and keeping them is what pushes a crowded new segment past the
    # 255 its u8 batch count can hold. See :func:`_dissolve`.
    faces, positions, colours = _dissolve(model, keep_batches=False)
    if not faces:
        return len(model.segments)

    layout = wave_grid or wave_layout(model, faces, positions)
    if layout is not None:
        return resegment_grid(model, faces, positions, colours, layout,
                              batch_target)

    # A model built from nothing has no bounds yet, and without them the split
    # below cannot divide space: it cuts by count into slices that each span
    # the track, and every one of them then crowds collision. The first island
    # exported from a mesh came out as eight segments all oversized.
    if not model.bounds or not any(model.bounds):
        used = [positions[index] for face in faces for index in face.vertices]
        xs, ys, zs = zip(*(position[:3] for position in used))
        model.bounds = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))

    # Comfortably inside OVERSIZED_BOX, because a box is measured from the
    # segment's vertices while the split is made on face centroids, and a
    # triangle reaches past its own centre.
    span = max(model.bounds[1] - model.bounds[0],
               model.bounds[3] - model.bounds[2],
               model.bounds[5] - model.bounds[4]) if model.bounds else 0
    max_extent = span * OVERSIZED_BOX * 0.5 if span > 0 else None
    groups = _partition(faces, positions, max(1, target), max_extent)
    # Coarser segments are the only way under MAX_SEGMENTS, so both stop
    # conditions loosen together until the model fits. It ends: past the whole
    # model's size and span, the split stops at one group.
    while len(groups) > MAX_SEGMENTS:
        target += max(1, target // 4)
        if max_extent is not None:
            max_extent *= 1.25
        groups = _partition(faces, positions, max(1, target), max_extent)

    model.segments = []
    for index, group in enumerate(groups):
        segment = Segment(index)
        local, pool_positions, pool_colours = {}, [], []
        rekeyed = []
        for face in group:
            mapped = []
            for pool_index in face.vertices:
                if pool_index not in local:
                    local[pool_index] = len(pool_positions)
                    pool_positions.append(positions[pool_index])
                    pool_colours.append(colours[pool_index])
                mapped.append(local[pool_index])
            rekeyed.append(Face(face.key, mapped, face.uvs, face.flags))
        rebatch_segment(segment, rekeyed, pool_positions, pool_colours,
                        batch_target)
        model.segments.append(segment)

    _rebuild_boxes(model)
    model.bsp = build_bsp(model.bounding_boxes)
    model.pvs = b"\xff" * pvs_size(len(model.segments))
    rebuild(model)
    return len(model.segments)


# ---------------------------------------------------------------------------
# The wave grid
# ---------------------------------------------------------------------------
#
# See :mod:`.water` for why a model with waves has to be a grid of equal
# squares, one segment a square. What follows builds one out of any geometry:
# faces that cross a grid line are cut along it, so every piece lies inside one
# square and every square's box starts on its own line, which is where the
# game's rounding looks for it.

def _wave_faces(faces):
    return [face for face in faces if water.is_wavy(face.key.flags)]


def wave_layout(model: LevelModel, faces=None, positions=None
                ) -> Optional[Tuple[int, int, int, int]]:
    """``(tile width, tile depth, origin x, origin z)`` for a model with waves.

    ``None`` when there is no wave water. With a reference, its water is one
    tile, whatever size its pieces are - the title screen lays its water in
    half-tile quads - so the grid is the game's own when the reference box
    agrees with that water, and the water's own when an edit has grown the
    box. Without one, the grid is read off the wave water itself, which the
    addon lays one quad to a square.
    """
    if faces is None or positions is None:
        faces, positions, _colours = _dissolve(model)
    wavy = _wave_faces(faces)
    if not wavy:
        return None

    grid = water.simulate(model)
    if grid is not None and grid.reference is not None:
        extent = water.reference_extent(model, grid.reference)
        if extent is not None and extent[2] > 0 and extent[3] > 0:
            if (grid.valid and abs(grid.tile_w - extent[2]) <= 2
                    and abs(grid.tile_h - extent[3]) <= 2):
                return grid.tile_w, grid.tile_h, grid.origin_x, grid.origin_z
            return extent[2], extent[3], extent[0], extent[1]

    extents: Dict[Tuple[int, int], int] = {}
    low_x = low_z = None
    for face in wavy:
        xs = [positions[i][0] for i in face.vertices]
        zs = [positions[i][2] for i in face.vertices]
        key = (max(xs) - min(xs), max(zs) - min(zs))
        if key[0] > 0 and key[1] > 0:
            extents[key] = extents.get(key, 0) + 1
        low_x = min(xs) if low_x is None else min(low_x, min(xs))
        low_z = min(zs) if low_z is None else min(low_z, min(zs))
    if not extents:
        raise LayoutError(
            "the wave water has no extent: every wave face is a line")
    width, depth = max(extents.items(), key=lambda kv: (kv[1], kv[0]))[0]
    return width, depth, low_x, low_z


def _lerp_corner(one, two, axis, value):
    """The point where the edge ``one``-``two`` crosses ``axis = value``.

    The two ends are put in a fixed order first, so the same edge cut from
    either of the faces that share it lands on the same integer point - the
    two sides of a cut then still meet.
    """
    first, second = sorted((one, two), key=lambda c: (c[0], c[2]))
    span = second[0][axis] - first[0][axis]
    t = (value - first[0][axis]) / float(span)
    position = [int(round(first[0][i] + (second[0][i] - first[0][i]) * t))
                for i in range(3)]
    position[axis] = int(value)
    # UVs belong to the face, so they are taken in the face's own direction.
    start, end = (one, two)
    t_face = (value - start[0][axis]) / float(end[0][axis] - start[0][axis])
    uv = tuple(int(round(start[1][i] + (end[1][i] - start[1][i]) * t_face))
               for i in range(2))
    colour = tuple(int(round(first[2][i] + (second[2][i] - first[2][i]) * t))
                   for i in range(4))
    return (tuple(position), uv, colour)


def _split_polygon(polygon, axis, value):
    """``(below, above)``: a convex polygon cut along ``axis = value``."""
    below, above = [], []
    count = len(polygon)
    for index in range(count):
        here = polygon[index]
        there = polygon[(index + 1) % count]
        a, b = here[0][axis], there[0][axis]
        if a <= value:
            below.append(here)
        if a >= value:
            above.append(here)
        if (a < value < b) or (b < value < a):
            point = _lerp_corner(here, there, axis, value)
            below.append(point)
            above.append(point)
    return below, above


def _cut_face(corners, lines_x, lines_z):
    """A face cut along every grid line it crosses, as convex polygons."""
    pieces = [corners]
    for axis, lines in ((0, lines_x), (2, lines_z)):
        cut = []
        for polygon in pieces:
            low = min(c[0][axis] for c in polygon)
            high = max(c[0][axis] for c in polygon)
            rest = polygon
            for value in lines:
                if value <= low or value >= high:
                    continue
                below, rest = _split_polygon(rest, axis, value)
                if len(below) >= 3:
                    cut.append(below)
                if len(rest) < 3:
                    break
            if len(rest) >= 3:
                cut.append(rest)
        pieces = cut
    return pieces


def _degenerate(a, b, c) -> bool:
    if a == b or b == c or a == c:
        return True
    u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return (u[1] * v[2] - u[2] * v[1] == 0 and u[2] * v[0] - u[0] * v[2] == 0
            and u[0] * v[1] - u[1] * v[0] == 0)


def _grid_lines(low, high, origin, step):
    first = (int(low) - origin) // step
    last = (int(high) - origin) // step + 1
    return [origin + index * step for index in range(first, last + 1)]


def resegment_grid(model: LevelModel, faces, positions, colours,
                   layout: Tuple[int, int, int, int],
                   batch_target: int = BATCH_VERTEX_TARGET) -> int:
    """Cut the model into the wave grid, one segment a square. Returns the count.

    * Wave water is never cut: each face goes to the square the game would put
      a box starting at its corner in. The addon lays wave water one quad a
      square, and retail's quads are a unit or two off their lines, which the
      game's own nudge absorbs.
    * Everything else is cut along the grid lines it crosses and each piece
      goes to the square it lies in. Positions, UVs and colours are
      interpolated along the cut and rounded; the ends of a shared edge are
      ordered first, so both faces cut it at the same point.
    * A square whose pieces all hug its far edge would have its box rounded
      into the next square, so it joins that square's segment.
    * Squares holding wave water come first, then the rest, each row by row.
      Wave squares switch ``hasWaves`` on; one of them, a square whose box is
      exactly a tile, carries the reference flag - the one that already did if
      it still qualifies.

    The result is checked with :func:`.water.problems`, and anything it finds
    is raised rather than written.
    """
    width, depth, origin_x, origin_z = (int(v) for v in layout)
    if width <= 0 or depth <= 0:
        raise LayoutError("a wave tile cannot be %dx%d" % (width, depth))

    pool_positions = list(positions)
    pool_colours = list(colours)
    seen: Dict[Tuple, int] = {}
    for index, (position, colour) in enumerate(zip(pool_positions, pool_colours)):
        seen.setdefault((tuple(position), tuple(colour)), index)

    def pool_index(position, colour):
        key = (tuple(position), tuple(colour))
        index = seen.get(key)
        if index is None:
            index = len(pool_positions)
            seen[key] = index
            pool_positions.append(tuple(position))
            pool_colours.append(tuple(colour))
        return index

    xs = [p[0] for p in pool_positions]
    zs = [p[2] for p in pool_positions]
    lines_x = _grid_lines(min(xs), max(xs), origin_x, width)
    lines_z = _grid_lines(min(zs), max(zs), origin_z, depth)

    def square(x, z):
        return ((x - origin_x) // width, (z - origin_z) // depth)

    had_reference = set()
    groups: Dict[Tuple[int, int], List[Face]] = {}
    for face in faces:
        points = [pool_positions[i] for i in face.vertices]
        if water.is_wavy(face.key.flags):
            cell = square(min(p[0] for p in points) + water.CORNER_NUDGE,
                          min(p[2] for p in points) + water.CORNER_NUDGE)
            if face.key.flags & water.RENDER_WAVE_REFERENCE:
                had_reference.add(cell)
            groups.setdefault(cell, []).append(face)
            continue
        corners = [(tuple(pool_positions[i]), tuple(face.uvs[at]),
                    tuple(pool_colours[i]))
                   for at, i in enumerate(face.vertices)]
        xs = [c[0][0] for c in corners]
        zs = [c[0][2] for c in corners]
        if (square(min(xs), min(zs)) == square(max(xs) - 1, max(zs) - 1)
                or (max(xs) - min(xs) <= 0 and max(zs) - min(zs) <= 0)):
            pieces = [corners]
        else:
            pieces = _cut_face(corners, lines_x, lines_z)
        for polygon in pieces:
            cx = sum(c[0][0] for c in polygon) / float(len(polygon))
            cz = sum(c[0][2] for c in polygon) / float(len(polygon))
            cell = (int((cx - origin_x) // width), int((cz - origin_z) // depth))
            for corner in range(1, len(polygon) - 1):
                trio = (polygon[0], polygon[corner], polygon[corner + 1])
                if _degenerate(*(c[0] for c in trio)):
                    continue
                groups.setdefault(cell, []).append(Face(
                    face.key,
                    [pool_index(c[0], c[2]) for c in trio],
                    [c[1] for c in trio],
                    face.flags,
                ))

    def box_of(group):
        points = [pool_positions[i] for face in group for i in face.vertices]
        return (min(p[0] for p in points), min(p[2] for p in points),
                max(p[0] for p in points), max(p[2] for p in points))

    def placed(group):
        low_x, low_z, _hx, _hz = box_of(group)
        return square(low_x + water.CORNER_NUDGE, low_z + water.CORNER_NUDGE)

    wavy_cells = {cell for cell, group in groups.items()
                  if any(water.is_wavy(face.key.flags) for face in group)}

    # A square whose box the game would round into another joins that one.
    for _round in range(len(groups) + 1):
        moved = False
        for cell in sorted(groups):
            if cell not in groups:
                continue
            where = placed(groups[cell])
            if where == cell:
                continue
            if cell in wavy_cells:
                raise LayoutError(
                    "the wave water in square %s starts %d units from its "
                    "line, past the %d the game allows"
                    % (cell, box_of(groups[cell])[0] - origin_x
                       - cell[0] * width, water.CORNER_NUDGE))
            groups.setdefault(where, []).extend(groups.pop(cell))
            moved = True
        if not moved:
            break

    wavy_cells = {cell for cell, group in groups.items()
                  if any(water.is_wavy(face.key.flags) for face in group)}
    segments = _join_dry_squares(groups, wavy_cells, placed)
    order = (sorted((key for key in segments if key in wavy_cells),
                    key=lambda c: (c[1], c[0]))
             + sorted((key for key in segments if key not in wavy_cells),
                      key=lambda c: (c[1], c[0])))
    if len(order) > MAX_SEGMENTS:
        raise LayoutError(
            "cut into %dx%d squares the track fills %d of them, and a model "
            "holds at most %d segments even with dry squares joined. Use "
            "larger wave tiles" % (width, depth, len(order), MAX_SEGMENTS))

    def exact(cell):
        low_x, low_z, high_x, high_z = box_of(segments[cell])
        return (high_x - low_x, high_z - low_z) == (width, depth)

    candidates = [cell for cell in order if cell in wavy_cells and exact(cell)]
    if wavy_cells and not candidates:
        raise LayoutError(
            "no wave square is exactly %dx%d, and the game sizes every tile "
            "after the one flagged as the reference" % (width, depth))
    preferred = [cell for cell in candidates if cell in had_reference]
    reference = (preferred or candidates or [None])[0]

    model.segments = []
    for index, cell in enumerate(order):
        segment = Segment(index)
        local, cell_positions, cell_colours = {}, [], []
        rekeyed = []
        for face in segments[cell]:
            mapped = []
            for pool in face.vertices:
                if pool not in local:
                    local[pool] = len(cell_positions)
                    cell_positions.append(pool_positions[pool])
                    cell_colours.append(pool_colours[pool])
                mapped.append(local[pool])
            flags = face.key.flags & ~water.RENDER_WAVE_REFERENCE
            if cell == reference and water.is_wavy(flags):
                flags |= water.RENDER_WAVE_REFERENCE
            key = face.key
            if flags != key.flags:
                key = BatchKey(key.texture_index, flags, key.misc,
                               key.texture_offset, key.vertex_override,
                               key.opaque, key.serial)
            rekeyed.append(Face(key, mapped, face.uvs, face.flags))
        rebatch_segment(segment, rekeyed, cell_positions, cell_colours,
                        batch_target)
        segment.has_waves = water.HAS_WAVES if cell in wavy_cells else 0
        model.segments.append(segment)

    _rebuild_boxes(model)
    model.bsp = build_bsp(model.bounding_boxes)
    model.pvs = b"\xff" * pvs_size(len(model.segments))
    rebuild(model)

    found = water.problems(model)
    if found:
        raise LayoutError("the wave grid did not come out right: %s" % found[0])
    return len(model.segments)


def _join_dry_squares(groups, wavy_cells, placed):
    """``{key square: faces}``, with dry squares joined into blocks if need be.

    One segment a square is what a wave square needs; a dry square only needs
    never to be placed on a wave square. So when a track fills more squares
    than a model holds segments, dry squares are joined into ``n x n`` blocks
    - the smallest ``n`` that fits - wherever a block holds no water at all.
    Such a block's box starts inside itself, so the game places it on one of
    its own dry squares. A block that would round onto a wave square anyway,
    through a sliver at its edge, is left as single squares. The key of a block
    is its lowest square, which is what orders it.
    """
    if len(groups) <= MAX_SEGMENTS:
        return dict(groups)
    best = dict(groups)
    for factor in range(2, 65):
        blocks: Dict[Tuple[int, int], List[Tuple[int, int]]] = {}
        for cell in groups:
            if cell in wavy_cells:
                continue
            blocks.setdefault((cell[0] // factor, cell[1] // factor), []).append(cell)
        wet_blocks = {(cell[0] // factor, cell[1] // factor) for cell in wavy_cells}
        joined: Dict[Tuple[int, int], List[Face]] = {}
        for cell in wavy_cells:
            joined[cell] = list(groups[cell])
        for block, cells in blocks.items():
            if block in wet_blocks:
                for cell in cells:
                    joined[cell] = list(groups[cell])
                continue
            faces = [face for cell in cells for face in groups[cell]]
            if placed(faces) in wavy_cells:
                for cell in cells:
                    joined[cell] = list(groups[cell])
                continue
            joined[min(cells, key=lambda c: (c[1], c[0]))] = faces
        best = joined
        if len(joined) <= MAX_SEGMENTS:
            break
    return best


def add_faces(model: LevelModel, faces: Sequence[Face], positions: Sequence,
              colours: Sequence) -> None:
    """Put loose faces into the model as one more segment, for a resegment.

    The segment is a staging area: :func:`resegment` dissolves every segment
    anyway, so what matters is only that the faces are in the model when it
    runs.
    """
    segment = Segment(len(model.segments))
    rebatch_segment(segment, list(faces), list(positions), list(colours))
    model.segments.append(segment)
    from . import level_model_edit

    model.bounding_boxes = list(model.bounding_boxes)
    model.bounding_boxes.append(level_model_edit.segment_box(segment)
                                or (0, 0, 0, 0, 0, 0))


#: ``LevelModel`` fields with no reader anywhere in the decomp that are still
#: given retail's value. Zero would almost certainly do - but the decomp still
#: has unmatched assembly, so a grep for readers is evidence and not proof, and
#: matching what every retail track ships costs nothing. If a reader is ever
#: found, this is the one place to change.
BLANK_HEADER_UNKNOWNS = {
    "unk1C": 1,
    "unk26": 9,
}


def blank_model(textures: Sequence = (), bounds=None) -> LevelModel:
    """A level model for a track with no ancestor, ready to be filled.

    The three steps of Phase 2 all start from a decoded retail model, so every
    field nobody sets was inherited. A model built from nothing has no such
    source, and something has to decide what the rest of the segment struct
    holds. This does, so that no caller has to name those fields - the same
    reason ``SurfaceType`` belongs in the catalogue rather than in a table
    copied into a UI module.

    Hand the result faces through :func:`rebatch_segment` and then call
    :func:`resegment`; the layout, the boxes, the BSP and the PVS all follow.

    **What the answer rests on.** The loader in ``tracks.c`` assigns ``unk10``,
    ``collisionPlanes``, ``unk30``, ``unk32``, ``unk34`` and ``unk38`` right
    after inflating, so whatever the file holds there is discarded - which is
    why retail's values look like leftovers, and why zero is fine. Of the rest:

    * ``unk8``, ``unk28``, ``unk2A``, ``unk3C`` and the padding have **no reader
      anywhere in the decomp**. Zero.
    * ``unk2C`` is read only by ``calc_dyn_lighting_for_level_segment``, which
      is marked ``UNUSED`` and exists only as unmatched assembly. Zero.
    * ``hasWaves`` is **not** scratch. ``tracks.c`` tests it to decide whether a
      segment carries water, so it is authored - and zero, no water, is both
      the right default for a new track and what every Ancient Lake segment
      holds.
    * ``collisionFacets`` is fixed up from the file, so the asset has to reserve
      its array. :func:`rebuild` does that; nothing here has to.
    """
    model = LevelModel()
    model.textures = list(textures)
    model.segments = [Segment(0)]
    model.bounding_boxes = [tuple(bounds) if bounds else (0, 0, 0, 0, 0, 0)]
    model.bounds = (0, 0, 0, 0, 0, 0)
    model.pvs = b"\xff" * pvs_size(1)
    model.bsp = build_bsp(model.bounding_boxes)
    for name, value in BLANK_HEADER_UNKNOWNS.items():
        setattr(model, name, value)
    # A new track has no animated textures until something says otherwise, and
    # the count is real rather than decorative - the renderer walks that many.
    model.animated_texture_count = 0
    rebuild(model)
    return model


def _dissolve(model: LevelModel, keep_batches: bool = True):
    """Every face in the model, against one flat pool of vertices.

    ``keep_batches`` carries each face's source batch through. A
    :class:`BatchKey`'s ``serial`` is only unique inside its own segment, so it
    is widened to ``(segment, batch)``; without that, batches from two source
    segments would collide and merge the moment they landed in the same new
    segment, and a decompose and rebuild of an untouched segmentation would
    stop being the identity.

    A caller **replacing** the segmentation wants the opposite, and says so.
    There, faces from many source segments land in one new segment, and keeping
    the serials makes that segment inherit the union of all their batches.
    Measured on an exported track whose wave grid forced the partition: one tile
    came out with 335 batches built from 20 distinct (texture, flags) pairs,
    past the 255 a ``u8 numberofOpaqueBatches`` can hold, and the export failed
    inside struct.pack. Dropping them merges what draws alike - the same tile
    comes out at 63 - and costs nothing a resegment has not already spent,
    since it destroys identity by definition.
    """
    faces: List[Face] = []
    positions: List = []
    colours: List = []
    seen: Dict[Tuple, int] = {}

    for segment in model.segments:
        local, pool, pool_colours = _dissolve_segment(segment)
        offset = {}
        for index, position in enumerate(pool):
            colour = tuple(pool_colours[index])
            key = (tuple(position), colour)
            if key not in seen:
                seen[key] = len(positions)
                positions.append(tuple(position))
                colours.append(colour)
            offset[index] = seen[key]
        for face in local:
            widened = BatchKey(
                face.key.texture_index, face.key.flags, face.key.misc,
                face.key.texture_offset, face.key.vertex_override,
                face.key.opaque,
                None if not keep_batches or face.key.serial is None
                else (segment.index << 16) | (face.key.serial & 0xFFFF),
            )
            faces.append(Face(widened, [offset[v] for v in face.vertices],
                              face.uvs, face.flags))
    return faces, positions, colours


def _dissolve_segment(segment: Segment):
    faces, positions, colours = decompose(segment)
    return faces, positions, colours


def _centroid(face: Face, positions) -> Tuple[float, float, float]:
    points = [positions[index] for index in face.vertices]
    return tuple(sum(point[axis] for point in points) / 3.0 for axis in range(3))


def _partition(faces: List[Face], positions, target: int,
               max_extent: Optional[float] = None) -> List[List[Face]]:
    """Recursive median split on the widest axis, by face centroid.

    Median rather than midpoint, so a dense corner of a track does not produce
    one enormous segment and a row of empty ones. The BSP built over the result
    is no worse than retail's: walking every retail model, 199 of 3911
    segment-to-split relationships already have the segment outside its own
    half-space, so exact containment is not a property the format has.

    **Two stop conditions, and the spatial one is the point.** Splitting only on
    triangle count leaves a sparse region as one segment stretched across the
    map - forty triangles spanning six thousand units, which is exactly the
    shape that crowds the collision candidate list and the shape re-segmenting
    exists to remove. So a group keeps splitting while it is either too
    populous or too wide.

    What cannot be fixed here is a segment made large by a single triangle
    larger than ``max_extent``. Splitting never separates a triangle from
    itself, so that box stays big; the honest limit is one face.
    """
    if len(faces) <= 1:
        return [faces]

    centroids = [_centroid(face, positions) for face in faces]
    extent = max(max(c[axis] for c in centroids) - min(c[axis] for c in centroids)
                 for axis in range(3))
    too_many = len(faces) > max(target, MIN_SEGMENT_TRIANGLES)
    # The spatial condition stops at the floor too. Splitting a handful of
    # triangles because they are spread out trades one wide segment for several
    # nearly as wide - a box is a fraction of the whole model, so on a small
    # model every piece is a large fraction of it however finely it is cut, and
    # more segments is strictly worse: each one costs a box, a BSP node and a
    # row of the PVS, and only ten can be collision candidates at once.
    too_wide = (max_extent is not None and extent > max_extent
                and len(faces) > MIN_SEGMENT_TRIANGLES)
    if not (too_many or too_wide):
        return [faces]

    axis = max(range(3), key=lambda a: (max(c[a] for c in centroids)
                                        - min(c[a] for c in centroids)))
    order = sorted(range(len(faces)), key=lambda i: centroids[i][axis])
    middle = len(order) // 2
    left = [faces[i] for i in order[:middle]]
    right = [faces[i] for i in order[middle:]]
    if not left or not right:
        return [faces]
    return (_partition(left, positions, target, max_extent)
            + _partition(right, positions, target, max_extent))


def _rebuild_boxes(model: LevelModel) -> None:
    """Boxes from the new segments, and the header bounds from all of them."""
    from . import level_model_edit

    model.bounding_boxes = []
    for segment in model.segments:
        box = level_model_edit.segment_box(segment)
        model.bounding_boxes.append(box or (0, 0, 0, 0, 0, 0))
    bounds = level_model_edit.model_bounds(model)
    if bounds is not None:
        model.bounds = bounds


def build_bsp(boxes: Sequence[Sequence[int]]) -> List[Tuple[int, int, int, int, int]]:
    """A BSP the game's traversal can walk, over the segments in their order.

    ``traverse_segments_bsp_tree`` (``tracks.c``) never draws a node's own
    segment. A node splits the run of segment indices it was handed at its
    ``segmentIndex`` - ``[lo, seg - 1]`` to the left child, ``[seg, hi]`` to the
    right - and a side with no child draws the one segment its run holds. So
    what the game needs is a tree over **contiguous runs of indices**, and that
    is what this builds: every node splits a run in two, and a run of one
    segment is a side with no child. ``n`` segments take ``n - 1`` nodes; the
    slot left over stays unreachable, as in 100 of retail's 110 models.

    An earlier version gave every segment a node naming itself, which retail
    resembles at a glance and which the game cannot walk: it drew some segments
    twice and others never - holes in the track - and a run that went below
    zero drew segment 255 of a 127-segment model, which crashes in
    ``render_level_segment``. :func:`draw_order` is the game's walk, and the
    tests hold every built tree and every retail one to it.

    The split axis and value only decide which half is drawn first - the
    camera's side - never *which* segments are drawn, so they come from the
    boxes of the two halves: the axis the halves are furthest apart on, and the
    midpoint between them. The segment order is the caller's; re-segmenting
    produces it by recursive spatial split, so each run is spatially coherent.
    """
    count = len(boxes)
    if not count:
        return []
    nodes: List[Optional[Tuple[int, int, int, int, int]]] = [None] * count
    slots = iter(range(1, count))

    def centre(index: int, axis: int) -> float:
        return (boxes[index][axis] + boxes[index][axis + 3]) / 2.0

    def build(low: int, high: int, slot: int) -> int:
        middle = (low + high + 1) // 2
        axis, gap, low_mean, high_mean = 0, None, 0.0, 0.0
        for candidate in range(3):
            left = (sum(centre(i, candidate) for i in range(low, middle))
                    / (middle - low))
            right = (sum(centre(i, candidate) for i in range(middle, high + 1))
                     / (high + 1 - middle))
            # Signed, so the left run is the low side: the game draws the left
            # child first when the camera is below the split value.
            if gap is None or right - left > gap:
                axis, gap, low_mean, high_mean = candidate, right - left, left, right
        value = max(-32768, min(32767, int(round((low_mean + high_mean) / 2.0))))
        left_child = build(low, middle - 1, next(slots)) if middle - 1 > low else -1
        right_child = build(middle, high, next(slots)) if high > middle else -1
        nodes[slot] = (left_child, right_child, axis, middle, value)
        return slot

    if count > 1:
        build(0, count - 1, 0)
    return [node or (-1, -1, 0, 0, 0) for node in nodes]


def draw_order(bsp: Sequence[Sequence[int]], count: int,
               camera: Sequence[float] = (0.0, 0.0, 0.0),
               limit: int = 4096) -> List[int]:
    """The segments the game would draw, in order, before the PVS and the
    frustum test thin the list.

    ``traverse_segments_bsp_tree`` as the game runs it. Both children of every
    node are always visited - the camera only picks which goes first - so the
    *set* this returns is the same from every viewpoint. Indices come back as
    the game stores them, in a ``u8`` array: ``add_segment_to_order`` guards with
    a signed ``index < numberOfSegments``, so a run gone negative adds -1 and
    the array holds 255.
    """
    if count <= 1:
        return [0] if count == 1 else []
    found: List[int] = []
    x, y, z = camera

    def add(index: int) -> None:
        if index < count:
            found.append(index & 0xFF)

    def walk(node: int, low: int, high: int, depth: int) -> None:
        if depth > limit or len(found) > limit or not 0 <= node < len(bsp):
            raise LayoutError("the BSP does not end: node %d is out of the tree "
                              "or part of a cycle" % node)
        left, right, split_type, segment_index, split_value = bsp[node][:5]
        value = x if split_type == 0 else y if split_type == 1 else z

        def left_side():
            if left != -1:
                walk(left, low, segment_index - 1, depth + 1)
            else:
                add(low)

        def right_side():
            if right != -1:
                walk(right, segment_index, high, depth + 1)
            else:
                add(high)

        if value < split_value:
            left_side()
            right_side()
        else:
            right_side()
            left_side()

    walk(0, 0, count - 1, 0)
    return found


def bsp_problems(model: LevelModel) -> List[str]:
    """What is wrong with a model's BSP, as the game would walk it. Empty if
    it draws every segment exactly once."""
    count = len(model.segments)
    if count <= 1:
        return []
    try:
        order = draw_order(model.bsp, count)
    except LayoutError as error:
        return [str(error)]
    problems = []
    stray = sorted({index for index in order if index >= count})
    if stray:
        problems.append(
            "it draws segment %d of a %d-segment model; the game reads that "
            "segment out of the vertex data and crashes" % (stray[0], count))
    repeated = len(order) - len(set(order))
    if repeated:
        problems.append("it draws %d segment(s) twice" % repeated)
    missed = count - len({index for index in order if index < count})
    if missed:
        problems.append("it never draws %d segment(s), which show as holes"
                        % missed)
    return problems


def headroom_triangles(model: LevelModel) -> int:
    """How many more collidable triangles fit, which is the unit an author adds.

    One costs 16 bytes as a ``Triangle``, 8 as its facet reservation, 2 in
    ``unk10`` and 64 in collision planes. A triangle that opts out of collision
    costs 26 rather than 90, so this is the pessimistic count.
    """
    return max(0, (BUDGET - runtime_size(model)) // 90)
