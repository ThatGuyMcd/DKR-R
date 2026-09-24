"""Import track geometry as an editable mesh, and drop objects onto it.

The geometry used to be a read-only reference: something to aim at while
placing objects, decoded from the model the game ships and never written back.
It is now the thing being authored, which changes what the importer owes the
exporter.

**One vertex, one mesh entry.** An edit is addressed in the file as
``(segment, vertex index)``, so every Blender vertex has to say which file
vertex it is. The old importer built up to three meshes and gave each of them
*all* of a segment's vertices whenever any of that segment's batches matched -
so a segment holding both surface and decoration batches had its vertices in two
meshes at once, and "which copy did the author move" had no answer. Across the
110 retail models that duplicated 223,968 of 250,578 vertex slots. So there is
now a single mesh holding every vertex of every segment exactly once, in file
order, carrying its identity in two integer attributes:

* ``dkr_segment`` - index into ``model.segments``
* ``dkr_vertex`` - index into that segment's vertex list

Both are on the ``POINT`` domain, and :mod:`geometry_export` reads them back.
Segments that contribute no drawable face still get their vertices, because a
vertex with no face is still a vertex the file has and still has to be
addressable.

**The three-way split becomes material slots.** Surface, decoration and
invisible wall mean genuinely different things to an author, and 986 invisible
walls across the retail tracks depend on being findable. They are now per face
rather than per object: one material slot per (kind, texture), which is exactly
what Blender's own material-slot Select button already filters on, so the
distinction stays selectable without a second copy of the geometry. Solid
viewport shading draws the slot's ``diffuse_color``, tinted per kind, so solid
mode reads as the kind view and material preview as the texture view.

**Batch flags ride on the faces.** ``dkr_flags`` on the ``FACE`` domain carries
the whole ``u32`` its batch was drawn with, not just the two bits that name the
kind - a batch's other bits are real render state and re-deriving them from a
kind would throw them away. Because batch vertex windows partition a segment's
vertex array exactly (measured: no overlap and no gap in any of the 2292 retail
segments), a face's batch is recoverable from any one of its vertices, which is
what keeps the mapping exact even where Blender drops a face.

**What is deliberately not called is ``mesh.validate()``.** It silently removes
degenerate and duplicate faces, and retail has 8 of the former and 134 of the
latter. Dropping them without saying so is fine for a viewer and not fine for an
editor, so the degenerate and out-of-range ones are filtered here, counted, and
reported.
"""

from __future__ import annotations

import json
import os
import traceback

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, StringProperty
from bpy_extras.io_utils import ImportHelper
from mathutils import Vector

from .. import (assets, level_model, level_model_layout, prefs, scene,
                textures as texture_module, transparency as looks)
from ..preview import _image, _srgb_to_linear, reset_node_tree

#: Marks a mesh as decoded track geometry. Its value used to name which of the
#: three meshes this was; there is one mesh now, so it names the whole track.
PROP_GEOMETRY = "dkr_geometry"
GEOMETRY_KIND = "track"

#: The ``.bin`` this mesh was decoded from. The exporter reloads it and applies
#: the author's edits to it rather than rebuilding a model from the mesh, so
#: everything untouched stays byte-identical.
PROP_MODEL_PATH = "dkr_model_path"

#: How many faces the import built, so an export can tell the author that
#: adding or deleting triangles is not carried yet.
PROP_FACE_COUNT = "dkr_face_count"

#: What the track costs at load, and how much room is left, both stashed at
#: import so the sidebar can show them without inflating the model on every
#: redraw. Recomputed whenever an export or a check rebuilds the model.
PROP_RUNTIME_SIZE = "dkr_runtime_size"
PROP_HEADROOM = "dkr_headroom"

#: How many segments have a bounding box spanning most of the track. Collision
#: considers at most ten segments at a time, chosen by box overlap, so a segment
#: stretched across the map holds a slot everywhere and can push the ground a
#: racer is standing on out of the running. The symptom is falling through the
#: floor somewhere else entirely, with no diagnostic at all - which is why it is
#: shown next to the memory figure rather than left to be discovered.
PROP_OVERSIZED = "dkr_oversized_segments"

GEOMETRY_COLLECTION = "geometry"

COLOUR_ATTRIBUTE = "baked"

#: Vertex identity, on the POINT domain. The export contract.
ATTR_SEGMENT = "dkr_segment"
ATTR_VERTEX = "dkr_vertex"
# Faces keep their source segment when Blender welds boundary vertices.
ATTR_FACE_SEGMENT = "dkr_face_segment"

#: The batch's render flags, on the FACE domain, stored as a signed 32-bit int
#: because Blender has no unsigned attribute and retail uses values as high as
#: 0x80014A00.
ATTR_FLAGS = "dkr_flags"

#: Schema of the mesh attributes. Bumped whenever their meaning changes, so a
#: scene saved by an older addon is refused rather than read off by one. The
#: identity attributes went biased by one at schema 2, which turns an unbiased
#: value into a silently wrong one rather than an obviously wrong one.
PROP_SCHEMA = "dkr_schema"
SCHEMA = 2

#: Faces the file holds that the mesh deliberately does not. A topology-changing
#: export rebuilds each segment from the mesh, so anything missing would be
#: dropped from the track - which makes that export unsafe and is refused.
#: Degenerate triangles are counted apart: Blender cannot represent one at all,
#: so losing those is accepted rather than avoidable.
PROP_OMITTED = "dkr_omitted_faces"

#: Set on the geometry when its base model is the author's own file rather than
#: one the game ships. It decides whether an unchanged export still has to send
#: the geometry: a remix that changed nothing can leave it out, because the
#: header goes on pointing at the shipped track. A track built from a mesh, or
#: one re-segmented, has no shipped track behind it - leaving the payload out
#: ships a header pointing at nothing.
PROP_AUTHORED_BASE = "dkr_authored_base"

#: Set on a mesh an author modelled once it has been converted into track
#: geometry. It stays in the scene - it is their own work and the addon has no
#: business deleting it - so without this the export would go on naming it as a
#: mesh it cannot use, which is true and no longer worth saying.
PROP_CONVERTED = "dkr_converted_to"

#: Whether the import built the hidden batches' faces. The exporter needs it to
#: know which faces the mesh was ever meant to hold, or a partial import looks
#: like an edit and a moved vertex would take the rebuilding path.
PROP_INCLUDE_HIDDEN = "dkr_include_hidden"
PROP_DEGENERATE = "dkr_degenerate_faces"

#: Which source batch a face came from, biased by one so 0 means "none". It is
#: what keeps an untouched segment untouched through a rebuild: retail routinely
#: holds two batches agreeing on every rendering field, so grouping on the
#: fields alone merges them. Blender propagates a face attribute to the faces an
#: extrude or a subdivide makes from it, so new geometry joins the draw call it
#: grew out of without the author saying anything.
ATTR_SERIAL = "dkr_serial"

#: The triangle's own ``u8`` flags, which is where TRI_FLAG_NO_COLLISION lives.
ATTR_TRI_FLAGS = "dkr_tri_flags"

#: The batch's texture table index and which side of numberofOpaqueBatches it
#: sat on. The side is the game's to decide - ``render_level_segment`` draws a
#: batch only in the pass its texture's render mode and its flags put it in,
#: see :mod:`..transparency` - so for a textured face the export derives it
#: afresh, and this attribute is what it falls back on for a face whose texture
#: it cannot see, and for an untextured one.
ATTR_TEXTURE = "dkr_texture"
ATTR_OPAQUE = "dkr_opaque"

#: The triangle's UVs as the file stores them: fixed point, in texels, per
#: corner. Kept raw and separate from the ``UVMap`` an author sees, because the
#: displayed one is normalised and V-flipped and cannot be converted back
#: without rounding - and a rebuilt segment has to write UVs that are exact for
#: every face nobody touched.
ATTR_UV = "dkr_uv"

#: The baked lighting as the file stores it, four bytes packed into one int.
#: The ``baked`` colour attribute is sRGB-converted for display and cannot come
#: back exactly; this can, and a rebuilt segment needs every colour it writes.
ATTR_COLOUR = "dkr_colour"
# Separate channels can be averaged by Merge by Distance without carrying
# bits between channels. The packed attribute remains for older saved scenes.
ATTR_COLOUR_CHANNELS = tuple("dkr_colour_" + channel for channel in "rgba")

#: The track's name, as the materials are named after it. Recorded rather than
#: recovered from the mesh's name, because an author is free to rename a mesh
#: and a material that stopped matching would be silently duplicated.
PROP_STEM = "dkr_stem"

