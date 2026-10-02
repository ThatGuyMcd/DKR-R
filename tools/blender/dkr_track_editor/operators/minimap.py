"""The Minimap panel's working parts: the road's edges, the picture, the overlay.

The author traces the road's edges with Grease Pencil on one object, *DKR
Minimap*, and :mod:`..minimap` does the rest - the nine numbers, the picture,
the payloads. Nothing here chooses a number: an edge that is drawn is the whole
of the input, which is why the panel has no options to get wrong.

What a track ships follows from what the author did, never from a setting:

* **Edges drawn** - the track's own minimap.
* **No edges, retail shape** - an imported retail track whose geometry was not
  reshaped keeps the minimap it already has: the picture and the numbers are in
  its model, and nothing they depend on moved.
* **No edges otherwise** - no minimap. The export sets the level header's "no
  minimap" bit, the one hubs use, because a picture of another track's road is
  worse than none.

"Reshaped" is decided by the same code the export runs: the edited model's
bounds against the base file's, and whether any vertex moved, appeared or went.
The panel cannot run that while it draws - the check writes to the object - so
it runs on a timer after the mesh changes and the panel shows the answer when
it arrives.
"""

from __future__ import annotations

import math
import os
import traceback
import zlib
from array import array
from typing import List, Optional

import bpy
from bpy.props import StringProperty
from bpy_extras.io_utils import ExportHelper, ImportHelper
from mathutils import Vector

from .. import level_model, level_types, minimap, prefs, scene
from . import geometry as geometry_ops

OBJECT_NAME = "DKR Minimap"
LAYER_NAME = "Road Edges"
MATERIAL_NAME = "DKR Minimap Edge"
EDGE_COLOUR = (1.0, 0.78, 0.22, 1.0)
#: How thick the edges the addon writes are drawn, as a share of the track.
RADIUS_FRACTION = 0.0025
#: The panel preview is the picture this many times over.
PREVIEW_SCALE = 3

SETUPPOINT = level_types.SETUPPOINT

OWN = "OWN"               # edges drawn: the track's own minimap
RETAIL = "RETAIL"         # a retail track, not reshaped: keeps its own
CHANGED = "CHANGED"       # reshaped, no edges: ships without one
NEW = "NEW"               # built from a mesh, no edges: ships without one
BASE_NONE = "BASE_NONE"   # the base level has no minimap (a hub, a cutscene)
PENDING = "PENDING"       # the shape is being checked
NO_TRACK = "NO_TRACK"     # nothing to put a minimap on


# ---------------------------------------------------------------------------
# The edges
# ---------------------------------------------------------------------------

def edge_object(context):
    """The Grease Pencil object holding the road's edges, or ``None``."""
    obj = bpy.data.objects.get(OBJECT_NAME)
    if obj is None or obj.type != "GREASEPENCIL":
        return None
    if context.scene not in obj.users_scene:
        return None
    return obj


def _drawings(obj):
    for layer in obj.data.layers:
        if layer.hide:
            continue
        frame = layer.current_frame()
        if frame is None and len(layer.frames):
            frame = layer.frames[0]
        if frame is None or frame.drawing is None:
            continue
        yield layer, frame.drawing


def _stroke_points(drawing):
    """``[(positions, cyclic)]`` for every stroke, read in bulk where possible."""
    strokes = drawing.strokes
    try:
        count = len(strokes)
        offsets = [0] * (count + 1)
        drawing.curve_offsets.foreach_get("value", offsets)
        position = drawing.attributes["position"]
        flat = array("f", [0.0]) * (len(position.data) * 3)
        position.data.foreach_get("vector", flat)
        cyclic = [False] * count
        if "cyclic" in drawing.attributes:
            drawing.attributes["cyclic"].data.foreach_get("value", cyclic)
        found = []
        for index in range(count):
            first, last = offsets[index], offsets[index + 1]
            found.append(([tuple(flat[at * 3:at * 3 + 3]) for at in range(first, last)],
                          bool(cyclic[index])))
        return found
    except (KeyError, AttributeError, TypeError, RuntimeError, ValueError):
        return [([tuple(p.position) for p in stroke.points], bool(stroke.cyclic))
                for stroke in strokes]


def read_edges(context) -> List[minimap.Edge]:
    """Every stroke on the edge object, in map space."""
    obj = edge_object(context)
    if obj is None:
        return []
    edges = []
    for layer, drawing in _drawings(obj):
        matrix = obj.matrix_world @ layer.matrix_local
        for points, cyclic in _stroke_points(drawing):
            edges.append(minimap.Edge(
                [tuple(scene.to_map(matrix @ Vector(point))) for point in points],
                cyclic))
    return edges


