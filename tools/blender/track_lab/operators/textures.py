"""Draw a track with any texture the ROM holds, and map it so it reads.

Until now a custom track could only use the textures its base model happened to
ship with - a donor track's two or three dozen images, chosen once when the
mesh was converted and unchangeable afterwards. That was never a limit of the
format. A ``TextureInfo`` stores an index into the ROM's global 3D texture list
and ``tracks.c`` resolves it with ``load_texture(id | 0x8000)``, exactly as an
object model does; nothing scopes an id to the track that shipped it. So the
browser here offers **1401** of them, not 25.

The other ceiling - that a ``.dkrmap`` had no texture section, so the artwork
had to already be in the ROM - is gone too, and :mod:`custom_textures` is where
that lives. Nothing in this module knows the difference: a texture a track
brought presents the same handful of attributes a ROM one does, so it browses,
applies, maps and exports through everything below unchanged.

So this module does two things, and the second is what makes the first usable.

**Picking.** :mod:`..textures` resolves the whole list to browsable entries and
the panel shows them as thumbnails. Applying one appends an entry to the mesh's
record of the track's texture table if it does not already hold a matching one,
and points the selected faces at it.

**Mapping.** A texture with no UVs under it is a solid colour, so an operator
that only changed the index would look broken most of the time. Two mappings
are offered and both are exact:

* *Keep the current mapping* rescales the raw texel UVs by the ratio of the two
  texture sizes, which leaves the picture covering the same ground as the one
  it replaced. This is the swap-a-picture case.
* *Project flat* is for faces that have no usable mapping at all - a face built
  in Blender, or one extruded out of the track, whose UVs were interpolated
  from its source and are well-formed and geometrically meaningless. It plants
  the texture on the world at a fixed number of map units per repeat, along
  whichever axis the face most faces. The per-face offset it subtracts is a
  **whole number of repeats**, so neighbouring faces still line up: tiling
  stays continuous across the join instead of restarting at every triangle.

The default scale is retail's own. Measured over 257,035 textured triangle
edges across all 55 level models, one repeat covers a median of 268 map units,
with quartiles at 149 and 449; :data:`..textures.DEFAULT_PROJECTION_SCALE` is
256, the power of two inside that.

**Animation is not offered as a choice, because it is not one.**
``RENDER_TEX_ANIM`` says the batch's artwork has more than one frame, and retail
agrees with itself exactly: over all 10,389 batches in the 55 level models the
bit is set on the 619 whose texture is animated and on none of the 9,770 whose
texture is not. So applying an animated texture sets it and applying a still one
clears it. Waterfalls use a separate TexScroll object, authored by
:mod:`.waterfall`; their movement does not depend on this frame-animation bit.

**Transparency is partly a choice, and the rest follows from it.** Which pass
the game draws a face in is the texture's to decide, so applying a see-through
texture moves the faces to the see-through side of ``numberofOpaqueBatches``
and a solid one moves them back - left where they were, a road retextured with
the ROM's water was never drawn at all. What the author does choose is whether
the alpha cuts holes or blends, which is ``RENDER_CUTOUT`` on the faces. See
:mod:`..transparency`.
"""

from __future__ import annotations

import os
import traceback

import bpy
from bpy.props import EnumProperty, IntProperty

from .. import level_model, prefs, scene, textures as texture_catalogue
from .. import transparency as looks
from . import geometry

#: How many thumbnails the browser draws before it asks for a narrower search.
#: Every one of them is a preview Blender has to generate and hold, and a grid
#: of 1401 is not a thing anybody reads anyway.
PAGE = 48


class TextureError(Exception):
    """The apply cannot be done as asked, with the reason an author can act on."""


# ---------------------------------------------------------------------------
# Thumbnails
# ---------------------------------------------------------------------------

_previews = {"collection": None}


def _collection():
    if _previews["collection"] is None:
        import bpy.utils.previews  # noqa: PLC0415 - optional, and only on demand

        _previews["collection"] = bpy.utils.previews.new()
    return _previews["collection"]


def preview_key(texture) -> str:
    """What one texture's thumbnail is cached under.

    Keyed by the file and not only by the id, because a track's own texture
    keeps its id when the author re-imports over it - and Blender's preview
    collection never reloads a key it already holds, so the picture that was
    replaced would go on being drawn for the new one until Blender restarted.

    A function rather than a format string in :func:`icon_for` because the tests
    look the key up, and two copies of it is exactly the pair that drifts.
    """
    return "dkr_tex_%d_%s" % (texture.index,
                              os.path.basename(texture.png or ""))