#: The base model's texture table, and the entries the author has added on top
#: of it, both as JSON lists of ``{"id", "w", "h", "format", "surface"}``.
#:
#: A face names its texture by table index, so anything that hands an author a
#: texture the base model never had has to say what the new indices mean - and
#: has to say it on the object, because the base ``.bin`` is not reloaded until
#: the export. The base half is recorded so that picking a texture the track
#: already carries reuses its entry instead of appending a duplicate.
#:
#: An added entry also carries ``"anim"``, the frame count of the artwork, which
#: is what decides whether the model's animation gate has to be raised for it.
PROP_BASE_TEXTURES = "dkr_base_textures"
PROP_EXTRA_TEXTURES = "dkr_extra_textures"

#: Set on each material so a hit face, or a slot, can name its kind.
PROP_CATEGORY = "dkr_category"

#: Which entry of the model's texture table a material draws, and what the
#: ground made of it behaves like. The surface type is a byte on the *texture
#: table entry*, not on the triangle and not on the texture file - so two
#: entries can point at the same image and behave differently, which is how the
#: game gets one picture that is grass in one place and road in another. That is
#: why a material is keyed by table entry rather than by the PNG it shows:
#: keying by image would merge exactly the entries the format keeps apart.
#: Measured, the split costs 8 extra slots across all 55 retail models.
PROP_TEXTURE_INDEX = "dkr_texture_index"
PROP_SURFACE = "dkr_surface"

#: The look a material shows its picture with: one of
#: :data:`..transparency.MODES`. Appearance only; the file's truth is the
#: texture's render mode and the faces' flags.
PROP_LOOK = "dkr_look"

#: How opaque a material's faces are drawn, 0 to 1, whatever their texture's
#: alpha says: ``Material.dkr_opacity``, registered in :mod:`..props`. Below 1
#: the export fades the faces through the loader's vertex alpha; see
#: :mod:`..transparency`.
PROP_OPACITY = "dkr_opacity"

#: The math node the preview scales the picture's alpha with, found by name so
#: a slider being dragged changes one value rather than rebuilding the tree.
OPACITY_NODE = "DKR Opacity"

#: What each surface *does*, for the picker's tooltips. The names and values
#: come from the catalogue, which generates them from the decomp's
#: ``include/enums.h``; this is only the behaviour measured alongside them, and
#: a member with no entry simply has no tooltip. Kept apart on purpose: naming
#: the enum is format knowledge and belongs with the catalogue, while what grass
#: costs you is what an author is choosing between.
SURFACE_HELP = {
    "SURFACE_DEFAULT": "Road. No speed penalty, no bobbing, no landing sound",
    "SURFACE_GRASS": "Costs 75% more speed than road, and bobs the car",
    "SURFACE_SAND": "Costs 150% more speed than road, bobs and rumbles",
    "SURFACE_ZIP_PAD": "Boosts on contact. No retail track uses it as a "
                       "surface - zippers are objects, not ground",
    "SURFACE_STONE": "Caps top speed and never pushes a racer upwards, so it "
                     "scrapes rather than carries",
    "SURFACE_FROZEN_WATER": "Ice",
    "SURFACE_WATER_CALM": "Water. Not solid - everything passes through it, and "
                          "the surface becomes a water plane",
    "SURFACE_TAJ_PAD": "Taj challenge trigger",
    "SURFACE_SNOW": "Snow",
    "SURFACE_WATER_WAVY": "Simulated waves. Never appears in a texture table - "
                          "the game synthesises it from the level header",
    "SURFACE_WATER_UNK_F": "Lowers the height at which a racer is reset. This "
                           "is the floor of the world",
    "SURFACE_INVIS_WALL": "Solid, and a plane passes through it",
    "SURFACE_UNK12": "Unidentified; blocks only the car",
    "SURFACE_NONE": "No surface",
}

SURFACE = "surface"
INVISIBLE_WALLS = "invisible walls"
DECORATION = "decoration"
CATEGORIES = (SURFACE, DECORATION, INVISIBLE_WALLS)

#: What solid viewport shading draws, so the kind reads at a glance without
#: turning the textures off.
CATEGORY_TINT = {
    SURFACE: (0.62, 0.62, 0.64, 1.0),
    DECORATION: (0.30, 0.55, 0.35, 1.0),
    INVISIBLE_WALLS: (0.85, 0.25, 0.25, 0.35),
}

#: Walls bury the track when drawn, so they are masked out rather than deleted -
#: the mesh keeps them, the viewport does not show them. A Mask modifier can do
#: that only because batch vertex windows never overlap, so a wall vertex is
#: never also a surface vertex.
WALL_GROUP = "dkr invisible walls"
WALL_MASK = "DKR Hide Walls"


def category_of(flags: int) -> str:
    """Which of the three kinds a batch's flags make it.

    Hidden wins: a batch the game does not draw is a wall to an author whether
    or not it is also solid, which is how the reference mesh has always split
    them.
    """
    if flags & level_model.RENDER_HIDDEN:
        return INVISIBLE_WALLS
    if flags & level_model.RENDER_NO_COLLISION:
        return DECORATION
    return SURFACE


def surface_items() -> list:
    """``[(value, name, description), ...]`` for the surface-type picker.

    Names and values come from the catalogue, which generates them from the
    decomp, so they cannot drift; only the tooltips live here. An empty list
    means the catalogue was built without the enum, and the operator says so
    rather than offering a picker with nothing in it.
    """
    from .. import catalog as catalog_module

    try:
        members = catalog_module.load().raw.get("enumValues", {}).get("SurfaceType")
    except Exception:  # noqa: BLE001 - a picker must not take the panel down
        members = None
    if not members:
        return []
    return [
        (int(value), name, SURFACE_HELP.get(name, ""))
        for name, value in sorted(members.items(), key=lambda kv: kv[1])
    ]


def surface_name(value: int) -> str:
    for entry in surface_items():
        if entry[0] == int(value):
            return entry[1]
    return "surface %d" % int(value)


def to_signed32(value: int) -> int:
    """A ``u32`` as the ``i32`` Blender's integer attribute can hold."""
    value = int(value) & 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def to_unsigned32(value: int) -> int:
    """Back again, for the file."""
    return int(value) & 0xFFFFFFFF


def pack_colour(rgba) -> int:
    """``(r, g, b, a)`` bytes into the one int an attribute can hold."""
    r, g, b, a = (int(c) & 0xFF for c in tuple(rgba)[:4])
    return to_signed32((r << 24) | (g << 16) | (b << 8) | a)


def unpack_colour(value: int):
    value = to_unsigned32(value)
    return ((value >> 24) & 0xFF, (value >> 16) & 0xFF,
            (value >> 8) & 0xFF, value & 0xFF)


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def _track_material(stem: str, category: str, png, texture_index: int,
                    surface: int, look=None):
    """One material per (kind, texture table entry) of one track.

    Named for the track as well, because a material now carries data that
    belongs to a particular model - which entry of its texture table it draws
    and what the ground behaves like - and materials are global in Blender. Two
    tracks sharing one datablock would have the second silently inherit the
    first's surface types.

    The node tree is built defensively, because node and socket names have
    shifted between Blender releases and a cosmetic material is never worth
    failing an import over. ``diffuse_color`` is the part that always works,
    and it is what solid shading draws.
    """
    image = _image(png) if png else None
    name = "dkr %s %s %d" % (stem, category, texture_index) \
        if texture_index >= 0 else "dkr %s %s" % (stem, category)

    existing = bpy.data.materials.get(name)
    if existing is not None:
        # A material left over from an earlier import may predate a property.
        existing[PROP_CATEGORY] = category
        existing[PROP_TEXTURE_INDEX] = texture_index
        existing[PROP_SURFACE] = surface
        # And may show another picture. The name is (track, kind, table
        # entry), and a track converted a second time - from another donor, or
        # keeping its mesh's own pictures - can put a different image at the
        # same entry. Reused as it stood, the viewport would show the old one
        # over a file that holds the new.
        try:
            if look is not None and existing.get(PROP_LOOK) != look:
                existing[PROP_LOOK] = look
                if image is not None and category != INVISIBLE_WALLS:
                    _build_material_nodes(existing, category, image, look)
            _show_image(existing, category, image)
        except Exception:  # noqa: BLE001 - appearance only
            traceback.print_exc()
        return existing

    material = bpy.data.materials.new(name)
    material[PROP_CATEGORY] = category
    material[PROP_TEXTURE_INDEX] = texture_index
    material[PROP_SURFACE] = surface
    material[PROP_LOOK] = look or looks.OPAQUE
    material.diffuse_color = CATEGORY_TINT[category]
    try:
        _build_material_nodes(material, category, image, look)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()
    return material


