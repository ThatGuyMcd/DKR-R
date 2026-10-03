"""Give a placed object the look it has in game.

An author placing a palm tree should see a palm tree. Before this, every object
was an Empty cross, so a track was a cloud of identical markers and there was no
way to tell a balloon from a checkpoint without clicking it.

Two representations, decided by the object's header:

* **sprite billboard** - an upright, texture-mapped quad. This is what most DKR
  scenery actually is: palm trees, balloons, coins and bushes are camera-facing
  2D sprites in the real game, not meshes, and they account for 84 of the 304
  object headers against 211 real models.
* **3D model** - a mesh built from the decoded ``ObjectModel``, with the baked
  vertex colours it ships with.
* **decal** - a flat square on the ground, for the one type whose look is its
  shadow: a ground zipper's arrow.

All three come out at the size the game draws them: the header's ``scale`` is
built into the shared mesh, and the object's own size byte - ``radius / 64`` on
a tree, ``scale / 64`` on a balloon - is the Blender object's scale.

A type whose header points at a debug placeholder keeps the addon's own marker
instead, because an AI node is a marker rather than a thing and a row of
identical debug dots reads far worse than sized, distinct markers.

Every object keeps its decoded fields as custom properties and exports through
the same path either way: what the object map is made of is the transform and
the fields, never what the object is drawn with. ``tests/test_blender_roundtrip``
imports every retail map twice, once with artwork and once without, and requires
both to reproduce the file byte for byte.

Mesh data, materials and images are shared between every instance of a type, so
a map with two hundred trees holds one tree mesh.
"""

from __future__ import annotations

import math
import os
import traceback
from typing import Dict, Optional

import bpy

from . import object_model, scene

#: Marks an object as carrying decoded artwork, so a re-import can rebuild it.
PROP_PREVIEW = "dkr_preview"

#: Object models are already in the same units as the map's coordinates; the
#: header's own ``scale`` is the only multiplier the game applies, so this is
#: here purely as a tuning hook.
MODEL_SCALE = 1.0

#: How many world units one sprite pixel covers, at a header scale of 1.
#:
#: Not a tuning value: it falls out of how the game draws a billboard.
#: ``render_sprite_billboard`` loads ``mtxf_billboard(scale, aspect)`` in place
#: of the whole model-view-projection matrix and the microcode adds each corner
#: to the anchor's *clip-space* position, so a pixel is ``scale`` clip units
#: across and ``scale * 4/3`` high. Undoing the projection - ``guPerspectiveF``
#: at 60 degrees with a 4:3 aspect - turns both into the same world size at the
#: anchor's depth: ``scale * 4/3 * tan(30 degrees)``, square pixels.
#:
#: The 60 is ``CAMERA_DEFAULT_FOV``, which 61 of the 65 retail level headers
#: keep. A level with a narrower field of view draws its sprites smaller on
#: screen than this, but the world they sit in shrinks with them.
SPRITE_FOV_DEGREES = 60.0
SPRITE_UNITS_PER_PIXEL = (320.0 / 240.0) * math.tan(math.radians(SPRITE_FOV_DEGREES) / 2.0)

#: ``shadow_generate``: a shadow's half width is ``10 * shadow scale``, and the
#: texture fills ``1 / sqrt(2)`` of that so it can turn with the object and
#: still fit the square the game searches for ground in.
SHADOW_HALF_WIDTH = 10.0 / math.sqrt(2.0)

#: The game lays a shadow 2 units above the surface it is projected onto.
SHADOW_LIFT = 2.0

_mesh_cache: Dict[str, Optional[bpy.types.Mesh]] = {}
_image_cache: Dict[str, Optional[bpy.types.Image]] = {}


def clear_cache():
    """Drop cached datablocks; they do not survive a new file."""
    _mesh_cache.clear()
    _image_cache.clear()


# ---------------------------------------------------------------------------
# Sprites
# ---------------------------------------------------------------------------

