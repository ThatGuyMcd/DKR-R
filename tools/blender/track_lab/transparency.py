"""What makes the game draw a face see-through, and how an author asks for it.

Three things decide it, and only one of them is the face's own.

**The texture's render mode.** ``material_init`` (``textures_sprites.c``) reads
the high nibble of ``TextureHeader.format`` and gives the texture
``RENDER_SEMI_TRANSPARENT`` when it says ``TRANSPARENT`` or ``TRANSPARENT_2`` -
for RGBA32, RGBA16 and CI4. The three greyscale-with-alpha formats (IA16, IA8,
IA4) get it whatever the nibble says, and the two plain greyscale ones (I8, I4)
never do. The flag is then OR-ed into the batch's own when it is drawn, which
turns the render mode into ``G_RM_AA_ZB_XLU_SURF``: blended by the texel's
alpha, and not writing depth.

**Which pass draws the batch.** ``render_level_segment`` (``tracks.c``) is
called twice per segment: once for batches ``[0, numberofOpaqueBatches)`` and
once for ``[numberofOpaqueBatches, numberOfBatches)``. Inside each run it draws
a batch only if the batch belongs to that pass, which is::

    opaque pass  <=>  (texture not SEMI_TRANSPARENT and batch not WATER)
                      or batch is DECAL

So a batch on the wrong side of the split is **never drawn at all** - skipped by
the pass it sits in, and outside the range of the pass it belongs to. This is
what made a transparent picture vanish from a custom track: the addon kept the
side a face came from, and a road retextured with the ROM's water stayed on the
opaque side, where the game will not draw water. The rule is not a guess:
across all 10,389 retail batches it puts 10,388 on the side retail wrote. The
one exception is an untextured, unflagged batch in ``volcano_track.bin`` that
the game never draws either, which is why :func:`draws_in_opaque_pass` is only
applied to textured faces.

**``RENDER_CUTOUT`` on the batch** (bit 4). ``material_set`` sends such a batch
through ``dRenderSettingsCutout``, whose z-buffered entries are all
``G_RM_AA_ZB_TEX_EDGE``: the texel's alpha cuts holes, the rest is solid and
writes depth, whatever the texture's render mode is. That is a fence, a leaf,
a grate. Retail sets it on 302 batches, every one of them over a transparent
texture, so the batch is drawn in the second pass; nothing breaks if the
texture is opaque instead, it is just drawn in the first.

So an author has three looks to choose from, and :data:`MODES` names them:

* ``OPAQUE`` - alpha is ignored. The texture is written ``OPAQUE``.
* ``CUTOUT`` - alpha cuts holes. The texture is written ``TRANSPARENT`` and the
  faces carry ``RENDER_CUTOUT``, as retail does.
* ``BLEND``  - alpha blends, glass or water. The texture is written
  ``TRANSPARENT`` and the faces do not carry ``RENDER_CUTOUT``.

For a texture of the track's own, the mode belongs to the texture - its render
mode is a byte of the texture - and the faces follow. For one of the ROM's the
render mode is fixed, so only the cut-out bit is left to choose.

**The pixels have to agree with the mode.** RGBA16 and IA4 keep one bit of
alpha, and the encoder writes it the way ``rgba2raw`` does: any alpha above zero
is opaque. A picture resampled by Blender has soft edges, so a cut-out written
straight would grow a fringe of texels that were meant to be holes - and whose
colour, in most exported PNGs, is black. :func:`prepare` hardens the alpha at
half and spreads each edge's colour into the holes beside it, so bilinear
filtering on the console and in RT64 blends into the right colour. It changes
the pixels the encoder reads and nothing about the encoder, which stays
byte-for-byte the asset tool's.

**Vertex alpha is a fourth path, and the loader owns it.** ``track_init_level_model``
turns a vertex coloured ``(1, 1, b, a)`` into grey 128 with alpha ``b`` and flags
its batch ``RENDER_VTX_ALPHA``. ``material_set`` then draws the batch with
``G_CC_MODULATERGBA``: the texel's alpha times the vertex's, blended, with fog
off. It is the one knob that fades a face whatever its texture is - one of the
ROM's included, whose pixels cannot change - so a material's **opacity** is
written this way: :func:`apply_opacity` gives every vertex of a faded batch that
colour. What it costs is the baked light (every vertex becomes the same grey)
and the fog.

**A faded face has to be drawn in the second pass.** ``dRenderSettingsVtxAlpha``
blends *and writes depth*: its no-write half is chosen by ``RENDER_Z_UPDATE``,
which is bit 8 - ``RENDER_HIDDEN`` on a batch, so never set on one that is
drawn. And the first pass walks the segments front to back. A faded face left
there is drawn before what stands behind it, blends with the sky, and then
hides everything behind it from the depth buffer: a lake that shows the sky
instead of its bed. Retail leaves 195 faded batches in the first pass, and every
one fades to alpha 0 - an edge vanishing into nothing, with nothing behind it to
hide - never to a see-through surface. The second pass comes after the solid
track and the racers, back to front, so there the fade shows what is behind it.

:func:`draws_in_opaque_pass` gives a batch over an opaque texture no way into
the second pass but ``RENDER_WATER``, so the export moves a faded face in one
of two ways:

* A texture of the track's own is written ``TRANSPARENT``
  (:func:`faded_render_mode`); its pixels stay as its look makes them, so an
  opaque one still ignores its alpha and the vertex alpha is the whole fade.
* Calm water - ``SURFACE_WATER_CALM`` over a ROM texture, whose render mode
  cannot change - gets ``RENDER_WATER`` (:func:`faded_flags`), which is what
  retail's own water carries: no shadows fall on it, and a racer crossing it
  gets the water effect. Anything else cannot be moved without side effects,
  and :func:`faded_in_opaque_pass` counts it so the export can say so.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

from typing import Iterable, Optional, Sequence, Tuple

#: ``RenderFlags`` in ``textures_sprites.h``.
RENDER_SEMI_TRANSPARENT = 1 << 2
RENDER_CUTOUT = 1 << 4
RENDER_DECAL = 1 << 11
RENDER_WATER = 1 << 13

OPAQUE = "OPAQUE"
CUTOUT = "CUTOUT"
BLEND = "BLEND"
#: Not a look: "whatever the texture is made as". What Apply uses by default.
AUTO = "AUTO"
MODES = (OPAQUE, CUTOUT, BLEND)

#: How each mode is described to an author, for the panels.
DESCRIPTIONS = {
    OPAQUE: "Solid. The picture's alpha is ignored",
    CUTOUT: "Alpha cuts holes: fences, leaves, grates. Solid where it is not "
            "cut, and drawn into the depth buffer",
    BLEND: "Alpha blends: glass, water, smoke. Drawn after the solid track and "
           "never hides what is behind it",
}

#: ``FORMAT_CODES`` values, spelled out so this module needs nothing else.
_RGBA32, _RGBA16, _I8, _I4, _IA16, _IA8, _IA4, _CI4, _CI8 = range(9)

#: Formats ``material_init`` makes see-through whatever the render mode says.
ALWAYS_TRANSLUCENT = frozenset((_IA16, _IA8, _IA4))
#: Formats whose render mode decides it.
MODE_DECIDES = frozenset((_RGBA32, _RGBA16, _CI4))
#: Formats that store a single bit of alpha, so blending is all or nothing.
ONE_BIT_ALPHA = frozenset((_RGBA16, _IA4))
#: Formats with no alpha channel at all; their "alpha" is the intensity.
NO_ALPHA = frozenset((_I8, _I4))

#: The render modes ``material_init`` treats as transparent.
TRANSPARENT_RENDER_MODES = frozenset(("TRANSPARENT", "TRANSPARENT_2"))

#: Alpha at or above this is solid when a picture is hardened.
HARD_THRESHOLD = 128


# ---------------------------------------------------------------------------
# What the game does
# ---------------------------------------------------------------------------

def translucent(texture_format, render_mode) -> bool:
    """Whether ``material_init`` gives this texture ``RENDER_SEMI_TRANSPARENT``."""
    code = int(texture_format) & 0xF
    if code in ALWAYS_TRANSLUCENT:
        return True
    if code in MODE_DECIDES:
        return str(render_mode or "").upper() in TRANSPARENT_RENDER_MODES
    return False


def draws_in_opaque_pass(flags, is_translucent) -> bool:
    """The side of ``numberofOpaqueBatches`` a batch has to sit on to be drawn.

    ``render_level_segment``'s own test; see the module docstring.
    """
    flags = int(flags)
    if flags & RENDER_DECAL:
        return True
    return not is_translucent and not flags & RENDER_WATER


def face_mode(flags, is_translucent) -> str:
    """The look a batch's flags and texture add up to."""
    if int(flags) & RENDER_CUTOUT:
        return CUTOUT
    return BLEND if is_translucent else OPAQUE


