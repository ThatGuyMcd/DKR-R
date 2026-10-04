"""Turn a mesh's material images into the track's DKR textures.

The case this covers: you have a textured mesh - materials with image nodes, a
UV map - and DKR track geometry whose texture table knows nothing about any of
it. This takes each image down to something the RDP can hold, adds it to the
track's own artwork, gives it a texture table entry, and points the faces at it
with the UVs *you already made*.

**Two shapes of that, and the script does both.**

*One object.* The DKR geometry carries the materials itself. Each material slot
is a group of faces, and its image becomes one table entry.

*Two objects, same space.* A textured donor beside the DKR geometry - what you
get when a scene is imported and then converted. The conversion triangulates
and re-splits, so face 7 of one is not face 7 of the other and nothing can be
matched by index. Matched by position instead: the donor face under each DKR
face's **centroid**, and then the UV of each corner interpolated inside *that*
face. Centroid first is the whole trick - a corner sits on a shared edge, where
the nearest donor face is a coin toss and the two sides of a UV seam disagree
completely, while a centroid is unambiguously inside one face.

**The mapping is the point.** The addon's Apply Texture offers "keep the
mapping" (rescale the texel UVs a face already had) and "project flat" (plant
it on the world); neither can carry an unwrap over. So this converts the UV
directly to the file's fixed point - ``raw = u * 32 * width`` with V flipped,
the exact inverse of what the importer does - and an unwrap survives intact.

Everything heavy is the addon's: the resampler, the PNG decoder, the texel
encoder, ``allocate``. This only decides *what* to feed them.

Run it from Blender's Text Editor with the track geometry selected, or:

    blender scene.blend --python tools/blender/apply_material_textures.py

Save the .blend first. Resampled PNGs are written beside it, and an unsaved
scene has nowhere to put them that survives the session.
"""

from __future__ import annotations

import importlib
import os
import sys

import bpy
from mathutils.bvhtree import BVHTree
from mathutils.interpolate import poly_3d_calc

# ---------------------------------------------------------------------------
# What to do. Edit these, then run.
# ---------------------------------------------------------------------------

#: Where the pictures and the UVs come from.
#:
#: * ``"AUTO"``  - the textured mesh in the scene that is not track geometry,
#:                 falling back to the track's own materials if there is none.
#: * ``""``      - the track geometry's own material slots, always.
#: * a name      - that object, by name in the outliner.
DONOR = "AUTO"

#: How each texture is stored. RGBA16 is the ordinary colour choice and gives
#: 2048 texels - 64x32. I8/I4 buy resolution at the cost of all colour.
FORMAT = "RGBA16"

#: "" takes the largest the format allows, in the shape closest to the picture.
#: Or pin one, as "64x32". Both sides must be powers of two, 64 at most.
SIZE = ""

#: What the ground made of these textures behaves like, for materials that do
#: not already say. The addon's Surface menu lists them, and a material already
#: carrying ``dkr_surface`` keeps its own.
SURFACE = 0

#: Only the faces selected in Edit Mode, rather than every face of the object.
ONLY_SELECTED = False

#: Repoint each material at its resampled PNG when done, so the viewport shows
#: the 64x32 the game will draw rather than the 2K original.
PREVIEW_RESAMPLED = True

#: How far a DKR face's centroid may sit from the donor before it is reported
#: as unmatched, in Blender units. Two meshes in the same space are within
#: floating point of each other; anything past this is a real mismatch.
MATCH_TOLERANCE = 1.0


# ---------------------------------------------------------------------------
# Finding the addon
# ---------------------------------------------------------------------------

def _addon():
    """The installed extension if it is loaded, else the copy beside this file."""
    for name, module in list(sys.modules.items()):
        if name == "track_lab" or name.endswith(".track_lab"):
            return module
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    return importlib.import_module("track_lab")


_ROOT = _addon().__name__
level_model = importlib.import_module(_ROOT + ".level_model")
texture_module = importlib.import_module(_ROOT + ".textures")
geometry = importlib.import_module(_ROOT + ".operators.geometry")
texture_ops = importlib.import_module(_ROOT + ".operators.textures")
custom_textures = importlib.import_module(_ROOT + ".operators.custom_textures")


# ---------------------------------------------------------------------------
# Finding the two objects
# ---------------------------------------------------------------------------