def material_for(stem: str, category: str, texture_index: int, png, surface: int,
                 look=None):
    """The material one (kind, texture table entry) is drawn with.

    The public form of the importer's own material rule, so that a texture
    applied by hand later lands in exactly the same slot scheme as one that came
    off the file - same naming, same properties, same node tree. ``look`` is how
    the picture's alpha is shown; ``None`` keeps whatever the material had.
    """
    return _track_material(stem, category, png, texture_index, surface, look)


def show_look(material, look) -> None:
    """Redraw a material's picture with another look. Appearance only."""
    if material is None or material.get(PROP_LOOK) == look:
        return
    material[PROP_LOOK] = look
    category = material.get(PROP_CATEGORY)
    if category == INVISIBLE_WALLS:
        return
    tree = getattr(material, "node_tree", None)
    image = next((node.image for node in tree.nodes
                  if node.type == "TEX_IMAGE" and node.image is not None),
                 None) if tree else None
    if image is None:
        return
    try:
        _build_material_nodes(material, category, image, look)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()


def _show_image(material, category, image):
    """Point a material at ``image``, building its nodes if it drew none.

    ``None`` takes the picture away rather than leaving it. The material is
    reused by (track, kind, table entry), and an entry whose picture cannot be
    loaded this time must not go on showing what the entry held last time:
    after a conversion that renumbered the table, that is another entry's
    picture, and the viewport would disagree with the file without a word.
    """
    if category == INVISIBLE_WALLS:
        return
    tree = getattr(material, "node_tree", None)
    nodes = [node for node in tree.nodes if node.type == "TEX_IMAGE"] if tree else []
    if image is None:
        if nodes:
            _build_material_nodes(material, category, None, material.get(PROP_LOOK))
        return
    if not nodes:
        _build_material_nodes(material, category, image, material.get(PROP_LOOK))
        return
    for node in nodes:
        if node.image is not image:
            node.image = image


def opacity_of(material) -> float:
    """A material's opacity, 0 to 1; 1 for one that never had it set."""
    if material is None:
        return 1.0
    value = getattr(material, PROP_OPACITY, None)
    if value is None:
        value = material.get(PROP_OPACITY)
    try:
        return max(0.0, min(1.0, float(1.0 if value is None else value)))
    except (TypeError, ValueError):
        return 1.0


def opacities(obj) -> dict:
    """``{(texture table index, kind): alpha byte}`` for every faded material.

    Keyed as the materials are - one per kind and table entry - with an
    untextured material's ``-1`` standing for the file's ``NO_TEXTURE``. Walls
    are not drawn, so there is nothing to fade on them.
    """
    found = {}
    for slot in obj.material_slots:
        material = slot.material
        if material is None or PROP_TEXTURE_INDEX not in material:
            continue
        category = material.get(PROP_CATEGORY)
        if category == INVISIBLE_WALLS:
            continue
        alpha = looks.opacity_byte(opacity_of(material))
        if alpha < 255:
            found[(int(material[PROP_TEXTURE_INDEX]), category)] = alpha
    return found


def batch_alpha(table):
    """``alpha_for`` for :func:`..transparency.apply_opacity`, from :func:`opacities`."""
    def alpha_for(batch):
        index = int(batch.texture_index)
        if index == level_model.NO_TEXTURE:
            index = -1
        return table.get((index, category_of(int(batch.flags))))
    return alpha_for


def faded_texture_ids(context) -> set:
    """The texture ids that faded materials draw, across every track mesh.

    Faces are faded through vertex alpha, whatever the texture. A texture of
    the track's own that one of these ids names is written see-through, so the
    game draws the faded faces in the second pass; see :mod:`..transparency`.
    """
    found = set()
    for obj in geometry_objects(context):
        table = texture_table(obj)
        for index, _category in opacities(obj):
            if 0 <= index < len(table):
                found.add(int(table[index].get("id", 0)))
    return found


def show_opacity(material) -> None:
    """Make the viewport follow a material's opacity. Appearance only.

    While the picture is already being faded only the one value changes, so a
    slider dragged across its range does not rebuild the tree on every step.
    """
    if material is None or material.get(PROP_CATEGORY) == INVISIBLE_WALLS:
        return
    opacity = opacity_of(material)
    tree = getattr(material, "node_tree", None)
    node = tree.nodes.get(OPACITY_NODE) if tree else None
    if node is not None and opacity < 1.0:
        node.inputs[1].default_value = opacity
        return
    image = next((node.image for node in tree.nodes
                  if node.type == "TEX_IMAGE" and node.image is not None),
                 None) if tree else None
    try:
        _build_material_nodes(material, material.get(PROP_CATEGORY), image,
                              material.get(PROP_LOOK))
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()


def _build_material_nodes(material, category, image, look=None):
    tree, surface = reset_node_tree(material)
    if tree is None:
        return
    opacity = opacity_of(material)

    if image is not None:
        # Level geometry is lit by its baked vertex colours, not by lamps, so
        # an emission shader shows the texture as the game does.
        texture = tree.nodes.new("ShaderNodeTexImage")
        texture.image = image
        texture.interpolation = "Closest"
        texture.location = (-700, 0)
        shader = tree.nodes.new("ShaderNodeEmission")
        shader.location = (-320, 60)
        tree.links.new(texture.outputs["Color"], shader.inputs["Color"])
        factor = None
        if look in (looks.CUTOUT, looks.BLEND):
            factor = texture.outputs["Alpha"]
            if look == looks.CUTOUT:
                cut = tree.nodes.new("ShaderNodeMath")
                cut.operation = "GREATER_THAN"
                cut.inputs[1].default_value = (looks.HARD_THRESHOLD - 0.5) / 255.0
                cut.location = (-460, -140)
                tree.links.new(factor, cut.inputs[0])
                factor = cut.outputs[0]
        _see_through(material, tree, shader, surface, factor, opacity, look)
        return

    shader = tree.nodes.new("ShaderNodeBsdfDiffuse")
    shader.location = (-200, 0)
    colour_input = shader.inputs.get("Color")
    if colour_input is not None:
        colour_input.default_value = CATEGORY_TINT[category]

    if category == INVISIBLE_WALLS or colour_input is None:
        # A wall has no look of its own - it is not drawn at all - so a flat
        # tint says what it is better than whatever texture it happens to carry.
        tree.links.new(shader.outputs[0], surface)
        return

    attribute = tree.nodes.new("ShaderNodeVertexColor")
    attribute.layer_name = COLOUR_ATTRIBUTE
    attribute.location = (-400, 0)
    tree.links.new(attribute.outputs["Color"], colour_input)
    _see_through(material, tree, shader, surface, None, opacity, None)


def _see_through(material, tree, shader, surface, factor, opacity, look):
    """Mix the picture with nothing by its alpha, as the game draws it.

    ``factor`` is the picture's alpha as the look reads it - hard-edged at half
    for a cut-out, which is where the export hardens the texture it writes, and
    as it is for a blend - or ``None`` where the alpha is not used. The
    material's opacity multiplies into it, as the vertex alpha does in game.
    """
    if opacity < 1.0:
        scale = tree.nodes.new("ShaderNodeMath")
        scale.name = scale.label = OPACITY_NODE
        scale.operation = "MULTIPLY"
        scale.location = (-320, -260)
        scale.inputs[0].default_value = 1.0
        scale.inputs[1].default_value = opacity
        if factor is not None:
            tree.links.new(factor, scale.inputs[0])
        factor = scale.outputs[0]
        look = looks.BLEND
    if factor is None:
        _set_render_method(material, None)
        tree.links.new(shader.outputs[0], surface)
        return
    transparent = tree.nodes.new("ShaderNodeBsdfTransparent")
    transparent.location = (-320, -120)
    mix = tree.nodes.new("ShaderNodeMixShader")
    mix.location = (-120, 0)
    tree.links.new(factor, mix.inputs["Fac"])
    tree.links.new(transparent.outputs["BSDF"], mix.inputs[1])
    tree.links.new(shader.outputs[0], mix.inputs[2])
    tree.links.new(mix.outputs["Shader"], surface)
    _set_render_method(material, look)


def _set_render_method(material, look):
    """Tell EEVEE how to sort a see-through material, in whichever words it knows.

    Blender 4.2 replaced ``blend_method`` with ``surface_render_method``; both
    are tried so the addon draws the same on either side of that change.
    """
    blended = look == looks.BLEND
    see_through = look in (looks.CUTOUT, looks.BLEND)
    for attribute, value in (
            ("surface_render_method", "BLENDED" if blended else "DITHERED"),
            ("blend_method", "BLEND" if blended
             else ("HASHED" if see_through else "OPAQUE")),
            ("use_transparency_overlap", True)):
        if hasattr(material, attribute):
            try:
                setattr(material, attribute, value)
            except (TypeError, ValueError, AttributeError):
                pass


# ---------------------------------------------------------------------------
# Building the mesh
# ---------------------------------------------------------------------------