def _alive(datablock) -> bool:
    """Whether a cached datablock still exists.

    Opening another file frees every datablock in the old one, and the Python
    wrapper left behind raises ``ReferenceError`` on any attribute access - not
    just on use, but on the very check for whether it is still valid. So the
    check itself has to be guarded.
    """
    if datablock is None:
        return False
    try:
        return bool(datablock.name)
    except ReferenceError:
        return False


def _image(path: str, rom_rows: bool = False) -> Optional[bpy.types.Image]:
    # Checked first because neither the cache nor ``check_existing`` looks at
    # the disk: a file deleted since it was loaded would come back as the image
    # it used to be, and a material would go on drawing a picture that is gone.
    if not path or not os.path.isfile(path):
        return None
    key = path + ("#rom" if rom_rows else "")
    cached = _image_cache.get(key)
    if _alive(cached):
        return cached
    try:
        image = bpy.data.images.load(path, check_existing=True)
    except RuntimeError:
        image = None
    if image is not None and rom_rows:
        image = _in_rom_rows(image)
    _image_cache[key] = image
    return image


ROM_ROWS_SUFFIX = " (rom rows)"


def _in_rom_rows(image) -> bpy.types.Image:
    """A copy of ``image`` with its rows in the order the ROM holds them.

    A retail 3D texture was turned right way up when it was extracted
    (:meth:`assets.AssetTree.texture_3d_flipped`), but a model's UVs count
    rows from the ROM's first. Drawn as extracted, every picture on a model
    stands on its head - a sky's mountains hang from it. Turning the picture
    back, rather than the UVs, leaves every coordinate a model holds exactly as
    decoded. Packed, so the ``.blend`` keeps it without the extraction.
    """
    name = image.name + ROM_ROWS_SUFFIX
    existing = bpy.data.images.get(name)
    if existing is not None:
        return existing
    width, height = image.size
    if not width or not height:
        return image
    pixels = list(image.pixels)
    stride = width * 4
    copy = bpy.data.images.new(name, width, height, alpha=True)
    copy.pixels = [value for row in range(height - 1, -1, -1)
                   for value in pixels[row * stride:(row + 1) * stride]]
    try:
        copy.pack()
    except RuntimeError:
        pass  # kept for this session; the .blend reloads it from the extraction
    return copy


def sprite_mesh(frame, scale: float, name: str = "sprite") -> Optional[bpy.types.Mesh]:
    """Upright, texture-mapped quads showing one sprite frame, at game size.

    A real mesh rather than an Empty with an image: an Empty's image is drawn
    only in the viewport, so a track rendered to a picture would lose every tree
    and balloon on it. A quad also takes a material, so the sprite's alpha cuts
    the background out properly.

    One quad per tile of the frame (:class:`assets.SpriteFrame`), each with its
    own texture and placed where ``sprite_init_frame`` puts it, so a palm tree
    shows all five of its strips rather than the top one. The object's origin is
    the sprite's anchor, as in game - a balloon hangs from the end of its string.

    The quads stand in the object's local XZ plane, and Blender's XYZ euler
    applies Z last, so the object's yaw still lives in ``rotation_euler.z`` and
    the angle field keeps working untouched.
    """
    if frame is None or not frame.tiles:
        return None

    key = "sprite:%s@%.4f" % ("|".join(t.png for t in frame.tiles), scale)
    cached = _mesh_cache.get(key)
    if _alive(cached):
        return cached

    unit = SPRITE_UNITS_PER_PIXEL * scale
    vertices, faces, uvs, materials, slots = [], [], [], [], []
    for tile in frame.tiles:
        image = _image(tile.png)
        if image is None:
            continue
        left, right, bottom, top = tile.quad(frame.anchor_x, frame.anchor_y)
        base = len(vertices)
        vertices += [(left * unit, 0.0, bottom * unit), (right * unit, 0.0, bottom * unit),
                     (right * unit, 0.0, top * unit), (left * unit, 0.0, top * unit)]
        faces.append((base, base + 1, base + 2, base + 3))
        # The game samples texels 0 to width - 1 across the quad, top row at
        # the top: the last row and column sit on the edge, where the next tile
        # starts on a copy of them.
        u = (tile.width - 1) / float(tile.width)
        v = 1.0 / tile.height
        uvs.append(((0.0, v), (u, v), (u, 1.0), (0.0, 1.0)))
        material = _sprite_material(image)
        if material not in materials:
            materials.append(material)
        slots.append(materials.index(material))
    if not faces:
        return None

    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    for material in materials:
        mesh.materials.append(material)

    uv = mesh.uv_layers.new(name="UVMap")
    for polygon, corners, slot in zip(mesh.polygons, uvs, slots):
        polygon.material_index = slot
        for loop_index, coordinate in zip(polygon.loop_indices, corners):
            uv.data[loop_index].uv = coordinate

    _mesh_cache[key] = mesh
    return mesh