def _signature(edges) -> int:
    values = array("d")
    for edge in edges:
        values.append(1.0 if edge.cyclic else 0.0)
        values.append(float(len(edge.points)))
        for point in edge.points:
            values.extend(point)
    return zlib.crc32(values.tobytes())


def _edge_material():
    material = bpy.data.materials.get(MATERIAL_NAME)
    if material is None:
        material = bpy.data.materials.new(MATERIAL_NAME)
    if material.grease_pencil is None:
        bpy.data.materials.create_gpencil_data(material)
    material.grease_pencil.color = EDGE_COLOUR
    material.grease_pencil.show_fill = False
    return material


def plane_height(bounds) -> float:
    """Where the edges are drawn: just above the highest point of the track,
    so a stroke is never buried in it. Only X and Z reach the picture."""
    if bounds is None:
        return 0.0
    return float(bounds[3]) + 50.0


def ensure_edge_object(context, bounds=None):
    """``(object, layer, drawing)``, making whatever is missing."""
    obj = edge_object(context)
    if obj is None:
        # Blender 4.3-4.5 keep the legacy datablocks under ``grease_pencils``,
        # which no longer make an object; the new kind is ``grease_pencils_v3``
        # there, and took over the plain name in 5.0. Not ``or``: an empty
        # collection is falsy.
        pencils = getattr(bpy.data, "grease_pencils_v3", None)
        if pencils is None:
            pencils = bpy.data.grease_pencils
        data = pencils.new(OBJECT_NAME)
        obj = bpy.data.objects.new(OBJECT_NAME, data)
        scene.ensure_root(context).objects.link(obj)
        obj.location = (0.0, 0.0, plane_height(bounds))
        obj.show_in_front = True
    data = obj.data
    material = _edge_material()
    if material.name not in data.materials:
        data.materials.append(material)
    if not len(data.layers):
        data.layers.new(LAYER_NAME)
    layer = data.layers.active or data.layers[0]
    frame = layer.current_frame()
    if frame is None:
        frame = layer.frames.new(context.scene.frame_current)
    return obj, layer, frame.drawing


def write_edges(context, polylines, bounds, replace=True) -> int:
    """Lay ``polylines`` (map space) down as closed strokes on the plane."""
    obj, layer, drawing = ensure_edge_object(context, bounds)
    if replace and len(drawing.strokes):
        drawing.remove_strokes(indices=list(range(len(drawing.strokes))))
    polylines = [line for line in polylines if len(line) >= 3]
    if not polylines:
        return 0
    start = len(drawing.strokes)
    drawing.add_strokes([len(line) for line in polylines])
    inverse = (obj.matrix_world @ layer.matrix_local).inverted()
    height = obj.matrix_world.translation.z
    span = max(bounds[1] - bounds[0], bounds[5] - bounds[4]) if bounds else 10000
    radius = max(2.0, span * RADIUS_FRACTION)
    strokes = list(drawing.strokes)[start:]
    for stroke, line in zip(strokes, polylines):
        for point, position in zip(stroke.points, line):
            where = scene.to_blender(position)
            point.position = inverse @ Vector((where.x, where.y, height))
            point.radius = radius
        stroke.cyclic = True
        stroke.material_index = 0
    return len(strokes)


# ---------------------------------------------------------------------------
# The track: what it ships
# ---------------------------------------------------------------------------

_BASE_MODELS = {}


def base_model(path):
    """A level model, loaded once per file and modification time."""
    try:
        stamp = os.path.getmtime(path)
    except OSError:
        return None
    cached = _BASE_MODELS.get(path)
    if cached is not None and cached[0] == stamp:
        return cached[1]
    try:
        model = level_model.load(path)
    except (level_model.LevelModelError, OSError):
        return None
    _BASE_MODELS[path] = (stamp, model)
    return model


class Shape:
    """Whether the track was reshaped, from the model the export would ship."""

    __slots__ = ("changed", "bounds", "base", "authored", "error", "path")

    def __init__(self, changed=False, bounds=None, base=None, authored=False,
                 error="", path=""):
        self.changed = changed
        self.bounds = tuple(bounds) if bounds else None
        self.base = base
        self.authored = authored
        self.error = error
        self.path = path


def shape_of_edit(edit) -> Shape:
    """:class:`Shape` for a :class:`.geometry_export.GeometryEdit`.

    Reshaped means the bounds moved, or any vertex moved, appeared or went. A
    track whose textures, colours, flags or surfaces changed keeps its shape -
    and with it the retail minimap, which shows the same road.
    """
    base = base_model(edit.path)
    summary = edit.summary
    moved = sum(int(getattr(summary, name, 0) or 0)
                for name in ("moved", "vertices_added", "vertices_removed",
                             "faces_added", "faces_removed"))
    bounds = tuple(edit.model.bounds)
    changed = bool(moved) or base is None or bounds != tuple(base.bounds)
    return Shape(changed, bounds, base, edit.authored_base, path=edit.path)


