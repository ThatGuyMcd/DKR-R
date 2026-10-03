"""Where a click in the viewport puts a new object, and the marker showing it.

A click lands on the first piece of track under the mouse. With the viewport's
snapping on - the magnet in the header, or Ctrl held to flip it, as in any
Blender transform - it follows the snap elements chosen there instead: a
vertex, an edge, an edge's or a face's midpoint, or the grid. Face snapping is
the plain landing, so it changes nothing.

The marker is drawn while the mouse moves, so where the next click goes is on
screen before it happens: a click that would land somewhere unexpected is seen
and not made.
"""

from __future__ import annotations

import traceback

import bpy
import gpu
import numpy as np
from bpy_extras import view3d_utils
from gpu_extras.batch import batch_for_shader
from mathutils import Vector

from . import geometry

#: How far a click looks into the scene for the track: past the far clip
#: distance of any view a track is drawn in.
CLICK_REACH = 1.0e7

#: How near the mouse, in pixels at UI scale 1, an element has to be to catch.
SNAP_PIXELS = 20.0

#: The finest grid snapped to, in pixels between lines, as the viewport's own
#: grid thins out lines closer than this.
GRID_PIXELS = 16.0

#: Nearest-on-screen candidates checked for being hidden behind the track.
CANDIDATES = 8

# What a click landed on.
SURFACE = "Surface"
PLANE = "Cursor Plane"
VERTEX = "Vertex"
EDGE = "Edge"
EDGE_MIDPOINT = "Edge Midpoint"
FACE_MIDPOINT = "Face Midpoint"
GRID = "Grid"

#: Snap elements, as Blender names them, that land somewhere other than the
#: surface. Face, Volume and the face projections are the surface already;
#: Perpendicular needs a point being moved from, which placing has not got.
POINT_ELEMENTS = (("VERTEX", VERTEX), ("EDGE_MIDPOINT", EDGE_MIDPOINT),
                  ("FACE_MIDPOINT", FACE_MIDPOINT))
GRID_ELEMENTS = {"GRID", "INCREMENT"}


def track_targets(context, viewport=None):
    """The track meshes a click can land on: visible ones, in this view."""
    return [o for o in geometry.geometry_objects(context)
            if o.visible_get(viewport=viewport)]


def click_location(context, origin, direction, targets=None, depsgraph=None,
                   matrix=None):
    """Where a click along this ray lands, in world space, or None.

    The first visible piece of track the ray meets, whatever kind of face it
    is: the object lands where the author pointed. Skipping ahead to the
    drivable surface, as *Drop To Surface* does, would send a click on a wall
    to whatever road lies behind it.

    Visible means drawn, too: given the view's ``matrix``, track past its far
    clip or before its near one is passed over. A retail track is thousands of
    units across, well past a default viewport's Clip End, and a click on the
    empty space where the far side was not drawn used to land there.

    A click that misses the track places nothing. Only a scene with no track
    at all falls back to the horizontal plane through the 3D cursor: a miss
    landing on that plane, near the horizon, puts the object thousands of
    units away.
    """
    if targets is None:
        targets = track_targets(context)
    if targets:
        return _first_drawn_hit(origin, direction, targets,
                                depsgraph or context.evaluated_depsgraph_get(),
                                matrix)

    height = context.scene.cursor.location.z
    if abs(direction.z) < 1e-6:
        return None
    distance = (height - origin.z) / direction.z
    if distance <= 0.0:
        return None  # looking away from the plane: a click at the sky
    return origin + direction * distance


def clip_depth(point, matrix):
    """Where a point sits between the near clip (-1) and the far clip (1).

    Outside that range the viewport does not draw it. A point behind the eye
    has no depth and reads as past the far clip.
    """
    clip = np.asarray(matrix, dtype=np.float64) @ np.array(
        (point[0], point[1], point[2], 1.0))
    return clip[2] / clip[3] if clip[3] > 1e-9 else float("inf")


#: Slack on the clip range: the view matrix is single precision.
CLIP_SLACK = 1e-6


def _first_drawn_hit(origin, direction, targets, depsgraph, matrix):
    start, reach = origin, CLICK_REACH
    for _ in range(16):
        hit = geometry.nearest_hit(start, direction, targets, depsgraph, reach,
                                   surface_only=False)
        if hit is None or matrix is None:
            return hit
        depth = clip_depth(hit, matrix)
        if depth > 1.0 + CLIP_SLACK:
            return None  # past the far clip: nothing drawn under the mouse
        if depth >= -1.0 - CLIP_SLACK:
            return hit
        # Before the near clip, so not drawn either: look on past it.
        step = 1e-4 * max(1.0, (hit - origin).length)
        reach -= (hit - start).length + step
        start = hit + direction * step
        if reach <= 0.0:
            return None
    return None