def decal_mesh(path: str, shadow_scale: float) -> Optional[bpy.types.Mesh]:
    """A flat square on the ground showing a shadow texture, at game size.

    For a ground zipper this is the whole object: the arrow on the road is its
    shadow, projected onto the track by ``shadow_generate``. The texture there
    is mapped by the object's yaw-relative position, so it turns with the
    object; a quad in the object's local plane does the same.

    The game drapes it over whatever track lies within 100 units below; this
    lays it flat at the object's own height instead, lifted the 2 units the
    game lifts it, so it reads on a flat road and never hides under one.
    """
    image = _image(path)
    if image is None or shadow_scale <= 0.0:
        return None
    key = "decal:%s@%.4f" % (path, shadow_scale)
    cached = _mesh_cache.get(key)
    if _alive(cached):
        return cached

    half = SHADOW_HALF_WIDTH * shadow_scale
    width, height = image.size[0] or 32, image.size[1] or 32
    corners = ((-half, -half), (half, -half), (half, half), (-half, half))
    # Map (x, z) as the game does: texel = local * (width / (2 * half)) + width / 2,
    # for both axes, with texel row 0 at the picture's top.
    vertices, coordinates = [], []
    for x, z in corners:
        vertices.append(tuple(scene.to_blender((x, SHADOW_LIFT, z))))
        coordinates.append(((x / (2.0 * half)) + 0.5,
                            1.0 - ((z * width / (2.0 * half)) + width / 2.0) / height))
    # ``to_blender`` mirrors Z, which flips the winding; keep the face looking up.
    order = (0, 3, 2, 1)

    mesh = bpy.data.meshes.new("decal_" + os.path.splitext(os.path.basename(path))[0])
    mesh.from_pydata(vertices, [], [order])
    mesh.update()
    uv = mesh.uv_layers.new(name="UVMap")
    for loop_index, vertex in zip(mesh.polygons[0].loop_indices, order):
        uv.data[loop_index].uv = coordinates[vertex]
    mesh.materials.append(_sprite_material(image))
    _mesh_cache[key] = mesh
    return mesh


def _sprite_material(image) -> bpy.types.Material:
    """An unlit, alpha-clipped material so a sprite reads as it does in game."""
    name = "dkr sprite %s" % image.name
    existing = bpy.data.materials.get(name)
    if existing is not None:
        return existing

    material = bpy.data.materials.new(name)
    material.diffuse_color = (1.0, 1.0, 1.0, 1.0)
    try:
        tree, surface = reset_node_tree(material)
        if tree is None:
            return material

        texture = tree.nodes.new("ShaderNodeTexImage")
        texture.image = image
        # Sprites are tiny; nearest keeps the pixel art crisp instead of blurring
        # a 32 pixel balloon into a smudge.
        texture.interpolation = "Closest"
        texture.location = (-500, 0)

        emission = tree.nodes.new("ShaderNodeEmission")
        emission.location = (-260, 100)
        transparent = tree.nodes.new("ShaderNodeBsdfTransparent")
        transparent.location = (-260, -80)
        mix = tree.nodes.new("ShaderNodeMixShader")
        mix.location = (-80, 0)

        tree.links.new(texture.outputs["Color"], emission.inputs["Color"])
        tree.links.new(texture.outputs["Alpha"], mix.inputs["Fac"])
        tree.links.new(transparent.outputs["BSDF"], mix.inputs[1])
        tree.links.new(emission.outputs[0], mix.inputs[2])
        tree.links.new(mix.outputs["Shader"], surface)

        for attribute, value in (("blend_method", "HASHED"),
                                 ("shadow_method", "NONE"),
                                 ("use_backface_culling", False)):
            if hasattr(material, attribute):
                setattr(material, attribute, value)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()
    return material