def with_mode(flags, mode) -> int:
    """``flags`` with ``RENDER_CUTOUT`` set for a cut-out and cleared otherwise.

    Nothing else is touched. The blend itself comes from the texture, and the
    batch's own ``RENDER_SEMI_TRANSPARENT`` - which retail sets on its water -
    is a choice this module does not make for anyone.
    """
    value = int(flags) & 0xFFFFFFFF
    if mode == CUTOUT:
        return value | RENDER_CUTOUT
    return value & ~RENDER_CUTOUT


# ---------------------------------------------------------------------------
# What an author may ask for
# ---------------------------------------------------------------------------

def render_mode_for(mode) -> str:
    """The render mode a texture of the track's own is written with."""
    return "OPAQUE" if mode in (None, "", OPAQUE) else "TRANSPARENT"


def own_modes(texture_format) -> Tuple[str, ...]:
    """The looks a texture of the track's own can have in this format.

    The greyscale-with-alpha formats are see-through by construction, so they
    cannot be opaque; the plain greyscale ones have no alpha to cut or blend by.
    """
    code = int(texture_format) & 0xF
    if code in ALWAYS_TRANSLUCENT:
        return (CUTOUT, BLEND)
    if code in NO_ALPHA:
        return (OPAQUE,)
    return MODES


