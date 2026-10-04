"""Turn the author's edits to the geometry mesh back into a level model.

There are two ways back, and which one runs is decided by what the author did
rather than chosen.

**Nothing was added or removed.** The original ``.bin`` is reloaded and only the
differences are applied to it, so every array, offset, pad byte and
uninitialised scratch region the encoder has to reproduce comes back untouched
for free. An import and immediate export reproduces the shipped model byte for
byte. This is the path a reshaped track takes, and it is why reshaping is cheap.

**Counts changed.** Then every offset after the change shifts and the file has
to be built rather than patched: each segment is rebatched from the mesh and
:func:`level_model_layout.rebuild` lays the blob out afresh. That path cannot be
byte-identical to retail and is not meant to be - retail's padding between
arrays runs 0, 4, 8, 10 ... 770 bytes with no pattern, so a rebuild writes a
*valid* layout rather than *the* layout. Keeping the two paths apart is what
stops an unedited track losing its byte equality just because the code can now
rebuild one.

WHAT THE MESH HAS TO CARRY, AND WHY IT IS WHAT IT IS

Blender propagates both point and face attributes through extrude, subdivide
and duplicate - measured, not assumed: a face extruded from one flagged
``0x7ABCDE`` comes out flagged ``0x7ABCDE``, and its vertices inherit the
identity of the vertices they were pulled from. That is the mechanism this
module rests on, and it has one consequence worth stating plainly, because it
is the opposite of what it looks like:

**A new vertex does not arrive as zero.** Only a vertex conjured from nothing
does. One made by extruding arrives carrying its source's identity, so the same
``(segment, vertex)`` pair legitimately appears twice. In step 2 that was a
collision to refuse; here it is simply how Blender says "this came from that".
So identity is read as *which segment a vertex belongs to* plus *where it sits
in the pool*, never as a unique key. The bias by one still earns its place: it
is what separates "conjured from nothing" from "vertex 0 of segment 0".

Everything a rebuilt segment needs is read raw off the mesh rather than
converted back - the UVs from ``dkr_uv``, the baked colours from the separate
``dkr_colour_r/g/b/a`` channels (or packed ``dkr_colour`` in older scenes)
- because the values an author sees are normalised, V-flipped and sRGB, and
none of those survive the trip back exactly. A segment nobody touched has to
come out unchanged, which rules out a lossy conversion anywhere near it.
"""

from __future__ import annotations

import os
from typing import Dict, List, Optional, Tuple

import bpy

from .. import (level_model, level_model_edit, level_model_layout, prefs,
                scene, transparency as looks, water)
from . import geometry


class GeometryExportError(Exception):
    """Something about the mesh means an export would be a guess."""


class RebuildSummary:
    """What a layout-rebuilding export changed, in the shape EditSummary has."""

    __slots__ = ("moved", "flags", "vertices_added", "vertices_removed",
                 "faces_added", "faces_removed", "bounds", "surfaces",
                 "textures")

    def __init__(self, vertices_added=0, vertices_removed=0, faces_added=0,
                 faces_removed=0, moved=0, flags=0, textures=0):
        self.moved = moved
        self.flags = flags
        self.vertices_added = vertices_added
        self.vertices_removed = vertices_removed
        self.faces_added = faces_added
        self.faces_removed = faces_removed
        self.bounds = 0
        self.surfaces = 0
        #: Entries the author added to the track's texture table.
        self.textures = textures

    def describe(self) -> str:
        parts = []
        for count, one, many, verb in (
            (self.vertices_added, "vertex", "vertices", "added"),
            (self.vertices_removed, "vertex", "vertices", "removed"),
            (self.faces_added, "triangle", "triangles", "added"),
            (self.faces_removed, "triangle", "triangles", "removed"),
            (self.moved, "vertex", "vertices", "moved"),
            (self.flags, "batch", "batches", "reflagged"),
            (self.surfaces, "surface type", "surface types", "changed"),
            (self.textures, "texture", "textures", "added"),
        ):
            if count:
                parts.append("%d %s %s" % (count, one if count == 1 else many, verb))
        return ", ".join(parts) if parts else "the layout was rebuilt"


class GeometryEdit:
    """A model with the author's edits applied, ready to encode."""

    __slots__ = ("model", "summary", "path", "object", "notes", "rebuilt")

    def __init__(self, model, summary, path, obj, notes, rebuilt=False):  # noqa: D107
        self.model = model
        self.summary = summary
        self.path = path
        self.object = obj
        #: Things worth telling the author that are not worth stopping for.
        self.notes: List[str] = notes
        #: Whether the blob was laid out afresh rather than patched in place.
        self.rebuilt = rebuilt

    @property
    def edited(self) -> bool:
        """Whether the author actually changed anything about the geometry."""
        if self.rebuilt:
            return True
        return bool(self.summary.moved or self.summary.painted
                    or self.summary.flags or getattr(self.summary, "surfaces", 0))

    @property
    def ships(self) -> bool:
        """Whether the package has to carry a model payload.

        Not the same question as :attr:`edited`, and conflating the two shipped
        a track with no geometry in it. A remix that changed nothing needs no
        payload: its header goes on naming the track the game already has. A
        track built from a mesh, or one re-segmented, has no shipped track
        behind it - the only copy of its geometry is the author's own file, so
        leaving the payload out sends a header pointing at nothing.
        """
        return self.edited or self.authored_base

    @property
    def authored_base(self) -> bool:
        """Whether the base model is the author's file rather than the game's."""
        return bool(self.object.get(geometry.PROP_AUTHORED_BASE, False))

    def describe(self) -> str:
        return self.summary.describe()