class BuildStats:
    """What the import had to leave out, so the operator can say so."""

    __slots__ = ("vertices", "faces", "degenerate", "out_of_range", "walls",
                 "uv_skipped", "omitted")

    def __init__(self):
        self.vertices = 0
        self.faces = 0
        self.degenerate = 0
        self.out_of_range = 0
        self.walls = 0
        self.uv_skipped = False
        #: Faces the file has and the mesh does not, degenerate ones apart.
        self.omitted = 0


def batch_of_vertex(model) -> list:
    """``[[batch index per vertex], ...]`` per segment, or ``-1`` where none.

    A batch's vertex window runs from its own offset to the next entry's, and
    those windows tile the segment's vertex array: measured across every retail
    model, no vertex is in two windows and none is in no window. That is what
    lets a face name its batch from any one of its corners.
    """
    table = []
    for segment in model.segments:
        owners = [-1] * len(segment.vertices)
        for index, batch in enumerate(segment.batches):
            start = max(0, batch.vertex_offset)
            end = min(len(owners), batch.vertex_offset + batch.vertex_count)
            for at in range(start, end):
                owners[at] = index
        table.append(owners)
    return table


def texture_png(texture_id, tree=None, own=()):
    """The PNG a texture table entry shows, or ``None``.

    The track's own artwork is looked up in ``own`` - the scene's
    :func:`.custom_textures.entries` - and never in the asset tree, which
    only knows the ROM: asked for ``0x7000`` it has nothing, and a track whose
    file names its own pictures correctly would come back looking untextured.
    """
    ordinal = texture_module.custom_ordinal(texture_id)
    if ordinal is not None:
        own = list(own or ())
        return own[ordinal].png if 0 <= ordinal < len(own) else None
    return tree.texture_3d_png(texture_id) if tree is not None else None


def show_own_pictures(context) -> int:
    """Point every material drawing one of the track's own textures at its PNG.

    For after those PNGs are written back
    (:func:`.custom_textures.restore_missing`) without the geometry being
    rebuilt: its materials still show whatever they loaded before - nothing,
    or another entry's picture. Only the track's own entries are touched; one
    of the ROM's is drawn from the asset tree, which is not what changed.
    Returns how many materials now show a picture.
    """
    from . import custom_textures  # noqa: PLC0415 - it imports this module

    own = custom_textures.entries(context)
    shown = 0
    for obj in geometry_objects(context):
        table = texture_table(obj)
        for material in obj.data.materials:
            if material is None or PROP_TEXTURE_INDEX not in material:
                continue
            index = int(material[PROP_TEXTURE_INDEX])
            if not 0 <= index < len(table):
                continue
            texture_id = table[index].get("id", 0)
            if texture_module.custom_ordinal(texture_id) is None:
                continue
            png = texture_png(texture_id, None, own)
            image = _image(png) if png else None
            try:
                _show_image(material, material.get(PROP_CATEGORY), image)
            except Exception:  # noqa: BLE001 - appearance only
                traceback.print_exc()
                continue
            shown += image is not None
    return shown


def texture_object(texture_id, tree=None, own=()):
    """The texture an id names - one of the track's own, or the ROM's - or ``None``.

    What it returns answers ``format``, ``render_mode``, ``translucent`` and
    ``transparency`` either way, which is all the transparency rules need.
    """
    ordinal = texture_module.custom_ordinal(texture_id)
    if ordinal is not None:
        own = list(own or ())
        return own[ordinal] if 0 <= ordinal < len(own) else None
    if tree is None:
        return None
    return texture_module.by_index(tree, texture_id)


def table_translucency(texture_ids, tree=None, own=()) -> dict:
    """``{table index: see-through}`` for the entries whose texture is known.

    An entry whose texture cannot be found - no asset tree, or an image missing
    from the scene - is left out, and a caller falls back on what the file
    said rather than guess.
    """
    found = {}
    for index, texture_id in enumerate(texture_ids):
        texture = texture_object(texture_id, tree, own)
        if texture is not None:
            found[index] = bool(texture.translucent)
    return found


def _build_geometry(stem, model, collection, tree=None, include_hidden=True,
                    own=(), translucency=None):
    """Build the one mesh, and return ``(object, stats)``.

    ``tree`` draws the ROM's textures and ``own`` - the scene's
    :func:`.custom_textures.entries` - the track's own; see
    :func:`texture_png`.

    Vertices go in first and unconditionally, every segment in file order, so
    the mapping from a Blender index to ``(segment, vertex)`` is decided before
    a single face is looked at and cannot be disturbed by one.

    Everything a rebuilt segment will need is written onto the mesh here, in the
    file's own units: the raw UVs, the raw baked colours, the triangle flags,
    the texture index, the opaque side and the source batch. None of it is
    derived on the way out, because a value converted for display - the sRGB
    colours, the normalised UVs - cannot be converted back exactly, and a
    segment nobody touched has to come out byte for byte.
    """
    stats = BuildStats()

    positions = []
    colours = []
    raw_colours = []
    segment_ids = []
    vertex_ids = []
    base = []

    for index, segment in enumerate(model.segments):
        base.append(len(positions))
        for at, (x, y, z) in enumerate(segment.vertices):
            positions.append(scene.to_blender((x, y, z)))
            rgba = segment.colours[at] if at < len(segment.colours) else (255,) * 4
            colours.append(
                tuple(_srgb_to_linear(c / 255.0) for c in rgba[:3]) + (rgba[3] / 255.0,)
            )
            raw_colours.append(pack_colour(rgba))
            # Biased by one: a vertex built from nothing arrives with zeroes,
            # and zero has to mean "no source" rather than segment 0 vertex 0.
            segment_ids.append(index + 1)
            vertex_ids.append(at + 1)
    stats.vertices = len(positions)

    owners = batch_of_vertex(model)
    if translucency is None:
        translucency = table_translucency(
            [texture.texture_id for texture in model.textures], tree, own)

    faces = []
    face_uvs = []
    face_raw_uvs = []
    face_slots = []
    face_flags = []
    face_serials = []
    face_segments = []
    face_tri_flags = []
    face_textures = []
    face_opaque = []
    slots = {}
    materials = []

    def slot_for(category, texture_index, png, surface, look):
        # Keyed by table entry, not by image: the surface type lives on the
        # entry, so two entries showing one picture are two materials.
        key = (category, texture_index)
        if key not in slots:
            slots[key] = len(materials)
            materials.append(
                _track_material(stem, category, png, texture_index, surface,
                                look)
            )
        return slots[key]

    for index, segment in enumerate(model.segments):
        start = base[index]
        count = len(segment.vertices)
        for batch_index, batch in enumerate(segment.batches):
            category = category_of(batch.flags)
            span = max(0, min(batch.face_count,
                              len(segment.triangles) - batch.face_offset))
            if category == INVISIBLE_WALLS:
                stats.walls += 1
                if not include_hidden:
                    # Their vertices are still imported - the file has them -
                    # but their faces are not, which is what makes a rebuild
                    # unsafe and is why it is counted.
                    stats.omitted += span
                    continue
            texture = model.texture_for(batch)
            png = (
                texture_png(texture.texture_id, tree, own)
                if texture is not None and category != INVISIBLE_WALLS
                else None
            )
            index_of_texture = (batch.texture_index
                                if batch.texture_index != level_model.NO_TEXTURE
                                else -1)
            slot = slot_for(
                category, index_of_texture, png,
                texture.surface_type if texture is not None else 0,
                looks.face_mode(batch.flags,
                                translucency.get(batch.texture_index, False)),
            )
            opaque = batch_index < segment.opaque_batches

            for face_index in range(batch.face_offset,
                                    batch.face_offset + batch.face_count):
                if not 0 <= face_index < len(segment.triangles):
                    break
                triangle_flags, vi0, vi1, vi2 = segment.triangles[face_index]
                local = (batch.vertex_offset + vi0,
                         batch.vertex_offset + vi1,
                         batch.vertex_offset + vi2)
                if min(local) < 0 or max(local) >= count:
                    stats.out_of_range += 1
                    stats.omitted += 1
                    continue
                if len(set(local)) != 3:
                    # Blender cannot hold a triangle that names a vertex twice,
                    # and retail has 8 of them. Counted rather than swallowed,
                    # and counted apart because this loss is not avoidable.
                    stats.degenerate += 1
                    continue
                raw_uv = (segment.uvs[face_index]
                          if face_index < len(segment.uvs) else ((0, 0),) * 3)
                faces.append(tuple(start + at for at in local))
                face_slots.append(slot)
                face_flags.append(to_signed32(batch.flags))
                face_serials.append(batch_index + 1)
                face_segments.append(index + 1)
                face_tri_flags.append(int(triangle_flags) & 0xFF)
                face_textures.append(int(batch.texture_index) & 0xFF)
                face_opaque.append(opaque)
                face_raw_uvs.append(raw_uv)
                face_uvs.append(
                    level_model.normalise_uv(raw_uv, texture) if png else None
                )
    stats.faces = len(faces)

    mesh = bpy.data.meshes.new("%s geometry" % stem)
    mesh.from_pydata([tuple(v) for v in positions], [], faces)
    # No mesh.validate(): it drops degenerate and duplicate faces without
    # saying which, and both exist in retail data. The two kinds it would have
    # had to fix are filtered above instead.
    mesh.update()

    _write_attribute(mesh, ATTR_SEGMENT, "INT", "POINT", segment_ids)
    _write_attribute(mesh, ATTR_VERTEX, "INT", "POINT", vertex_ids)
    _write_attribute(mesh, ATTR_COLOUR, "INT", "POINT", raw_colours)
    for channel, name in enumerate(ATTR_COLOUR_CHANNELS):
        _write_attribute(mesh, name, "INT", "POINT",
                         [unpack_colour(value)[channel] for value in raw_colours])

    if colours:
        layer = mesh.color_attributes.new(COLOUR_ATTRIBUTE, "FLOAT_COLOR", "POINT")
        layer.data.foreach_set(
            "color", [channel for colour in colours for channel in colour]
        )

    for material in materials:
        mesh.materials.append(material)

    aligned = len(mesh.polygons) == len(faces)
    if not aligned:
        # Never seen on any retail model, and the whole per-face contract rests
        # on it, so the mesh is marked unsafe to rebuild rather than written
        # with per-face data that is off by however many faces Blender dropped.
        stats.omitted += max(0, len(faces) - len(mesh.polygons))
        stats.uv_skipped = True
    else:
        for polygon, slot in zip(mesh.polygons, face_slots):
            polygon.material_index = slot
        _write_attribute(mesh, ATTR_FLAGS, "INT", "FACE", face_flags)
        _write_attribute(mesh, ATTR_SERIAL, "INT", "FACE", face_serials)
        _write_attribute(mesh, ATTR_FACE_SEGMENT, "INT", "FACE", face_segments)
        _write_attribute(mesh, ATTR_TRI_FLAGS, "INT", "FACE", face_tri_flags)
        _write_attribute(mesh, ATTR_TEXTURE, "INT", "FACE", face_textures)
        _write_attribute(mesh, ATTR_OPAQUE, "BOOLEAN", "FACE", face_opaque)
        _write_attribute(
            mesh, ATTR_UV, "INT32_2D", "CORNER",
            [c for uv in face_raw_uvs for pair in uv for c in pair],
        )

        uv_layer = mesh.uv_layers.new(name="UVMap")
        for index, polygon in enumerate(mesh.polygons):
            coordinates = face_uvs[index]
            if coordinates is None:
                continue
            for corner, loop_index in enumerate(polygon.loop_indices):
                if corner < len(coordinates):
                    uv_layer.data[loop_index].uv = coordinates[corner]

    mesh[PROP_SCHEMA] = SCHEMA

    obj = bpy.data.objects.new(mesh.name, mesh)
    obj[PROP_GEOMETRY] = GEOMETRY_KIND
    obj[PROP_STEM] = stem
    obj[PROP_FACE_COUNT] = len(mesh.polygons)
    obj[PROP_OMITTED] = stats.omitted
    obj[PROP_INCLUDE_HIDDEN] = bool(include_hidden)
    obj[PROP_DEGENERATE] = stats.degenerate
    record_texture_table(obj, model)
    collection.objects.link(obj)

    vertex_batch = vertex_batch_table(model, base, owners)
    _add_wall_mask(obj, vertex_batch, model)
    return obj, stats