def face_modes(texture_format, render_mode) -> Tuple[str, ...]:
    """The looks a face drawing this texture can be given.

    Its render mode is fixed, so a see-through one can be cut out or blended
    and a solid one can be cut out or left solid.
    """
    if int(texture_format) & 0xF in NO_ALPHA:
        return (OPAQUE,)
    if translucent(texture_format, render_mode):
        return (CUTOUT, BLEND)
    return (OPAQUE, CUTOUT)


def settle(requested, allowed: Sequence[str], natural: str) -> str:
    """The look to use: the request if the texture can have it, else its own."""
    if requested in allowed:
        return requested
    if natural in allowed:
        return natural
    return allowed[0]


def own_mode(requested, texture_format) -> str:
    """A mode a texture of the track's own can really be written with."""
    allowed = own_modes(texture_format)
    natural = BLEND if int(texture_format) & 0xF in ALWAYS_TRANSLUCENT else OPAQUE
    return settle(requested, allowed, natural)


# ---------------------------------------------------------------------------
# Reading a picture's alpha
# ---------------------------------------------------------------------------

#: A cut-out painted with anti-aliased edges still has some half-covered
#: pixels. Up to this fraction of the whole picture, they are read as edges.
EDGE_FRACTION = 0.15


def census(alphas: Iterable[int]) -> Tuple[int, int, int]:
    """``(solid, clear, partial)``: how many pixels are 255, 0, and between."""
    solid = clear = partial = 0
    for alpha in alphas:
        if alpha >= 255:
            solid += 1
        elif alpha <= 0:
            clear += 1
        else:
            partial += 1
    return solid, clear, partial