_SHAPE = {"key": None, "shape": None, "context": None}


def _mesh_key(context):
    objects = geometry_ops.geometry_objects(context)
    if len(objects) != 1:
        return ("meshes", len(objects))
    obj = objects[0]
    mesh = obj.data
    positions = array("f", [0.0]) * (len(mesh.vertices) * 3)
    mesh.vertices.foreach_get("co", positions)
    return (obj.name, mesh.name, len(mesh.vertices), len(mesh.polygons),
            zlib.crc32(positions.tobytes()),
            tuple(round(v, 4) for row in obj.matrix_world for v in row),
            str(obj.get(geometry_ops.PROP_MODEL_PATH, "")),
            bool(obj.get(geometry_ops.PROP_AUTHORED_BASE, False)))


def _compute_shape(context) -> Optional[Shape]:
    from . import geometry_export  # noqa: PLC0415 - it imports this package

    try:
        edit = geometry_export.build_edited_model(context)
    except geometry_export.GeometryExportError as error:
        return Shape(changed=True, error=str(error))
    except Exception as error:  # noqa: BLE001 - the panel must still draw
        traceback.print_exc()
        return Shape(changed=True, error=str(error))
    if edit is None:
        return None
    return shape_of_edit(edit)


def _shape_timer():
    try:
        key = _mesh_key(bpy.context)
        _SHAPE.update(key=key, shape=_compute_shape(bpy.context))
    except Exception:  # noqa: BLE001 - a timer must never raise
        traceback.print_exc()
        _SHAPE.update(key=None, shape=Shape(changed=True, error="the check failed"))
    _redraw()
    return None


def shape(context, wait=False):
    """The track's :class:`Shape`, ``None`` with no geometry, or
    :data:`PENDING` while the check runs. ``wait`` computes it now."""
    if not geometry_ops.geometry_objects(context):
        return None
    key = _mesh_key(context)
    if _SHAPE["key"] == key:
        return _SHAPE["shape"]
    if wait:
        found = _compute_shape(context)
        _SHAPE.update(key=key, shape=found)
        return found
    if not bpy.app.timers.is_registered(_shape_timer):
        bpy.app.timers.register(_shape_timer, first_interval=0.05)
    return PENDING


def remember_shape(context, found) -> None:
    """Keep the export's own answer, so the panel does not ask again."""
    try:
        _SHAPE.update(key=_mesh_key(context), shape=found)
    except Exception:  # noqa: BLE001 - only a cache
        pass


def _redraw():
    try:
        for window in bpy.context.window_manager.windows:
            for area in window.screen.areas:
                if area.type == "VIEW_3D":
                    area.tag_redraw()
    except Exception:  # noqa: BLE001 - redrawing is a nicety
        pass


class State:
    """What the Minimap panel shows and the export does."""

    __slots__ = ("kind", "edges", "open", "bounds", "base", "retail", "label",
                 "error")

    def __init__(self, kind, edges=(), bounds=None, base=None, retail=None,
                 label="", error=""):
        self.kind = kind
        self.edges = list(edges)
        self.open = []
        self.bounds = tuple(bounds) if bounds else None
        self.base = base
        self.retail = retail
        self.label = label
        self.error = error

    @property
    def ships_own(self) -> bool:
        return self.kind == OWN

    @property
    def keeps_retail(self) -> bool:
        # Not BASE_NONE: a base with no minimap numbers still names sprite 0,
        # and without the header bit the game draws Fossil Canyon's map over
        # it. The hubs that have none set the bit already; setting it again
        # changes nothing for them.
        return self.kind == RETAIL


def _imported_level(context):
    from . import level_type as level_type_ops  # noqa: PLC0415

    level, _header = level_type_ops.imported_level(context)
    return level