def reset_node_tree(material):
    """Clear a material down to a bare output node and return ``(tree, surface)``.

    Removing nodes reallocates the tree's node collection, which leaves any
    reference taken beforehand dangling - it survives as a Python object but
    reports zero inputs. So the output node is looked up *after* the clear, not
    before. Getting this wrong is silent: the material simply comes out blank.
    """
    material.use_nodes = True
    tree = material.node_tree
    if tree is None:
        return None, None

    keep = next((n for n in tree.nodes if n.type == "OUTPUT_MATERIAL"), None)
    keep_name = keep.name if keep else None
    for node in list(tree.nodes):
        if node.name != keep_name:
            tree.nodes.remove(node)

    output = next((n for n in tree.nodes if n.type == "OUTPUT_MATERIAL"), None)
    if output is None:
        output = tree.nodes.new("ShaderNodeOutputMaterial")
    surface = output.inputs.get("Surface")
    if surface is None and len(output.inputs):
        surface = output.inputs[0]
    return (tree, surface) if surface is not None else (None, None)


# ---------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------

def _mesh(path: str, scale: float, tree=None) -> Optional[bpy.types.Mesh]:
    """Build, or reuse, the Blender mesh for one object model.

    Batches are grouped by texture into material slots and the triangles carry
    their own UVs, so a decoded model comes out looking like the object rather
    than a grey shape. Where a batch has no texture, or the texture was not
    extracted, the material falls back to the model's baked vertex colours.
    """
    key = "%s@%.4f@%s" % (path, scale, tree.root if tree else "-")
    cached = _mesh_cache.get(key)
    if _alive(cached):
        return cached

    try:
        model = object_model.load(path)
    except Exception:  # noqa: BLE001 - a bad model must not stop an import
        traceback.print_exc()
        _mesh_cache[key] = None
        return None

    faces = []
    face_slots = []
    face_uvs = []
    slots = {}
    materials = []

    for batch in model.batches:
        texture = model.texture_for(batch)
        png = tree.texture_3d_png(texture.texture_id) if (tree and texture) else None
        rom_rows = bool(png) and tree.texture_3d_flipped(texture.texture_id)
        slot_key = (png + ("#rom" if rom_rows else "")) if png else "<vertex colours>"
        if slot_key not in slots:
            slots[slot_key] = len(materials)
            materials.append(
                _textured_material(png, rom_rows) if png else _vertex_colour_material()
            )
        slot = slots[slot_key]

        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face >= len(model.triangles):
                break
            _flags, vi0, vi1, vi2 = model.triangles[face]
            indices = tuple(batch.vertex_offset + i for i in (vi0, vi1, vi2))
            if max(indices) >= len(model.vertices):
                continue
            faces.append(indices)
            face_slots.append(slot)
            face_uvs.append(model.face_uvs(face, texture) if png else None)

    if not faces or not model.vertices:
        _mesh_cache[key] = None
        return None

    vertices = [
        tuple(c * scale for c in scene.to_blender(v)) for v in model.vertices
    ]
    mesh = bpy.data.meshes.new(os.path.splitext(os.path.basename(path))[0])
    mesh.from_pydata(vertices, [], faces)
    mesh.validate(verbose=False)
    mesh.update()

    for material in materials:
        mesh.materials.append(material)

    # ``validate`` can drop a degenerate face, so only apply the per-face data
    # when the counts still line up.
    if len(mesh.polygons) == len(faces):
        uv_layer = mesh.uv_layers.new(name="UVMap")
        for index, polygon in enumerate(mesh.polygons):
            polygon.material_index = face_slots[index]
            coordinates = face_uvs[index]
            if coordinates is None:
                continue
            for corner, loop_index in enumerate(polygon.loop_indices):
                if corner < len(coordinates):
                    uv_layer.data[loop_index].uv = coordinates[corner]

    if len(model.colours) == len(mesh.vertices):
        layer = mesh.color_attributes.new("baked", "FLOAT_COLOR", "POINT")
        for index, rgba in enumerate(model.colours):
            layer.data[index].color = tuple(_srgb_to_linear(c / 255.0) for c in rgba[:3]) + (
                rgba[3] / 255.0,
            )
    _mesh_cache[key] = mesh
    return mesh


