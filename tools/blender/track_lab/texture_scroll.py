"""TexScroll references, waterfall mapping and runtime bounds, without Blender."""

from __future__ import annotations

import math

from . import textures

OBJECT_ID = "ASSET_OBJECT_TEXSCROLL"
PROP_ENTRY = "dkr_scroll_entry"
WRAP_REPEATS = 8
SUBSTEPS = 4
TICKS_PER_SECOND = 60
MAX_WRAP_SIZE = textures.MAX_WRAP_SIZE
DEFAULT_SPEED = 95
SIGNATURE = ("id", "w", "h", "format", "surface")
PRESETS = (
    ("ASSET_TEX3D_COMMON_WATERFALL", "Waterfall"),
    ("ASSET_TEX3D_COMMON_WATERFALL2", "Waterfall 2"),
    ("ASSET_TEX3D_COMMON_WATERFALL3", "Waterfall 3"),
    ("ASSET_TEX3D_WINTER_ICYWATERFALL", "Icy Waterfall"),
    ("ASSET_TEX3D_DINO_MAGMAFALL", "Magma Fall"),
    ("ASSET_TEX3D_MEDIEVAL_WATERFOUNTAIN", "Water Fountain"),
)


class ScrollError(ValueError):
    """A scrolling entry cannot safely be used as requested."""


def texels_per_second(speed, texel_size=textures.TEXEL):
    return float(speed) * TICKS_PER_SECOND / SUBSTEPS / texel_size


def speed_from_texels(value, texel_size=textures.TEXEL):
    if not math.isfinite(value):
        raise ScrollError("speed must be a finite number")
    return max(-128, min(127, round(value * SUBSTEPS * texel_size / TICKS_PER_SECOND)))


def reference(table, index):
    if not 0 <= index < len(table):
        raise ScrollError("texture index %d is outside the track's texture table" % index)
    return dict(index=index, **{key: table[index][key] for key in SIGNATURE})


def resolve_index(table, ref):
    if not isinstance(ref, dict) or any(key not in ref for key in ("index",) + SIGNATURE):
        raise ScrollError("invalid texture reference; pick the texture from an active face")
    def matches(entry):
        return all(entry.get(key) == ref[key] for key in SIGNATURE)
    index = ref["index"]
    if isinstance(index, int) and 0 <= index < len(table) and matches(table[index]):
        return index
    candidates = [i for i, entry in enumerate(table) if matches(entry)]
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise ScrollError("moves a texture the track no longer has")
    # Identical images can belong to separate waterfalls. Guessing here would
    # animate the wrong one, so a moved duplicate needs an explicit new pick.
    raise ScrollError("texture reference is ambiguous; pick the intended entry from an active face")


def axis_problem(texture, axis):
    side = texture.width if axis == 0 else texture.height
    wrap = getattr(texture, "wrap_s" if axis == 0 else "wrap_t", "Wrap")
    if side <= 0 or side > MAX_WRAP_SIZE:
        return "%s must be between 1 and %d texels for scrolling" % (
            "width" if axis == 0 else "height", MAX_WRAP_SIZE)
    if str(wrap).lower() != "wrap":
        return "%s is set to %s; scrolling needs Wrap" % ("U" if axis == 0 else "V", wrap)
    return None


def horizontal_axis(corners):
    # Newell's normal works on triangles and author-created polygons alike.
    nx = nz = 0.0
    for a, b in zip(corners, corners[1:] + corners[:1]):
        nx += (a[1] - b[1]) * (a[2] + b[2])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    length = math.hypot(nx, nz)
    if length < 1e-8:
        raise ScrollError("a waterfall needs sloping or vertical faces; horizontal faces have no fall direction")
    axis = [nz / length, 0.0, -nx / length]
    dominant = 0 if abs(axis[0]) >= abs(axis[2]) else 2
    if axis[dominant] < 0:
        axis = [-v for v in axis]
    return axis