def track_state(context, found=Ellipsis) -> State:
    """What the track will ship. ``found`` is a :class:`Shape` the caller
    already has - the export's own - and is looked up otherwise."""
    edges = minimap.usable_edges(read_edges(context))
    if found is Ellipsis:
        found = shape(context)

    level = _imported_level(context)
    if found is None:
        # No geometry in the scene: the track is its base level's model.
        path = level.model_path if level is not None else ""
        base = base_model(path) if path else None
        bounds = base.bounds if base is not None else None
        changed = False
        authored = False
    elif found is PENDING:
        return State(PENDING, edges)
    else:
        base, bounds = found.base, found.bounds
        changed, authored = found.changed, found.authored
        if found.error and not edges:
            return State(CHANGED, edges, bounds, base, error=found.error)

    label = level.label if level is not None else ""
    if edges:
        state = State(OWN, edges, bounds, base, label=label)
        if bounds is None:
            state.error = ("there is no track to fit the minimap to: import a "
                           "track's geometry first")
        else:
            try:
                state.open = minimap.open_edges(edges, bounds)
            except minimap.MinimapError as error:
                state.error = str(error)
        return state
    if base is None:
        return State(NO_TRACK, edges, bounds, label=label)
    if authored:
        return State(NEW, edges, bounds, base, label=label)
    if not minimap.shows_minimap(base):
        return State(BASE_NONE, edges, bounds, base, label=label)
    if changed:
        return State(CHANGED, edges, bounds, base, label=label)
    tree = prefs.resolve(context)
    retail = minimap.retail_minimap(tree.root, base.minimap_sprite_index) if tree else None
    return State(RETAIL, edges, bounds, base, retail,
                 label or (retail.label if retail else "the base track"))


# ---------------------------------------------------------------------------
# The picture
# ---------------------------------------------------------------------------

def colour_value(settings) -> int:
    red, green, blue = (max(0, min(255, int(round(c * 255))))
                        for c in settings.minimap_colour)
    return (red << 16) | (green << 8) | blue


def rotation_value(settings):
    return None if settings.minimap_rotation == "AUTO" else int(settings.minimap_rotation)


def start_grid(context):
    """``(centre, ahead, positions)`` of entrance 0's grid, in map space, or
    ``None``. ``ahead`` is the first gate the racers aim for, or a point the
    way the grid faces; ``positions`` are ``(racerIndex, point)``."""
    found = [obj for obj in scene.iter_dkr_objects(context)
             if str(obj.get(scene.PROP_ID)) == SETUPPOINT
             and int(obj.get("entranceID", 0)) == 0]
    if not found:
        return None
    found.sort(key=lambda obj: (int(obj.get("racerIndex", 0)), obj.name))
    positions = [(int(obj.get("racerIndex", 0)),
                  tuple(scene.to_map(obj.matrix_world.translation)))
                 for obj in found]
    centre = tuple(sum(p[axis] for _i, p in positions) / len(positions)
                   for axis in range(3))
    ahead = None
    try:
        from . import race_ai as race_ai_ops  # noqa: PLC0415

        route, _objects = race_ai_ops.read_route(context)
        if route.main:
            gate = route.main[0]
            if math.hypot(gate.x - centre[0], gate.z - centre[2]) > 1.0:
                ahead = (gate.x, gate.y, gate.z)
    except Exception:  # noqa: BLE001 - the grid's facing will do
        ahead = None
    if ahead is None:
        facing = found[0].matrix_world.to_3x3() @ Vector((0.0, 1.0, 0.0))
        step = scene.to_map(facing)
        ahead = (centre[0] + step[0] * 100.0, centre[1], centre[2] + step[2] * 100.0)
    return centre, ahead, positions


class Built:
    """A picture made for the track, or why it could not be.

    ``edges``, ``soft`` and ``flag`` are what the picture was drawn from, kept
    so the export can draw it again at high resolution; ``from_png`` says the
    texture is the author's own PNG instead, which has no larger version.
    """

    __slots__ = ("placement", "rgba", "error", "png_error", "markers", "key",
                 "edges", "soft", "flag", "from_png")

    def __init__(self, placement=None, rgba=None, error="", png_error="",
                 markers=(), key=None, edges=(), soft=1, flag=None,
                 from_png=False):
        self.placement = placement
        self.rgba = rgba
        self.error = error
        self.png_error = png_error
        self.markers = list(markers)
        self.key = key
        self.edges = list(edges)
        self.soft = soft
        self.flag = flag
        self.from_png = from_png

    def hd_picture(self):
        """``(width, height, rgba)`` at :data:`minimap.HD_SCALE`, or ``None``
        when the texture is the author's PNG."""
        if self.from_png or self.placement is None:
            return None
        return minimap.hd_picture(self.edges, self.placement, self.soft,
                                  self.flag)


_BUILT = {"key": None, "built": None}


def _png_stamp(path):
    if not path:
        return None
    try:
        return path, os.path.getmtime(bpy.path.abspath(path))
    except OSError:
        return path, None