def _target(context):
    """The geometry to texture: the active one, or the only one in the scene."""
    found = geometry.geometry_objects(context)
    if not found:
        raise SystemExit(
            "no DKR track geometry in this scene - import a track, or convert "
            "a mesh with Track From Mesh, before running this"
        )
    active = context.active_object
    return active if active in found else found[0]


def _donor(context, obj):
    """The object the pictures come from, or ``None`` for the track's own."""
    if not DONOR:
        return None
    if DONOR != "AUTO":
        found = context.scene.objects.get(DONOR)
        if found is None:
            raise SystemExit("no object named %r in this scene" % DONOR)
        if found.type != "MESH":
            raise SystemExit("%s is not a mesh" % DONOR)
        return found

    track = set(geometry.geometry_objects(context))
    candidates = [
        other for other in context.scene.objects
        if other.type == "MESH" and other not in track and other is not obj
        and other.data.materials and other.data.uv_layers.active is not None
        and any(_image_of(material)[0] is not None
                for material in other.data.materials)
    ]
    if not candidates:
        return None
    if len(candidates) > 1:
        raise SystemExit(
            "more than one textured mesh could be the donor (%s). Set DONOR at "
            "the top of this script to the one you mean"
            % ", ".join(repr(other.name) for other in candidates)
        )
    return candidates[0]


# ---------------------------------------------------------------------------
# Materials and their pictures
# ---------------------------------------------------------------------------

def _image_of(material):
    """``(image, node)`` for the picture a material draws, or ``(None, None)``.

    The addon's own rule - walked back from the output - so this script and
    *Track From Mesh* agree on which picture a material is.
    """
    node = custom_textures.image_node(material)
    return (node.image, node) if node is not None else (None, None)


def _import_image(context, image, code, directory, cache):
    """One Blender image as a texture the track ships. Cached per run and per scene.

    Through :func:`custom_textures.add_image`, the one importer the Add button
    and *Track From Mesh* use too - which also keeps the full-resolution
    original the export's HD pack is built from.
    """
    if image.name in cache:
        return cache[image.name]

    # Added by an earlier run? The stored source is what says so, and re-adding
    # would spend one of the 255 ordinals on a duplicate.
    source = custom_textures.image_source(image)
    for entry in custom_textures.entries(context):
        if custom_textures.same_source(entry.source, source):
            cache[image.name] = entry
            print("  %-28s already in this track's artwork" % image.name)
            return entry

    try:
        texture, was = custom_textures.add_image(
            context, custom_textures.image_path(image), code, SIZE,
            name=custom_textures.image_label(image), note=source,
        )
    except custom_textures.CustomTextureError as error:
        raise SystemExit("%s: %s" % (image.name, error))

    cache[image.name] = texture
    print("  %-28s %dx%d, down from %dx%d" % (image.name, texture.width,
                                              texture.height, was[0], was[1]))
    return texture


# ---------------------------------------------------------------------------
# Where each face's picture and mapping come from
# ---------------------------------------------------------------------------

def _from_own_materials(obj, polygons):
    """``{face: (material, [uv per corner])}`` from the object's own slots."""
    mesh = obj.data
    uv_layer = mesh.uv_layers.active
    if uv_layer is None:
        raise SystemExit(
            "%s has no UV map, so there is no mapping to carry over. Unwrap it "
            "first, or use the addon's Project Flat instead" % mesh.name
        )
    if not mesh.materials:
        raise SystemExit("%s has no materials to take pictures from" % mesh.name)

    found = {}
    for polygon in polygons:
        slot = polygon.material_index
        material = mesh.materials[slot] if slot < len(mesh.materials) else None
        found[polygon.index] = (
            material,
            [tuple(uv_layer.data[corner].uv) for corner in polygon.loop_indices],
        )
    return found