def icon_for(texture) -> int:
    """A thumbnail id for one texture, or 0 if previews are unavailable.

    Loaded lazily and kept: Blender generates the thumbnail the first time it is
    drawn, so a browser page costs one decode per texture and nothing after
    that. Never allowed to raise - a panel that cannot draw is worse than a
    panel with no pictures in it.
    """
    try:
        collection = _collection()
        key = preview_key(texture)
        item = collection.get(key)
        if item is None:
            item = collection.load(key, texture.png, "IMAGE")
        return item.icon_id
    except Exception:  # noqa: BLE001 - appearance only
        return 0


def teardown() -> None:
    """Release the thumbnails. Called from the addon's ``unregister``."""
    collection = _previews["collection"]
    _previews["collection"] = None
    if collection is None:
        return
    try:
        import bpy.utils.previews  # noqa: PLC0415

        bpy.utils.previews.remove(collection)
    except Exception:  # noqa: BLE001 - shutting down anyway
        pass


# ---------------------------------------------------------------------------
# Finding the pieces
# ---------------------------------------------------------------------------

def target(context):
    """The track geometry to texture: the active one, or the only one."""
    found = geometry.geometry_objects(context)
    active = context.active_object
    if active is not None and active in found:
        return active
    return found[0] if found else None


def picked(context):
    """The texture the browser has selected, or ``None``.

    The track's own artwork is looked up first and does not need an asset tree:
    a track can be textured entirely with pictures the author brought, on a
    machine that has never seen an extraction.
    """
    from . import custom_textures  # noqa: PLC0415 - registered alongside this

    try:
        index = int(context.scene.dkr.texture_id)
    except (AttributeError, TypeError, ValueError):
        return None
    own = custom_textures.by_id(context, index)
    if own is not None:
        return own
    tree = prefs.resolve(context)
    return None if tree is None else texture_catalogue.by_index(tree, index)


def texture_by_id(context, texture_id):
    """The texture an id in a table entry names, the track's own or the ROM's."""
    from . import custom_textures  # noqa: PLC0415 - registered alongside this

    return geometry.texture_object(texture_id, prefs.resolve(context),
                                   custom_textures.entries(context))


def look_for(texture, requested=looks.AUTO) -> str:
    """The look a face drawing ``texture`` gets when ``requested`` is asked for.

    ``AUTO`` is the texture's own look. Anything the texture cannot have - a
    blend on a texture the game draws solid - settles on what it can.
    """
    allowed = looks.face_modes(texture.format, texture.render_mode)
    natural = texture.transparency
    wanted = natural if requested in (None, "", looks.AUTO) else requested
    return looks.settle(wanted, allowed, natural)


def _attribute(mesh, name, width=1):
    """``(attribute, values)`` for one mesh attribute, or ``(None, None)``."""
    attribute = mesh.attributes.get(name)
    if attribute is None:
        return None, None
    values = [0] * (len(attribute.data) * width)
    attribute.data.foreach_get("value", values)
    return attribute, values


def selected_faces(mesh) -> list:
    return [polygon.index for polygon in mesh.polygons if polygon.select]


# ---------------------------------------------------------------------------
# The texture table
# ---------------------------------------------------------------------------

def allocate(obj, texture, surface: int, reserved=None, dedicated=False) -> int:
    """The table index for this texture at this surface type, adding one if new.

    Matching includes the surface type on purpose. Two entries pointing at one
    image and behaving differently is something the format expresses deliberately
    - it is how the game gets one picture that is grass in one place and road in
    another - so merging them would take an expressible track and make it
    inexpressible.
    """
    # A face names its texture by table index, so an entry can only be appended
    # against a record of what the indices already mean. Geometry imported
    # before the addon kept one has no such record, and allocating against an
    # empty table would hand back index 0 - repointing every face that already
    # draws the base model's first texture. Silent and wrong, so it is refused.
    if geometry.PROP_BASE_TEXTURES not in obj:
        raise TextureError(
            "%s was imported before the addon could add textures, so it does "
            "not record what its texture indices mean and a new entry would "
            "land on top of one of them. Import the track geometry again"
            % obj.data.name
        )

    record = {
        "id": int(texture.index),
        "w": int(texture.width) & 0xFF,
        "h": int(texture.height) & 0xFF,
        "format": int(texture.format) & 0xFF,
        "surface": int(surface) & 0xFF,
        "anim": int(texture.frames),
    }

    table = geometry.texture_table(obj)
    if reserved is None:
        from . import waterfall
        reserved = waterfall.reserved_indices(bpy.context, table)
    for index, entry in enumerate(table):
        if not dedicated and index not in reserved and all(entry.get(field) == record[field]
               for field in ("id", "w", "h", "format", "surface")):
            return index

    if len(table) >= level_model.MAX_TEXTURES:
        raise TextureError(
            "this track already names %d textures, and a batch picks one with a "
            "u8 where 0xFF means none - so %d is the ceiling the format sets. "
            "Retail's largest table is 63. Reuse one the track already has"
            % (len(table), level_model.MAX_TEXTURES)
        )

    extras = geometry.extra_textures(obj)
    extras.append(record)
    geometry.set_extra_textures(obj, extras)
    return len(table)