def build(context, state=None) -> Optional[Built]:
    """The track's own picture and numbers, cached until something changes."""
    state = state or track_state(context)
    if state.kind != OWN:
        return None
    if state.error:
        return Built(error=state.error)
    settings = context.scene.dkr
    grid = start_grid(context)
    key = (_signature(state.edges), state.bounds, settings.minimap_size,
           settings.minimap_rotation, colour_value(settings), settings.minimap_soft,
           settings.minimap_flag, repr(grid), _png_stamp(settings.minimap_png))
    if _BUILT["key"] == key:
        return _BUILT["built"]
    try:
        placement = minimap.fit(state.edges, state.bounds, settings.minimap_size,
                                rotation_value(settings), colour_value(settings))
        flag = None
        if settings.minimap_flag and grid is not None:
            flag = minimap.flag_texels(placement, grid[0], grid[1])
        rgba = minimap.picture(state.edges, placement, settings.minimap_soft, flag)
    except minimap.MinimapError as error:
        built = Built(error=str(error), key=key)
        _BUILT.update(key=key, built=built)
        return built

    png_error = ""
    from_png = False
    if settings.minimap_png:
        try:
            rgba = minimap.read_own_png(bpy.path.abspath(settings.minimap_png),
                                        placement.width, placement.height)
            from_png = True
        except minimap.MinimapError as error:
            png_error = str(error)

    markers = []
    if grid is not None:
        centre, ahead, positions = grid
        u0, v0 = minimap.texel(placement, centre[0], centre[2])
        u1, v1 = minimap.texel(placement, ahead[0], ahead[2])
        heading = (u1 - u0, v1 - v0)
        for number, (_index, point) in enumerate(positions[:8]):
            u, v = minimap.texel(placement, point[0], point[2])
            if number == 0:
                markers.append((u, v, minimap.PREVIEW_PLAYER, heading))
            else:
                markers.append((u, v, minimap.PREVIEW_CPUS[(number - 1) % 7], None))
        # The player's arrow is drawn over the dots, as the game draws it.
        markers = markers[1:] + markers[:1]
    built = Built(placement, rgba, png_error=png_error, markers=markers, key=key,
                  edges=state.edges, soft=settings.minimap_soft, flag=flag,
                  from_png=from_png)
    _BUILT.update(key=key, built=built)
    return built


# ---------------------------------------------------------------------------
# The panel's preview
# ---------------------------------------------------------------------------

_PREVIEWS = {"collection": None, "keys": []}


def _collection():
    if _PREVIEWS["collection"] is None:
        import bpy.utils.previews  # noqa: PLC0415 - optional, and only on demand

        _PREVIEWS["collection"] = bpy.utils.previews.new()
    return _PREVIEWS["collection"]


def _icon_name(key) -> str:
    return "dkr_minimap_%08x" % (zlib.crc32(repr(key).encode("utf-8")) & 0xFFFFFFFF)


def _cached_icon(key) -> int:
    """The icon already made for ``key``, or 0 - checked before any drawing,
    because the panel asks on every redraw."""
    collection = _collection()
    name = _icon_name(key)
    return collection[name].icon_id if name in collection else 0


def _icon(key, width, height, rgba) -> int:
    collection = _collection()
    name = _icon_name(key)
    if name in collection:
        return collection[name].icon_id
    # Old pictures are dropped as new ones arrive; a handful is plenty.
    while len(_PREVIEWS["keys"]) > 6:
        stale = _PREVIEWS["keys"].pop(0)
        if stale in collection:
            collection.pop(stale)
    pixels = [0.0] * (width * height * 4)
    for row in range(height):
        source = (height - 1 - row) * width * 4
        target = row * width * 4
        for at in range(width * 4):
            pixels[target + at] = rgba[source + at] / 255.0
    item = collection.new(name)
    item.image_size = (width, height)
    item.image_pixels_float = pixels
    _PREVIEWS["keys"].append(name)
    return item.icon_id


def preview_icon(built: Built, colour: int) -> int:
    """The picture as the game draws it, enlarged, with the racers on the grid."""
    try:
        key = ("own", built.key, colour)
        found = _cached_icon(key)
        if found:
            return found
        placement = built.placement
        big = minimap.preview(built.rgba, placement.width, placement.height,
                              colour, built.markers, PREVIEW_SCALE)
        return _icon(key, placement.width * PREVIEW_SCALE,
                     placement.height * PREVIEW_SCALE, big)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()
        return 0


def retail_icon(retail, colour: int) -> int:
    """A retail minimap as the game draws it, enlarged."""
    from .. import textures  # noqa: PLC0415

    try:
        found = _cached_icon(("retail", retail.png, colour))
        if found:
            return found
        width, height, rgba = textures.read_png(retail.png)
        big = minimap.preview(rgba, width, height, colour, (), PREVIEW_SCALE)
        return _icon(("retail", retail.png, colour), width * PREVIEW_SCALE,
                     height * PREVIEW_SCALE, big)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()
        return 0