def _from_donor(context, obj, donor, polygons):
    """``{face: (material, [uv per corner])}`` read off another mesh in the same space.

    The donor is evaluated and moved into world space before anything is asked
    of it, so a modifier stack, a parent, or the two objects sitting at
    different transforms all come out right rather than being assumed away.
    """
    depsgraph = context.evaluated_depsgraph_get()
    evaluated = donor.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        uv_layer = mesh.uv_layers.active
        if uv_layer is None:
            raise SystemExit("%s has no UV map to take a mapping from" % donor.name)
        if not mesh.materials:
            raise SystemExit("%s has no materials to take pictures from" % donor.name)

        matrix = donor.matrix_world
        places = [matrix @ vertex.co for vertex in mesh.vertices]
        faces = [tuple(polygon.vertices) for polygon in mesh.polygons]
        tree = BVHTree.FromPolygons(places, faces, all_triangles=False, epsilon=0.0)

        corners = [
            [tuple(uv_layer.data[corner].uv) for corner in polygon.loop_indices]
            for polygon in mesh.polygons
        ]
        slots = [polygon.material_index for polygon in mesh.polygons]

        target = obj.data
        to_world = obj.matrix_world
        found = {}
        missed = 0
        farthest = 0.0

        for polygon in polygons:
            centre = to_world @ polygon.center
            location, _normal, index, distance = tree.find_nearest(centre)
            if index is None:
                missed += 1
                continue
            farthest = max(farthest, distance or 0.0)
            if distance is not None and distance > MATCH_TOLERANCE:
                missed += 1
                continue

            shape = [places[vertex] for vertex in faces[index]]
            uvs = corners[index]
            mapping = []
            for corner in polygon.loop_indices:
                point = to_world @ target.vertices[target.loops[corner].vertex_index].co
                weights = poly_3d_calc(shape, point)
                u = sum(weight * uv[0] for weight, uv in zip(weights, uvs))
                v = sum(weight * uv[1] for weight, uv in zip(weights, uvs))
                mapping.append((u, v))

            slot = slots[index]
            material = mesh.materials[slot] if slot < len(mesh.materials) else None
            found[polygon.index] = (material, mapping)

        print("matched %d of %d face(s) against %s; farthest centroid %.4f units"
              % (len(found), len(polygons), donor.name, farthest))
        if missed:
            print("  %d face(s) had no donor surface within %g units and were "
                  "left alone. If the two meshes are not in the same place, "
                  "move them together or raise MATCH_TOLERANCE"
                  % (missed, MATCH_TOLERANCE))
        return found
    finally:
        evaluated.to_mesh_clear()


# ---------------------------------------------------------------------------
# Writing it onto the track
# ---------------------------------------------------------------------------

def _raw_uvs(mapping, width, height):
    """A face's UVs as the texels the file stores.

    The inverse of :func:`..textures.normalise_pair`, which is what the importer
    ran to build a UV layer in the first place: a UV is ``s16`` with five
    fractional bits, measured in texels against the texture's own size.
    """
    texel = texture_module.TEXEL
    across, down = float(width or 1), float(height or 1)
    return [(int(round(u * texel * across)),
             int(round((1.0 - v) * texel * down)))
            for u, v in mapping]


def _attribute(mesh, name, width=1):
    attribute = mesh.attributes.get(name)
    if attribute is None:
        raise SystemExit(
            "%s is missing %s - an attribute the exporter reads. Import the "
            "track geometry again" % (mesh.name, name)
        )
    values = [0] * (len(attribute.data) * width)
    attribute.data.foreach_get("value", values)
    return attribute, values


def _slot_on(obj, material, borrowed):
    """The material slot on the track for one material, adding it if it is the donor's.

    A donor material is copied rather than used, because the next thing done to
    it is to stamp it with this track's texture index and repoint its image -
    and doing that to the datablock the donor object is still drawing with would
    change the donor's own look to match.
    """
    mesh = obj.data
    for slot, existing in enumerate(mesh.materials):
        if existing is material:
            return slot, material
    if not borrowed:
        mesh.materials.append(material)
        return len(mesh.materials) - 1, material

    name = "dkr %s" % material.name
    copy = bpy.data.materials.get(name)
    if copy is None:
        copy = material.copy()
        copy.name = name
    for slot, existing in enumerate(mesh.materials):
        if existing is copy:
            return slot, copy
    mesh.materials.append(copy)
    return len(mesh.materials) - 1, copy


def _stamp(material, index, surface, texture, category):
    """Make the material a DKR one, in place.

    In place rather than through ``material_for``, which would build a new
    material and leave this slot unused: the names and the slot order are the
    author's, and the point of this script is that their work survives.
    """
    material[geometry.PROP_CATEGORY] = category
    material[geometry.PROP_TEXTURE_INDEX] = int(index)
    material[geometry.PROP_SURFACE] = int(surface) & 0xFF
    if not PREVIEW_RESAMPLED or not texture.png:
        return
    _image, node = _image_of(material)
    if node is None:
        return
    try:
        node.image = bpy.data.images.load(texture.png, check_existing=True)
        node.interpolation = "Closest"
    except (RuntimeError, OSError) as error:
        print("  (could not show the resampled picture: %s)" % error)