# ---------------------------------------------------------------------------
# UVs
# ---------------------------------------------------------------------------

def _rescaled(raw, was, now):
    """This face's UVs under the new texture, or ``None`` if it had none.

    The arithmetic is :func:`..textures.rescale_uvs`; what is decided here is
    that a face with no texture has no mapping to carry over. Inventing one
    would be a guess, and the operator says so instead.
    """
    if was is None:
        return None
    return texture_catalogue.rescale_uvs(
        raw, was.get("w"), was.get("h"), now["w"], now["h"]
    )


def _projected(corners, now, scale):
    return texture_catalogue.project_face(corners, now["w"], now["h"], scale)


def _fits(raw) -> bool:
    return texture_catalogue.fits_s16(raw)


def _normalised(raw, entry):
    return texture_catalogue.normalise_pair(raw, entry["w"], entry["h"])


# ---------------------------------------------------------------------------
# Applying
# ---------------------------------------------------------------------------

KEEP = "KEEP"
PROJECT = "PROJECT"


class ApplyResult:
    """What one apply changed, so the operator can say it in one line."""

    __slots__ = ("index", "faces", "mapped", "unmapped", "walls", "animated",
                 "look", "settled")

    def __init__(self, index):
        self.index = index
        self.faces = 0
        self.mapped = 0
        #: Faces left with the UVs they had, because there was nothing to
        #: rescale from - an untextured face under "keep the mapping".
        self.unmapped = 0
        self.walls = 0
        self.animated = False
        #: The look the faces were given, and whether it is not the one asked
        #: for because the texture cannot have that one.
        self.look = looks.OPAQUE
        self.settled = False


def apply_texture(obj, faces, texture, surface, mapping, scale,
                  look=looks.AUTO, *, dedicated=False, mapped_uvs=None,
                  opacity=None) -> ApplyResult:
    """Point faces at a texture, map them, and give them a material.

    Every write lands on the mesh's own record of the file - the face's texture
    index, its render flags, its side of the opaque split and its raw texel UVs
    - because those are what the exporter reads. The ``UVMap`` and the material
    are updated alongside so the viewport agrees with what was written, and
    neither is read back.
    """
    mesh = obj.data
    index = allocate(obj, texture, surface, dedicated=dedicated)
    entry = geometry.texture_table(obj)[index]
    result = ApplyResult(index)
    result.animated = texture.frames > 1
    result.look = look_for(texture, look)
    result.settled = look not in (None, "", looks.AUTO, result.look)
    translucent = bool(texture.translucent)

    texture_attr, texture_values = _attribute(mesh, geometry.ATTR_TEXTURE)
    flags_attr, flag_values = _attribute(mesh, geometry.ATTR_FLAGS)
    uv_attr, uv_values = _attribute(mesh, geometry.ATTR_UV, width=2)
    opaque_attr, opaque_values = _attribute(mesh, geometry.ATTR_OPAQUE)
    if (texture_attr is None or flags_attr is None or uv_attr is None
            or opaque_attr is None):
        raise TextureError(
            "%s is missing the attributes the exporter reads, so a texture "
            "written onto it would not reach the file; import the track "
            "geometry again" % mesh.name
        )

    matrix = obj.matrix_world
    overflowed = []
    pending = {}

    for face in faces:
        polygon = mesh.polygons[face]
        corners = list(polygon.loop_indices)
        was = geometry.texture_entry(obj, texture_values[face])
        raw = [(uv_values[c * 2], uv_values[c * 2 + 1]) for c in corners]

        if mapped_uvs is not None:
            mapped = mapped_uvs[face]
        elif mapping == PROJECT:
            places = [
                scene.to_map(matrix @ mesh.vertices[v].co)
                for v in polygon.vertices
            ]
            mapped = _projected(places, entry, scale)
        else:
            mapped = _rescaled(raw, was, entry)

        if mapped is None:
            result.unmapped += 1
            continue
        if not _fits(mapped):
            overflowed.append(face)
            continue
        pending[face] = (corners, mapped)

    if overflowed:
        raise TextureError(_overflow_message(overflowed, mapping, scale))

    uv_layer = mesh.uv_layers.active
    for face, (corners, mapped) in pending.items():
        for at, corner in enumerate(corners):
            if at >= len(mapped):
                break
            uv_values[corner * 2], uv_values[corner * 2 + 1] = mapped[at]
        if uv_layer is not None:
            for at, coordinates in enumerate(_normalised(mapped, entry)):
                if at < len(corners):
                    uv_layer.data[corners[at]].uv = coordinates
        result.mapped += 1

    stem = str(obj.get(geometry.PROP_STEM) or obj.name)
    for face in faces:
        texture_values[face] = index
        value = _with_animation(flag_values[face], texture.frames > 1)
        value = geometry.to_unsigned32(value)
        value = looks.with_mode(value, result.look)
        flag_values[face] = geometry.to_signed32(value)
        opaque_values[face] = looks.draws_in_opaque_pass(value, translucent)
        category = geometry.category_of(value)
        if category == geometry.INVISIBLE_WALLS:
            result.walls += 1
        mesh.polygons[face].material_index = _slot_for(
            obj, stem, category, index, texture, surface, result.look, opacity
        )
        result.faces += 1

    texture_attr.data.foreach_set("value", texture_values)
    flags_attr.data.foreach_set("value", flag_values)
    uv_attr.data.foreach_set("value", uv_values)
    opaque_attr.data.foreach_set("value", opaque_values)
    mesh.update()
    return result


