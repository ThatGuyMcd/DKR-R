"""Apply an author's geometry edits to a decoded level model.

Step 2 of Phase 2: reshape a track without changing how much of it there is.
Vertices move, colours and textures change, batches switch between drawn, solid
and both - and every count and offset stays what it was, so
:mod:`level_model_encoder` can write the result at the offsets it was read from.

What has to be recomputed when a vertex moves is exactly two things, and both
rules were measured against every extracted retail model rather than assumed:

* **Segment bounding boxes** are the axis-aligned bounds of the segment's own
  vertices. Exactly so in 1008 of 1146 segments; the other 138 are the ones
  whose vertices are all at the same height, and every one of those has its
  ``y2`` one greater. A zero-height box is what that guards against, and no
  segment in the retail set is flat in X or Z, so nothing says the guard applies
  to those axes and it is not applied here.
* **The header bounds** at 0x3C are the same thing over every vertex in the
  model. Exactly so in all 55 models.

What deliberately is *not* recomputed is the BSP. It reads as a containment
tree, and it is not one: walking every retail model, 132 of 1977 segment-to-
split relationships have the segment poking out of its own half-space, by a
median of 8 units and as much as 5440. It partitions the camera, not the
geometry, so a topology-preserving edit leaves it alone - and a check that a
segment stays inside its half-space would fire on 33 of the 55 retail tracks.
Re-segmenting, and the BSP rebuild that comes with it, is a later step.

Deliberately free of ``bpy``: the Blender side hands over positions and colours
keyed by ``(segment, index)`` and this module owns what that means for the file.
"""

from __future__ import annotations

from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from .level_model import MAX_TEXTURES, LevelModel, Segment, TextureRef

#: Height given to a segment whose vertices all sit at one Y. Retail does this
#: and only this; see the module docstring.
FLAT_SEGMENT_HEIGHT = 1

#: An edit addresses a vertex by the segment it belongs to and its index in that
#: segment - which is what the importer has to record, because a Blender mesh
#: renumbers and merges freely.
VertexKey = Tuple[int, int]


class EditError(Exception):
    pass


# ---------------------------------------------------------------------------
# Derived values
# ---------------------------------------------------------------------------

def segment_box(segment: Segment) -> Optional[Tuple[int, int, int, int, int, int]]:
    """The bounding box a segment's vertices imply, or ``None`` if it has none."""
    if not segment.vertices:
        return None
    xs = [v[0] for v in segment.vertices]
    ys = [v[1] for v in segment.vertices]
    zs = [v[2] for v in segment.vertices]
    lower_y, upper_y = min(ys), max(ys)
    if upper_y == lower_y:
        upper_y = lower_y + FLAT_SEGMENT_HEIGHT
    return (min(xs), lower_y, min(zs), max(xs), upper_y, max(zs))


def model_bounds(model: LevelModel) -> Optional[Tuple[int, ...]]:
    """``(lowerX, upperX, lowerY, upperY, lowerZ, upperZ)`` over every vertex."""
    xs: List[int] = []
    ys: List[int] = []
    zs: List[int] = []
    for segment in model.segments:
        for x, y, z in segment.vertices:
            xs.append(x)
            ys.append(y)
            zs.append(z)
    if not xs:
        return None
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))


def recompute_bounds(model: LevelModel) -> int:
    """Bring the boxes and the header bounds back in line with the vertices.

    Returns how many values it had to change, so a caller can tell an edit that
    moved geometry from one that only recoloured it.
    """
    changed = 0
    for index, segment in enumerate(model.segments):
        if index >= len(model.bounding_boxes):
            break
        box = segment_box(segment)
        if box is not None and tuple(model.bounding_boxes[index]) != box:
            model.bounding_boxes[index] = box
            changed += 1

    bounds = model_bounds(model)
    if bounds is not None and tuple(model.bounds) != bounds:
        model.bounds = bounds
        changed += 1
    return changed


# ---------------------------------------------------------------------------
# Edits
# ---------------------------------------------------------------------------

def _segment(model: LevelModel, index: int) -> Segment:
    if not 0 <= index < len(model.segments):
        raise EditError(
            "segment %d does not exist; the model has %d"
            % (index, len(model.segments))
        )
    return model.segments[index]


def set_vertex_positions(model: LevelModel,
                         positions: Dict[VertexKey, Sequence[float]]) -> int:
    """Move vertices, keyed by ``(segment, index)``.

    Coordinates are stored as ``s16``, so anything outside that is refused
    rather than wrapped: a vertex that silently reappears on the far side of the
    track is far harder to diagnose than an export that stops.
    """
    moved = 0
    for (segment_index, vertex_index), position in positions.items():
        segment = _segment(model, segment_index)
        if not 0 <= vertex_index < len(segment.vertices):
            raise EditError(
                "segment %d has no vertex %d; it has %d"
                % (segment_index, vertex_index, len(segment.vertices))
            )
        rounded = tuple(int(round(float(component))) for component in position)
        if len(rounded) != 3:
            raise EditError(
                "vertex %d of segment %d was given %d coordinates, not 3"
                % (vertex_index, segment_index, len(rounded))
            )
        for component in rounded:
            if not -32768 <= component <= 32767:
                raise EditError(
                    "vertex %d of segment %d would sit at %r, outside the s16 a "
                    "level model stores positions in"
                    % (vertex_index, segment_index, rounded)
                )
        if segment.vertices[vertex_index] != rounded:
            segment.vertices[vertex_index] = rounded
            moved += 1
    return moved