# ---------------------------------------------------------------------------
# The run
# ---------------------------------------------------------------------------

def run():
    context = bpy.context
    obj = _target(context)
    mesh = obj.data

    if obj.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")

    if geometry.PROP_BASE_TEXTURES not in obj:
        raise SystemExit(
            "%s was imported before the addon recorded what its texture indices "
            "mean, so a new entry would land on top of one of them. Import the "
            "track geometry again" % mesh.name
        )

    donor = _donor(context, obj)
    code = texture_module.FORMAT_CODES[FORMAT]
    directory = custom_textures.folder(context)
    os.makedirs(directory, exist_ok=True)
    if not bpy.data.filepath:
        print("! this scene has never been saved, so the resampled PNGs are "
              "going to Blender's temporary folder and will be gone next "
              "session. Save the .blend and run this again.")

    wanted = [polygon for polygon in mesh.polygons
              if polygon.select or not ONLY_SELECTED]
    if not wanted:
        raise SystemExit("no faces are selected, and ONLY_SELECTED is on")

    print("texturing %s: %d face(s), from %s"
          % (mesh.name, len(wanted),
             "%r" % donor.name if donor else "its own materials"))

    if donor is not None:
        resolved = _from_donor(context, obj, donor, wanted)
    else:
        resolved = _from_own_materials(obj, wanted)

    by_material = {}
    for face, (material, mapping) in resolved.items():
        by_material.setdefault(material.name if material else None, []).append(
            (face, material, mapping)
        )

    texture_attr, texture_values = _attribute(mesh, geometry.ATTR_TEXTURE)
    flags_attr, flag_values = _attribute(mesh, geometry.ATTR_FLAGS)
    uv_attr, uv_values = _attribute(mesh, geometry.ATTR_UV, width=2)
    uv_layer = mesh.uv_layers.active

    cache = {}
    done = skipped = overflowed = walls = 0

    for name in sorted(by_material, key=lambda text: text or ""):
        group = by_material[name]
        material = group[0][1]
        image, _node = _image_of(material)

        if image is None:
            print("  %-28s no image node - left alone" % (name or "no material"))
            skipped += len(group)
            continue

        texture = _import_image(context, image, code, directory, cache)
        surface = int(material.get(geometry.PROP_SURFACE, SURFACE))
        index = texture_ops.allocate(obj, texture, surface)
        entry = geometry.texture_table(obj)[index]
        slot, material = _slot_on(obj, material, donor is not None)

        # The category is the faces', not the material's: what a batch is comes
        # off its render flags, and this script does not change those.
        category = geometry.category_of(
            geometry.to_unsigned32(flag_values[group[0][0]])
        )

        for face, _material, mapping in group:
            polygon = mesh.polygons[face]
            corners = list(polygon.loop_indices)
            raw = _raw_uvs(mapping, entry["w"], entry["h"])
            if not texture_module.fits_s16(raw):
                overflowed += 1
                continue
            for at, corner in enumerate(corners):
                uv_values[corner * 2], uv_values[corner * 2 + 1] = raw[at]
                if uv_layer is not None and at < len(mapping):
                    uv_layer.data[corner].uv = mapping[at]

            texture_values[face] = index
            # A custom texture is one frame, so the animation bit comes off.
            flag_values[face] = geometry.to_signed32(
                geometry.to_unsigned32(flag_values[face])
                & ~level_model.RENDER_TEX_ANIM
            )
            polygon.material_index = slot
            if category == geometry.INVISIBLE_WALLS:
                walls += 1
            done += 1

        _stamp(material, index, surface, texture, category)
        print("  %-28s -> table entry %d, surface %d, %d face(s)"
              % (name or "no material", index, surface, len(group)))

    texture_attr.data.foreach_set("value", texture_values)
    flags_attr.data.foreach_set("value", flag_values)
    uv_attr.data.foreach_set("value", uv_values)
    mesh.update()

    print("done: %d face(s) textured, %d table entries on this track"
          % (done, len(geometry.texture_table(obj))))
    if skipped:
        print("  %d face(s) untouched - their material had no image" % skipped)
    if overflowed:
        print("  %d face(s) kept their old UVs: the unwrap tiles past 32 repeats, "
              "which is all an s16 UV holds. Scale that island down in the UV "
              "editor" % overflowed)
    if walls:
        print("  %d of them are invisible walls, which the game does not draw"
              % walls)


if __name__ == "__main__":
    run()