class LookResult:
    """What a transparency change did, for the operator's report."""

    __slots__ = ("changed", "untextured", "refused", "unknown")

    def __init__(self):
        self.changed = 0
        self.untextured = 0
        #: ``{texture name: look it can have}`` for faces that cannot take the
        #: look asked for.
        self.refused = {}
        self.unknown = 0


def set_face_look(context, obj, faces, look) -> LookResult:
    """Give faces a look without changing their texture.

    Only the cut-out bit is the face's; whether the texture blends is the
    texture's own. So a blend on a solid texture is refused with the reason,
    rather than quietly giving the faces something else.
    """
    mesh = obj.data
    result = LookResult()
    texture_attr, texture_values = _attribute(mesh, geometry.ATTR_TEXTURE)
    flags_attr, flag_values = _attribute(mesh, geometry.ATTR_FLAGS)
    opaque_attr, opaque_values = _attribute(mesh, geometry.ATTR_OPAQUE)
    if texture_attr is None or flags_attr is None or opaque_attr is None:
        raise TextureError(
            "%s is missing the attributes the exporter reads; import the track "
            "geometry again" % mesh.name
        )
    table = geometry.texture_table(obj)
    known = {}
    for face in faces:
        index = texture_values[face] & 0xFF
        if index == level_model.NO_TEXTURE or not 0 <= index < len(table):
            result.untextured += 1
            continue
        if index not in known:
            known[index] = texture_by_id(context, table[index].get("id", 0))
        texture = known[index]
        if texture is None:
            result.unknown += 1
            continue
        allowed = looks.face_modes(texture.format, texture.render_mode)
        if look not in allowed:
            result.refused[texture.name] = allowed
            continue
        value = looks.with_mode(geometry.to_unsigned32(flag_values[face]), look)
        flag_values[face] = geometry.to_signed32(value)
        opaque_values[face] = looks.draws_in_opaque_pass(value,
                                                         texture.translucent)
        slot = mesh.polygons[face].material_index
        if 0 <= slot < len(mesh.materials):
            geometry.show_look(mesh.materials[slot], look)
        result.changed += 1
    flags_attr.data.foreach_set("value", flag_values)
    opaque_attr.data.foreach_set("value", opaque_values)
    mesh.update()
    return result