# ---------------------------------------------------------------------------
# Screen-space nearest elements. Plain arrays and a matrix, not a region, so
# they can be tested without a window.
# ---------------------------------------------------------------------------

def project(points, matrix, size):
    """``(pixels, w, drawn)`` for world points under a perspective matrix.

    ``w`` is the clip-space depth: a point with ``w <= 0`` is behind the eye
    and its pixel means nothing. ``drawn`` is whether it lies in the view's
    clip range at all.
    """
    points = np.asarray(points, dtype=np.float64).reshape(-1, 3)
    clip = np.c_[points, np.ones(len(points))] @ np.asarray(matrix, dtype=np.float64).T
    w = clip[:, 3]
    safe = np.where(w > 1e-9, w, 1.0)
    pixels = np.empty((len(points), 2))
    pixels[:, 0] = (clip[:, 0] / safe * 0.5 + 0.5) * size[0]
    pixels[:, 1] = (clip[:, 1] / safe * 0.5 + 0.5) * size[1]
    depth = clip[:, 2] / safe
    drawn = (w > 1e-9) & (np.abs(depth) <= 1.0 + CLIP_SLACK)
    return pixels, w, drawn


def nearest_points(points, matrix, size, mouse, threshold, limit=CANDIDATES):
    """``[(pixels away, point), ...]`` within ``threshold``, nearest first."""
    if len(points) == 0:
        return []
    pixels, _w, drawn = project(points, matrix, size)
    away = np.hypot(pixels[:, 0] - mouse[0], pixels[:, 1] - mouse[1])
    away[~drawn] = np.inf
    near = np.flatnonzero(away <= threshold)
    order = near[np.argsort(away[near], kind="stable")][:limit]
    return [(float(away[i]), Vector(points[i])) for i in order]


def nearest_on_edges(vertices, edges, matrix, size, mouse, threshold,
                     limit=CANDIDATES):
    """``[(pixels away, point), ...]``: the point of each edge under the mouse.

    Found on screen and carried back onto the edge in 3D. The carrying back
    has to undo the perspective divide - halfway along an edge on screen is
    not halfway along it in the world when one end is nearer the eye.
    """
    if len(edges) == 0:
        return []
    pixels, w, _drawn = project(vertices, matrix, size)
    a, b = edges[:, 0], edges[:, 1]
    p0, p1 = pixels[a], pixels[b]
    span = p1 - p0
    length2 = (span * span).sum(axis=1)
    mouse = np.asarray(mouse, dtype=np.float64)
    along = ((mouse - p0) * span).sum(axis=1) / np.maximum(length2, 1e-12)
    along = np.clip(along, 0.0, 1.0)
    closest = p0 + span * along[:, None]
    away = np.hypot(closest[:, 0] - mouse[0], closest[:, 1] - mouse[1])
    away[(w[a] <= 1e-9) | (w[b] <= 1e-9)] = np.inf
    near = np.flatnonzero(away <= threshold)
    found = []
    for i in near[np.argsort(away[near], kind="stable")][:limit]:
        s, w0, w1 = along[i], w[a[i]], w[b[i]]
        t = s * w0 / (s * w0 + (1.0 - s) * w1)
        start = vertices[a[i]]
        point = Vector(start + (vertices[b[i]] - start) * t)
        if abs(clip_depth(point, matrix)) <= 1.0 + CLIP_SLACK:
            found.append((float(away[i]), point))
    return found


def grid_step(location, matrix, size, base, subdivisions, least=GRID_PIXELS):
    """The grid spacing that a snap there uses, like the viewport's own grid.

    The viewport thins its grid by ``subdivisions`` as the view pulls out and
    adds finer lines as it closes in, so a fixed step would be too fine to see
    from above the whole track and too coarse up close.
    """
    if base <= 0.0:
        return None
    factor = subdivisions if subdivisions > 1 else 10

    def spacing(step):
        ends = [location, location + Vector((step, 0.0, 0.0)),
                location + Vector((0.0, step, 0.0))]
        pixels, w, _drawn = project(ends, matrix, size)
        if (w <= 1e-9).any():
            return float("inf")
        return float(max(np.hypot(*(pixels[1] - pixels[0])),
                         np.hypot(*(pixels[2] - pixels[0]))))

    step = base
    for _ in range(12):
        if spacing(step) >= least:
            break
        step *= factor
    for _ in range(12):
        if spacing(step / factor) < least:
            break
        step /= factor
    return step