def teardown() -> None:
    """Release the previews and the overlay. Called from ``unregister``."""
    unregister_overlay()
    if bpy.app.timers.is_registered(_shape_timer):
        bpy.app.timers.unregister(_shape_timer)
    collection = _PREVIEWS["collection"]
    _PREVIEWS.update(collection=None, keys=[])
    _SHAPE.update(key=None, shape=None)
    _BUILT.update(key=None, built=None)
    if collection is None:
        return
    try:
        import bpy.utils.previews  # noqa: PLC0415

        bpy.utils.previews.remove(collection)
    except Exception:  # noqa: BLE001 - shutting down anyway
        pass


# ---------------------------------------------------------------------------
# Show on Track: the picture laid over the track
# ---------------------------------------------------------------------------

def overlay_corners(placement: minimap.Placement, height: float):
    """The picture's four corners in Blender space, top-left first.

    The mapping from ground to picture is a turn, a scale and a shift, so the
    corners are all it takes to lay the whole picture over the track.
    """
    corners = []
    for u, v in ((0.0, 0.0), (placement.width, 0.0),
                 (placement.width, placement.height), (0.0, placement.height)):
        x, z = minimap.world_of(placement, u, v)
        where = scene.to_blender((x, 0.0, z))
        corners.append((where.x, where.y, height))
    return corners


_OVERLAY = {"handle": None, "key": None, "batch": None, "texture": None}
_OVERLAY_ERROR = [None]


def _overlay_draw():
    context = bpy.context
    try:
        settings = getattr(context.scene, "dkr", None)
        if settings is None or not settings.show_minimap_overlay:
            return
        state = track_state(context)
        built = build(context, state)
        if built is None or built.placement is None:
            return
        import gpu  # noqa: PLC0415
        from gpu_extras.batch import batch_for_shader  # noqa: PLC0415

        key = built.key
        if _OVERLAY["key"] != key:
            placement = built.placement
            width, height = placement.width, placement.height
            pixels = []
            for row in range(height - 1, -1, -1):
                for column in range(width):
                    at = (row * width + column) * 4
                    alpha = built.rgba[at + 3] / 255.0 * 0.55
                    grey = built.rgba[at] / 255.0
                    pixels += [grey, grey, grey, alpha]
            buffer = gpu.types.Buffer("FLOAT", width * height * 4, pixels)
            texture = gpu.types.GPUTexture((width, height), format="RGBA8",
                                           data=buffer)
            shader = gpu.shader.from_builtin("IMAGE")
            corners = overlay_corners(placement, plane_height(state.bounds))
            batch = batch_for_shader(
                shader, "TRI_FAN",
                {"pos": corners,
                 "texCoord": ((0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0))})
            _OVERLAY.update(key=key, batch=(shader, batch), texture=texture)
        shader, batch = _OVERLAY["batch"]
        gpu.state.blend_set("ALPHA")
        gpu.state.depth_test_set("NONE")
        try:
            shader.bind()
            shader.uniform_sampler("image", _OVERLAY["texture"])
            batch.draw(shader)
        finally:
            gpu.state.blend_set("NONE")
        _OVERLAY_ERROR[0] = None
    except Exception:  # noqa: BLE001 - a draw handler must never raise
        message = traceback.format_exc()
        if message != _OVERLAY_ERROR[0]:
            _OVERLAY_ERROR[0] = message
            print("DKR track editor: could not draw the minimap\n" + message)


def register_overlay():
    if _OVERLAY["handle"] is None:
        _OVERLAY["handle"] = bpy.types.SpaceView3D.draw_handler_add(
            _overlay_draw, (), "WINDOW", "POST_VIEW")


def unregister_overlay():
    if _OVERLAY["handle"] is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_OVERLAY["handle"], "WINDOW")
    _OVERLAY.update(handle=None, key=None, batch=None, texture=None)


# ---------------------------------------------------------------------------
# What the export and Validate need
# ---------------------------------------------------------------------------

def header_value(current, state: State) -> int:
    """Level header byte 0xBC for the track: bit 0 is "no minimap"."""
    current = int(current or 0) & 0xFF
    if state.ships_own:
        return current & ~minimap.NO_MINIMAP_BIT
    if state.keeps_retail:
        return current
    return current | minimap.NO_MINIMAP_BIT


def issue_text(state: State, built: Optional[Built] = None) -> tuple:
    """``(severity, message)`` for the Validate panel."""
    if state.kind == OWN:
        if state.error or (built is not None and built.error):
            return "error", "Minimap: %s" % (state.error or built.error)
        if state.open:
            return "warning", ("Minimap: %d open edge%s. The picture closes %s with "
                               "a straight line - Close Edges to do it yourself"
                               % (len(state.open), "" if len(state.open) == 1 else "s",
                                  "it" if len(state.open) == 1 else "them"))
        if built is not None and built.png_error:
            return "warning", "Minimap: your PNG is not used - %s" % built.png_error
        return "info", "Minimap: ok"
    if state.kind == RETAIL:
        return "info", "Minimap: %s's own, unchanged" % state.label
    if state.kind == PENDING:
        return "info", "Minimap: still checking whether the track was reshaped"
    if state.kind == CHANGED:
        return "warning", ("Minimap: none - the track was reshaped, so %s's map "
                           "would show the wrong road. Draw the road's edges to "
                           "ship one" % (state.label or "the base track"))
    return "info", "Minimap: none"