# ---------------------------------------------------------------------------
# Finding the pieces
# ---------------------------------------------------------------------------

def base_model_path(context, obj) -> str:
    """The ``.bin`` the mesh was decoded from.

    Recorded on the object at import, because that is the file this particular
    mesh's vertex identities are indices into. The scene setting is the
    fallback for a mesh imported before the object carried it, and the asset
    tree is the last resort for a ``.blend`` moved to another machine.
    """
    candidates = [
        obj.get(geometry.PROP_MODEL_PATH),
        context.scene.dkr.geometry_path,
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(bpy.path.abspath(candidate)):
            return bpy.path.abspath(candidate)

    tree = prefs.resolve(context)
    source = context.scene.dkr.source_path or context.scene.dkr.geometry_path
    level = tree.level_using(source) if (tree and source) else None
    if level is not None and level.model_path and os.path.isfile(level.model_path):
        return level.model_path

    named = next((c for c in candidates if c), None)
    raise GeometryExportError(
        "the track geometry was imported from %s, which is not there any more, "
        "so there is no base model to apply the edits to. Re-import the "
        "geometry, or point the addon preferences at the extracted assets"
        % (os.path.basename(named) if named else "an unrecorded file")
    )


def _attribute(mesh, name: str, domain: str, data_type: str, width: int = 1):
    attribute = mesh.attributes.get(name)
    if attribute is None:
        return None
    if attribute.domain != domain or attribute.data_type != data_type:
        return None
    values = [0] * (len(attribute.data) * width)
    attribute.data.foreach_get("value", values)
    return values


def _require_schema(mesh) -> None:
    """Refuse a mesh whose attributes do not mean what this version thinks.

    The identity attributes went biased by one, so reading an older mesh would
    not fail - it would quietly place every vertex one slot off and name a
    segment that exists. Silent and wrong is the reason this is a refusal and
    not a guess.
    """
    schema = mesh.get(geometry.PROP_SCHEMA)
    if schema is None:
        raise GeometryExportError(
            "%s was imported by an older version of the addon, which stored the "
            "vertex identities differently. Reading it now would place every "
            "vertex one slot off rather than fail, so it is refused: delete the "
            "geometry and import the track again"
            % mesh.name
        )
    if int(schema) != geometry.SCHEMA:
        raise GeometryExportError(
            "%s carries geometry schema %d and this addon writes %d; import the "
            "track geometry again" % (mesh.name, int(schema), geometry.SCHEMA)
        )


# ---------------------------------------------------------------------------
# Reading the mesh
# ---------------------------------------------------------------------------

class MeshRead:
    """The mesh, re-expressed in the file's own terms, one entry per segment."""

    __slots__ = ("faces", "positions", "colours", "pool_of", "notes",
                 "watered")

    def __init__(self):
        #: segment -> [layout.Face, ...]
        self.faces: Dict[int, List] = {}
        #: segment -> [(x, y, z), ...] in map space
        self.positions: Dict[int, List] = {}
        #: segment -> [(r, g, b, a), ...]
        self.colours: Dict[int, List] = {}
        #: segment -> {blender vertex index: pool index}
        self.pool_of: Dict[int, Dict[int, int]] = {}
        self.notes: List[str] = []
        #: ``{(segment, key)}``: the draw calls of faded calm water that were
        #: given ``RENDER_WATER`` to be drawn in the second pass.
        self.watered = set()


def _segment_of_vertex(mesh, segments_biased, model, face_segments=None) -> List[int]:
    """Which segment each Blender vertex belongs to.

    Inherited from the vertex it was made from, which covers extrude, subdivide
    and duplicate - Blender propagates the attribute, so ordinary modelling
    needs no help. A vertex conjured from nothing has no source, and the only
    thing that can speak for it is the faces it is part of.

    The answer spreads outwards rather than being read once: a face touching
    existing track names its new corners, those corners name the next face
    along, and so on until nothing more can be reached. A single pass would
    only reach geometry directly attached to the road and would refuse a ramp
    built two faces out from it, which is a perfectly ordinary thing to model.
    """
    assigned = [value - 1 for value in segments_biased]
    if face_segments is None:
        _drop_mixed_claims(mesh, assigned)
    else:
        # A welded POINT attribute may name a segment neither source used.
        # FACE attributes survive welding, so use the incident faces instead.
        votes = [{} for _ in assigned]
        for polygon, segment in zip(mesh.polygons, face_segments):
            if not 0 < segment <= len(model.segments):
                continue
            for vertex in polygon.vertices:
                counts = votes[vertex]
                counts[segment - 1] = counts.get(segment - 1, 0) + 1
        for vertex, counts in enumerate(votes):
            if counts:
                assigned[vertex] = max(sorted(counts), key=counts.get)
    unknown = {at for at, value in enumerate(assigned) if value < 0}
    if not unknown:
        _check_segment_range(assigned, model)
        return assigned

    faces = [
        list(polygon.vertices) for polygon in mesh.polygons
        if any(vertex in unknown for vertex in polygon.vertices)
    ]

    while unknown:
        spreading = True
        while spreading and unknown:
            spreading = False
            for vertices in faces:
                reachable = {assigned[v] for v in vertices if assigned[v] >= 0}
                if len(reachable) != 1:
                    continue
                segment_index = next(iter(reachable))
                for vertex in vertices:
                    if vertex in unknown:
                        assigned[vertex] = segment_index
                        unknown.discard(vertex)
                        spreading = True
        if not unknown:
            break
        # Nothing unanimous is left, so what remains sits between two
        # segments - a gap filled across the join. Either segment is a right
        # answer: the face that ends up crossing is written into one of them
        # with copies of the corners the other owns (see read_mesh). The lower
        # index is taken so the result does not depend on face order.
        settled = False
        for vertices in faces:
            reachable = {assigned[v] for v in vertices if assigned[v] >= 0}
            if len(reachable) < 2:
                continue
            choice = min(reachable)
            for vertex in vertices:
                if vertex in unknown:
                    assigned[vertex] = choice
                    unknown.discard(vertex)
                    settled = True
            if settled:
                break
        if not settled:
            break

    if unknown:
        raise GeometryExportError(_unplaceable_message(mesh, unknown, assigned))

    _check_segment_range(assigned, model)
    return assigned


def _drop_mixed_claims(mesh, assigned) -> int:
    """Forget segment claims that Merge by Distance made up. Returns how many.

    Blender averages integer attributes when it welds vertices: a boundary
    vertex of segment 1 merged with its twin in segment 3 comes out claiming
    segment 2, which may lie on the far side of the track, and every face
    using it would stretch that segment's box across the map - crowding the
    ten-slot collision candidate list and the culling with it. Measured on
    Ancient Lake, a full-mesh merge left 32 such vertices and six segments
    spanning more than half the track.

    A claim that none of the vertex's face-neighbours shares is such a mix, so
    it is dropped and the vertex placed by its faces, like one made from
    nothing. Retail never trips this: a segment's vertices are its own, so
    every neighbour of a vertex shares its segment, and an untouched track
    still exports byte for byte.
    """
    claimed = list(assigned)
    neighbours = [set() for _ in claimed]
    for polygon in mesh.polygons:
        corners = list(polygon.vertices)
        for vertex in corners:
            neighbours[vertex].update(corners)
    dropped = 0
    for vertex, value in enumerate(claimed):
        if value < 0:
            continue
        around = {claimed[n] for n in neighbours[vertex]
                  if n != vertex and claimed[n] >= 0}
        if around and value not in around:
            assigned[vertex] = -1
            dropped += 1
    return dropped


def _unplaceable_message(mesh, unknown, assigned) -> str:
    """Say which of the three ways a vertex ended up with nowhere to go.

    They need different fixes, so they need different messages. Naming an index
    alone is no use either - Blender gives an author no way to jump to vertex
    1671 - so each one names the command that finds them.
    """
    in_a_face = set()
    conflicted = set()
    for polygon in mesh.polygons:
        reachable = {assigned[v] for v in polygon.vertices if assigned[v] >= 0}
        for vertex in polygon.vertices:
            if vertex in unknown:
                in_a_face.add(vertex)
                if len(reachable) > 1:
                    conflicted.add(vertex)

    loose = sorted(unknown - in_a_face)
    detached = sorted(in_a_face - conflicted)
    straddling = sorted(conflicted)

    if loose:
        return (
            "%s not part of any face. The track is divided into segments and "
            "every vertex has to belong to one; a vertex built from nothing "
            "inherits no segment, and with no face there is nothing to tell it "
            "which. In Edit Mode, Select > All by Trait > Loose Geometry selects "
            "exactly these - then X > Vertices to delete them, or build a face "
            "on them to attach them to the track." % _count(loose)
        )

    if detached:
        return (
            "%s part of new geometry that no face joins to the track. New "
            "geometry takes its segment from what it is attached to, so an "
            "island floating free of the road has nothing to take it from. "
            "Extrude out from the existing track instead of building "
            "separately, or select an edge of each and press F to bridge them."
            % _count(detached)
        )

    return (
        "%s part of new geometry whose faces reach two different segments of "
        "the track, so which one it belongs to has no answer. A triangle "
        "belongs to exactly one segment - build outwards from a single segment "
        "rather than across the join between two." % _count(straddling)
    )


def _count(indices) -> str:
    """Name the vertices, in a form that reads for one as well as for fifty."""
    shown = ", ".join(str(index) for index in indices[:6])
    if len(indices) > 6:
        shown += " and %d more" % (len(indices) - 6)
    if len(indices) == 1:
        return "Mesh vertex %s is" % shown
    return "%d mesh vertices (%s) are" % (len(indices), shown)


def _check_segment_range(assigned, model) -> None:
    for at, value in enumerate(assigned):
        if not 0 <= value < len(model.segments):
            raise GeometryExportError(
                "mesh vertex %d claims segment %d, which the model does not "
                "have; it holds %d. The mesh and the base model do not match, "
                "so re-import the geometry"
                % (at, value, len(model.segments))
            )


def read_mesh(obj, model, translucency=None, fades=None,
              surfaces=None) -> MeshRead:
    """Re-express the mesh as loose faces and one vertex pool per segment.

    ``translucency`` is :func:`.geometry.table_translucency` for the mesh's
    texture table. Where it knows a face's texture, the face's side of
    ``numberofOpaqueBatches`` is the one the game will draw it on, whatever the
    face carried; see :mod:`..transparency`.

    ``fades`` is :func:`.geometry.opacities` and ``surfaces`` is
    :func:`.geometry.surface_types`: a faded face of calm water that would be
    drawn in the first pass is flagged ``RENDER_WATER`` to be drawn in the
    second, by :func:`..transparency.faded_flags`.
    """
    translucency = translucency or {}
    fades = fades or {}
    surfaces = surfaces or {}
    mesh = obj.data
    _require_schema(mesh)

    count = len(mesh.vertices)
    segments_biased = _attribute(mesh, geometry.ATTR_SEGMENT, "POINT", "INT")
    vertices_biased = _attribute(mesh, geometry.ATTR_VERTEX, "POINT", "INT")
    raw_colours = _attribute(mesh, geometry.ATTR_COLOUR, "POINT", "INT")
    for name, values in ((geometry.ATTR_SEGMENT, segments_biased),
                         (geometry.ATTR_VERTEX, vertices_biased),
                         (geometry.ATTR_COLOUR, raw_colours)):
        if values is None or len(values) != count:
            raise GeometryExportError(
                "%s is missing its %s vertex attribute, or it does not match "
                "the mesh; import the track geometry again" % (mesh.name, name)
            )

    face_count = len(mesh.polygons)
    flags = _attribute(mesh, geometry.ATTR_FLAGS, "FACE", "INT")
    serials = _attribute(mesh, geometry.ATTR_SERIAL, "FACE", "INT")
    tri_flags = _attribute(mesh, geometry.ATTR_TRI_FLAGS, "FACE", "INT")
    textures = _attribute(mesh, geometry.ATTR_TEXTURE, "FACE", "INT")
    opaque = _attribute(mesh, geometry.ATTR_OPAQUE, "FACE", "BOOLEAN")
    for name, values in ((geometry.ATTR_FLAGS, flags),
                         (geometry.ATTR_SERIAL, serials),
                         (geometry.ATTR_TRI_FLAGS, tri_flags),
                         (geometry.ATTR_TEXTURE, textures),
                         (geometry.ATTR_OPAQUE, opaque)):
        if values is None or len(values) != face_count:
            raise GeometryExportError(
                "%s is missing its %s face attribute, or it does not match the "
                "mesh; import the track geometry again" % (mesh.name, name)
            )
    uvs = _attribute(mesh, geometry.ATTR_UV, "CORNER", "INT32_2D", width=2)
    if uvs is None or len(uvs) != len(mesh.loops) * 2:
        raise GeometryExportError(
            "%s is missing its %s corner attribute, or it does not match the "
            "mesh; import the track geometry again"
            % (mesh.name, geometry.ATTR_UV)
        )

    face_segments = _attribute(mesh, geometry.ATTR_FACE_SEGMENT, "FACE", "INT")
    owner = _segment_of_vertex(mesh, segments_biased, model, face_segments)
    channels = [_attribute(mesh, name, "POINT", "INT")
                for name in geometry.ATTR_COLOUR_CHANNELS]
    colours = (list(zip(*channels)) if all(c is not None for c in channels)
               else [geometry.unpack_colour(value) for value in raw_colours])
    read = MeshRead()
    matrix = obj.matrix_world

    # Pool order is the file's order wherever the file still decides it: a
    # vertex keeps the slot it came from, one made from nothing goes last, and
    # a clone sits immediately after the vertex it was cloned from. An untouched
    # segment therefore comes back in exactly its own order, which is what makes
    # a rebatch of it the identity.
    members: Dict[int, List[int]] = {}
    for at, segment_index in enumerate(owner):
        members.setdefault(segment_index, []).append(at)

    for segment_index, indices in members.items():
        indices.sort(key=lambda at: (vertices_biased[at] or (1 << 30), at))
        read.pool_of[segment_index] = {at: pool for pool, at in enumerate(indices)}
        read.positions[segment_index] = [
            _map_position(matrix, mesh.vertices[at], at) for at in indices
        ]
        read.colours[segment_index] = [
            colours[at] for at in indices
        ]
        read.faces[segment_index] = []

    crossing = copied = 0
    for polygon in mesh.polygons:
        index = polygon.index
        verts = list(polygon.vertices)
        owners = [owner[v] for v in verts]
        source_segment = (face_segments[index] - 1
                          if face_segments is not None else -1)
        if 0 <= source_segment < len(model.segments):
            segment_index = source_segment
        elif len(set(owners)) == 1:
            segment_index = owners[0]
        else:
            # A face across the join between segments - a merge, a fill or a
            # bridge over the boundary. A triangle lives in exactly one segment
            # and a segment's vertices are its own, so the face goes to the
            # segment owning most of its corners and the others are copied into
            # that segment's pool. Retail never shares a vertex between
            # segments either: every boundary is a pair of coincident copies.
            segment_index = max(sorted(set(owners)), key=owners.count)
        if any(owner[v] != segment_index for v in verts):
            crossing += 1
            pool = read.pool_of.setdefault(segment_index, {})
            read.positions.setdefault(segment_index, [])
            read.colours.setdefault(segment_index, [])
            read.faces.setdefault(segment_index, [])
            for vertex in verts:
                if owner[vertex] != segment_index and vertex not in pool:
                    pool[vertex] = len(read.positions[segment_index])
                    read.positions[segment_index].append(
                        _map_position(matrix, mesh.vertices[vertex], vertex))
                    read.colours[segment_index].append(
                        colours[vertex])
                    copied += 1
        pool = read.pool_of[segment_index]

        serial = serials[index] - 1
        texture = textures[index] & 0xFF
        face_flags = geometry.to_unsigned32(flags[index])
        side = bool(opaque[index])
        watered = False
        if texture != level_model.NO_TEXTURE and texture in translucency:
            side = looks.draws_in_opaque_pass(face_flags, translucency[texture])
            if side and (texture, geometry.category_of(face_flags)) in fades:
                moved = looks.faded_flags(face_flags, translucency[texture],
                                          surfaces.get(texture))
                watered = moved != face_flags
                if watered:
                    face_flags, side = moved, False
        key = _batch_key(model, segment_index, serial, face_flags,
                         texture, side)
        if watered:
            read.watered.add((segment_index, key))

        corners = list(polygon.loop_indices)
        # The file stores triangles. Blender's extrude makes quads out of the
        # sides it sweeps, so anything with more than three corners is fanned
        # rather than refused - refusing would make extrude unusable, which is
        # the commonest way anyone adds geometry at all.
        for corner in range(1, len(verts) - 1):
            picks = (0, corner, corner + 1)
            read.faces[segment_index].append(level_model_layout.Face(
                key,
                tuple(pool[verts[p]] for p in picks),
                tuple((uvs[corners[p] * 2], uvs[corners[p] * 2 + 1])
                      for p in picks),
                tri_flags[index] & 0xFF,
            ))
    if crossing:
        read.notes.append(
            "%d face(s) crossed between segments; each was written into one of "
            "them, with %d corner vertex copies, since a segment's vertices are "
            "its own" % (crossing, copied))
    return read


def _map_position(matrix, vertex, at):
    """One vertex in the ``s16`` the file stores, refused rather than wrapped."""
    position = scene.to_map(matrix @ vertex.co)
    rounded = tuple(int(round(float(c))) for c in position)
    for component in rounded:
        if not -32768 <= component <= 32767:
            raise GeometryExportError(
                "mesh vertex %d would sit at %r, outside the s16 a level model "
                "stores positions in; a vertex that silently reappeared on the "
                "far side of the track would be far harder to find than this"
                % (at, rounded)
            )
    return rounded


def _batch_key(model, segment_index, serial, flags, texture, opaque):
    """The draw call a face belongs to.

    ``misc``, ``texture_offset`` and ``vertex_override`` are read off the batch
    the face came from rather than carried on the mesh: nothing exposes them to
    an author, and a face that grew out of an existing one inherits its serial,
    so the lookup is exact wherever they are not zero. A face with no source
    takes the zeroes, which is what every retail batch that has never needed
    them holds.
    """
    misc = texture_offset = vertex_override = 0
    if serial >= 0:
        batches = model.segments[segment_index].batches
        if serial < len(batches):
            source = batches[serial]
            misc = source.misc
            texture_offset = source.texture_offset
            vertex_override = source.vertex_override
        else:
            serial = -1
    return level_model_layout.BatchKey(
        texture, geometry.to_unsigned32(flags), misc, texture_offset,
        vertex_override, opaque, None if serial < 0 else serial,
    )


# ---------------------------------------------------------------------------
# Which way back
# ---------------------------------------------------------------------------

def _structure_of(faces) -> list:
    """A segment's faces without their render flags, for comparison.

    Flags are left out on purpose: an author who only reflagged faces has not
    changed the shape of anything, and should still take the path that patches
    the file in place and comes out byte-identical everywhere else.
    """
    return [
        (face.key.texture_index, face.key.misc, face.key.texture_offset,
         face.key.vertex_override, face.key.opaque, face.key.serial,
         face.vertices, face.uvs, face.flags)
        for face in faces
    ]


def _importable(faces, pool_size, include_hidden=True) -> list:
    """The faces of a decomposed segment that a Blender mesh can actually hold.

    A triangle naming the same vertex twice has no representation in Blender at
    all - retail ships 8 of them - so the importer leaves them out. Comparing
    against the raw decomposition would therefore report those four tracks as
    edited the moment they were opened, and send an untouched track down the
    rebuilding path, which is exactly the byte equality this split exists to
    protect.
    """
    return [
        face for face in faces
        if len(set(face.vertices)) == 3
        and 0 <= min(face.vertices) and max(face.vertices) < pool_size
        and (include_hidden or not face.key.flags & level_model.RENDER_HIDDEN)
    ]


def _topology_changed(read, model, include_hidden=True) -> bool:
    """Whether the mesh says anything the file's own layout cannot express.

    Compared against what the import was ever going to build, not against the
    whole file: a track loaded without its invisible walls is missing their
    faces by design, and reading that as an edit would send a merely reshaped
    track down the rebuilding path and lose its byte equality.
    """
    for index, segment in enumerate(model.segments):
        original, positions, _colours = level_model_layout.decompose(segment)
        if len(read.positions.get(index, [])) != len(positions):
            return True
        mine = _structure_of(read.faces.get(index, []))
        theirs = _structure_of(
            _importable(original, len(positions), include_hidden)
        )
        if mine != theirs:
            return True
    return False


def _flag_edits(read, model):
    """``{(segment, batch): flags}``, or ``None`` if a batch disagrees with itself.

    ``None`` is not a failure here - it means the in-place path cannot express
    what the author did, and the rebuilding path can, by giving the faces that
    differ a batch of their own.
    """
    gathered: Dict[Tuple[int, int], set] = {}
    for segment_index, faces in read.faces.items():
        for face in faces:
            if face.key.serial is None:
                return None
            gathered.setdefault((segment_index, face.key.serial), set()).add(
                face.key.flags
            )
    edits = {}
    for key, values in gathered.items():
        if len(values) != 1:
            return None
        edits[key] = next(iter(values))
    return edits


# ---------------------------------------------------------------------------
# One call for the export path
# ---------------------------------------------------------------------------

def build_edited_model(context) -> Optional[GeometryEdit]:
    """Apply what the author changed to the base model, or return ``None``.

    ``None`` means the scene holds no track geometry at all, which is a
    perfectly good track: a remix that reworks the objects standing on shipped
    geometry ships no model payload.
    """
    targets = geometry.geometry_objects(context)
    if not targets:
        return None
    if len(targets) > 1:
        raise GeometryExportError(
            "the scene holds %d track-geometry meshes (%s) and there has to be "
            "exactly one - a vertex that exists twice cannot say which of the "
            "two the author moved. Earlier versions of the addon split the "
            "track into surface, decoration and invisible walls; delete them "
            "and import the geometry again"
            % (len(targets), ", ".join(sorted(o.name for o in targets)))
        )

    obj = targets[0]
    mesh = obj.data
    path = base_model_path(context, obj)

    try:
        model = level_model.load(path)
    except level_model.LevelModelError as error:
        raise GeometryExportError(
            "could not read the base geometry %s: %s" % (os.path.basename(path), error)
        )

    problems = level_model_layout.check_windows(model)
    if problems:
        raise GeometryExportError(
            "%s has %d batch window problem(s), so a face cannot be resolved to "
            "the batch whose flags it carries: %s"
            % (os.path.basename(path), len(problems), problems[0])
        )

    # ``matrix_world`` is derived, and an object linked or moved since the last
    # depsgraph evaluation still carries the identity matrix.
    context.view_layer.update()

    conflicts = geometry.surface_conflicts(obj)
    if conflicts:
        raise GeometryExportError(
            "%d texture entr%s given two different surface types: %s. What the "
            "ground behaves like belongs to the texture table entry, so every "
            "material drawing that entry has to agree"
            % (len(conflicts), "y is" if len(conflicts) == 1 else "ies are",
               "; ".join(conflicts[:3]))
        )

    translucency = texture_translucency(context, obj)
    promoted = _promoted_by_fade(context, obj, translucency)
    read = read_mesh(obj, model, translucency, geometry.opacities(obj),
                     geometry.surface_types(obj))
    notes: List[str] = list(read.notes)
    moved = _sides_moved(read, model, promoted)
    if moved:
        notes.append(
            "%d draw call(s) move between the solid and the see-through pass "
            "to follow their texture's transparency - the game draws a batch "
            "only in the pass its texture belongs to" % moved)

    added = _add_textures(obj, model, notes)

    include_hidden = bool(obj.get(geometry.PROP_INCLUDE_HIDDEN, True))
    flags = _flag_edits(read, model)
    # A texture table that grew cannot be written at the offsets the file was
    # read from: it is the first array after the header, so an extra entry does
    # not run off the end - it runs into the segment array. Nothing else about
    # the mesh has to have changed for that to be true, so the path is forced
    # here rather than left to the topology comparison to notice.
    draw_counts_fit = all(
        batch.face_count <= level_model_layout.MAX_BATCH_TRIANGLES
        for segment in model.segments for batch in segment.batches)
    if (not added and flags is not None and draw_counts_fit
            and not _topology_changed(read, model, include_hidden)):
        edit = _patch_in_place(model, read, flags, path, obj, notes)
    else:
        edit = _rebuild(model, read, path, obj, notes, added)
    _explain_fades(edit, read, promoted)
    _settle_waves(context, edit)
    return edit


def _settle_waves(context, edit) -> None:
    """Put a track with waves back on its grid if an edit knocked it off.

    The game places every segment on the wave grid by its box corner, so
    moving a vertex across a tile's edge, extruding past it or deleting the
    reference water each break the waves somewhere with nothing to say why.
    :func:`..water.problems` finds exactly those, and a retail track has none of
    them, so an untouched remix keeps its bytes; anything it does find is fixed
    by cutting the track into the grid again, and the export says so.
    """
    model = edit.model
    found = water.problems(model)
    if found:
        try:
            count = level_model_layout.resegment(model)
        except level_model_layout.LayoutError as error:
            raise GeometryExportError(
                "the track has wave water and could not be cut into its wave "
                "grid again (%s): %s" % (found[0], error))
        edit.rebuilt = True
        edit.notes.append(
            "the wave water needed the track cut into its grid again - %s. It "
            "now has %d segments" % (found[0], count))
        _record(edit.object, model, edit.notes)
    translucent = _waves_translucent(context)
    texture = _reference_texture(model)
    edit.notes.extend(water.notes(model, texture, translucent))


def _waves_translucent(context) -> bool:
    """The header's ``wavesXlu`` as the export will write it."""
    from . import header as header_ops  # noqa: PLC0415

    value = context.scene.get(header_ops.key_for("/waves/unk70"))
    if value is None:
        value = header_ops.surveyed("/waves/unk70")
    try:
        return bool(int(value if value is not None else 1))
    except (TypeError, ValueError):
        return True


def _reference_texture(model):
    grid = water.simulate(model)
    if grid is None or grid.reference is None:
        return None
    for batch in model.segments[grid.reference].batches:
        if water.is_reference(batch.flags):
            texture = model.texture_for(batch)
            if texture is None:
                return None
            return texture.width, texture.height, texture.format & 0xF
    return None


def texture_translucency(context, obj, exported=True) -> dict:
    """:func:`.geometry.table_translucency` for this mesh's texture table.

    ``exported`` reads the track's own textures as the package writes them,
    where a faded one is see-through; ``False`` reads them as their looks say.
    """
    from . import custom_textures  # noqa: PLC0415 - it imports geometry

    own = (custom_textures.exported(context) if exported
           else custom_textures.entries(context))
    return geometry.table_translucency(
        [record.get("id", 0) for record in geometry.texture_table(obj)],
        prefs.resolve(context), own)


def _promoted_by_fade(context, obj, translucency) -> set:
    """The table entries whose texture is written see-through only because a
    material fades faces drawing it."""
    looked = texture_translucency(context, obj, exported=False)
    return {index for index, see_through in translucency.items()
            if see_through and not looked.get(index, True)}


def _sides_moved(read, model, promoted=()) -> int:
    """How many source batches the derived opaque side moves.

    Only faces that still name their source batch are counted, so a face the
    author added is not reported as a correction. Nor are the ones a fade moved,
    which :func:`_explain_fades` reports: faded water given ``RENDER_WATER``,
    and faces whose texture is see-through only because of a fade.
    """
    moved = set()
    for segment_index, faces in read.faces.items():
        segment = model.segments[segment_index]
        for face in faces:
            serial = face.key.serial
            if serial is None or serial >= len(segment.batches):
                continue
            if ((segment_index, face.key) in read.watered
                    or face.key.texture_index in promoted):
                continue
            if face.key.texture_index != segment.batches[serial].texture_index:
                continue
            if face.key.opaque != (serial < segment.opaque_batches):
                moved.add((segment_index, serial))
    return len(moved)


def _add_textures(obj, model, notes) -> int:
    """Give the model the textures the author picked, and return how many.

    A face names its texture by table index, so the mesh's indices and the
    model's table have to agree exactly. Two things make that checkable rather
    than hoped for: the base table is recorded on the object at import, so a
    mesh built against a different model is caught before anything is written;
    and each addition has to land at the index the mesh already assumed, which
    is what would break first if the two ever drifted apart.
    """
    base = geometry.base_textures(obj)
    extras = geometry.extra_textures(obj)
    recorded = geometry.PROP_BASE_TEXTURES in obj
    if recorded and len(base) != len(model.textures):
        raise GeometryExportError(
            "%s was imported from a model with %d textures and the base file "
            "now has %d, so the texture a face names may not be the one it "
            "meant. Import the track geometry again"
            % (obj.data.name, len(base), len(model.textures))
        )
    if not extras:
        return 0

    animated = 0
    for position, record in enumerate(extras):
        try:
            # These entries already have identities on the mesh. Identical
            # images may be separate TexScroll targets and must stay separate.
            index = level_model_edit.add_texture(
                model,
                record.get("id", 0), record.get("w", 0), record.get("h", 0),
                record.get("format", 1), record.get("surface", 0),
                dedicated=True,
            )
        except level_model_edit.EditError as error:
            raise GeometryExportError(str(error))
        if index != len(base) + position:
            raise GeometryExportError(
                "texture %d landed at table index %d instead of %d, so the "
                "faces drawing it would draw something else. Import the track "
                "geometry again" % (record.get("id", -1), index,
                                    len(base) + position)
            )
        if int(record.get("anim", 1) or 1) > 1:
            animated += 1

    if level_model_edit.set_animation_gate(model, animated):
        notes.append(
            "%d of the textures you added are animated, and this track declared "
            "no animated textures at all - which is a gate the renderer tests "
            "before it advances any of them, so they would have been drawn "
            "frozen. It is now open" % animated
        )

    # No note for the addition itself: the summary already says how many
    # textures were added, and notes are reported as warnings. Something that
    # worked is not a warning.
    return len(extras)


def _patch_in_place(model, read, flags, path, obj, notes):
    """Nothing was added or removed, so the file keeps its own layout."""
    positions = {}
    for segment_index, pool in read.positions.items():
        for pool_index, position in enumerate(pool):
            positions[(segment_index, pool_index)] = position

    # The baked lighting travels the same pool indices as the positions, and it
    # has to travel: it multiplies into the texture, so a track whose colours
    # were repainted and not written renders unlit - black, in the worst case -
    # while every other edit in the same export lands. A rebuild has always
    # carried them; this path silently did not.
    colours = {}
    for segment_index, pool in read.colours.items():
        for pool_index, colour in enumerate(pool):
            colours[(segment_index, pool_index)] = colour

    try:
        summary = level_model_edit.apply(
            model, positions=positions, colours=colours, batch_flags=flags,
            surface_types=geometry.surface_types(obj),
        )
    except level_model_edit.EditError as error:
        raise GeometryExportError(str(error))

    summary.painted += _fade(model, obj, notes)
    _record(obj, model, notes)
    return GeometryEdit(model, summary, path, obj, notes, rebuilt=False)


def _rebuild(model, read, path, obj, notes, textures_added=0):
    """Counts changed, so every offset moves and the blob is laid out afresh."""
    omitted = int(obj.get(geometry.PROP_OMITTED, 0) or 0)
    if omitted:
        raise GeometryExportError(
            "this mesh is missing %d face(s) the track has, so rebuilding it "
            "would delete them from the track. That happens when the geometry "
            "was imported without Include Invisible Walls. Import it again with "
            "that on, then redo the edit" % omitted
        )

    before_vertices = model.vertex_count
    before_faces = model.triangle_count
    degenerate = int(obj.get(geometry.PROP_DEGENERATE, 0) or 0)
    # A tile whose wave water the author deleted must stop drawing waves; one
    # that never had any keeps whatever retail gave it.
    had_waves = [any(water.is_wavy(b.flags) for b in segment.batches)
                 for segment in model.segments]

    for index, segment in enumerate(model.segments):
        try:
            level_model_layout.rebatch_segment(
                segment,
                read.faces.get(index, []),
                read.positions.get(index, []),
                read.colours.get(index, []),
            )
        except level_model_layout.LayoutError as error:
            raise GeometryExportError(
                "segment %d could not be rebuilt: %s" % (index, error)
            )
        wavy = any(water.is_wavy(b.flags) for b in segment.batches)
        if had_waves[index] and not wavy:
            segment.has_waves = 0
        elif wavy and not segment.has_waves:
            segment.has_waves = water.HAS_WAVES

    # Surface types ride on the texture table, which re-segmenting and
    # re-batching never touch, so they are applied straight rather than through
    # the layout - but they still have to be applied, or an edit made on the
    # same trip as an added triangle would be dropped.
    try:
        surfaces = level_model_edit.set_surface_types(
            model, geometry.surface_types(obj)
        )
    except level_model_edit.EditError as error:
        raise GeometryExportError(str(error))

    bounds = level_model_edit.recompute_bounds(model)
    try:
        level_model_layout.rebuild(model)
    except level_model_layout.LayoutError as error:
        raise GeometryExportError("the model could not be laid out: %s" % error)

    summary = RebuildSummary(
        vertices_added=max(0, model.vertex_count - before_vertices),
        vertices_removed=max(0, before_vertices - model.vertex_count),
        faces_added=max(0, model.triangle_count - before_faces),
        faces_removed=max(0, before_faces - model.triangle_count),
        textures=textures_added,
    )
    summary.bounds = bounds
    summary.surfaces = surfaces
    _fade(model, obj, notes)

    if summary.faces_added:
        notes.append(
            "%d new triangle(s) inherited the UVs of the face they grew from, "
            "so the texture stretches across them rather than tiling. The "
            "format stores a UV as s16 with five fractional bits, which caps a "
            "face at 1024 texels, so a very large face cannot be made to tile "
            "proportionally at all" % summary.faces_added
        )

    if degenerate:
        notes.append(
            "%d triangle(s) in this track name the same vertex twice, which "
            "Blender cannot hold, so the rebuilt model does not have them. That "
            "is the one thing a rebuild loses" % degenerate
        )

    _record(obj, model, notes)
    return GeometryEdit(model, summary, path, obj, notes, rebuilt=True)


def _fade(model, obj, notes) -> int:
    """Write the materials' opacity into the vertices it fades. Returns how many.

    Last, after every other edit, because the colour it writes replaces the
    baked light the mesh carries for those vertices; see :mod:`..transparency`.
    """
    table = geometry.opacities(obj)
    if not table:
        return 0
    batches, vertices = looks.apply_opacity(model, geometry.batch_alpha(table))
    if batches:
        notes.append(
            "%d draw call(s) are faded by their material's opacity. The game "
            "draws them with a flat grey in place of their baked light and "
            "without fog" % batches)
    return vertices


def _explain_fades(edit, read, promoted) -> None:
    """Say how the faded faces reach the second pass, and which cannot.

    A faded face in the first pass hides everything behind it, so the export
    moves it; see :mod:`..transparency`. What it could not move, and what moved
    along with a texture the fade made see-through, is the author's to know.
    """
    table = geometry.opacities(edit.object)
    if not table:
        return
    alpha_for = geometry.batch_alpha(table)
    batches = [batch for segment in edit.model.segments
               for batch in segment.batches]
    if read.watered:
        edit.notes.append(
            "%d draw call(s) of faded calm water are flagged RENDER_WATER, as "
            "the game's own water is, so they are drawn after the solid track "
            "and show what is under them. No shadows fall on them"
            % len(read.watered))
    if promoted:
        drawing = [batch for batch in batches
                   if batch.texture_index in promoted]
        solid = sum(1 for batch in drawing if alpha_for(batch) is None)
        note = ("%d draw call(s) are faded over a texture of the track's own, "
                "which is written see-through so they are drawn after the "
                "solid track" % (len(drawing) - solid))
        if solid:
            note += (". %d other draw call(s) draw that texture unfaded and are "
                     "drawn after the solid track with it; give the faded faces "
                     "a copy of the picture if that shows" % solid)
        edit.notes.append(note)
    stuck = looks.faded_in_opaque_pass(edit.model, alpha_for)
    if stuck:
        edit.notes.append(
            "%d faded draw call(s) are still drawn with the solid track, where "
            "the game draws them before what is behind them and then hides it "
            "all. Only a texture of the track's own, or calm water, can be "
            "moved: use one, or give the faces a see-through texture" % stuck)


def _record(obj, model, notes) -> None:
    """Stash the load cost, and say so when the track will not fit."""
    geometry.record_budget(obj, model)
    # The quieter of the two ceilings. A memory overflow at least writes a debug
    # print; crowding the collision candidate list produces no diagnostic at
    # all, and the symptom appears somewhere other than the cause.
    notes.extend(level_model_layout.check_collision_pressure(model))
    used = level_model_layout.runtime_size(model)
    if used > level_model_layout.BUDGET:
        notes.append(
            "this track needs %d bytes at load, over the %d the game reserves. "
            "The overflow does not refuse - it writes past the heap - so this "
            "has to come down before the track is played"
            % (used, level_model_layout.BUDGET)
        )