def vertex_batch_table(model, base, owners) -> list:
    """``[(segment, batch), ...]`` indexed by Blender vertex, ``None`` if none."""
    table = [None] * sum(len(s.vertices) for s in model.segments)
    for index, segment in enumerate(model.segments):
        start = base[index]
        for at, owner in enumerate(owners[index]):
            if owner >= 0:
                table[start + at] = (index, owner)
    return table


def batch_of_polygon(vertices, vertex_batch):
    """The one batch a face belongs to, or ``None`` if its corners disagree."""
    found = None
    for vertex in vertices:
        owner = vertex_batch[vertex] if vertex < len(vertex_batch) else None
        if owner is None:
            return None
        if found is None:
            found = owner
        elif found != owner:
            return None
    return found


def _write_attribute(mesh, name, data_type, domain, values) -> None:
    attribute = mesh.attributes.new(name, data_type, domain)
    if values:
        attribute.data.foreach_set("value", values)


def _add_wall_mask(obj, vertex_batch, model) -> None:
    """Group the wall vertices and mask them out, so they do not bury the track.

    An author still needs to see where the walls are, so this is a modifier
    that can be switched off rather than geometry left out: the mesh always
    holds every vertex, whatever the viewport is showing.
    """
    wall_vertices = [
        index for index, owner in enumerate(vertex_batch)
        if owner is not None
        and category_of(model.segments[owner[0]].batches[owner[1]].flags)
        == INVISIBLE_WALLS
    ]
    if not wall_vertices:
        return

    group = obj.vertex_groups.new(name=WALL_GROUP)
    group.add(wall_vertices, 1.0, "REPLACE")

    modifier = obj.modifiers.new(WALL_MASK, "MASK")
    modifier.vertex_group = WALL_GROUP
    # Keep everything that is *not* a wall.
    modifier.invert_vertex_group = True
    modifier.show_in_editmode = False
    modifier.show_viewport = True


def _geometry_collection(context):
    root = scene.ensure_root(context)
    for child in root.children:
        if child.name == GEOMETRY_COLLECTION:
            return child
    collection = bpy.data.collections.new(GEOMETRY_COLLECTION)
    root.children.link(collection)
    return collection


def record_budget(obj, model) -> None:
    """Stash what this model costs at load time.

    The figures come from :mod:`level_model_layout` rather than being worked out
    here, so the panel cannot drift from what the encoder believes. They are
    stored rather than computed on demand because a panel redraws constantly and
    the answer needs the model inflated off disk.
    """
    obj[PROP_RUNTIME_SIZE] = level_model_layout.runtime_size(model)
    obj[PROP_HEADROOM] = level_model_layout.headroom_triangles(model)
    obj[PROP_OVERSIZED] = len(level_model_layout.oversized_segments(model))
    record_water(obj, model)


#: What the Water panel shows, as JSON: the wave grid and anything wrong.
PROP_WATER = "dkr_water"


def record_water(obj, model) -> None:
    """Stash the water the model holds, for the Water panel to read."""
    from .. import water  # noqa: PLC0415

    summary = {"calm": 0, "wavy": 0, "tiles": 0, "tile": [0, 0],
               "reference": -1, "problems": [], "notes": []}
    for segment in model.segments:
        for batch in segment.batches:
            if water.is_wavy(batch.flags):
                summary["wavy"] += batch.face_count
            elif water.is_water(batch.flags):
                summary["calm"] += batch.face_count
    if water.has_waves(model):
        grid = water.simulate(model)
        if grid is not None:
            summary["tiles"] = sum(1 for wavy in grid.wavy if wavy)
            summary["tile"] = [grid.tile_w, grid.tile_h]
            summary["reference"] = -1 if grid.reference is None else grid.reference
        summary["problems"] = water.problems(model)
        texture = None
        if grid is not None and grid.reference is not None:
            for batch in model.segments[grid.reference].batches:
                if water.is_reference(batch.flags):
                    reference = model.texture_for(batch)
                    if reference is not None:
                        texture = (reference.width, reference.height,
                                   reference.format & 0xF)
                    break
        summary["notes"] = water.notes(model, texture)
    obj[PROP_WATER] = json.dumps(summary)


def water_summary(obj) -> dict:
    """What :func:`record_water` stashed, or ``{}``."""
    try:
        return json.loads(obj.get(PROP_WATER) or "{}")
    except (TypeError, ValueError):
        return {}