def refresh_texture_faces(context, texture_id) -> int:
    """Bring every face drawing ``texture_id`` in line with its texture's look.

    For a texture of the track's own whose look just changed: its render mode
    moved, so the faces' pass does, and the cut-out bit follows the look.
    Returns how many faces changed.
    """
    texture = texture_by_id(context, texture_id)
    if texture is None:
        return 0
    changed = 0
    for obj in geometry.geometry_objects(context):
        table = geometry.texture_table(obj)
        indices = {index for index, record in enumerate(table)
                   if int(record.get("id", -1)) == int(texture_id)}
        if not indices:
            continue
        mesh = obj.data
        texture_attr, texture_values = _attribute(mesh, geometry.ATTR_TEXTURE)
        flags_attr, flag_values = _attribute(mesh, geometry.ATTR_FLAGS)
        opaque_attr, opaque_values = _attribute(mesh, geometry.ATTR_OPAQUE)
        if texture_attr is None or flags_attr is None or opaque_attr is None:
            continue
        for face, value in enumerate(texture_values):
            if value & 0xFF not in indices:
                continue
            flags = looks.with_mode(geometry.to_unsigned32(flag_values[face]),
                                    texture.transparency)
            flag_values[face] = geometry.to_signed32(flags)
            opaque_values[face] = looks.draws_in_opaque_pass(
                flags, texture.translucent)
            changed += 1
        flags_attr.data.foreach_set("value", flag_values)
        opaque_attr.data.foreach_set("value", opaque_values)
        for material in mesh.materials:
            if (material is not None
                    and material.get(geometry.PROP_TEXTURE_INDEX) in indices):
                geometry.show_look(material, texture.transparency)
        mesh.update()
    return changed


def _with_animation(flags: int, animated: bool) -> int:
    """Set or clear ``RENDER_TEX_ANIM`` to match the artwork. See the docstring."""
    value = geometry.to_unsigned32(flags)
    if animated:
        value |= level_model.RENDER_TEX_ANIM
    else:
        value &= ~level_model.RENDER_TEX_ANIM
    return geometry.to_signed32(value)


def _overflow_message(faces, mapping, scale) -> str:
    count = len(faces)
    plural = "" if count == 1 else "s"
    if mapping == PROJECT:
        return (
            "%d face%s would need UVs past the 1024 texels the format stores - "
            "a UV is s16 fixed point with five fractional bits, so a face can "
            "span at most 32 repeats of a 32-texel texture. Raise the scale "
            "above %g so each repeat covers more ground, or split the faces"
            % (count, plural, scale)
        )
    return (
        "%d face%s already reach close to the 1024-texel ceiling a UV can hold, "
        "and this texture is large enough that keeping the same mapping would "
        "put them over it. Project the texture flat instead, which starts each "
        "face's UVs from a whole repeat" % (count, plural)
    )


def _slot_for(obj, stem, category, index, texture, surface, look=None,
              opacity=None) -> int:
    """The material slot for one (kind, table entry), creating it if needed."""
    for slot, material in enumerate(obj.data.materials):
        if material is None:
            continue
        if (material.get(geometry.PROP_CATEGORY) == category
                and material.get(geometry.PROP_TEXTURE_INDEX) == index):
            material[geometry.PROP_SURFACE] = int(surface) & 0xFF
            if look is not None:
                geometry.show_look(material, look)
            _set_opacity(material, category, opacity)
            return slot

    # A wall is not drawn, so it gets the flat tint the importer gives one
    # rather than whatever picture it happens to name - same rule as the import.
    png = None if category == geometry.INVISIBLE_WALLS else texture.png
    material = geometry.material_for(stem, category, index, png, surface, look)
    _set_opacity(material, category, opacity)
    obj.data.materials.append(material)
    return len(obj.data.materials) - 1


def _set_opacity(material, category, opacity) -> None:
    """Carry the panel's opacity onto a material. A wall is not drawn to fade.

    Only on a change, since the property's update rebuilds the preview.
    """
    if (opacity is None or category == geometry.INVISIBLE_WALLS
            or not hasattr(material, geometry.PROP_OPACITY)):
        return
    opacity = max(0.0, min(1.0, float(opacity)))
    if abs(geometry.opacity_of(material) - opacity) > 1e-6:
        setattr(material, geometry.PROP_OPACITY, opacity)


def materials_drawing(obj, texture_id) -> list:
    """The drawn materials of ``obj`` whose table entry is this texture.

    Every surface type's entry counts: the slider speaks of the picture, and
    the same picture as grass and as road fades together.
    """
    if obj is None or texture_id is None or int(texture_id) < 0:
        return []
    indices = {index for index, entry in enumerate(geometry.texture_table(obj))
               if entry.get("id") == int(texture_id)}
    return [material for material in obj.data.materials
            if material is not None
            and material.get(geometry.PROP_CATEGORY) != geometry.INVISIBLE_WALLS
            and material.get(geometry.PROP_TEXTURE_INDEX) in indices]


def picked_opacity(context):
    """The opacity the picked texture is drawn with on the track, or ``None``."""
    found = materials_drawing(target(context), context.scene.dkr.texture_id)
    return geometry.opacity_of(found[0]) if found else None


def show_panel_opacity(context) -> None:
    """Fade the picked texture's faces live as the panel's slider moves.

    A texture not on the track yet has no faces to fade, so for it the value
    waits for Apply.
    """
    opacity = float(context.scene.dkr.texture_opacity)
    for material in materials_drawing(target(context), context.scene.dkr.texture_id):
        _set_opacity(material, material.get(geometry.PROP_CATEGORY), opacity)


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