def suggest(solid: int, clear: int, partial: int) -> str:
    """The look a picture's alpha asks for.

    No transparency at all is opaque. Holes with at most a thin band of
    half-covered pixels around them are a cut-out - that band is what an
    anti-aliased brush leaves. Anything softer is a blend.
    """
    total = solid + clear + partial
    if total <= 0 or (clear == 0 and partial == 0):
        return OPAQUE
    if clear > partial and partial <= total * EDGE_FRACTION:
        return CUTOUT
    return BLEND


def suggest_rgba(rgba) -> str:
    """:func:`suggest` for eight-bit RGBA, four bytes a pixel."""
    return suggest(*census(rgba[3::4]))


# ---------------------------------------------------------------------------
# Making the pixels agree with the mode
# ---------------------------------------------------------------------------

def prepare(rgba, width: int, height: int, mode: Optional[str],
            texture_format) -> bytes:
    """The picture as the texture should hold it, for this look and format.

    ``mode`` ``None`` leaves the pixels exactly as they are, which is what a
    texture added before transparency existed is encoded with - its bytes must
    not change under an author who did nothing.
    """
    out = bytearray(rgba)
    if mode is None:
        return bytes(out)
    code = int(texture_format) & 0xF
    count = int(width) * int(height)
    if code in NO_ALPHA:
        return bytes(out)
    if mode == OPAQUE:
        for pixel in range(count):
            out[pixel * 4 + 3] = 255
        return bytes(out)
    if mode == CUTOUT or code in ONE_BIT_ALPHA:
        harden(out, count)
    bleed(out, int(width), int(height))
    return bytes(out)


def harden(rgba: bytearray, count: int, threshold: int = HARD_THRESHOLD) -> None:
    """Every alpha to 0 or 255, at ``threshold``. In place."""
    for pixel in range(count):
        at = pixel * 4 + 3
        rgba[at] = 255 if rgba[at] >= threshold else 0


#: How many texels deep a colour is spread into a hole. Filtering reads one
#: neighbour on the console and a handful in a mipmapped RT64 sample, so a few
#: rings are plenty and the rest of a hole keeps whatever it held.
BLEED_PASSES = 4


