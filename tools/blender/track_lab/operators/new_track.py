"""Turn a mesh an author modelled into track geometry.

The three steps of Phase 2 all start from a track the game already ships. This
is the one that does not: a mesh built in Blender, with no segmentation, no
texture table and no identity on any vertex, becoming a level model.

**How it works, and why in that order.** The mesh is read into loose faces and
one vertex pool, exactly the shape :func:`level_model_layout.rebatch_segment`
takes. :func:`level_model_layout.blank_model` supplies a model with every field
of the segment struct that is not safe to leave at zero already set - which is
why nothing here names one. The faces go into its single segment, and then
:func:`level_model_layout.resegment` partitions the whole thing properly and
builds the bounding boxes, the BSP and the PVS to match.

The result is written out as a ``.bin`` and then imported back through the
ordinary path, rather than being turned into a mesh directly. That is not a
detour: the author gets the same editable geometry as any other track, with the
identity attributes, the material slots and the base file that every other
operator here already understands, and there is exactly one importer to keep
correct instead of two.

**Where the textures come from.** Three places, and a conversion can use more
than one:

* **The mesh's own materials.** A mesh an author textured in Blender carries
  its pictures on its materials, and :func:`_adopt_images` makes each of them
  one of the track's own textures - resampled to what the RDP can load, with
  the original kept for the export's high-resolution pack. This is the flow
  the whole feature exists for: model, ``Ctrl+J``, *Track From Mesh*, export,
  and the track draws what the author drew. It happens here, before any
  triangulating, because this is the one moment a polygon and its material
  and its UVs are all still in hand - ``apply_material_textures.py`` has to
  match faces back by position precisely because it runs after that moment.
* **A donor track's table**, for :class:`DKR_OT_track_from_mesh`: a coherent
  set of the ROM's images with their surface types, and whatever a material
  that came off an imported track already names.
* **Nothing**, for a material with no picture. Its faces come out untextured,
  and the Textures panel - where all 1401 of the ROM's textures are available
  rather than one track's table - is where they get their look.
"""

from __future__ import annotations

import os
import traceback

import bpy
from bpy.props import BoolProperty, StringProperty
from bpy_extras.io_utils import ImportHelper

from .. import (assets, level_model, level_model_encoder, level_model_layout,
                prefs, rice_identity, scene, textures as texture_module,
                transparency as looks)
from . import custom_textures, geometry

#: Where the marker lives, so both this module and the export's "meshes I cannot
#: use" check read one name rather than two that have to agree.
PROP_CONVERTED = geometry.PROP_CONVERTED

#: Two UVs closer than this are the same point. A face whose corners all share
#: one has no mapping - it draws a single texel of its texture.
_SAME_UV = 1e-9


def convertible(context) -> list:
    """Meshes that could be turned into a track: the author's own, unconverted."""
    return [
        obj for obj in geometry.unusable_meshes(context)
        if PROP_CONVERTED not in obj
    ]


def converted_meshes(context) -> list:
    """The author's meshes already turned into a track, kept hidden beside it."""
    return [
        obj for obj in context.scene.objects
        if obj.type == "MESH" and PROP_CONVERTED in obj
        and geometry.PROP_GEOMETRY not in obj
    ]


def _texture_for(material, slot: int, count: int, own=None) -> int:
    """Which entry of the starting texture table a material draws.

    A material whose picture became one of the track's own textures names that
    entry, through ``own``. A material that came from an imported track already
    names one. Any other falls back to its slot position in the ``count``
    borrowed entries - which is arbitrary, and is why the operator says which
    textures it used. With nothing borrowed it comes out untextured, which is
    the honest answer: the author picks the textures afterwards, in the
    Textures panel, where the whole ROM is available rather than one track's
    table.
    """
    if material is not None and own and material.name in own:
        return own[material.name]
    if material is not None and geometry.PROP_TEXTURE_INDEX in material:
        index = int(material[geometry.PROP_TEXTURE_INDEX])
        if 0 <= index < count:
            return index
    if count <= 0:
        return level_model.NO_TEXTURE
    return min(max(0, slot), count - 1)


def _flags_for(material) -> int:
    """Render flags from the material's kind, defaulting to drawn and solid."""
    kind = material.get(geometry.PROP_CATEGORY) if material is not None else None
    if kind == geometry.INVISIBLE_WALLS:
        return level_model.RENDER_HIDDEN
    if kind == geometry.DECORATION:
        return level_model.RENDER_NO_COLLISION
    return 0