class _FaceOperator:
    """Shared plumbing: find the geometry, and read the face selection safely.

    Face selection is only readable off the mesh in Object Mode - in Edit Mode
    the flags on ``mesh.polygons`` are whatever they were when the mode was
    entered - so the mode is dropped and restored around the work. Blender keeps
    the selection across that, which is what makes it invisible to the author.
    """

    @classmethod
    def poll(cls, context):
        return target(context) is not None

    def _run(self, context, work):
        obj = target(context)
        if obj is None:
            self.report({"ERROR"}, "no track geometry in the scene; import it first")
            return {"CANCELLED"}

        mode = obj.mode
        if mode != "OBJECT":
            try:
                bpy.ops.object.mode_set(mode="OBJECT")
            except RuntimeError as error:
                self.report({"ERROR"}, "could not read the selection: %s" % error)
                return {"CANCELLED"}

        try:
            faces = selected_faces(obj.data)
            if not faces:
                self.report(
                    {"ERROR"},
                    "no faces are selected. Press Edit Geometry, switch to face "
                    "select and pick the faces to change - this writes onto the "
                    "faces you choose, not onto the whole track",
                )
                return {"CANCELLED"}
            return work(obj, faces)
        except TextureError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "could not change the texture: %s" % error)
            return {"CANCELLED"}
        finally:
            if mode != "OBJECT":
                try:
                    bpy.ops.object.mode_set(mode=mode)
                except RuntimeError:
                    pass


class DKR_OT_pick_texture(bpy.types.Operator):
    """Choose this texture. Apply Texture then puts it on the selected faces"""

    bl_idname = "dkr.pick_texture"
    bl_label = "Pick Texture"
    bl_options = {"REGISTER", "UNDO"}

    index: IntProperty(name="Texture", default=-1)

    @classmethod
    def description(cls, context, properties):
        """Name the texture in the tooltip, since the button is only a picture."""
        from . import custom_textures  # noqa: PLC0415

        entry = custom_textures.by_id(context, properties.index)
        if entry is None:
            tree = prefs.resolve(context)
            entry = (texture_catalogue.by_index(tree, properties.index)
                     if tree else None)
        if entry is None:
            return "Choose this texture"
        return "%s\n%s\n%dx%d, %s%s" % (
            entry.name, entry.asset_id, entry.width, entry.height, entry.group,
            ", animated" if entry.animated else "",
        )

    def execute(self, context):
        context.scene.dkr.texture_id = int(self.index)
        # The slider shows the picked texture's opacity where the track draws it.
        opacity = picked_opacity(context)
        if opacity is not None:
            context.scene.dkr.texture_opacity = opacity
        for area in (context.screen.areas if context.screen else []):
            area.tag_redraw()
        return {"FINISHED"}


class DKR_OT_apply_texture(_FaceOperator, bpy.types.Operator):
    """Draw the selected faces with the chosen texture"""

    bl_idname = "dkr.apply_texture"
    bl_label = "Apply To Selected Faces"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return target(context) is not None and picked(context) is not None

    def execute(self, context):
        texture = picked(context)
        if texture is None:
            self.report(
                {"ERROR"},
                "no texture chosen. Pick one from the browser above - a track "
                "can use any of the ROM's, not only the ones it shipped with",
            )
            return {"CANCELLED"}

        settings = context.scene.dkr
        surface = int(settings.texture_surface)
        mapping = settings.texture_mapping
        scale = float(settings.texture_scale)
        look = settings.texture_transparency
        opacity = float(settings.texture_opacity)

        def work(obj, faces):
            result = apply_texture(obj, faces, texture, surface, mapping, scale,
                                   look, opacity=opacity)
            for message in _notes(result, texture):
                self.report({"WARNING"}, message)
            self.report(
                {"INFO"},
                "%d face(s) now draw %s as texture %d (%s, %s, %d%% opaque)"
                % (result.faces, texture.name, result.index,
                   geometry.surface_name(surface),
                   LOOK_WORDS.get(result.look, result.look),
                   round(opacity * 100.0)),
            )
            return {"FINISHED"}

        return self._run(context, work)