def issues(context) -> list:
    """The Validate panel's minimap line, as the export would decide it."""
    from .. import validate  # noqa: PLC0415

    state = track_state(context, found=shape(context, wait=True))
    built = build(context, state) if state.kind == OWN else None
    severity, message = issue_text(state, built)
    level = {"error": validate.ERROR, "warning": validate.WARNING}.get(
        severity, validate.INFO)
    return [validate.Issue(level, message)]


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

def _view3d(context):
    """``(area, region)`` of a 3D view to draw in, or ``(None, None)``."""
    area = context.area if context.area and context.area.type == "VIEW_3D" else None
    if area is None and context.screen is not None:
        area = next((a for a in context.screen.areas if a.type == "VIEW_3D"), None)
    if area is None:
        return None, None
    region = next((r for r in area.regions if r.type == "WINDOW"), None)
    return area, region


class DKR_OT_minimap_draw_edge(bpy.types.Operator):
    """Trace an edge of the road from above. Each closed line is an edge, and
    the road is what lies between edges"""

    bl_idname = "dkr.minimap_draw_edge"
    bl_label = "Draw Edge"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        state = track_state(context, found=shape(context, wait=True))
        obj, _layer, _drawing = ensure_edge_object(context, state.bounds)
        if context.object is not None and context.object.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        for other in context.selected_objects:
            other.select_set(False)
        obj.hide_set(False)
        obj.select_set(True)
        context.view_layer.objects.active = obj

        tools = context.scene.tool_settings
        tools.gpencil_stroke_placement_view3d = "ORIGIN"
        tools.gpencil_sculpt.lock_axis = "AXIS_Z"

        area, region = _view3d(context)
        if area is not None:
            with context.temp_override(area=area, region=region):
                bpy.ops.view3d.view_axis(type="TOP")
                targets = geometry_ops.geometry_objects(context)
                if targets:
                    for other in context.selected_objects:
                        other.select_set(False)
                    for target in targets:
                        target.select_set(True)
                    bpy.ops.view3d.view_selected()
                    for target in targets:
                        target.select_set(False)
                    obj.select_set(True)
                    context.view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="PAINT_GREASE_PENCIL")
        self.report({"INFO"}, "Trace the outside border of the road, then the "
                              "inside one. Tab back to Object Mode when done")
        return {"FINISHED"}


class DKR_OT_minimap_from_ai(bpy.types.Operator):
    """Start the minimap from the computer racers' line: two closed edges, one
    either side of it, at about the road's width, to adjust rather than draw"""

    bl_idname = "dkr.minimap_from_ai"
    bl_label = "Start From AI Path"
    bl_options = {"REGISTER", "UNDO"}

    def invoke(self, context, event):
        if read_edges(context):
            return context.window_manager.invoke_confirm(
                self, event, title="Replace the edges already drawn?",
                message="Start From AI Path replaces every edge on %s"
                        % OBJECT_NAME)
        return self.execute(context)

    def execute(self, context):
        from . import race_ai as race_ai_ops  # noqa: PLC0415

        state = track_state(context, found=shape(context, wait=True))
        if state.bounds is None:
            self.report({"ERROR"}, "there is no track to fit the minimap to: "
                                   "import a track's geometry first")
            return {"CANCELLED"}
        try:
            route, _objects = race_ai_ops.read_route(context)
            polylines = minimap.edges_from_route(route)
        except minimap.MinimapError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "the checkpoints could not be read: %s" % error)
            return {"CANCELLED"}
        count = write_edges(context, polylines, state.bounds)
        self.report({"INFO"}, "%d edges either side of the AI path; widen them "
                              "where the road is wide and add the shortcuts" % count)
        _redraw()
        return {"FINISHED"}


class DKR_OT_minimap_close_edges(bpy.types.Operator):
    """Close every open edge with a straight line between its ends"""

    bl_idname = "dkr.minimap_close_edges"
    bl_label = "Close Edges"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return edge_object(context) is not None

    def execute(self, context):
        closed = 0
        for _layer, drawing in _drawings(edge_object(context)):
            for stroke in drawing.strokes:
                if not stroke.cyclic:
                    stroke.cyclic = True
                    closed += 1
        self.report({"INFO"}, "closed %d edge%s" % (closed, "" if closed == 1 else "s"))
        _redraw()
        return {"FINISHED"}