def _raw_uv(uv, texture):
    """A Blender UV back into the fixed point the file stores.

    The inverse of :func:`level_model.normalise_uv`. Exact for anything the
    format can hold: the drift is bounded by float32's relative precision times
    the coordinate, and an s16 coordinate keeps that three hundred times under
    the half unit rounding needs.
    """
    if texture is None:
        return (0, 0)
    u, v = float(uv[0]), float(uv[1])
    return (
        int(round(u * level_model.UV_FRACTIONAL_BITS * texture.width)),
        int(round((1.0 - v) * level_model.UV_FRACTIONAL_BITS * texture.height)),
    )


def _fit(uvs, texture):
    """``(uvs, fits)``: moved by whole repeats into the s16 the file stores.

    A raw UV is absolute, so an unwrap that sits sixty repeats out - common
    once an author scales an island up to tile - is past what an s16 holds. A
    shift by a whole number of repeats cannot be seen on a texture that wraps,
    so the triangle is moved back towards zero by exactly that; only one whose
    own span is too wide is clamped, and counted.
    """
    if texture is None or texture_module.fits_s16(uvs):
        return uvs, True
    repeat_s = int(level_model.UV_FRACTIONAL_BITS * texture.width)
    repeat_t = int(level_model.UV_FRACTIONAL_BITS * texture.height)
    offset_s = (min(s for s, _t in uvs) // repeat_s) * repeat_s
    offset_t = (min(t for _s, t in uvs) // repeat_t) * repeat_t
    moved = [(s - offset_s, t - offset_t) for s, t in uvs]
    if texture_module.fits_s16(moved):
        return moved, True
    return [(max(-32768, min(32767, s)), max(-32768, min(32767, t)))
            for s, t in moved], False


def _flat(uvs) -> bool:
    """Whether every corner shares one UV - a face with no mapping at all."""
    first = uvs[0]
    return all(abs(u - first[0]) < _SAME_UV and abs(v - first[1]) < _SAME_UV
               for u, v in uvs[1:])


def _polygon_uvs(mesh, polygon, stats):
    """The UVs a face was mapped with, one per corner, or ``None``.

    The active map, unless the face has no mapping there and another map does
    - which is what ``Ctrl+J`` leaves behind when the pieces' maps had different
    names: it matches maps by name, so the joined mesh has one per name and
    each piece's faces are mapped in only one of them. Reading the active map
    alone would drop the mapping of every other piece without a word.
    """
    layers = mesh.uv_layers
    active = layers.active
    if active is None:
        return None
    corners = list(polygon.loop_indices)
    uvs = [tuple(active.data[corner].uv) for corner in corners]
    if len(layers) > 1 and _flat(uvs):
        for layer in layers:
            if layer.name == active.name:
                continue
            other = [tuple(layer.data[corner].uv) for corner in corners]
            if not _flat(other):
                stats["uv_rescued"] += 1
                stats.setdefault("uv_layers_used", set()).add(layer.name)
                return other
    return uvs


def read_source_mesh(obj, textures, own=None, borrowed=None, stats=None,
                     table_looks=None):
    """``(faces, positions, colours)`` for :func:`rebatch_segment`.

    Quads are fanned into triangles rather than refused, for the same reason the
    geometry export fans them: the file stores triangles and Blender's modelling
    tools produce quads, so refusing would make the ordinary way of building a
    mesh unusable.

    ``own`` maps a material's name to the table entry its picture became;
    ``borrowed`` is how many of ``textures`` came from a donor, which is what a
    material with no picture of its own falls back into. ``stats``, if given,
    is filled with what the UVs needed: ``uv_rescued`` faces whose mapping was
    in a map other than the active one, ``uv_unmapped`` textured faces with no
    mapping anywhere, and ``uv_clamped`` faces too wide for the s16 a UV is.

    ``table_looks`` is :func:`table_looks`: for each entry whose texture is
    known, the look its faces take and whether the game draws it see-through,
    which together decide the cut-out bit and the pass each face is drawn in.
    """
    table_looks = table_looks or {}
    mesh = obj.data
    matrix = obj.matrix_world
    borrowed = len(textures) if borrowed is None else int(borrowed)
    stats = {} if stats is None else stats
    for key in ("uv_rescued", "uv_unmapped", "uv_clamped"):
        stats.setdefault(key, 0)

    positions = []
    for index, vertex in enumerate(mesh.vertices):
        place = scene.to_map(matrix @ vertex.co)
        rounded = tuple(int(round(float(c))) for c in place)
        for component in rounded:
            if not -32768 <= component <= 32767:
                raise ValueError(
                    "vertex %d sits at %r, outside the s16 a level model stores "
                    "positions in. Scale the mesh down - a retail track spans "
                    "roughly 20000 units end to end" % (index, rounded)
                )
        positions.append(rounded)

    colours = _read_colours(mesh, stats)

    faces = []
    for polygon in mesh.polygons:
        material = (mesh.materials[polygon.material_index]
                    if polygon.material_index < len(mesh.materials) else None)
        index = _texture_for(material, polygon.material_index, borrowed, own)
        texture = textures[index] if 0 <= index < len(textures) else None
        flags = _flags_for(material)
        opaque = True
        if index in table_looks:
            look, translucent = table_looks[index]
            flags = looks.with_mode(flags, look)
            opaque = looks.draws_in_opaque_pass(flags, translucent)
        key = level_model_layout.BatchKey(
            index, flags, 0, 0, 0, opaque, None,
        )
        mapping = _polygon_uvs(mesh, polygon, stats)
        if texture is not None and (mapping is None or _flat(mapping)):
            stats["uv_unmapped"] += 1
        vertices = list(polygon.vertices)
        clamped = False
        for corner in range(1, len(vertices) - 1):
            picks = (0, corner, corner + 1)
            if mapping is None:
                uvs = [(0, 0)] * 3
            else:
                uvs, fits = _fit([_raw_uv(mapping[p], texture) for p in picks],
                                 texture)
                clamped = clamped or not fits
            faces.append(level_model_layout.Face(
                key, tuple(vertices[p] for p in picks),
                tuple(tuple(pair) for pair in uvs), 0
            ))
        if clamped:
            stats["uv_clamped"] += 1
    return faces, positions, colours


def table_looks(textures, tree=None, own=()) -> dict:
    """``{table index: (look, see-through)}`` for the entries whose texture is known.

    A picture the mesh brought takes the look its alpha asked for; one of the
    ROM's takes the one it was made with.
    """
    found = {}
    for index, reference in enumerate(textures):
        texture = geometry.texture_object(reference.texture_id, tree, own)
        if texture is not None:
            found[index] = (texture.transparency, bool(texture.translucent))
    return found


def _written(layer) -> bool:
    """True when this colour layer carries anything usable as lighting.

    An all-zero layer is not something Blender produces: measured on 5.2, a new
    colour attribute starts **white** in every domain and storage type, and
    painting writes an opaque alpha. What does produce one is carrying a model
    whose vertices are already black through a mesh and back, which is exactly
    the case that needs catching.

    Alpha is part of the test, and it is what keeps a deliberate black: black
    painted on purpose has alpha 1, while an all-zero layer is also fully
    transparent, which is not lighting anyone asked for.
    """
    try:
        values = [0.0] * (len(layer.data) * 4)
        layer.data.foreach_get("color", values)
    except (RuntimeError, TypeError):
        return False
    return any(values)


def _read_colours(mesh, stats=None) -> list:
    """The baked lighting, white where the author painted none.

    White rather than black: the colours are multiplied into the texture, so
    black would render the whole track unlit and look like a broken import.

    A layer counts as lighting only if it carries something - see
    :func:`_written`. An all-zero layer used to defeat this fallback, because
    the fallback tested only whether a layer existed, and the track it produced
    was black everywhere with no diagnostic anywhere. Painting black on purpose
    still reaches the file.
    """
    white = (255, 255, 255, 255)
    stats = {} if stats is None else stats
    layer = mesh.color_attributes.get(geometry.COLOUR_ATTRIBUTE)
    if layer is not None and layer.domain != "POINT":
        stats["colour_domain"] = layer.domain
        layer = None
    if layer is not None and not _written(layer):
        stats["colour_pristine"] = layer.name
        layer = None
    if layer is None:
        # Name the layers that do carry paint. Which of them the author meant
        # is theirs to say - choosing here would swap one silent guess for
        # another - so this only reports what is there.
        stats["colour_elsewhere"] = sorted(
            other.name for other in mesh.color_attributes
            if other.name != geometry.COLOUR_ATTRIBUTE and _written(other)
        )
        return [white] * len(mesh.vertices)

    values = [0.0] * (len(layer.data) * 4)
    try:
        layer.data.foreach_get("color", values)
    except (RuntimeError, TypeError):
        return [white] * len(mesh.vertices)

    from ..preview import _linear_to_srgb  # noqa: PLC0415 - optional helper

    colours = []
    for index in range(len(mesh.vertices)):
        at = index * 4
        if at + 4 > len(values):
            colours.append(white)
            continue
        colours.append(tuple(
            max(0, min(255, int(round(_linear_to_srgb(values[at + c]) * 255.0))))
            for c in range(4)
        ))
    return colours


# ---------------------------------------------------------------------------
# The mesh's own pictures
# ---------------------------------------------------------------------------

def material_images(obj) -> list:
    """``[(material, image), ...]`` for the pictures the mesh's faces draw.

    Only materials some face actually uses, since each picture spends one of
    the track's 255 ordinals; and not a material the addon made itself - one
    carrying a texture table index already names its entry.
    """
    mesh = obj.data
    used = {polygon.material_index for polygon in mesh.polygons}
    found = []
    for slot, material in enumerate(mesh.materials):
        if slot not in used or material is None:
            continue
        if geometry.PROP_TEXTURE_INDEX in material:
            continue
        image = custom_textures.image_of(material)
        if image is not None:
            found.append((material, image))
    return found


class Adoption:
    """What :func:`_adopt_images` did, for the operator to act on and report."""

    def __init__(self, table):
        #: The starting texture table: whatever was borrowed, then the entries
        #: the mesh's pictures became.
        self.table = list(table)
        #: Material name -> the entry of ``table`` it draws.
        self.own = {}
        #: How many textures were added to the scene - the tail of its list,
        #: which is what is taken back if the conversion fails afterwards.
        self.added = 0
        self.reused = 0
        #: ``(picture, reason, [material, ...])`` for the ones that could not
        #: be read. Their faces stay untextured; the rest go ahead.
        self.failed = []
        #: ``(texture name, reason)`` for textures with no high-resolution form.
        self.no_hd = []

    @property
    def pictures(self) -> int:
        return self.added + self.reused


def _format_code(settings) -> int:
    try:
        return int(settings.custom_format)
    except (TypeError, ValueError):
        return texture_module.FORMAT_CODES["RGBA16"]


def _adopt_images(context, obj, textures) -> Adoption:
    """Make each picture the mesh draws one of the track's own textures.

    One texture per *picture*, however many materials show it, and one table
    entry per picture and surface type - the surface lives on the entry. A
    picture the scene already holds, from an earlier conversion or the Add
    button, is reused rather than added again.

    The two ceilings - 255 textures of a track's own, and 255 entries in a
    model's table - are checked **before** anything is written, and hitting one
    refuses the whole conversion with the materials named: a track is never
    left half converted. A picture Blender cannot read is different: its faces
    stay untextured and the rest go ahead, because refusing a whole track over
    one unreadable file is the worse answer.
    """
    result = Adoption(textures)
    pairs = material_images(obj)
    if not pairs:
        return result

    settings = context.scene.dkr
    code = _format_code(settings)
    size = settings.custom_size

    pictures = {}
    order = []
    for material, image in pairs:
        source = custom_textures.image_source(image)
        key = os.path.normcase(os.path.normpath(source))
        if key not in pictures:
            pictures[key] = (source, image, [])
            order.append(key)
        pictures[key][2].append(material)

    held = {}
    for ordinal, record in enumerate(settings.custom_textures):
        if record.source:
            held.setdefault(os.path.normcase(os.path.normpath(record.source)),
                            ordinal)
    new = [key for key in order if key not in held]

    room = texture_module.CUSTOM_ID_COUNT - len(settings.custom_textures)
    if len(new) > room:
        left = sorted({material.name for key in new[room:]
                       for material in pictures[key][2]})
        raise custom_textures.CustomTextureError(
            "the mesh's materials draw %d picture(s) this track does not have "
            "yet, and a track can hold %d of its own - there is room for %d. "
            "%d material(s) would be left out: %s. Nothing was converted; give "
            "materials that should look alike one picture, or turn off Keep "
            "The Mesh's Textures"
            % (len(new), texture_module.CUSTOM_ID_COUNT, room, len(left),
               ", ".join(left[:8]) + (" ..." if len(left) > 8 else ""))
        )

    entries_needed = len({
        (key, int(material.get(geometry.PROP_SURFACE, 0)) & 0xFF)
        for key in order for material in pictures[key][2]
    })
    if len(textures) + entries_needed > level_model.MAX_TEXTURES:
        raise custom_textures.CustomTextureError(
            "the mesh's pictures need %d texture table entries on top of the %d "
            "borrowed, and a model's table holds %d. Nothing was converted; "
            "give materials that should look alike one picture, or start from "
            "no donor" % (entries_needed, len(textures),
                          level_model.MAX_TEXTURES)
        )

    ordinals = {}
    window = context.window_manager
    window.progress_begin(0, max(1, len(new)))
    try:
        for key in order:
            if key in held:
                ordinals[key] = held[key]
                result.reused += 1
                continue
            source, image, materials = pictures[key]
            try:
                path = custom_textures.image_path(image)
                entry, _was = custom_textures.add_image(
                    context, path, code, size,
                    name=custom_textures.image_label(image), note=source,
                )
            except custom_textures.CustomTextureError as error:
                result.failed.append((image.name, str(error),
                                      [m.name for m in materials]))
                continue
            except Exception as error:  # noqa: BLE001 - Blender image errors vary
                traceback.print_exc()
                result.failed.append((image.name, str(error),
                                      [m.name for m in materials]))
                continue
            ordinals[key] = entry.ordinal
            result.added += 1
            window.progress_update(result.added)
    finally:
        window.progress_end()

    found = custom_textures.entries(context)
    entry_of = {}
    for key in order:
        if key not in ordinals:
            continue
        entry = found[ordinals[key]]
        for material in pictures[key][2]:
            surface = int(material.get(geometry.PROP_SURFACE, 0)) & 0xFF
            if (key, surface) not in entry_of:
                result.table.append(level_model.TextureRef(
                    entry.index, entry.width, entry.height, entry.format, surface,
                ))
                entry_of[(key, surface)] = len(result.table) - 1
            result.own[material.name] = entry_of[(key, surface)]
        problem = rice_identity.hd_problem(entry.width, entry.height, entry.format)
        if problem:
            result.no_hd.append((entry.name, problem))
    return result


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

_KEEP_TEXTURES_DESCRIPTION = (
    "Make the pictures the mesh's materials draw into this track's own "
    "textures, mapped with the UVs you gave them. Each is reduced to what the "
    "console can load, and the export's HD pack gives DKR-R the original. "
    "Materials with no picture are unaffected"
)


class DKR_OT_track_from_mesh(bpy.types.Operator, ImportHelper):
    """Turn the selected mesh into DKR track geometry, borrowing a track's textures"""

    bl_idname = "dkr.track_from_mesh"
    #: What the file browser's confirm button says. It is not "Track From Mesh":
    #: by the time the browser is open the author has already asked for that, and
    #: what the button actually does is choose the track to borrow textures from.
    #: Left as the operator name it read as "pick your mesh", which is the one
    #: thing this dialog is not for.
    bl_label = "Use This Track's Textures"
    bl_options = {"REGISTER"}

    filename_ext = ".bin"
    filter_glob: StringProperty(default="*.bin;*.json", options={"HIDDEN"})

    keep_source: BoolProperty(
        name="Keep The Original Mesh",
        description=(
            "Leave the mesh you modelled in the scene, hidden. It is your own "
            "work and the addon does not delete it; the converted geometry is a "
            "separate object"
        ),
        default=True,
    )

    keep_textures: BoolProperty(
        name="Keep The Mesh's Textures",
        description=_KEEP_TEXTURES_DESCRIPTION,
        default=True,
    )

    @classmethod
    def poll(cls, context):
        return bool(convertible(context))

    def draw(self, context):
        """Say what the file browser is asking for.

        Without this it offers a file dialog with no explanation, and the
        obvious guess - that it wants the mesh, or the track being replaced - is
        wrong. It wants a texture table to start from.
        """
        layout = self.layout
        sources = convertible(context)

        box = layout.box()
        box.label(text="Pick a track to start from", icon="TEXTURE")
        box.label(text="levels/models/<world>/*.bin")
        box.separator()
        box.label(text="Its texture table is where")
        box.label(text="your track begins - a coherent")
        box.label(text="set of images with their")
        box.label(text="surface types. You can add any")
        box.label(text="of the ROM's other textures")
        box.label(text="afterwards, in the Textures")
        box.label(text="panel.")

        if sources:
            note = layout.box()
            note.label(text="Converting: %s" % sources[0].name, icon="MESH_DATA")
            note.label(text="%d faces" % len(sources[0].data.polygons))
            _draw_pictures(note, sources[0], self.keep_textures)

        layout.prop(self, "keep_textures")
        layout.prop(self, "keep_source")

    def execute(self, context):
        obj = _source_mesh(self, context)
        if obj is None:
            return {"CANCELLED"}

        donor = self.filepath
        if donor.lower().endswith(".json"):
            donor = level_model.sidecar_target(donor) or donor
        try:
            textures = level_model.load(donor).textures
        except level_model.LevelModelError as error:
            self.report(
                {"ERROR"},
                "could not read the texture table from %s: %s"
                % (os.path.basename(donor), error),
            )
            return {"CANCELLED"}
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "could not read %s: %s"
                        % (os.path.basename(donor), error))
            return {"CANCELLED"}

        return build_track(self, context, obj, textures, self.keep_source,
                           donor=donor, keep_textures=self.keep_textures)


def _distinct_pictures(obj) -> int:
    return len({os.path.normcase(custom_textures.image_source(image))
                for _material, image in material_images(obj)})


def _draw_pictures(layout, obj, keep):
    count = _distinct_pictures(obj)
    if not count:
        return
    if keep:
        layout.label(text="%d picture(s) from its materials" % count,
                     icon="IMAGE_DATA")
        layout.label(text="become this track's own")
        layout.label(text="textures, at the size the")
        layout.label(text="console loads. The export's")
        layout.label(text="HD pack restores the rest.")
    else:
        layout.label(text="%d picture(s) on its materials" % count,
                     icon="IMAGE_DATA")
        layout.label(text="will be left behind.")


def _source_mesh(operator, context):
    """The mesh being converted, or ``None`` with the reason already reported."""
    sources = convertible(context)
    active = context.active_object
    obj = active if active in sources else (sources[0] if sources else None)
    if obj is None:
        operator.report(
            {"ERROR"},
            "no mesh of your own in the scene to convert. Model one, or "
            "import a track's geometry and reshape that instead",
        )
        return None

    if not bpy.data.filepath:
        operator.report(
            {"ERROR"},
            "save the .blend first. A converted track is written as its own "
            "model file beside it, because that file becomes the base every "
            "later export is applied to - and a texture the mesh brings is "
            "written beside it too",
        )
        return None
    return obj


def build_track(operator, context, obj, textures, keep_source, donor=None,
                keep_textures=False):
    """Build a level model out of one mesh and import it back as geometry.

    Shared by the two ways in, which differ only in where the starting texture
    table comes from: a donor track's, or nothing at all. Everything after that
    point is the same, which is the reason it is one function - the segmenting,
    the layout and the re-import are exactly what must not drift between them.

    With ``keep_textures`` the mesh's own pictures are added to the table
    first. Anything that fails after that takes them back out again, so a
    refused conversion leaves the scene holding what it held before.
    """
    context.view_layer.update()
    # A scene moved without the folder beside it has lost the PNGs its own
    # textures are drawn and exported from. The materials rebuilt below are
    # reused by table entry, and a conversion can renumber the table, so each
    # picture has to be loadable again before they are.
    restored, lost = custom_textures.restore_missing(context)
    for level, message in custom_textures.restoration_reports(
            context, restored, lost):
        operator.report(level, message)
    borrowed = len(textures)
    adopted = None
    if keep_textures:
        try:
            adopted = _adopt_images(context, obj, textures)
        except custom_textures.CustomTextureError as error:
            operator.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        textures = adopted.table

    def refuse(message):
        if adopted is not None and adopted.added:
            custom_textures.discard_last(context, adopted.added)
        operator.report({"ERROR"}, message)
        return {"CANCELLED"}

    stats = {}
    tree = (assets.AssetTree.find(donor) if donor else None) or prefs.resolve(context)
    try:
        faces, positions, colours = read_source_mesh(
            obj, textures, own=adopted.own if adopted else None,
            borrowed=borrowed, stats=stats,
            table_looks=table_looks(textures, tree,
                                    custom_textures.entries(context)),
        )
    except ValueError as error:
        return refuse(str(error))

    if not faces:
        return refuse("%s has no faces to build a track from" % obj.name)

    try:
        model = level_model_layout.blank_model(textures)
        level_model_layout.rebatch_segment(
            model.segments[0], faces, positions, colours
        )
        segments = level_model_layout.resegment(model)
        payload = level_model_encoder.pack(model)
    except (level_model_layout.LayoutError,
            level_model_encoder.LevelModelEncodeError) as error:
        return refuse("could not build the track: %s" % error)

    stem = os.path.splitext(os.path.basename(bpy.data.filepath))[0]
    target = os.path.join(
        os.path.dirname(bpy.data.filepath), "%s-geometry.bin" % stem
    )
    try:
        with open(target, "wb") as handle:
            handle.write(payload)
    except OSError as error:
        return refuse("could not write %s: %s" % (target, error))

    name = obj.name
    uv_layers = [layer.name for layer in obj.data.uv_layers]
    # A material made see-through in Blender - glass, water - keeps that as its
    # opacity, read before the mesh it came from can be removed.
    faded = {}
    for material_name, entry in (adopted.own.items() if adopted else ()):
        alpha = custom_textures.material_alpha(bpy.data.materials.get(material_name))
        if alpha < 1.0:
            faded[entry] = min(alpha, faded.get(entry, 1.0))
    obj[PROP_CONVERTED] = target
    if keep_source:
        obj.hide_set(True)
    else:
        bpy.data.objects.remove(obj, do_unlink=True)

    # Imported back through the ordinary path, so the author gets the same
    # editable geometry as any other track and there is one importer to keep
    # correct rather than two.
    for existing in list(context.scene.objects):
        if geometry.PROP_GEOMETRY in existing:
            bpy.data.objects.remove(existing, do_unlink=True)
    collection = geometry._geometry_collection(context)
    built, _stats = geometry._build_geometry(
        stem, model, collection, tree, include_hidden=True,
        own=custom_textures.entries(context),
    )
    built[geometry.PROP_MODEL_PATH] = target
    built[geometry.PROP_AUTHORED_BASE] = True
    for slot in built.material_slots:
        material = slot.material
        entry = material.get(geometry.PROP_TEXTURE_INDEX) if material else None
        if entry is not None and int(entry) in faded:
            setattr(material, geometry.PROP_OPACITY, faded[int(entry)])
    geometry.record_budget(built, model)
    context.scene.dkr.geometry_path = target

    for warning in _budget_warnings(model, textures):
        operator.report({"WARNING"}, warning)
    for warning in _texture_warnings(adopted, stats, uv_layers):
        operator.report({"WARNING"}, warning)

    parts = []
    if donor:
        parts.append("%s's %d textures" % (os.path.basename(donor), borrowed))
    if adopted is not None and adopted.pictures:
        parts.append("%d picture(s) of the mesh's own" % adopted.pictures)
        # The one thing a material cannot say is what the ground is made of,
        # and finding out by driving is the wrong way to learn it.
        operator.report(
            {"INFO"},
            "the mesh's pictures all start as road (SURFACE_DEFAULT): select a "
            "material and use Set Surface Type for grass, sand, ice and the rest",
        )
    source = " and ".join(parts) if parts else "no textures yet"
    operator.report(
        {"INFO"},
        "built %d triangles into %d segments from %s, with %s, and wrote %s"
        % (model.triangle_count, segments, name if keep_source else "the mesh",
           source, os.path.basename(target)),
    )
    return {"FINISHED"}


def _texture_warnings(adopted, stats, uv_layers) -> list:
    messages = []
    if adopted is not None:
        for picture, reason, materials in adopted.failed:
            messages.append(
                "%s could not be made a texture, so the faces of %s stay "
                "untextured: %s" % (picture, ", ".join(materials), reason)
            )
        if adopted.no_hd:
            name, reason = adopted.no_hd[0]
            messages.append(
                "%d texture(s) will have no high-resolution version (%s): %s"
                % (len(adopted.no_hd), ", ".join(n for n, _r in adopted.no_hd),
                   reason)
            )
    if stats.get("uv_rescued"):
        messages.append(
            "the mesh has %d UV maps (%s), and %d face(s) had no mapping in the "
            "active one - what Ctrl+J leaves when the joined pieces' maps had "
            "different names. Their mapping was taken from %s. Renaming the "
            "maps to match before joining avoids this"
            % (len(uv_layers), ", ".join(uv_layers), stats["uv_rescued"],
               ", ".join(sorted(stats.get("uv_layers_used", ()))))
        )
    if stats.get("uv_unmapped"):
        messages.append(
            "%d textured face(s) have no UV mapping in any map, so each shows a "
            "single texel of its texture. Unwrap them, or select them and use "
            "Project Flat in the Textures panel" % stats["uv_unmapped"]
        )
    # Saying so is the point. The track still builds, and a white track looks
    # plausible enough that an author can ship it without ever learning that
    # the lighting they painted is not in it.
    if stats.get("colour_pristine") or stats.get("colour_domain"):
        if stats.get("colour_pristine"):
            reason = ("the '%s' colour layer has never been painted"
                      % stats["colour_pristine"])
        else:
            reason = ("the '%s' colour layer is on the %s domain, and the "
                      "baked lighting is read from Vertex"
                      % (geometry.COLOUR_ATTRIBUTE,
                         str(stats["colour_domain"]).title()))
        message = (
            "%s, so the track was built with white lighting. Vertex colour is "
            "multiplied into the texture, so this is what keeps a track from "
            "being drawn black" % reason
        )
        elsewhere = stats.get("colour_elsewhere") or []
        if elsewhere:
            message += (
                ". This mesh does carry paint in %s - convert it to the Vertex "
                "domain and name it '%s' to use it instead"
                % (", ".join("'%s'" % name for name in elsewhere),
                   geometry.COLOUR_ATTRIBUTE)
            )
        messages.append(message)
    if stats.get("uv_clamped"):
        messages.append(
            "%d face(s) stretch their texture across more repeats than the s16 "
            "a UV is stored in can reach, and were clamped. Scale those UV "
            "islands down" % stats["uv_clamped"]
        )
    return messages


def _budget_warnings(model, textures) -> list:
    messages = []
    used = level_model_layout.runtime_size(model)
    if used > level_model_layout.BUDGET:
        messages.append(
            "this track needs %d bytes at load, over the %d the game reserves. "
            "The overflow does not refuse - it writes past the heap - so this "
            "has to come down before the track is played"
            % (used, level_model_layout.BUDGET)
        )
    messages.extend(level_model_layout.check_collision_pressure(model))
    if not textures:
        messages.append(
            "this track starts with no texture table, so every face is "
            "untextured until you give it one. Select faces and apply a texture "
            "from the Textures panel; any of the ROM's will load"
        )
    return messages


class DKR_OT_track_from_mesh_blank(bpy.types.Operator):
    """Turn the selected mesh into DKR track geometry, keeping the pictures its materials draw"""

    # The same conversion as DKR_OT_track_from_mesh, without borrowing a
    # starting texture table. A face whose material draws a picture keeps it,
    # as one of the track's own textures; any other comes out untextured, and
    # the Textures panel is where it gets its look - which is the honest shape
    # of the job now that a track can name any texture in the ROM.

    bl_idname = "dkr.track_from_mesh_blank"
    bl_label = "Track From Mesh"
    bl_options = {"REGISTER"}

    keep_source: BoolProperty(
        name="Keep The Original Mesh",
        description=(
            "Leave the mesh you modelled in the scene, hidden. It is your own "
            "work and the addon does not delete it; the converted geometry is a "
            "separate object"
        ),
        default=True,
    )

    keep_textures: BoolProperty(
        name="Keep The Mesh's Textures",
        description=_KEEP_TEXTURES_DESCRIPTION,
        default=True,
    )

    @classmethod
    def poll(cls, context):
        return bool(convertible(context))

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def draw(self, context):
        layout = self.layout
        sources = convertible(context)
        if sources:
            box = layout.box()
            box.label(text="Converting: %s" % sources[0].name, icon="MESH_DATA")
            box.label(text="%d faces" % len(sources[0].data.polygons))
            _draw_pictures(box, sources[0], self.keep_textures)

        box = layout.box()
        box.label(text="A face with no picture comes", icon="INFO")
        box.label(text="out untextured. Pick textures")
        box.label(text="for those in the Textures")
        box.label(text="panel - any in the ROM will do.")

        layout.prop(self, "keep_textures")
        layout.prop(self, "keep_source")

    def execute(self, context):
        obj = _source_mesh(self, context)
        if obj is None:
            return {"CANCELLED"}
        return build_track(self, context, obj, [], self.keep_source,
                           keep_textures=self.keep_textures)


class DKR_OT_make_convertible(bpy.types.Operator):
    """Remove the converted mark from a mesh and show it, so Track From Mesh
    offers it again. Converting replaces the current track geometry, and edits
    made on the converted geometry are lost"""

    bl_idname = "dkr.make_convertible"
    bl_label = "Make Convertible Again"
    bl_options = {"REGISTER", "UNDO"}

    object_name: StringProperty(options={"HIDDEN"})

    def execute(self, context):
        obj = bpy.data.objects.get(self.object_name)
        if obj is None or PROP_CONVERTED not in obj:
            self.report({"ERROR"}, "no converted mesh called %r" % self.object_name)
            return {"CANCELLED"}
        del obj[PROP_CONVERTED]
        try:
            obj.hide_set(False)
        except RuntimeError:
            pass  # not in this view layer; nothing to show
        self.report({"INFO"}, "%s can be converted again; converting replaces the "
                    "current track geometry" % obj.name)
        return {"FINISHED"}


CLASSES = (DKR_OT_track_from_mesh, DKR_OT_track_from_mesh_blank,
           DKR_OT_make_convertible)