def fall_mapping(corners, texture, repeats=1.0, across=None, bounds=None):
    """Raw UVs with positive V motion falling in map Y, continuous on a sheet.

    Sampling V increases upward: adding V in the game moves the picture down.
    U spans the sheet once (retail waterfalls clamp U). V repeats over its full
    height. Shared bounds keep triangulated faces on the same mapping.
    """
    if not math.isfinite(repeats) or repeats <= 0:
        raise ScrollError("repeats must be a positive finite number")
    horizontal_axis(corners)  # Refuse flat faces even when a shared axis exists.
    across = across or horizontal_axis(corners)
    projected = [(sum(a * b for a, b in zip(p, across)), p[1]) for p in corners]
    if bounds is None:
        bounds = (min(p[0] for p in projected), max(p[0] for p in projected),
                  min(p[1] for p in projected), max(p[1] for p in projected))
    left, right, bottom, top = bounds
    if right - left < 1e-8 or top - bottom < 1e-8:
        raise ScrollError("the selected faces need both width and height")
    period = texture.height * textures.TEXEL
    raw = [((u - left) / (right - left) * texture.width * textures.TEXEL,
            (v - bottom) / (top - bottom) * repeats * period) for u, v in projected]
    offset = math.floor(min(v for u, v in raw) / period) * period
    return [(round(u), round(v - offset)) for u, v in raw]


def uv_problems(faces, texture, axis, speed, max_update_rate=6, max_step=None):
    """Conservative s16 bound, including the wrap and pre-wrap update overshoot.

    The runtime wraps both axes, even when one speed is zero. Face extension
    bounds every possible uv0 chosen by triangulation, not just today's uv0.
    """
    side = texture.width if axis == 0 else texture.height
    span = side * 256
    step = (math.ceil(abs(speed) * max_update_rate / SUBSTEPS)
            if max_step is None else max_step)
    found = []
    for number, raw in enumerate(faces):
        values = [uv[axis] for uv in raw]
        if not values:
            continue
        unsafe = min(values) < -32768 or max(values) > 32767 or span <= 0
        if not unsafe and speed == 0:
            # The inactive axis only normalises its initial uv0; it never
            # traverses the whole wrap span. Wide textures may still scroll V.
            shift = 0
            if values[0] < 0:
                shift = math.ceil(-values[0] / span) * span
            elif values[0] > span:
                shift = -math.ceil((values[0] - span) / span) * span
            unsafe = min(values) + shift < -32768 or max(values) + shift > 32767
        elif not unsafe:
            unsafe = span + step + max(values) - min(values) > 32767
        if unsafe:
            found.append("face %d can overflow scrolling %s UVs; reduce repeats or subdivide the face"
                         % (number, "U" if axis == 0 else "V"))
    return found


def problems(faces, texture, speeds):
    """``(severity, message)`` pairs for faces using a single scrolling entry.

    Faces are dictionaries with raw ``uvs`` and triangle ``flags``.
    Speeds contains every TexScroll on that entry, since they add together.
    """
    found = []
    if not faces:
        found.append(("warning", "no faces use this scrolling texture entry"))
    if len(speeds) > 1:
        found.append(("warning", "multiple TexScroll objects use this entry; their speeds add together"))
    if any(u == 0 and v == 0 for u, v in speeds):
        found.append(("warning", "TexScroll has zero speed on both axes"))
    skipped = sum(bool(face.get("flags", 0) & 0x80) for face in faces)
    if skipped:
        found.append(("error", "%d face(s) have triangle flag 0x80 and will not scroll" % skipped))
    for axis in (0, 1):
        speed = sum(abs(pair[axis]) for pair in speeds)
        if speed:
            problem = axis_problem(texture, axis)
            if problem:
                found.append(("warning", problem))
        found.extend(("error", p) for p in uv_problems(
            [f["uvs"] for f in faces if not f.get("flags", 0) & 0x80], texture, axis, speed,
            max_step=sum(math.ceil(abs(pair[axis]) * 6 / SUBSTEPS) for pair in speeds)))
    return found