def _textured_material(png: str, rom_rows: bool = False) -> bpy.types.Material:
    """An unlit material showing one of a model's textures."""
    image = _image(png, rom_rows)
    if image is None:
        return _vertex_colour_material()
    name = "dkr texture %s" % image.name
    existing = bpy.data.materials.get(name)
    if existing is not None:
        return existing

    material = bpy.data.materials.new(name)
    material.diffuse_color = (0.85, 0.85, 0.85, 1.0)
    try:
        tree, surface = reset_node_tree(material)
        if tree is None:
            return material
        texture = tree.nodes.new("ShaderNodeTexImage")
        texture.image = image
        texture.interpolation = "Closest"
        texture.location = (-500, 0)
        emission = tree.nodes.new("ShaderNodeEmission")
        emission.location = (-220, 0)
        tree.links.new(texture.outputs["Color"], emission.inputs["Color"])
        tree.links.new(emission.outputs[0], surface)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()
    return material


def _vertex_colour_material() -> bpy.types.Material:
    """One shared unlit material that displays the ``baked`` colour attribute."""
    name = "dkr object shading"
    existing = bpy.data.materials.get(name)
    if existing is not None:
        return existing

    material = bpy.data.materials.new(name)
    material.diffuse_color = (0.8, 0.8, 0.8, 1.0)
    try:
        tree, surface = reset_node_tree(material)
        if tree is None:
            return material
        attribute = tree.nodes.new("ShaderNodeVertexColor")
        attribute.layer_name = "baked"
        attribute.location = (-400, 0)
        emission = tree.nodes.new("ShaderNodeEmission")
        emission.location = (-200, 0)
        tree.links.new(attribute.outputs["Color"], emission.inputs["Color"])
        tree.links.new(emission.outputs[0], surface)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()
    return material


def _srgb_to_linear(value: float) -> float:
    """Vertex colours are authored in sRGB; Blender colour attributes are linear.

    Without this the baked lighting washes out to near white, which is how the
    track first rendered.
    """
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


def _linear_to_srgb(value: float) -> float:
    """The way back, for a colour an author painted rather than one decoded.

    Exact where it has to be: converting all 256 byte values to linear and back
    returns every one of them unchanged, in float32 as well as in double, so a
    colour that came out of a track and went straight back in is untouched.
    """
    if value <= 0.0031308:
        return value * 12.92
    return 1.055 * (max(0.0, value) ** (1.0 / 2.4)) - 0.055


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

#: Fields that pick among the models an object header lists.
VARIANT_FIELDS = ("balloonType", "modelIndex")