class DKR_OT_minimap_clear(bpy.types.Operator):
    """Delete every edge. With none, the track ships without a minimap"""

    bl_idname = "dkr.minimap_clear"
    bl_label = "Clear"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return edge_object(context) is not None

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(
            self, event, title="Delete every edge?",
            message="With no edges the track ships without a minimap")

    def execute(self, context):
        removed = 0
        for _layer, drawing in _drawings(edge_object(context)):
            removed += len(drawing.strokes)
            if len(drawing.strokes):
                drawing.remove_strokes(indices=list(range(len(drawing.strokes))))
        self.report({"INFO"}, "deleted %d edge%s" % (removed, "" if removed == 1 else "s"))
        _redraw()
        return {"FINISHED"}


class DKR_OT_minimap_save_png(bpy.types.Operator, ExportHelper):
    """Save the minimap picture as a PNG, to retouch by hand and bring back
    with Use My PNG. Keep its size"""

    bl_idname = "dkr.minimap_save_png"
    bl_label = "Save PNG"
    bl_options = {"REGISTER"}

    filename_ext = ".png"
    filter_glob: StringProperty(default="*.png", options={"HIDDEN"})

    @classmethod
    def poll(cls, context):
        return edge_object(context) is not None

    def execute(self, context):
        built = build(context)
        if built is None or built.placement is None:
            self.report({"ERROR"}, built.error if built else "draw the road's edges first")
            return {"CANCELLED"}
        placement = built.placement
        try:
            minimap.write_png(self.filepath, placement.width, placement.height, built.rgba)
        except OSError as error:
            self.report({"ERROR"}, "could not write %s: %s" % (self.filepath, error))
            return {"CANCELLED"}
        self.report({"INFO"}, "saved the %dx%d picture" % (placement.width, placement.height))
        return {"FINISHED"}


class DKR_OT_minimap_use_png(bpy.types.Operator, ImportHelper):
    """Use a retouched picture instead of the one the edges make. It has to be
    the same size: the dots are placed for that size exactly"""

    bl_idname = "dkr.minimap_use_png"
    bl_label = "Use My PNG"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".png"
    filter_glob: StringProperty(default="*.png", options={"HIDDEN"})

    @classmethod
    def poll(cls, context):
        return edge_object(context) is not None

    def execute(self, context):
        settings = context.scene.dkr
        previous = settings.minimap_png
        settings.minimap_png = ""
        built = build(context)
        if built is None or built.placement is None:
            settings.minimap_png = previous
            self.report({"ERROR"}, built.error if built else "draw the road's edges first")
            return {"CANCELLED"}
        placement = built.placement
        try:
            minimap.read_own_png(self.filepath, placement.width, placement.height)
        except minimap.MinimapError as error:
            settings.minimap_png = previous
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        settings.minimap_png = self.filepath
        self.report({"INFO"}, "the minimap now uses %s" % os.path.basename(self.filepath))
        return {"FINISHED"}


class DKR_OT_minimap_forget_png(bpy.types.Operator):
    """Go back to the picture the edges make"""

    bl_idname = "dkr.minimap_forget_png"
    bl_label = "Use The Drawn Picture"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return bool(context.scene.dkr.minimap_png)

    def execute(self, context):
        context.scene.dkr.minimap_png = ""
        return {"FINISHED"}


class DKR_OT_minimap_colour(bpy.types.Operator):
    """Tint the minimap the way a retail track does"""

    bl_idname = "dkr.minimap_colour"
    bl_label = "Minimap Colour"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    preset: StringProperty(default="WHITE")

    def execute(self, context):
        for key, _label, value in minimap.COLOUR_PRESETS:
            if key == self.preset:
                context.scene.dkr.minimap_colour = (
                    ((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0,
                    (value & 0xFF) / 255.0)
                return {"FINISHED"}
        return {"CANCELLED"}


class DKR_MT_minimap_colours(bpy.types.Menu):
    bl_idname = "DKR_MT_minimap_colours"
    bl_label = "Retail Colours"

    def draw(self, context):
        for key, label, _value in minimap.COLOUR_PRESETS:
            self.layout.operator("dkr.minimap_colour", text=label).preset = key


CLASSES = (
    DKR_OT_minimap_draw_edge,
    DKR_OT_minimap_from_ai,
    DKR_OT_minimap_close_edges,
    DKR_OT_minimap_clear,
    DKR_OT_minimap_save_png,
    DKR_OT_minimap_use_png,
    DKR_OT_minimap_forget_png,
    DKR_OT_minimap_colour,
    DKR_MT_minimap_colours,
)