def replace_base(context, obj, model, include_hidden=None):
    """Write ``model`` as the track's own base file and rebuild the mesh from it.

    What re-segmenting does, and anything else that renumbers every vertex:
    the shipped ``.bin`` stops describing the mesh the moment segments change,
    so the model is written beside the ``.blend`` and imported back, and every
    later export is applied to that file. Returns ``(object, path, stats)``.
    Raises :class:`OSError` if the file cannot be written.
    """
    from .. import level_model_encoder  # noqa: PLC0415
    from . import custom_textures  # noqa: PLC0415 - it imports this module
    from . import waterfall

    previous_textures = texture_table(obj)
    payload = level_model_encoder.pack(model)
    stem = os.path.splitext(os.path.basename(bpy.data.filepath))[0]
    target = os.path.join(os.path.dirname(bpy.data.filepath),
                          "%s-geometry.bin" % stem)
    with open(target, "wb") as handle:
        handle.write(payload)

    if include_hidden is None:
        include_hidden = bool(obj.get(PROP_INCLUDE_HIDDEN, True))
    locked = bool(obj.hide_select)
    collection = _geometry_collection(context)
    for existing in list(context.scene.objects):
        if PROP_GEOMETRY in existing:
            bpy.data.objects.remove(existing, do_unlink=True)

    tree = assets.AssetTree.find(target) or prefs.resolve(context)
    rebuilt, stats = _build_geometry(
        stem, model, collection, tree, include_hidden=include_hidden,
        own=custom_textures.entries(context),
    )
    rebuilt[PROP_MODEL_PATH] = target
    rebuilt[PROP_AUTHORED_BASE] = True
    rebuilt.hide_select = locked
    record_budget(rebuilt, model)
    waterfall.refresh_rebuilt(context, previous_textures, texture_table(rebuilt))
    context.scene.dkr.geometry_path = target
    return rebuilt, target, stats


def budget_of(obj):
    """``(fraction of the load budget, headroom in triangles)``, or ``None``."""
    size = obj.get(PROP_RUNTIME_SIZE)
    headroom = obj.get(PROP_HEADROOM)
    if size is None or headroom is None:
        return None
    return float(size) / level_model_layout.BUDGET, int(headroom)


def collision_pressure(obj):
    """``(oversized segments, tolerated, candidate slots)``, or ``None``.

    The thresholds come from :mod:`level_model_layout` rather than being
    restated here, so the panel cannot drift from what the encoder warns about.
    """
    count = obj.get(PROP_OVERSIZED)
    if count is None:
        return None
    return (int(count), level_model_layout.OVERSIZED_BUDGET,
            level_model_layout.COLLISION_CANDIDATES)


def describe_budget(model) -> str:
    used = level_model_layout.runtime_size(model)
    return "%d%% of the load budget, room for %d more collidable triangle(s)" % (
        round(100.0 * used / level_model_layout.BUDGET),
        level_model_layout.headroom_triangles(model),
    )


def geometry_objects(context) -> list:
    """Every decoded-geometry mesh in the scene."""
    return [
        obj for obj in context.scene.objects
        if PROP_GEOMETRY in obj and obj.type == "MESH"
    ]


def unusable_meshes(context) -> list:
    """Meshes in the scene that look like track geometry but cannot be exported.

    An author who models a track themselves gets no ``dkr_geometry`` marking on
    it, so the export skips it - and skipping it in silence is the worst first
    experience the addon can give: no payload, no warning, nothing to explain
    why the track came out empty. Naming the mesh does not make it exportable,
    but it replaces a mystery with a reason.

    Objects the addon put there are excluded, because they are not attempts at
    geometry: a placed object drawn with its own artwork is a mesh too.
    """
    from .. import preview

    found = []
    for obj in context.scene.objects:
        if obj.type != "MESH" or obj.data is None:
            continue
        if PROP_GEOMETRY in obj or scene.PROP_ID in obj:
            continue
        if PROP_CONVERTED in obj:
            continue
        if preview.PROP_PREVIEW in obj:
            continue
        if not len(obj.data.polygons):
            continue
        found.append(obj)
    return found


def slot_categories(obj) -> list:
    """The kind each material slot draws, indexed by slot."""
    return [
        (slot.material.get(PROP_CATEGORY) if slot.material else None)
        for slot in obj.material_slots
    ]


def _surface_slots(obj):
    """``{texture table index: {surface type: [material name, ...]}}``."""
    found = {}
    for slot in obj.material_slots:
        material = slot.material
        if material is None:
            continue
        index = material.get(PROP_TEXTURE_INDEX)
        surface = material.get(PROP_SURFACE)
        if index is None or surface is None or int(index) < 0:
            continue
        found.setdefault(int(index), {}).setdefault(
            int(surface) & 0xFF, []
        ).append(material.name)
    return found


def surface_types(obj) -> dict:
    """``{texture table index: surface type}`` as the materials now hold it."""
    return {
        index: sorted(claims)[0]
        for index, claims in _surface_slots(obj).items()
    }


# ---------------------------------------------------------------------------
# The texture table, as the mesh carries it
# ---------------------------------------------------------------------------

def _read_records(obj, key) -> list:
    """A JSON list off an object property, or ``[]``.

    Stored as JSON rather than as Blender's own nested collections because a
    list of small dicts survives a save, a re-open and a library link unchanged,
    and because it is readable in the object's Custom Properties panel when
    something has gone wrong.
    """
    raw = obj.get(key)
    if not raw:
        return []
    try:
        found = json.loads(raw)
    except (TypeError, ValueError):
        return []
    return found if isinstance(found, list) else []


def _write_records(obj, key, records) -> None:
    obj[key] = json.dumps(list(records))


def texture_record(texture, animated: int = 0) -> dict:
    """One texture table entry, in the shape the mesh stores it."""
    return {
        "id": int(texture.texture_id),
        "w": int(texture.raw_width) & 0xFF,
        "h": int(texture.raw_height) & 0xFF,
        "format": int(texture.format) & 0xFF,
        "surface": int(texture.surface_type) & 0xFF,
        "anim": int(animated),
    }


def record_texture_table(obj, model) -> None:
    """Stash the base model's texture table, and drop any added entries.

    Called wherever a mesh is built from a model, which is also every point at
    which previously added entries have just become part of the base - a
    re-segment and a track built from a mesh both write the model out and import
    it back. Clearing the extras there is what stops them being added twice.
    """
    _write_records(obj, PROP_BASE_TEXTURES,
                   [texture_record(texture) for texture in model.textures])
    _write_records(obj, PROP_EXTRA_TEXTURES, [])


def base_textures(obj) -> list:
    return _read_records(obj, PROP_BASE_TEXTURES)


def extra_textures(obj) -> list:
    return _read_records(obj, PROP_EXTRA_TEXTURES)


def set_extra_textures(obj, records) -> None:
    _write_records(obj, PROP_EXTRA_TEXTURES, records)


def texture_table(obj) -> list:
    """The whole table a face's index addresses: the base, then the additions."""
    return base_textures(obj) + extra_textures(obj)


def texture_entry(obj, index: int):
    """The table entry a face names, or ``None`` for an untextured face."""
    if index is None or int(index) == level_model.NO_TEXTURE:
        return None
    table = texture_table(obj)
    index = int(index)
    return table[index] if 0 <= index < len(table) else None


def surface_conflicts(obj) -> list:
    """Texture entries two materials disagree about.

    Materials are keyed by kind *and* table entry, so one entry drawn as both
    surface and decoration is two materials - and the surface type belongs to
    the entry, not to either of them. Setting them differently asks for
    something the format cannot store, so it is reported rather than resolved by
    whichever slot happens to come last.
    """
    problems = []
    for index, claims in sorted(_surface_slots(obj).items()):
        if len(claims) > 1:
            problems.append(
                "texture %d is given %s by %s"
                % (index,
                   " and ".join(surface_name(v) for v in sorted(claims)),
                   " and ".join(
                       ", ".join(sorted(names)) for _v, names in sorted(claims.items())
                   ))
            )
    return problems


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