def set_vertex_colours(model: LevelModel,
                       colours: Dict[VertexKey, Sequence[int]]) -> int:
    """Repaint vertices, keyed by ``(segment, index)``.

    The four channels are the baked lighting, not a material colour, and they
    are bytes. Values are clamped, because a colour a shade out of range is a
    slider at its end rather than a mistake worth stopping an export for.
    """
    painted = 0
    for (segment_index, vertex_index), colour in colours.items():
        segment = _segment(model, segment_index)
        if not 0 <= vertex_index < len(segment.colours):
            raise EditError(
                "segment %d has no vertex %d to colour" % (segment_index, vertex_index)
            )
        clamped = tuple(max(0, min(255, int(round(float(c))))) for c in colour)
        if len(clamped) != 4:
            raise EditError(
                "vertex %d of segment %d was given %d colour channels, not 4"
                % (vertex_index, segment_index, len(clamped))
            )
        if segment.colours[vertex_index] != clamped:
            segment.colours[vertex_index] = clamped
            painted += 1
    return painted


def set_batch_flags(model: LevelModel, flags: Dict[Tuple[int, int], int]) -> int:
    """Set a batch's render flags, keyed by ``(segment, batch)``.

    This is what turns a wall invisible, a decoration solid, or a solid piece of
    scenery into something a racer drives through. The bits are independent:
    ``RENDER_HIDDEN`` without ``RENDER_NO_COLLISION`` is an invisible wall,
    which is how retail builds 986 of them.
    """
    changed = 0
    for (segment_index, batch_index), value in flags.items():
        segment = _segment(model, segment_index)
        if not 0 <= batch_index < len(segment.batches):
            raise EditError(
                "segment %d has no batch %d; it has %d"
                % (segment_index, batch_index, len(segment.batches))
            )
        value = int(value) & 0xFFFFFFFF
        if segment.batches[batch_index].flags != value:
            segment.batches[batch_index].flags = value
            changed += 1
    return changed


def set_batch_textures(model: LevelModel,
                       textures: Dict[Tuple[int, int], int]) -> int:
    """Point batches at a different entry of the model's own texture table.

    The index is into ``model.textures``, not the global 3D texture list, and
    ``0xFF`` means untextured. A Phase 2 edit reuses the host track's table, so
    an index it does not hold is refused.
    """
    changed = 0
    for (segment_index, batch_index), index in textures.items():
        segment = _segment(model, segment_index)
        if not 0 <= batch_index < len(segment.batches):
            raise EditError(
                "segment %d has no batch %d" % (segment_index, batch_index)
            )
        index = int(index)
        if index != 0xFF and not 0 <= index < len(model.textures):
            raise EditError(
                "batch %d of segment %d was pointed at texture %d, but this "
                "model's table holds %d; a track can only use the textures its "
                "level model already carries"
                % (batch_index, segment_index, index, len(model.textures))
            )
        if segment.batches[batch_index].texture_index != index:
            segment.batches[batch_index].texture_index = index
            changed += 1
    return changed


def set_surface_types(model: LevelModel, surfaces: Dict[int, int]) -> int:
    """Change what the ground behaves like, keyed by texture table index.

    ``SurfaceType`` rides on the texture entry rather than on the triangle, so
    this is per material and not per face - which is also why changing it is
    cheap and why it cannot be made per face without a new texture entry.
    """
    changed = 0
    for index, surface in surfaces.items():
        if not 0 <= index < len(model.textures):
            raise EditError(
                "this model has no texture %d to give a surface type" % index
            )
        surface = int(surface) & 0xFF
        if model.textures[index].surface_type != surface:
            model.textures[index].surface_type = surface
            changed += 1
    return changed


# ---------------------------------------------------------------------------
# Growing the texture table
# ---------------------------------------------------------------------------