def _notes(result, texture) -> list:
    messages = []
    if result.unmapped:
        messages.append(
            "%d face(s) had no texture to rescale from, so they kept the UVs "
            "they had - which for a face that was never textured is all zeroes, "
            "and draws one texel across the whole face. Use Project Flat on "
            "those" % result.unmapped
        )
    if result.walls:
        messages.append(
            "%d of the faces are invisible walls, which the game does not draw "
            "at all. The texture is written and will do nothing until they are "
            "made visible" % result.walls
        )
    if result.animated:
        messages.append(
            "%s has %d frames. The batches drawing it are flagged for animation, "
            "which is what retail does for every animated texture and only for "
            "those" % (texture.name, texture.frames)
        )
    if result.settled:
        messages.append(
            "%s is drawn %s by the game, so the faces were made %s instead. %s"
            % (texture.name,
               "see-through" if texture.translucent else "solid",
               LOOK_WORDS.get(result.look, result.look),
               "Change the texture's own transparency under This track's own "
               "artwork to blend it" if getattr(texture, "own", False)
               else "One of the ROM's textures keeps the render mode it was "
                    "made with")
        )
    return messages


#: How a report names each look.
LOOK_WORDS = {looks.OPAQUE: "opaque", looks.CUTOUT: "cut out",
              looks.BLEND: "blended"}


def _face_look_items(self, context):
    from .. import props  # noqa: PLC0415 - registered after this module loads

    return _keep("face_look", props.transparency_items(auto=False))


_ITEMS = {}


def _keep(key, items):
    _ITEMS[key] = items
    return items


class DKR_OT_set_face_transparency(_FaceOperator, bpy.types.Operator):
    """Make the selected faces solid, cut out or blended by their texture's alpha"""

    bl_idname = "dkr.set_face_transparency"
    bl_label = "Set Transparency"
    bl_options = {"REGISTER", "UNDO"}

    look: EnumProperty(name="Transparency", items=_face_look_items)

    def execute(self, context):
        def work(obj, faces):
            result = set_face_look(context, obj, faces, self.look)
            if result.untextured:
                self.report({"WARNING"},
                            "%d face(s) have no texture, so there is no alpha "
                            "to use; they were left alone" % result.untextured)
            if result.unknown:
                self.report({"WARNING"},
                            "%d face(s) draw a texture the addon cannot see "
                            "(no asset tree, or a missing image); they were "
                            "left alone" % result.unknown)
            reasons = [
                "%s can only be %s: the game decides whether a texture blends "
                "from how the texture was made, and only the cut-out is up to "
                "the faces"
                % (name, " or ".join(LOOK_WORDS.get(a, a) for a in allowed))
                for name, allowed in sorted(result.refused.items())
            ]
            if not result.changed:
                self.report({"ERROR"}, "no face could be made %s%s"
                            % (LOOK_WORDS.get(self.look, self.look),
                               ". " + reasons[0] if reasons else ""))
                return {"CANCELLED"}
            for reason in reasons[:3]:
                self.report({"WARNING"}, reason)
            self.report({"INFO"}, "%d face(s) are now %s"
                        % (result.changed, LOOK_WORDS.get(self.look, self.look)))
            return {"FINISHED"}

        return self._run(context, work)


class DKR_OT_clear_texture(_FaceOperator, bpy.types.Operator):
    """Draw the selected faces with no texture, on their baked colours alone"""

    bl_idname = "dkr.clear_texture"
    bl_label = "Remove Texture"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        def work(obj, faces):
            mesh = obj.data
            texture_attr, texture_values = _attribute(mesh, geometry.ATTR_TEXTURE)
            flags_attr, flag_values = _attribute(mesh, geometry.ATTR_FLAGS)
            if texture_attr is None or flags_attr is None:
                raise TextureError(
                    "%s is missing the attributes the exporter reads; import "
                    "the track geometry again" % mesh.name
                )

            opaque_attr, opaque_values = _attribute(mesh, geometry.ATTR_OPAQUE)
            stem = str(obj.get(geometry.PROP_STEM) or obj.name)
            for face in faces:
                texture_values[face] = level_model.NO_TEXTURE
                value = geometry.to_unsigned32(
                    _with_animation(flag_values[face], False))
                # No texture, no alpha: nothing to cut out, and the face is
                # drawn with the solid track.
                value = looks.with_mode(value, looks.OPAQUE)
                flag_values[face] = geometry.to_signed32(value)
                if opaque_values is not None:
                    opaque_values[face] = looks.draws_in_opaque_pass(value, False)
                category = geometry.category_of(value)
                material = geometry.material_for(stem, category, -1, None, 0)
                mesh.polygons[face].material_index = _append_slot(obj, material)
            texture_attr.data.foreach_set("value", texture_values)
            flags_attr.data.foreach_set("value", flag_values)
            if opaque_attr is not None:
                opaque_attr.data.foreach_set("value", opaque_values)
            mesh.update()
            self.report({"INFO"}, "%d face(s) now draw untextured" % len(faces))
            return {"FINISHED"}

        return self._run(context, work)