def variant_for(object_id: str, fields, catalog) -> int:
    """Which of the header's models this object uses.

    A weapon balloon's header lists five sprites and its ``balloonType`` picks
    among them, so a boost balloon and a trap balloon look different the way
    they do in game.

    The index has to come from the enum's **declaration** order, because that is
    what the header's list is ordered by. Using the catalogue's ``values`` - the
    members retail was observed to use, which are sorted alphabetically - lines
    the lists up wrongly: ``BALLOON_TYPE_MISSILE`` is second by declaration but
    third alphabetically, so a missile balloon would be drawn as a trap.
    """
    object_type = catalog.get(object_id) if catalog else None
    if object_type is None:
        return 0

    for field in object_type.fields:
        if field.name not in VARIANT_FIELDS or field.name not in fields:
            continue
        value = fields[field.name]
        if field.kind == "enum":
            members = catalog.enums.get(field.enum) or list(field.values)
            try:
                return members.index(str(value))
            except ValueError:
                return 0
        if isinstance(value, int):
            return value
    return 0


#: The game draws its own invisible helpers - AI nodes, camera hints, triggers -
#: with a debug sphere, because they are markers rather than things. Reproducing
#: that faithfully would replace the addon's clear, sized markers with a handful
#: of identical dots, which reads far worse when the whole point is to see the
#: shape of an AI graph. So a debug placeholder means "keep the marker".
DEBUG_TEXTURE_PREFIX = "debug_"


def _resolve(object_id, fields, tree, catalog):
    """``(kind, path, header, model_id)`` for one placed object, variant included.

    A type whose model is a debug placeholder but which has a shadow texture is
    drawn as that shadow - a ``"decal"`` - because that is all the game shows of
    it: a ground zipper is an arrow on the road and nothing else.
    """
    try:
        variant = variant_for(object_id, fields, catalog)
        kind, path, header = tree.preview_for(object_id, variant)
        model_id = tree.model_id_for(object_id, variant)
    except Exception:  # noqa: BLE001 - artwork must never stop an import
        traceback.print_exc()
        return "none", None, None, None

    if kind == "sprite" and path:
        if os.path.basename(path).startswith(DEBUG_TEXTURE_PREFIX):
            shadow = tree.shadow_png(header)
            if shadow:
                return "decal", shadow, header, model_id
            return "none", None, header, model_id
    return kind, path, header, model_id


def mesh_for(object_id: str, fields, tree, catalog):
    """The shared mesh datablock for this object, and what it is.

    Returns ``(mesh, kind)``. ``kind`` is ``"mesh"`` for a decoded object model,
    ``"sprite"`` for a billboard, ``"decal"`` for a shadow laid on the ground,
    or ``"none"`` when the type has no artwork - an AI node or a trigger, which
    are markers rather than things and stay Empties.

    Everything is built at the header's ``scale``. The per-object part of the
    size - a tree's ``radius``, a balloon's ``scale`` - is the object's own
    Blender scale (:func:`scene._apply_scale`), so the mesh can be shared:
    datablocks are shared between every instance of a type, and a map with two
    hundred trees holds one tree mesh and one material.
    """
    if tree is None:
        return None, "none"
    kind, path, header, model_id = _resolve(object_id, fields, tree, catalog)
    if not path:
        return None, "none"
    scale = header.scale if header else 1.0

    if kind == "sprite":
        try:
            frame = tree.sprite_frame(model_id) if model_id else None
        except Exception:  # noqa: BLE001 - artwork must never stop an import
            traceback.print_exc()
            frame = None
        name = "sprite_" + (model_id or "").replace("ASSET_SPRITE_", "").lower()
        mesh = sprite_mesh(frame, scale, name)
        return (mesh, "sprite") if mesh is not None else (None, "none")
    if kind == "decal":
        mesh = decal_mesh(path, header.shadow_scale if header else 0.0)
        return (mesh, "decal") if mesh is not None else (None, "none")
    if kind == "mesh":
        mesh = _mesh(path, scale * MODEL_SCALE, tree)
        return (mesh, "mesh") if mesh is not None else (None, "none")
    return None, "none"


def has_artwork(object_id: str, tree) -> bool:
    """Whether this type has anything to draw.

    Asked before the object is created, because Blender fixes an object's type
    at creation: a mesh object cannot later become an Empty, or the reverse.
    """
    if tree is None:
        return False
    try:
        header = tree.object_header(object_id)
    except Exception:  # noqa: BLE001
        return False
    return bool(header and header.models)