def add_texture(model: LevelModel, texture_id: int, width: int, height: int,
                texture_format: int = 1, surface_type: int = 0, *,
                dedicated=False, reserved=()) -> int:
    """Give the model a texture it does not carry yet, and return its index.

    ``texture_id`` indexes the ROM's global 3D texture list, which is what a
    ``TextureInfo`` stores and what ``load_texture(id | 0x8000)`` resolves at
    load. Nothing scopes such an id to the track that shipped it, so this is how
    a custom track draws with artwork its donor never had - the ceiling a
    ``.dkrmap`` imposes is "textures the ROM holds", not "textures this table
    already lists".

    An entry that matches in every field is reused unless ``dedicated`` is set
    or its index is ``reserved`` by a TexScroll. The
    match has to include ``surface_type``, because two entries pointing at one
    image and behaving differently is a thing the format expresses on purpose
    and the addon must not merge.

    **The table grows, so the layout has to be rebuilt.** Every array after the
    texture table moves. :func:`level_model_encoder.check_layout` refuses an
    in-place write once the count field disagrees with the table, which is what
    stops this from quietly overwriting the segment array.
    """
    texture_id = int(texture_id)
    if texture_id < 0:
        raise EditError("texture id %d is not an index into the ROM's texture "
                        "list" % texture_id)
    width = int(width) & 0xFF
    height = int(height) & 0xFF
    texture_format = int(texture_format) & 0xFF
    surface_type = int(surface_type) & 0xFF

    for index, existing in enumerate(model.textures):
        if (not dedicated and index not in reserved and existing.texture_id == texture_id
                and existing.raw_width == width
                and existing.raw_height == height
                and existing.format == texture_format
                and existing.surface_type == surface_type):
            return index

    if len(model.textures) >= MAX_TEXTURES:
        raise EditError(
            "this track already names %d textures and a batch selects one with "
            "a u8 where 0xFF means none, so %d is the ceiling. Retail's largest "
            "table is 63. Reuse a texture already in the table, or give two "
            "entries that differ only in surface type the same one"
            % (len(model.textures), MAX_TEXTURES)
        )

    # ``texture_count_field`` is deliberately left as the file wrote it. It is
    # the count field disagreeing with the table that tells
    # :func:`level_model_encoder.check_layout` an in-place write is no longer
    # safe; syncing it here would silence exactly the check that protects the
    # segment array. :func:`level_model_layout.rebuild` sets it, once the
    # layout it describes actually exists.
    model.textures.append(
        TextureRef(texture_id, width, height, texture_format, surface_type)
    )
    return len(model.textures) - 1


def set_animation_gate(model: LevelModel, animated: int) -> bool:
    """Make sure an animated texture will actually be animated.

    ``numberOfAnimatedTextures`` is **not** a count of anything the addon can
    derive. Measured against retail it disagrees with the number of multi-frame
    textures in the table in 35 of the 55 models, and Pirate Lagoon declares 106
    against a table of 26. What it *is* is a gate: ``tracks.c`` tests
    ``> 0`` and calls ``track_tex_anim``, which then walks every batch looking
    for ``RENDER_TEX_ANIM`` and pays no further attention to the number.

    So a model that has just been given an animated texture and declares zero
    would draw it frozen on its first frame. Raising the gate is the fix;
    lowering one a track already carries is not this function's business, since
    the value it holds cannot be reconstructed.
    """
    if animated > 0 and model.animated_texture_count <= 0:
        model.animated_texture_count = int(animated)
        return True
    return False


# ---------------------------------------------------------------------------
# One call for the export path
# ---------------------------------------------------------------------------

class EditSummary:
    """What an :func:`apply` call actually changed, for the export report."""

    __slots__ = ("moved", "painted", "flags", "textures", "surfaces", "bounds")

    def __init__(self, moved=0, painted=0, flags=0, textures=0, surfaces=0,
                 bounds=0):
        self.moved = moved
        self.painted = painted
        self.flags = flags
        self.textures = textures
        self.surfaces = surfaces
        #: Bounding boxes and header bounds brought back in line.
        self.bounds = bounds

    @property
    def total(self) -> int:
        return (self.moved + self.painted + self.flags + self.textures
                + self.surfaces)

    def describe(self) -> str:
        parts = []
        for count, noun in ((self.moved, "vertex moved"),
                            (self.painted, "vertex recoloured"),
                            (self.flags, "batch reflagged"),
                            (self.textures, "batch retextured"),
                            (self.surfaces, "surface type changed")):
            if count:
                parts.append("%d %s%s" % (count, noun, "" if count == 1 else "s"))
        if not parts:
            return "no geometry changes"
        if self.bounds:
            parts.append("%d bound%s recomputed"
                         % (self.bounds, "" if self.bounds == 1 else "s"))
        return ", ".join(parts)

    def __repr__(self):
        return "EditSummary(%s)" % self.describe()


def apply(model: LevelModel, positions=None, colours=None, batch_flags=None,
          batch_textures=None, surface_types=None) -> EditSummary:
    """Apply every kind of edit, then bring the derived values back in line.

    Recomputing the bounds last is the point of having one entry: a caller that
    moved vertices and forgot to update the boxes would ship a track whose
    geometry is culled before it is drawn, and the failure looks like missing
    scenery rather than like a stale bounding box.
    """
    summary = EditSummary()
    if positions:
        summary.moved = set_vertex_positions(model, positions)
    if colours:
        summary.painted = set_vertex_colours(model, colours)
    if batch_flags:
        summary.flags = set_batch_flags(model, batch_flags)
    if batch_textures:
        summary.textures = set_batch_textures(model, batch_textures)
    if surface_types:
        summary.surfaces = set_surface_types(model, surface_types)
    summary.bounds = recompute_bounds(model)
    return summary