class DKR_OT_import_geometry(bpy.types.Operator, ImportHelper):
    """Load a track's geometry as an editable mesh"""

    bl_idname = "dkr.import_geometry"
    bl_label = "Import DKR Track Geometry"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".bin"
    filter_glob: StringProperty(default="*.bin;*.json", options={"HIDDEN"})

    replace_existing: BoolProperty(
        name="Replace Existing",
        description="Remove geometry already imported first",
        default=True,
    )
    include_hidden: BoolProperty(
        name="Include Invisible Walls",
        description=(
            "Build the faces of batches flagged RENDER_HIDDEN. They are not "
            "drawn in game but most are still solid. Their vertices are "
            "imported either way, because the file has them"
        ),
        default=True,
    )
    textured: BoolProperty(
        name="Use Track Textures",
        description=(
            "Draw the track with the textures it ships with. Without it the "
            "geometry falls back to its baked vertex colours, which is faster "
            "but far harder to read"
        ),
        default=True,
    )
    make_unselectable: BoolProperty(
        name="Lock Selection",
        description=(
            "Stop the geometry being picked in the viewport, so box-selecting "
            "objects over the track does not grab the track itself. Edit "
            "Geometry unlocks it when you want to reshape the track"
        ),
        default=True,
    )

    def execute(self, context):
        path = self.filepath

        kind = assets.identify(path)
        if kind is not None and kind != assets.KIND_LEVEL_MODEL:
            tree = assets.AssetTree.find(path) or prefs.resolve(context)
            level = tree.level_using(path) if tree else None
            if level is not None:
                self.report(
                    {"ERROR"},
                    "%s is an object map for %s, not geometry. Use Import Track "
                    "to load the whole thing, or Import Object Map for just this"
                    % (os.path.basename(path), level.label),
                )
            else:
                self.report(
                    {"ERROR"},
                    "%s is a %s, not a level model; use Import Object Map"
                    % (os.path.basename(path), kind),
                )
            return {"CANCELLED"}

        try:
            if path.lower().endswith(".json"):
                resolved = level_model.sidecar_target(path)
                if not resolved:
                    self.report({"ERROR"}, "%s names no binary" % os.path.basename(path))
                    return {"CANCELLED"}
                path = resolved
            model = level_model.load(path)
        except level_model.LevelModelError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "could not read %s: %s" % (os.path.basename(path), error))
            return {"CANCELLED"}

        if self.replace_existing:
            for obj in list(context.scene.objects):
                if PROP_GEOMETRY in obj:
                    bpy.data.objects.remove(obj, do_unlink=True)

        collection = _geometry_collection(context)
        stem = os.path.splitext(os.path.basename(path))[0]
        tree = assets.AssetTree.find(path) or prefs.resolve(context)
        if self.textured and tree is None:
            self.report(
                {"WARNING"},
                "no extracted assets found, so the track is drawn with its baked "
                "vertex colours instead of its textures",
            )

        from . import custom_textures  # noqa: PLC0415 - it imports this module

        if self.textured:
            restored, lost = custom_textures.restore_missing(context)
            for level, message in custom_textures.restoration_reports(
                    context, restored, lost):
                self.report(level, message)

        obj, stats = _build_geometry(
            stem, model, collection,
            tree if self.textured else None,
            include_hidden=self.include_hidden,
            own=custom_textures.entries(context) if self.textured else (),
        )
        obj[PROP_MODEL_PATH] = path
        record_budget(obj, model)
        from . import waterfall
        waterfall.bind_imported(context)
        if self.make_unselectable:
            obj.hide_select = True

        if not stats.vertices:
            bpy.data.objects.remove(obj, do_unlink=True)
            self.report({"ERROR"}, "the model decoded but held no vertices")
            return {"CANCELLED"}

        context.scene.dkr.geometry_path = path
        tree = assets.AssetTree.find(path)
        if tree is not None:
            context.scene.dkr.asset_root = tree.root
            prefs.invalidate()
        _frame_view(context)

        for message in _build_warnings(stats):
            self.report({"WARNING"}, message)

        # Every face resolves to its batch through the batch vertex windows, so
        # a gap or an overlap in them would mis-assign render flags and
        # mis-group the walls with nothing raised. It is a postcondition of the
        # encoder's re-batcher and holds in all 2292 retail segments, which is
        # exactly why it is worth checking where it is relied on.
        problems = level_model_layout.check_windows(model)
        if problems:
            self.report(
                {"WARNING"},
                "%d batch window problem(s) in this model, so face kinds and "
                "flags may be wrong: %s" % (len(problems), problems[0]),
            )

        solid = sum(1 for s in model.segments for b in s.batches if b.invisible_wall)
        self.report(
            {"INFO"},
            "%s: %d segments, %d vertices, %d faces, %d invisible wall batch(es)"
            % (stem, len(model.segments), stats.vertices, stats.faces, solid),
        )
        return {"FINISHED"}


def _build_warnings(stats) -> list:
    messages = []
    if stats.degenerate:
        messages.append(
            "%d triangle(s) name the same vertex twice and cannot be built as "
            "faces; their vertices are still imported and still exportable"
            % stats.degenerate
        )
    if stats.out_of_range:
        messages.append(
            "%d triangle(s) index past their batch's vertex window and were "
            "skipped" % stats.out_of_range
        )
    if stats.uv_skipped:
        messages.append(
            "Blender changed the face list while building, so the textures are "
            "left unmapped; the geometry itself is unaffected"
        )
    return messages


def _frame_view(context):
    for area in (context.screen.areas if context.screen else []):
        if area.type != "VIEW_3D":
            continue
        for space in area.spaces:
            if space.type == "VIEW_3D":
                space.clip_start = max(space.clip_start, 1.0)
                space.clip_end = max(space.clip_end, 100000.0)


class DKR_OT_edit_geometry(bpy.types.Operator):
    """Unlock the track geometry and open it for editing"""

    bl_idname = "dkr.edit_geometry"
    bl_label = "Edit Geometry"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        targets = geometry_objects(context)
        if not targets:
            self.report({"ERROR"}, "no track geometry in the scene; import it first")
            return {"CANCELLED"}

        obj = targets[0]
        if context.object is not None and context.object.mode != "OBJECT":
            try:
                bpy.ops.object.mode_set(mode="OBJECT")
            except RuntimeError:
                pass

        obj.hide_select = False
        obj.hide_set(False)
        for other in context.selected_objects:
            other.select_set(False)
        obj.select_set(True)
        context.view_layer.objects.active = obj

        try:
            bpy.ops.object.mode_set(mode="EDIT")
        except RuntimeError as error:
            self.report(
                {"WARNING"},
                "unlocked %s but could not enter Edit Mode here: %s"
                % (obj.name, error),
            )
            return {"FINISHED"}

        self.report(
            {"INFO"},
            "editing %s - move vertices freely, but do not add or delete them"
            % obj.name,
        )
        return {"FINISHED"}


class DKR_OT_toggle_walls(bpy.types.Operator):
    """Show or hide the invisible-wall geometry"""

    bl_idname = "dkr.toggle_walls"
    bl_label = "Toggle Invisible Walls"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        targets = [o for o in geometry_objects(context)
                   if WALL_GROUP in o.vertex_groups]
        if not targets:
            self.report({"WARNING"}, "no invisible-wall geometry in the scene")
            return {"CANCELLED"}

        hidden = None
        for obj in targets:
            modifier = obj.modifiers.get(WALL_MASK)
            if modifier is None:
                modifier = obj.modifiers.new(WALL_MASK, "MASK")
                modifier.vertex_group = WALL_GROUP
                modifier.invert_vertex_group = True
                modifier.show_in_editmode = False
            if hidden is None:
                hidden = modifier.show_viewport
            modifier.show_viewport = not hidden

        self.report({"INFO"}, "invisible walls %s" % ("shown" if hidden else "hidden"))
        return {"FINISHED"}


def walls_hidden(context) -> bool:
    """Whether the wall mask is on, for the panel's button label."""
    for obj in geometry_objects(context):
        modifier = obj.modifiers.get(WALL_MASK)
        if modifier is not None:
            return bool(modifier.show_viewport)
    return False


class DKR_OT_check_geometry(bpy.types.Operator):
    """Report what an export would write for the track geometry"""

    bl_idname = "dkr.check_geometry"
    bl_label = "Check Geometry Changes"
    bl_options = {"REGISTER"}

    def execute(self, context):
        from . import geometry_export

        try:
            edit = geometry_export.build_edited_model(context)
        except geometry_export.GeometryExportError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        if edit is None:
            self.report({"WARNING"}, "no track geometry in the scene")
            return {"CANCELLED"}

        for note in edit.notes:
            self.report({"WARNING"}, note)

        if not edit.edited:
            self.report(
                {"INFO"},
                "geometry unchanged, so an export would ship no model payload "
                "and keep pointing at %s" % os.path.basename(edit.path),
            )
        else:
            self.report({"INFO"}, "geometry: %s" % edit.summary.describe())

        self.report({"INFO"}, describe_budget(edit.model))
        for warning in level_model_layout.check_collision_pressure(edit.model):
            self.report({"WARNING"}, warning)
        return {"FINISHED"}


#: Blender's enum callbacks hand their strings to C without taking a reference,
#: so a list built fresh on every call can be collected while the menu is still
#: using it - the documented symptom being a picker that opens empty. Holding the
#: last list returned for each key is what keeps them alive.
_ITEMS = {}


def _keep(key, items):
    """Hold a reference to what an enum callback returned, and return it."""
    _ITEMS[key] = items
    return items