def _append_slot(obj, material) -> int:
    for slot, existing in enumerate(obj.data.materials):
        if existing is material:
            return slot
    obj.data.materials.append(material)
    return len(obj.data.materials) - 1


class DKR_OT_sync_uvs(_FaceOperator, bpy.types.Operator):
    """Write the UV editor's mapping of the selected faces into the track"""

    bl_idname = "dkr.sync_uvs"
    bl_label = "Apply UV Editing"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        def work(obj, faces):
            mesh = obj.data
            uv_layer = mesh.uv_layers.active
            if uv_layer is None:
                raise TextureError(
                    "%s has no UV map to read; import the track geometry again"
                    % mesh.name
                )
            texture_attr, texture_values = _attribute(mesh, geometry.ATTR_TEXTURE)
            uv_attr, uv_values = _attribute(mesh, geometry.ATTR_UV, width=2)
            if texture_attr is None or uv_attr is None:
                raise TextureError(
                    "%s is missing the attributes the exporter reads; import "
                    "the track geometry again" % mesh.name
                )

            fixed = level_model.UV_FRACTIONAL_BITS
            written = untextured = 0
            overflowed = []
            pending = {}
            for face in faces:
                entry = geometry.texture_entry(obj, texture_values[face])
                if entry is None:
                    untextured += 1
                    continue
                width = float(entry["w"] or 1)
                height = float(entry["h"] or 1)
                corners = list(mesh.polygons[face].loop_indices)
                raw = []
                for corner in corners:
                    u, v = uv_layer.data[corner].uv
                    raw.append((int(round(float(u) * fixed * width)),
                                int(round((1.0 - float(v)) * fixed * height))))
                if not _fits(raw):
                    overflowed.append(face)
                    continue
                pending[face] = (corners, raw)

            if overflowed:
                raise TextureError(
                    "%d face(s) are mapped over more than the 1024 texels a UV "
                    "can hold. Scale their island down in the UV editor - the "
                    "format stores a UV as s16 with five fractional bits, so 32 "
                    "repeats of a 32-texel texture is the whole range"
                    % len(overflowed)
                )

            for corners, raw in pending.values():
                for at, corner in enumerate(corners):
                    if at < len(raw):
                        uv_values[corner * 2], uv_values[corner * 2 + 1] = raw[at]
                written += 1
            uv_attr.data.foreach_set("value", uv_values)
            mesh.update()

            if untextured:
                self.report(
                    {"WARNING"},
                    "%d of the selected faces have no texture, so there is no "
                    "size to measure their UVs in and they were left alone"
                    % untextured,
                )
            self.report(
                {"INFO"},
                "%d face(s) now carry the mapping you see in the UV editor"
                % written,
            )
            return {"FINISHED"}

        return self._run(context, work)


class DKR_OT_select_by_texture(bpy.types.Operator):
    """Select every face drawn with the chosen texture"""

    bl_idname = "dkr.select_by_texture"
    bl_label = "Select Faces Using It"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return target(context) is not None and picked(context) is not None

    def execute(self, context):
        obj = target(context)
        texture = picked(context)
        if obj is None or texture is None:
            self.report({"ERROR"}, "nothing to select against")
            return {"CANCELLED"}

        mode = obj.mode
        if mode != "OBJECT":
            try:
                bpy.ops.object.mode_set(mode="OBJECT")
            except RuntimeError as error:
                self.report({"ERROR"}, "could not change the selection: %s" % error)
                return {"CANCELLED"}

        mesh = obj.data
        _attr, values = _attribute(mesh, geometry.ATTR_TEXTURE)
        wanted = {
            index for index, entry in enumerate(geometry.texture_table(obj))
            if entry.get("id") == texture.index
        }
        count = 0
        if values is not None:
            for polygon in mesh.polygons:
                hit = values[polygon.index] in wanted
                polygon.select = hit
                count += 1 if hit else 0
        mesh.update()

        if mode != "OBJECT":
            try:
                bpy.ops.object.mode_set(mode=mode)
            except RuntimeError:
                pass

        self.report({"INFO"}, "%d face(s) draw %s" % (count, texture.name))
        return {"FINISHED"}


CLASSES = (
    DKR_OT_pick_texture,
    DKR_OT_apply_texture,
    DKR_OT_clear_texture,
    DKR_OT_sync_uvs,
    DKR_OT_select_by_texture,
    DKR_OT_set_face_transparency,
)