# ---------------------------------------------------------------------------
# One session's snapping
# ---------------------------------------------------------------------------

class Snapper:
    """Snaps for one placing session, with the track's arrays read once.

    Reading every vertex of the track through Python on each mouse move would
    stall the viewport, so they are read the first time and kept until the
    mesh or its transform changes.
    """

    def __init__(self):
        self._arrays = {}

    def pick(self, context, region, view, coordinate, snap):
        """``(location, kind)`` for a click at this pixel, or ``None``."""
        space = _space_of(context, region)
        targets = track_targets(context, space)
        depsgraph = context.evaluated_depsgraph_get()
        origin = view3d_utils.region_2d_to_origin_3d(region, view, coordinate)
        direction = view3d_utils.region_2d_to_vector_3d(region, view, coordinate)
        landing = click_location(context, origin, direction, targets, depsgraph,
                                 view.perspective_matrix)
        plain = None if landing is None else (landing, SURFACE if targets else PLANE)
        if not snap:
            return plain

        elements = context.scene.tool_settings.snap_elements
        matrix = view.perspective_matrix
        size = (region.width, region.height)
        threshold = SNAP_PIXELS * context.preferences.system.ui_scale
        arrays = [self._read(obj, depsgraph) for obj in targets]
        xray = _xray(space)

        def visible(point):
            return xray or not _hidden(point, view, targets, depsgraph, matrix)

        wanted = [kind for element, kind in POINT_ELEMENTS if element in elements]
        points = []
        for vertices, edges, faces in arrays:
            for kind in wanted:
                if kind == VERTEX:
                    source = vertices
                elif kind == FACE_MIDPOINT:
                    source = faces
                else:
                    source = (vertices[edges[:, 0]] + vertices[edges[:, 1]]) * 0.5
                points += [(away, point, kind) for away, point in
                           nearest_points(source, matrix, size, coordinate, threshold)]
        for _away, point, kind in sorted(points, key=lambda found: found[0]):
            if visible(point):
                return point, kind

        # An edge is only tried once no point caught: the nearest point of an
        # edge is never further than its ends, so it would win every time.
        if "EDGE" in elements:
            lines = []
            for vertices, edges, _faces in arrays:
                lines += nearest_on_edges(vertices, edges, matrix, size,
                                          coordinate, threshold)
            for _away, point in sorted(lines, key=lambda found: found[0]):
                if visible(point):
                    return point, EDGE

        if elements & GRID_ELEMENTS and landing is not None and space is not None:
            snapped = self._grid(context, space, landing, matrix, size,
                                 targets, depsgraph)
            if snapped is not None:
                return snapped, GRID
        return plain

    def _grid(self, context, space, landing, matrix, size, targets, depsgraph):
        """The grid point nearest the landing, set back down on the track."""
        overlay = space.overlay
        base = overlay.grid_scale
        units = context.scene.unit_settings
        if units.system != "NONE" and units.scale_length > 0.0:
            base /= units.scale_length
        step = grid_step(landing, matrix, size, base, overlay.grid_subdivisions)
        if step is None:
            return None
        snapped = Vector((round(landing.x / step) * step,
                          round(landing.y / step) * step, landing.z))
        if not targets:
            return snapped
        # The grid is flat and the track is not: down onto it from the height
        # the click landed at, looking no further than a couple of cells so
        # a road passing underneath is not taken for this one.
        dropped = geometry.surface_below(snapped, targets, depsgraph,
                                         search_distance=step * 2.0,
                                         surface_only=False)
        return dropped if dropped is not None else snapped

    def _read(self, obj, depsgraph):
        """``(vertices, edges, face centres)`` of an evaluated mesh, in world space."""
        mesh = obj.evaluated_get(depsgraph).data
        matrix = obj.matrix_world
        signature = (len(mesh.vertices), len(mesh.edges), len(mesh.polygons),
                     tuple(value for row in matrix for value in row))
        cached = self._arrays.get(obj.name)
        if cached is not None and cached[0] == signature:
            return cached[1]

        local = np.empty(len(mesh.vertices) * 3)
        mesh.vertices.foreach_get("co", local)
        edges = np.empty(len(mesh.edges) * 2, dtype=np.int64)
        mesh.edges.foreach_get("vertices", edges)
        centres = np.empty(len(mesh.polygons) * 3)
        mesh.polygons.foreach_get("center", centres)

        world = np.asarray(matrix, dtype=np.float64)

        def to_world(flat):
            points = flat.reshape(-1, 3)
            return points @ world[:3, :3].T + world[:3, 3]

        arrays = (to_world(local), edges.reshape(-1, 2), to_world(centres))
        self._arrays[obj.name] = (signature, arrays)
        return arrays


