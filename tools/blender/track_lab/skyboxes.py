"""The skyboxes a track can be drawn under, and a picture of each.

A level header's ``/background/skybox/id`` names an object the game spawns when
the level loads (``tracks.c:1324-1345``) and moves to the camera every frame
before drawing it (``tracks.c:1644-1659``). So a sky never gets closer: it is a
dome around the viewer, drawn at the object's own scale, in single player only -
split screen draws a gradient instead. Retail uses sixteen of the eighteen
``ASSET_OBJECT_DOME*`` models; eight levels have none.

The picture is a **panorama**: every triangle of the dome rasterised by the
direction it is seen in from the centre, azimuth across and elevation up, with
its texture and its vertex colours, batch by batch in the order the game draws
them. That is what the sky looks like from the track, which is the question a
gallery of skies has to answer - a dome's textures on their own are strips of
gradient that look like nothing.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import json
import math
from typing import Callable, Dict, List, Optional, Sequence, Tuple

from . import assets, textures as texture_module

PREFIX = "ASSET_OBJECT_DOME"

#: The panorama spans from a little under the horizon, or the dome's lowest
#: point, up to the dome's highest point or this, whichever is lower. Many domes
#: are open at the top - Ancient Lake's stops at 38 degrees, and past its edge
#: the game shows its clear colour - because a racer's camera never looks up
#: that far, so a picture of what a racer sees stops there too.
BELOW_HORIZON = 15.0
HIGHEST = 60.0

#: A vertex this near straight up or down has no azimuth worth the name: the top
#: of a cap is a ring of triangles meeting at it, and giving it one azimuth
#: draws them as teeth with gaps between.
POLE_ELEVATION = 84.0

#: What shows where no triangle covers.
BACKGROUND = (0.07, 0.07, 0.08, 1.0)


class Skybox:
    """One dome a header can name."""

    __slots__ = ("asset_id", "model_path", "scale", "used_by")

    def __init__(self, asset_id, model_path, scale, used_by):
        self.asset_id = asset_id
        self.model_path = model_path
        #: The object header's scale, which the game draws the dome at.
        self.scale = scale
        #: Labels of the retail levels whose header names it.
        self.used_by: List[str] = list(used_by)

    @property
    def label(self) -> str:
        """``ASSET_OBJECT_DOME13`` -> ``Dome 13``."""
        suffix = self.asset_id[len(PREFIX):] if self.asset_id.startswith(PREFIX) else ""
        return ("Dome " + suffix) if suffix else "Dome"

    @property
    def description(self) -> str:
        if self.used_by:
            return "%s. Used by %s" % (self.asset_id, ", ".join(self.used_by))
        return "%s. No retail track uses it" % self.asset_id

    def __repr__(self):
        return "Skybox(%r, %d track(s))" % (self.asset_id, len(self.used_by))


_CATALOGUES: Dict[str, List[Skybox]] = {}


def catalogue(tree) -> List[Skybox]:
    """Every dome the tree holds, in asset order, with who uses it.

    The domes are found by name, and any other object a retail header uses as
    its sky is added too, so a sky retail uses can never be missing from the
    gallery. Cached per tree: the answer needs every level header read once.
    """
    if tree is None:
        return []
    if tree.root in _CATALOGUES:
        return _CATALOGUES[tree.root]

    used: Dict[str, List[str]] = {}
    for level in tree.levels():
        try:
            with open(level.header_path, "r", encoding="utf-8") as handle:
                header = json.load(handle)
        except (OSError, ValueError, TypeError):
            continue
        sky = ((header.get("background") or {}).get("skybox") or {}).get("id")
        if sky:
            used.setdefault(sky, []).append(level.label)

    ids = [a for a in tree.order(assets.META_OBJECTS) if a.startswith(PREFIX)]
    ids += [a for a in used if a not in ids]
    found = []
    for asset_id in ids:
        try:
            kind, path, header = tree.preview_for(asset_id)
        except Exception:  # noqa: BLE001 - one bad header must not empty the list
            continue
        if kind != "mesh" or not path:
            continue
        found.append(Skybox(asset_id, path, header.scale if header else 1.0,
                            sorted(used.get(asset_id, []))))
    _CATALOGUES[tree.root] = found
    return found


def find(tree, asset_id: str) -> Optional[Skybox]:
    for skybox in catalogue(tree):
        if skybox.asset_id == asset_id:
            return skybox
    return None


# ---------------------------------------------------------------------------
# The panorama
# ---------------------------------------------------------------------------

Sample = Callable[[float, float], Tuple[float, float, float, float]]


class PngSampler:
    """Texture lookups for :func:`panorama`, from the extracted PNGs.

    Called with a model's texture table entry, it returns a function of a
    normalised, V-flipped UV (the convention :func:`level_model.normalise_uv`
    produces) giving ``(r, g, b, a)`` from 0 to 1, wrapping as the RDP does -
    or ``None`` for a texture that was not extracted, whose faces then show
    their vertex colours alone.

    The UV names a row of the texture as the ROM holds it, and a retail PNG
    holds its rows the other way up (:meth:`AssetTree.texture_3d_flipped`).
    Reading the PNG's own row instead is what hung Ancient Lake's mesas from
    the sky rather than standing them on the horizon.
    """

    def __init__(self, tree):
        self.tree = tree
        self._images: Dict[str, Optional[tuple]] = {}

    def __call__(self, texture) -> Optional[Sample]:
        png = self.tree.texture_3d_png(texture.texture_id) if self.tree else None
        if not png:
            return None
        if png not in self._images:
            try:
                self._images[png] = texture_module.read_png(png)
            except Exception:  # noqa: BLE001 - a thumbnail is not worth failing
                self._images[png] = None
        image = self._images[png]
        if image is None:
            return None
        width, height, rgba = image
        flipped = bool(getattr(self.tree, "texture_3d_flipped", None)
                       and self.tree.texture_3d_flipped(texture.texture_id))

        def sample(u, v):
            x = int(math.floor(u * width)) % width
            row = int(math.floor((1.0 - v) * height)) % height  # the ROM's row
            y = height - 1 - row if flipped else row  # the PNG's, top first
            at = (y * width + x) * 4
            return (rgba[at] / 255.0, rgba[at + 1] / 255.0,
                    rgba[at + 2] / 255.0, rgba[at + 3] / 255.0)
        return sample


def _direction(vertex) -> Tuple[float, float, bool]:
    """``(azimuth 0..1, elevation in degrees, at the pole)`` of a model vertex."""
    x, y, z = vertex
    across = math.hypot(x, z)
    elevation = math.degrees(math.atan2(y, across))
    pole = across <= 1e-6 * max(1.0, abs(y)) or abs(elevation) >= POLE_ELEVATION
    azimuth = (math.atan2(x, z) / (2.0 * math.pi)) % 1.0
    return azimuth, elevation, pole


def window(model) -> Tuple[float, float]:
    """The elevations, in degrees, the panorama of this dome spans."""
    elevations = [_direction(v)[1] for v in model.vertices]
    if not elevations:
        return -BELOW_HORIZON, HIGHEST
    low = max(-BELOW_HORIZON, min(elevations))
    high = min(HIGHEST, max(elevations))
    if high - low < 5.0:
        high = low + 5.0
    return low, high


def _polygon(points):
    """The screen polygon for one triangle, its seam and its pole mended.

    ``points`` are ``(azimuth, elevation, pole, attributes)``. A triangle that
    straddles the line where azimuth wraps from 1 back to 0 would otherwise be
    drawn the long way round, across the whole picture; and a vertex at the
    zenith has no azimuth at all, so it becomes two points along the top edge,
    one under each neighbour, turning the triangle into the quad it covers.
    """
    edge = [p for p in points if not p[2]]
    if not edge:
        return []
    us = [p[0] for p in edge]
    wrap = max(us) - min(us) > 0.5
    shifted = []
    for u, elevation, pole, attributes in points:
        if not pole and wrap and u < 0.5:
            u += 1.0
        shifted.append((u, elevation, pole, attributes))

    polygon = []
    count = len(shifted)
    for index, (u, elevation, pole, attributes) in enumerate(shifted):
        if not pole:
            polygon.append((u, elevation, attributes))
            continue
        before = shifted[index - 1]
        after = shifted[(index + 1) % count]
        for neighbour in (before, after):
            if not neighbour[2]:
                polygon.append((neighbour[0], elevation, attributes))
    return polygon


def _fill(pixels, covered, width, height, triangle, sample):
    """Rasterise one screen triangle, blending over what is already there."""
    (x0, y0, a0), (x1, y1, a1), (x2, y2, a2) = triangle
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if abs(area) < 1e-9:
        return
    left = max(0, int(math.floor(min(x0, x1, x2))))
    right = min(width - 1, int(math.ceil(max(x0, x1, x2))))
    bottom = max(0, int(math.floor(min(y0, y1, y2))))
    top = min(height - 1, int(math.ceil(max(y0, y1, y2))))
    slack = -1e-4  # shared edges drawn from both sides rather than neither
    for row in range(bottom, top + 1):
        cy = row + 0.5
        for column in range(left, right + 1):
            cx = column + 0.5
            w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area
            w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area
            w2 = 1.0 - w0 - w1
            if w0 < slack or w1 < slack or w2 < slack:
                continue
            u = w0 * a0[0] + w1 * a1[0] + w2 * a2[0]
            v = w0 * a0[1] + w1 * a1[1] + w2 * a2[1]
            r = w0 * a0[2] + w1 * a1[2] + w2 * a2[2]
            g = w0 * a0[3] + w1 * a1[3] + w2 * a2[3]
            b = w0 * a0[4] + w1 * a1[4] + w2 * a2[4]
            a = w0 * a0[5] + w1 * a1[5] + w2 * a2[5]
            if sample is not None:
                tr, tg, tb, ta = sample(u, v)
                r, g, b, a = r * tr, g * tg, b * tb, a * ta
            a = min(1.0, max(0.0, a))
            if a > 0.0:
                covered[row * width + column] = 1
            at = (row * width + column) * 4
            keep = 1.0 - a
            pixels[at] = r * a + pixels[at] * keep
            pixels[at + 1] = g * a + pixels[at + 1] * keep
            pixels[at + 2] = b * a + pixels[at + 2] * keep
            pixels[at + 3] = 1.0


def panorama(model, sampler: Optional[Callable] = None, width: int = 96,
             height: int = 48, background: Sequence[float] = BACKGROUND) -> List[float]:
    """The dome seen from its centre, as ``width * height * 4`` floats.

    Rows run bottom first - Blender's image order - across the elevations
    :func:`window` picks. Colours stay in the sRGB the textures and vertex
    colours are authored in, texture times vertex colour, which is what a
    preview's pixels are shown as.
    """
    pixels = list(background) * (width * height)
    covered = bytearray(width * height)
    low, high = window(model)
    span = high - low
    for batch in model.batches:
        texture = model.texture_for(batch)
        sample = sampler(texture) if (sampler is not None and texture is not None) else None
        for face in range(batch.face_offset, batch.face_offset + batch.face_count):
            if face >= len(model.triangles):
                break
            _flags, i0, i1, i2 = model.triangles[face]
            indices = [batch.vertex_offset + i for i in (i0, i1, i2)]
            if max(indices) >= len(model.vertices):
                continue
            uvs = model.face_uvs(face, texture) if sample is not None else None
            points = []
            for corner, index in enumerate(indices):
                azimuth, elevation, pole = _direction(model.vertices[index])
                colour = (model.colours[index] if index < len(model.colours)
                          else (255, 255, 255, 255))
                u, v = uvs[corner] if uvs else (0.0, 0.0)
                attributes = (u, v, colour[0] / 255.0, colour[1] / 255.0,
                              colour[2] / 255.0, colour[3] / 255.0)
                points.append((azimuth, elevation, pole, attributes))

            polygon = [(u * width, (elevation - low) / span * height, attributes)
                       for u, elevation, attributes in _polygon(points)]
            if len(polygon) < 3:
                continue
            reach = max(p[0] for p in polygon)
            shifts = (0.0, -float(width)) if reach > width else (0.0,)
            for shift in shifts:
                moved = [(x + shift, y, attributes) for x, y, attributes in polygon]
                for corner in range(1, len(moved) - 1):
                    _fill(pixels, covered, width, height,
                          (moved[0], moved[corner], moved[corner + 1]), sample)

    # A dome's top edge is ragged, and past it the game shows its clear colour
    # where no racer looks. Carrying each column's highest sky colour up keeps
    # the picture reading as a sky rather than as a torn strip.
    for column in range(width):
        top = next((row for row in range(height - 1, -1, -1)
                    if covered[row * width + column]), -1)
        if top < 0:
            continue
        source = (top * width + column) * 4
        for row in range(top + 1, height):
            at = (row * width + column) * 4
            pixels[at:at + 4] = pixels[source:source + 4]
    return pixels