class DKR_OT_set_surface_type(bpy.types.Operator):
    """Say what the ground made of this material behaves like"""

    bl_idname = "dkr.set_surface_type"
    bl_label = "Set Surface Type"
    bl_options = {"REGISTER", "UNDO"}

    def _items(self, context):
        found = [(str(v), n, h) for v, n, h in surface_items()]
        return _keep("surface",
                     found or [("0", "the catalogue has no SurfaceType", "")])

    value: EnumProperty(name="Surface", items=_items)

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return (obj is not None and PROP_GEOMETRY in obj
                and obj.active_material is not None)

    def invoke(self, context, event):
        # A dialog rather than a search popup: it draws the enum as an ordinary
        # dropdown, which types-to-filter the same way and does not depend on
        # the search UI reading the operator's other properties.
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        if not surface_items():
            self.report(
                {"ERROR"},
                "the catalogue carries no SurfaceType enum, so there is nothing "
                "to choose from; regenerate it with generate_catalog.py",
            )
            return {"CANCELLED"}
        material = context.active_object.active_material
        if material is None or PROP_TEXTURE_INDEX not in material:
            self.report(
                {"ERROR"},
                "this material does not belong to a texture table entry, so "
                "there is nothing to give a surface type to",
            )
            return {"CANCELLED"}
        material[PROP_SURFACE] = int(self.value) & 0xFF
        self.report(
            {"INFO"},
            "%s is now %s" % (material.name, surface_name(int(self.value))),
        )
        for area in (context.screen.areas if context.screen else []):
            area.tag_redraw()
        return {"FINISHED"}


class DKR_OT_resegment(bpy.types.Operator):
    """Divide the track into segments afresh, so collision and culling fit it"""

    bl_idname = "dkr.resegment"
    bl_label = "Re-segment Track"
    bl_options = {"REGISTER"}

    @classmethod
    def poll(cls, context):
        return bool(geometry_objects(context))

    def execute(self, context):
        from .. import level_model_encoder, level_model_layout, water
        from . import geometry_export

        targets = geometry_objects(context)
        if not targets:
            self.report({"ERROR"}, "no track geometry in the scene")
            return {"CANCELLED"}
        obj = targets[0]

        if not bpy.data.filepath:
            self.report(
                {"ERROR"},
                "save the .blend first. Re-segmenting writes the track as its "
                "own model file beside it, because the shipped .bin stops being "
                "the base the moment the segments are renumbered",
            )
            return {"CANCELLED"}

        try:
            edit = geometry_export.build_edited_model(context)
        except geometry_export.GeometryExportError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        if edit is None:
            self.report({"ERROR"}, "no track geometry in the scene")
            return {"CANCELLED"}

        before = len(edit.model.segments)
        waves = water.has_waves(edit.model)
        try:
            after = level_model_layout.resegment(edit.model)
            level_model_encoder.pack(edit.model)
        except (level_model_layout.LayoutError,
                level_model_encoder.LevelModelEncodeError) as error:
            self.report({"ERROR"}, "could not re-segment: %s" % error)
            return {"CANCELLED"}

        # The track becomes its own base. That is not a side effect: every
        # vertex has just been renumbered, so the shipped .bin no longer
        # describes this mesh, and an export against it would place geometry by
        # indices that mean something else. Writing it out makes re-segmenting a
        # checkpoint rather than a one-way door - reshaping afterwards is back on
        # the in-place path and byte-exact against this file.
        try:
            _rebuilt, target, _stats = replace_base(context, obj, edit.model)
        except OSError as error:
            self.report({"ERROR"}, "could not write the model: %s" % error)
            return {"CANCELLED"}

        problems = level_model_layout.check_windows(edit.model)
        if problems:
            self.report({"WARNING"}, "batch windows: %s" % problems[0])

        self.report(
            {"INFO"},
            "re-segmented %d into %d%s, and rebuilt the mesh from it - every "
            "vertex was renumbered, so the track is now its own base at %s"
            % (before, after,
               " along the wave grid, so the waves keep working" if waves else "",
               os.path.basename(target)),
        )
        return {"FINISHED"}


class DKR_OT_drop_to_surface(bpy.types.Operator):
    """Drop the selected objects straight down onto the track surface"""

    bl_idname = "dkr.drop_to_surface"
    bl_label = "Drop To Surface"
    bl_options = {"REGISTER", "UNDO"}

    offset: FloatProperty(
        name="Height Above Surface",
        description="Leave the object this far above where it lands",
        default=0.0,
        soft_min=-200.0,
        soft_max=200.0,
    )
    search_distance: FloatProperty(
        name="Search Distance",
        description="How far to look up and down for a surface",
        default=20000.0,
        min=1.0,
    )
    surface_only: BoolProperty(
        name="Surface Only",
        description=(
            "Land on the drivable surface, ignoring decoration and walls. The "
            "three now share one mesh, so the ray is carried on through "
            "anything that is not the road"
        ),
        default=True,
    )

    @classmethod
    def poll(cls, context):
        return any(scene.is_dkr_object(o) for o in context.selected_objects)

    def execute(self, context):
        targets = geometry_objects(context)
        if not targets:
            self.report(
                {"ERROR"},
                "no track geometry in the scene; import the geometry first",
            )
            return {"CANCELLED"}

        depsgraph = context.evaluated_depsgraph_get()
        moved = missed = 0
        for obj in context.selected_objects:
            if not scene.is_dkr_object(obj):
                continue
            landing = self._raycast(obj.matrix_world.translation, targets, depsgraph)
            if landing is None:
                missed += 1
                continue
            # The world position, not ``location``: a start position in a grid
            # is parented, and its location is relative to the grid's root.
            obj.matrix_world.translation = landing + Vector((0.0, 0.0, self.offset))
            moved += 1

        if not moved:
            self.report({"WARNING"}, "nothing landed on the surface")
            return {"CANCELLED"}
        if missed:
            self.report({"INFO"}, "dropped %d, %d found no surface below"
                        % (moved, missed))
        else:
            self.report({"INFO"}, "dropped %d object(s)" % moved)
        return {"FINISHED"}

    def _raycast(self, origin, targets, depsgraph):
        return surface_below(origin, targets, depsgraph,
                             self.search_distance, self.surface_only)


def surface_below(origin, targets, depsgraph, search_distance=20000.0,
                  surface_only=True):
    """Nearest hit straight down, falling back to straight up, in world space.

    Looking up matters: an object sitting slightly under the road should
    come back to the surface rather than be left buried. Shared with the start
    grid, which drops each start position it makes.
    """
    for direction in (Vector((0.0, 0.0, -1.0)), Vector((0.0, 0.0, 1.0))):
        landing = nearest_hit(origin, direction, targets, depsgraph,
                              search_distance, surface_only)
        if landing is not None:
            return landing
    return None


def nearest_hit(origin, direction, targets, depsgraph, search_distance,
                surface_only=True):
    """Nearest hit along one ray across every target, in world space.

    The ray can point anywhere, which is what placing by clicking needs: it
    casts from the viewport's eye through the mouse, not straight down.
    """
    best = None
    for target in targets:
        world = _first_hit(target, depsgraph, origin, direction,
                           search_distance, surface_only)
        if world is None:
            continue
        distance = (world - origin).length
        if best is None or distance < best[0]:
            best = (distance, world)
    return best[1] if best else None


def _first_hit(target, depsgraph, origin, direction, search_distance,
               surface_only):
    """First hit on this object that counts, in world space.

    Surface, decoration and wall all live in one mesh now, so a hit has to
    be asked which it is; a decoration that is drawn but not driven on is
    the wrong thing to stand a checkpoint on. The ray is restarted just past
    a rejected hit rather than filtered afterwards, because a ray cast
    reports only the first thing it meets.
    """
    matrix = target.matrix_world
    inverse = matrix.inverted()
    start = inverse @ origin
    local_direction = (inverse.to_3x3() @ direction).normalized()
    evaluated = target.evaluated_get(depsgraph)
    categories = slot_categories(target)
    remaining = search_distance

    for _attempt in range(8):
        hit, location, _normal, index = target.ray_cast(
            start, local_direction, distance=remaining
        )
        if not hit:
            return None
        if not surface_only or \
                _category_at(evaluated, categories, index) == SURFACE:
            return matrix @ location
        stepped = location + local_direction * 0.01
        remaining -= (stepped - start).length
        if remaining <= 0.0:
            return None
        start = stepped
    return None


def _category_at(evaluated, categories, index):
    """The kind of the face a ray hit, read off the evaluated mesh.

    The index a ray cast reports belongs to the evaluated geometry, which the
    wall mask can have made shorter than the mesh being edited, so the material
    has to be looked up there rather than in the original.
    """
    try:
        polygon = evaluated.data.polygons[index]
    except (AttributeError, IndexError, ReferenceError):
        return None
    slot = polygon.material_index
    return categories[slot] if 0 <= slot < len(categories) else None


CLASSES = (
    DKR_OT_import_geometry,
    DKR_OT_edit_geometry,
    DKR_OT_check_geometry,
    DKR_OT_set_surface_type,
    DKR_OT_resegment,
    DKR_OT_drop_to_surface,
    DKR_OT_toggle_walls,
)