def _space_of(context, region):
    for area in context.window.screen.areas if context.window else ():
        if area.type == "VIEW_3D" and any(r == region for r in area.regions):
            return area.spaces.active
    return None


def _xray(space):
    """Whether the view shows what is behind the track, so snaps reach it too."""
    if space is None:
        return False
    shading = space.shading
    if shading.type == "WIREFRAME":
        return shading.show_xray_wireframe
    return shading.show_xray


def _hidden(point, view, targets, depsgraph, matrix):
    """Whether drawn track stands between the eye and this point."""
    if view.is_perspective:
        eye = view.view_matrix.inverted().translation
        toward = eye - point
        distance = toward.length
    else:
        toward = view.view_rotation @ Vector((0.0, 0.0, 1.0))
        distance = CLICK_REACH
    if distance < 1e-6:
        return False
    toward.normalize()
    # Stepped off the point first: rays from a vertex meet the faces around it.
    slack = 1e-3 + 1e-4 * min(distance, CLICK_REACH / 1000.0)
    blocker = geometry.nearest_hit(point + toward * slack, toward, targets,
                                   depsgraph, distance - 2.0 * slack,
                                   surface_only=False)
    # The nearest blocker is the one furthest from the eye: if even it is
    # before the near clip, everything in the way is undrawn.
    return blocker is not None and \
        clip_depth(blocker, matrix) >= -1.0 - CLIP_SLACK


# ---------------------------------------------------------------------------
# The marker
# ---------------------------------------------------------------------------

_MARKER = {"location": None, "kind": None}
_HANDLE = []


def start_marker():
    if not _HANDLE:
        _HANDLE.append(bpy.types.SpaceView3D.draw_handler_add(
            _draw_marker, (), "WINDOW", "POST_PIXEL"))


def stop_marker():
    while _HANDLE:
        bpy.types.SpaceView3D.draw_handler_remove(_HANDLE.pop(), "WINDOW")
    _MARKER["location"] = _MARKER["kind"] = None


def show_marker(found):
    """Move the marker; whether it changed, so the caller knows to redraw."""
    location, kind = found if found is not None else (None, None)
    if location == _MARKER["location"] and kind == _MARKER["kind"]:
        return False
    _MARKER["location"] = None if location is None else location.copy()
    _MARKER["kind"] = kind
    return True


def _draw_marker():
    """A draw handler that raises prints on every redraw, so this one does not."""
    try:
        _draw_marker_now()
        _LAST_ERROR[0] = None
    except Exception:  # noqa: BLE001 - see the docstring
        message = traceback.format_exc()
        if message != _LAST_ERROR[0]:
            _LAST_ERROR[0] = message
            print("Track Lab: could not draw the placing marker\n" + message)


_LAST_ERROR = [None]


def _draw_marker_now():
    location = _MARKER["location"]
    region, view = bpy.context.region, bpy.context.region_data
    if location is None or region is None or view is None:
        return
    pixel = view3d_utils.location_3d_to_region_2d(region, view, location)
    if pixel is None:
        return

    snapped = _MARKER["kind"] not in (SURFACE, PLANE)
    x, y = pixel
    lines = []
    if snapped:
        count = 24
        ring = [(x + 7.0 * np.cos(2 * np.pi * i / count),
                 y + 7.0 * np.sin(2 * np.pi * i / count), 0.0)
                for i in range(count)]
        for i in range(count):
            lines += [ring[i], ring[(i + 1) % count]]
    arm = 10.0
    lines += [(x - arm, y, 0.0), (x - 3.0, y, 0.0), (x + 3.0, y, 0.0), (x + arm, y, 0.0),
              (x, y - arm, 0.0), (x, y - 3.0, 0.0), (x, y + 3.0, 0.0), (x, y + arm, 0.0)]

    shader = gpu.shader.from_builtin("POLYLINE_UNIFORM_COLOR")
    batch = batch_for_shader(shader, "LINES", {"pos": lines})
    shader.bind()
    shader.uniform_float("viewportSize", (region.width, region.height))
    gpu.state.blend_set("ALPHA")
    # A dark outline under the colour, so the marker reads on sand and snow.
    for width, colour in ((4.0, (0.0, 0.0, 0.0, 0.6)),
                          (2.0, (1.0, 0.6, 0.1, 1.0) if snapped
                           else (1.0, 1.0, 1.0, 1.0))):
        shader.uniform_float("lineWidth", width)
        shader.uniform_float("color", colour)
        batch.draw(shader)
    gpu.state.blend_set("NONE")