def bleed(rgba: bytearray, width: int, height: int,
          passes: int = BLEED_PASSES) -> None:
    """Give fully clear texels the colour of their visible neighbours. In place.

    A clear texel is never seen, but a filtered sample beside it mixes its
    colour in, and in an exported PNG that colour is usually black: the dark
    halo around every cut-out leaf. Each pass gives every clear texel that
    touches a coloured one the average of those neighbours; the texture wraps,
    so the neighbours do too. Alpha is never changed.
    """
    if width <= 0 or height <= 0:
        return
    count = width * height
    known = [rgba[pixel * 4 + 3] > 0 for pixel in range(count)]
    if all(known) or not any(known):
        return
    for _each in range(max(0, int(passes))):
        filled = []
        for pixel in range(count):
            if known[pixel]:
                continue
            x, y = pixel % width, pixel // width
            red = green = blue = found = 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if not dx and not dy:
                        continue
                    other = ((y + dy) % height) * width + (x + dx) % width
                    if known[other]:
                        at = other * 4
                        red += rgba[at]
                        green += rgba[at + 1]
                        blue += rgba[at + 2]
                        found += 1
            if found:
                filled.append((pixel, (red + found // 2) // found,
                               (green + found // 2) // found,
                               (blue + found // 2) // found))
        if not filled:
            break
        for pixel, red, green, blue in filled:
            at = pixel * 4
            rgba[at], rgba[at + 1], rgba[at + 2] = red, green, blue
            known[pixel] = True


# ---------------------------------------------------------------------------
# Opacity: fading a whole face through the loader's vertex alpha
# ---------------------------------------------------------------------------

#: ``RENDER_VTX_ALPHA``: set by the loader, never by the file.
RENDER_VTX_ALPHA = 1 << 27

#: The grey ``track_init_level_model`` gives a faded vertex in place of its
#: baked light.
VERTEX_ALPHA_GREY = 0x80


def opacity_byte(opacity) -> int:
    """An opacity from 0 to 1 as the vertex alpha byte that says it."""
    return max(0, min(255, int(round(float(opacity) * 255.0))))


def vertex_alpha_colour(alpha: int) -> Tuple[int, int, int, int]:
    """The vertex colour the loader reads as ``alpha``: ``(1, 1, alpha, 255)``."""
    return (1, 1, max(0, min(255, int(alpha))), 255)


def apply_opacity(model, alpha_for) -> Tuple[int, int]:
    """Fade the batches ``alpha_for(batch)`` gives an alpha below 255. In place.

    Every vertex in such a batch's window is given :func:`vertex_alpha_colour`,
    which works because a batch's vertices are its own - the windows tile each
    segment with no overlap. ``None`` or 255 leaves a batch exactly as it is,
    retail's own faded edges included. Returns ``(batches, vertices)`` faded.
    """
    batches = vertices = 0
    for segment in model.segments:
        for batch in segment.batches:
            alpha = alpha_for(batch)
            if alpha is None or int(alpha) >= 255:
                continue
            colour = vertex_alpha_colour(alpha)
            start = int(batch.vertex_offset)
            end = min(start + int(batch.vertex_count), len(segment.colours))
            for index in range(start, end):
                if tuple(segment.colours[index]) != colour:
                    segment.colours[index] = colour
                    vertices += 1
            batches += 1
    return batches, vertices


#: ``SurfaceType``'s still water in ``include/enums.h``: the one water an author
#: puts in a texture table (the wavy one is synthesised from the header).
SURFACE_WATER_CALM = 11


def faded_render_mode(render_mode, texture_format) -> str:
    """The render mode a texture drawn by faded faces has to be written with.

    ``TRANSPARENT`` where the render mode decides it, so the faces are drawn in
    the second pass; see the module docstring. A format that is see-through
    anyway keeps its own, and one the render mode cannot make see-through - the
    greyscale ones, CI8 - keeps it too and is left to :func:`faded_in_opaque_pass`.
    """
    mode = render_mode or "OPAQUE"
    if translucent(texture_format, mode) or int(texture_format) & 0xF not in MODE_DECIDES:
        return mode
    return "TRANSPARENT"


def faded_flags(flags, is_translucent, surface) -> int:
    """``flags`` for a faded batch, with ``RENDER_WATER`` if it is calm water
    that would otherwise be drawn in the first pass.

    Only for water, because the flag does more than move the batch; see the
    module docstring. A decal stays in the first pass whatever it carries, so it
    is left alone.
    """
    value = int(flags) & 0xFFFFFFFF
    if (surface == SURFACE_WATER_CALM and not value & RENDER_DECAL
            and draws_in_opaque_pass(value, is_translucent)):
        return value | RENDER_WATER
    return value


def fade_stays_in_first_pass(texture_format, render_mode, own, surface) -> bool:
    """Whether faces drawing this texture, faded, would still be drawn in the
    first pass - and so hide what is behind them. For the panel to warn before
    the export does."""
    if translucent(texture_format, render_mode):
        return False
    if own and translucent(texture_format,
                           faded_render_mode(render_mode, texture_format)):
        return False
    return surface != SURFACE_WATER_CALM


def faded_in_opaque_pass(model, alpha_for) -> int:
    """How many batches ``alpha_for`` fades that sit in the first pass anyway."""
    count = 0
    for segment in model.segments:
        for index, batch in enumerate(segment.batches):
            alpha = alpha_for(batch)
            if (alpha is not None and int(alpha) < 255
                    and index < segment.opaque_batches):
                count += 1
    return count


def advice(mode: str, texture_format) -> Optional[str]:
    """Something worth telling an author about this mode in this format."""
    code = int(texture_format) & 0xF
    if mode == BLEND and code in ONE_BIT_ALPHA:
        return ("this format keeps one bit of alpha, so the blend is all or "
                "nothing - RGBA32 keeps the soft edges, at 32x32")
    if mode != OPAQUE and code in NO_ALPHA:
        return "this format has no alpha, so it can only be opaque"
    return None
