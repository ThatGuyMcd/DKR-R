"""The name RT64 replaces a track's own texture by, worked out before the game runs.

A texture pack in Rice's layout names each replacement image by an *identity*::

    <crc>#<fmt>#<siz>

RT64's patch ``patches/rt64/0011-enable-runtime-rice-texture-aliases.patch``
computes it from the texture's bytes in RDRAM and a handful of numbers it reads
off the tile descriptors the game set up. For a texture this addon wrote, every
one of those inputs is known offline: the bytes are the ones in
``textures/N.bin``, and the descriptors are what ``material_init`` builds from
the ``TextureHeader`` in front of them. So the identity can be computed here,
and a pack naming the author's full-resolution original under it has RT64 draw
that original where the game loaded 64x32.

It is safe for the reason a texture pack usually is not. A pack replaces by
hash, globally - but the texture this one replaces was invented by the addon
and exists nowhere else in the ROM, so replacing it globally reaches only the
track that shipped it. And it fails cleanly: an identity that does not match
simply finds no replacement, and the game draws the 64x32.

Two parts, both transcriptions, both held to their sources by
``tests/test_rice_identity.py``:

* :func:`rice_crc32` is the patch's ``riceCRC32``. Three things about it are
  easy to get wrong, and each gives a CRC that is plausible and wrong:

  1. It reads each four bytes with ``memcpy`` into a ``uint32_t`` - a
     **little-endian** word on every machine RT64 runs on. Hence
     ``int.from_bytes(..., "little")``.
  2. The arithmetic is ``uint32_t`` and wraps; every sum is masked.
  3. Rows are read **in memory order** - ``source`` only ever advances - and
     each row right to left in steps of four. What counts *down* is only the
     row number XORed in at the end of each row, so the image's first row is
     combined with ``y == height - 1``.

  And one thing about its *input*, which is not the payload's texels as
  written: the patch reads ``state->RDRAM``, and N64Recomp stores RDRAM with
  every 32-bit word byte-swapped on a little-endian host (the ``^ 3`` in
  ``MEM_B``). The game's texture load and DKR-R's asset-load hook both write
  through that, so the bytes ``riceCRC32`` walks are the texels with each four
  reversed. :func:`rice_identity` feeds :func:`rice_crc32` that swapped view
  via :func:`rice_word_order`; every HD-eligible texture has a 4-aligned row,
  so a whole-buffer word swap is exact.

* :func:`live_rectangle` is the patch's derivation of the ``width``,
  ``height`` and ``bytesPerRow`` the CRC runs over, for the ``Block`` load that
  ``material_init`` issues - fed with exactly what ``gDPLoadTextureBlock``
  (``PR/gbi.h``) and ``material_init`` (``textures_sprites.c``) put in the
  tiles. The rectangle does **not** come from the image's size. It comes from
  the tile mask, the clamp bits and the load's DXT, reversed by search. That it
  lands on the image's own size is a property of the sizes the addon accepts,
  and :func:`derive` checks it rather than assuming it.

A texture flagged ``RENDER_LINE_SWAP`` - the minimap is one - is loaded with
``gDPLoadTextureBlockS`` instead, which passes ``gDPLoadBlock`` a DXT of zero:
the patch then takes the row from the render tile's ``line``, and the CRC walks
the odd rows still swapped, as they sit in RDRAM. Pass ``swapped=True`` for
those. All 33 retail minimaps a Rice pack replaces come out under the names
that pack gives them (``tests/test_rice_identity.py``).

Deliberately free of ``bpy``.
"""

from __future__ import annotations

from typing import Optional, Tuple

from .textures import (FORMAT_CODES, TMEM_BYTES, TextureEncodeError,
                       check_size, nudge_texels, texel_bytes)

# ``PR/gbi.h``.
G_IM_FMT_RGBA = 0
G_IM_FMT_IA = 3
G_IM_FMT_I = 4

G_IM_SIZ_4b = 0
G_IM_SIZ_8b = 1
G_IM_SIZ_16b = 2
G_IM_SIZ_32b = 3

G_TX_WRAP = 0
G_TX_CLAMP = 2
G_TX_NOMASK = 0
G_TX_DXT_FRAC = 11
G_TEXTURE_IMAGE_FRAC = 2

#: ``(fmt, siz)`` the tile carries for each format a custom texture may be
#: written in. The colour-indexed two are absent on purpose: their identity
#: grows a fourth component, the palette's CRC, and a track cannot add a
#: palette - so a custom texture is never one.
TILE_FORMATS = {
    FORMAT_CODES["RGBA32"]: (G_IM_FMT_RGBA, G_IM_SIZ_32b),
    FORMAT_CODES["RGBA16"]: (G_IM_FMT_RGBA, G_IM_SIZ_16b),
    FORMAT_CODES["IA16"]: (G_IM_FMT_IA, G_IM_SIZ_16b),
    FORMAT_CODES["IA8"]: (G_IM_FMT_IA, G_IM_SIZ_8b),
    FORMAT_CODES["IA4"]: (G_IM_FMT_IA, G_IM_SIZ_4b),
    FORMAT_CODES["I8"]: (G_IM_FMT_I, G_IM_SIZ_8b),
    FORMAT_CODES["I4"]: (G_IM_FMT_I, G_IM_SIZ_4b),
}

# ``siz##_LOAD_BLOCK``, ``siz##_BYTES`` and ``siz##_LINE_BYTES`` from gbi.h,
# for the sizes ``gDPLoadTextureBlock`` takes. Four-bit textures go through
# ``gDPLoadTextureBlock_4b`` instead, which has its own arithmetic.
_LOAD_BLOCK = {G_IM_SIZ_8b: G_IM_SIZ_16b, G_IM_SIZ_16b: G_IM_SIZ_16b,
               G_IM_SIZ_32b: G_IM_SIZ_32b}
_BYTES = {G_IM_SIZ_8b: 1, G_IM_SIZ_16b: 2, G_IM_SIZ_32b: 4}
_LINE_BYTES = {G_IM_SIZ_8b: 1, G_IM_SIZ_16b: 2, G_IM_SIZ_32b: 2}

_U32 = 0xFFFFFFFF


class IdentityError(Exception):
    """This texture has no identity that can be known offline, and why."""


def tile_format(texture_format) -> Tuple[int, int]:
    """``(fmt, siz)`` - the ``G_IM_FMT_*`` and ``G_IM_SIZ_*`` of the tile."""
    try:
        return TILE_FORMATS[int(texture_format)]
    except (KeyError, TypeError, ValueError):
        raise IdentityError(
            "format %r cannot be a track's own texture, so it has no "
            "replacement identity" % (texture_format,)
        )


# ---------------------------------------------------------------------------
# The CRC
# ---------------------------------------------------------------------------

def rice_word_order(texels: bytes) -> bytes:
    """The texels as ``riceCRC32`` sees them in RDRAM: each 32-bit word reversed.

    N64Recomp stores RDRAM word-byte-swapped on a little-endian host, and the
    patch's ``riceCRC32`` reads RDRAM directly. A trailing 1-3 bytes (never
    reached for an HD-eligible texture, whose rows are 4-aligned) are left as
    they are.
    """
    whole = (len(texels) // 4) * 4
    swapped = bytearray(texels)
    for offset in range(0, whole, 4):
        swapped[offset:offset + 4] = swapped[offset:offset + 4][::-1]
    return bytes(swapped)


def rice_crc32(source, width, height, size, row_stride) -> int:
    """``riceCRC32`` from patch 0011, line for line. See the module docstring."""
    width, height, size = int(width), int(height), int(size)
    row_stride = int(row_stride)
    bytes_per_line = (width << size) >> 1
    if height > 0 and bytes_per_line >= 4:
        needed = (height - 1) * row_stride + bytes_per_line
        if needed > len(source):
            raise ValueError(
                "the CRC reads %d bytes and only %d were given"
                % (needed, len(source))
            )

    result = 0
    at = 0
    for y in range(height - 1, -1, -1):
        value = 0
        for x in range(bytes_per_line - 4, -1, -4):
            value = int.from_bytes(source[at + x:at + x + 4], "little")
            value ^= x
            result = ((result << 4) + ((result >> 28) & 15)) & _U32
            result = (result + value) & _U32
        value ^= y
        result = (result + value) & _U32
        at += row_stride
    return result


# ---------------------------------------------------------------------------
# The rectangle the CRC runs over
# ---------------------------------------------------------------------------

def txl2words(width, size) -> int:
    """The patch's ``txl2Words``: 64-bit words in one row, as RT64 counts them."""
    size_bytes = (0, 1, 2, 4)
    if size == 0:
        return max(1, width // 16)
    return max(1, width * size_bytes[size] // 8)


def calculate_dxt(words) -> int:
    """The patch's ``calculateDXT``."""
    return 1 if words == 0 else (2048 + words - 1) // words


def reverse_dxt(value, width, size) -> int:
    """The patch's ``reverseDXT``: the row length a DXT was calculated from.

    A DXT is a rounded reciprocal, so several row lengths can share one; the
    patch searches for the right one and, failing a match, takes the middle.
    """
    value = int(value)
    if value == 0x800:
        return 1
    if value <= 1:
        return 1
    low = 2047 // value
    if calculate_dxt(low) > value:
        low += 1
    high = 2047 // (value - 1)
    if low == high:
        return low
    for candidate in range(low, high + 1):
        if txl2words(width, size) == candidate:
            return candidate
    return (low + high) // 2


def _gbi_dxt(width, bytes_per_texel) -> int:
    """``CALC_DXT(width, b_txl)`` from gbi.h."""
    words = max(1, (width * bytes_per_texel) // 8)
    return ((1 << G_TX_DXT_FRAC) + words - 1) // words


def _gbi_dxt_4b(width) -> int:
    """``CALC_DXT_4b(width)`` from gbi.h - which, unlike ``CALC_DXT``, has no
    ``MAX(1, ...)`` around the word count."""
    words = width // 16
    if words == 0:
        raise IdentityError(
            "a 4-bit texture %d wide has no identity. gbi.h's CALC_DXT_4b "
            "divides by width/16, which is zero below 16 texels - the load the "
            "game issues for this texture divides by zero" % width
        )
    return ((1 << G_TX_DXT_FRAC) + words - 1) // words


def material_tile(width, height, clamp_s=False, clamp_t=False):
    """``(cms, cmt, masks, maskt)`` as ``material_init`` chooses them.

    Transcribed from ``textures_sprites.c``: the loop walks ``size`` over 1, 2,
    4 ... 64, raising the mask while the side is larger and turning the clamp
    off when it lands on the side exactly. A clamp flag in the header, or a
    side the loop never landed on, clamps with no mask.
    """
    size = 1
    masks = maskt = 1
    u_clamp = v_clamp = True
    for index in range(7):
        if size < width:
            masks = index + 1
        elif size == width:
            u_clamp = False
        if size < height:
            maskt = index + 1
        elif size == height:
            v_clamp = False
        size *= 2

    if u_clamp or clamp_s:
        cms, masks = G_TX_CLAMP, G_TX_NOMASK
    else:
        cms = G_TX_WRAP
    if v_clamp or clamp_t:
        cmt, maskt = G_TX_CLAMP, G_TX_NOMASK
    else:
        cmt = G_TX_WRAP
    return cms, cmt, masks, maskt


class Tiles:
    """What the RT64 patch reads for a ``Block`` load: the draw tile, the load
    tile's ``lrt`` (which holds the DXT) and the texture image's width and size.
    """

    __slots__ = ("fmt", "siz", "line", "uls", "ult", "lrs", "lrt", "cms", "cmt",
                 "masks", "maskt", "load_lrt", "image_width", "image_siz")

    def __init__(self, **fields):
        for name in self.__slots__:
            setattr(self, name, fields[name])


def load_texture_block(width, height, texture_format, clamp_s=False,
                       clamp_t=False, swapped=False) -> Tiles:
    """The tiles ``material_init``'s ``gDPLoadTextureBlock`` leaves behind.

    ``gDPSetTextureImage`` is given width 1 and the load-block size;
    ``gDPLoadBlock`` puts the DXT where the load tile's ``lrt`` goes; the render
    tile's ``line`` is the row in 64-bit words; ``gDPSetTileSize`` covers the
    whole image from 0,0. ``swapped`` is the ``...S`` variant, identical but
    for a DXT of zero.
    """
    width, height = int(width), int(height)
    fmt, siz = tile_format(texture_format)
    cms, cmt, masks, maskt = material_tile(width, height, clamp_s, clamp_t)
    if siz == G_IM_SIZ_4b:
        image_siz = G_IM_SIZ_16b
        dxt = 0 if swapped else _gbi_dxt_4b(width)
        line = ((width >> 1) + 7) >> 3
    else:
        image_siz = _LOAD_BLOCK[siz]
        dxt = 0 if swapped else _gbi_dxt(width, _BYTES[siz])
        line = ((width * _LINE_BYTES[siz]) + 7) >> 3
    return Tiles(
        fmt=fmt, siz=siz, line=line,
        uls=0, ult=0,
        lrs=(width - 1) << G_TEXTURE_IMAGE_FRAC,
        lrt=(height - 1) << G_TEXTURE_IMAGE_FRAC,
        cms=cms, cmt=cmt, masks=masks, maskt=maskt,
        load_lrt=dxt, image_width=1, image_siz=image_siz,
    )


def live_rectangle(tiles: Tiles):
    """``(width, height, bytesPerRow)`` - the patch's ``Block`` branch."""
    tile_width = max((tiles.lrs >> 2) - (tiles.uls >> 2), 0) + 1
    tile_height = max((tiles.lrt >> 2) - (tiles.ult >> 2), 0) + 1
    mask_width = tile_width if tiles.masks == 0 else 1 << tiles.masks
    mask_height = tile_height if tiles.maskt == 0 else 1 << tiles.maskt
    clamp_s = tiles.masks == 0 or bool(tiles.cms & G_TX_CLAMP)
    clamp_t = tiles.maskt == 0 or bool(tiles.cmt & G_TX_CLAMP)
    width = (min(mask_width, tile_width)
             if clamp_s and tile_width <= 256 else mask_width)
    height = (min(mask_height, tile_height)
              if (clamp_t and tile_height <= 256) or mask_height > 256
              else mask_height)
    if tiles.siz == G_IM_SIZ_32b:
        bytes_per_row = tiles.line << 4
    elif tiles.load_lrt == 0:
        bytes_per_row = tiles.line << 3
    else:
        dxt = tiles.load_lrt
        if dxt > 1:
            dxt = reverse_dxt(dxt, tiles.image_width, tiles.image_siz)
        bytes_per_row = dxt << 3
    return width, height, bytes_per_row


def _check_swapped_size(width, height, texture_format) -> None:
    """The limits a line-swapped texture answers to.

    Not :func:`..textures.check_size`'s: that one is a level texture's, which
    has to tile and so has power-of-two sides up to 64. A line-swapped one is a
    sprite - clamped, any size the header and texture memory hold, as the
    retail minimaps' 80x50 and 48x73 are.
    """
    if width <= 0 or height <= 0 or width > 255 or height > 255:
        raise IdentityError("%dx%d does not fit a TextureHeader" % (width, height))
    size = texel_bytes(width, height, texture_format)
    if size > TMEM_BYTES:
        raise IdentityError(
            "%dx%d in this format is %d bytes, past the %d of texture memory "
            "it is loaded into at once" % (width, height, size, TMEM_BYTES))


def derive(width, height, texture_format, clamp_s=False, clamp_t=False,
           swapped=False):
    """``(fmt, siz, width, height, bytes_per_row)`` the live CRC will use.

    Raises :class:`IdentityError` for a texture whose identity cannot be known
    offline, with the reason. There are two, and the second is the one that
    matters:

    * a row under **four** bytes, where the CRC's inner loop never runs and the
      identity is a function of the height alone - every such texture of that
      height would share it;
    * a row under **eight** bytes - one 64-bit word of texture memory. A load
      block moves whole words, so the game reads such a texture with a stride
      of eight and the rows the CRC hashes run past the end of the image, into
      whatever the loader put after it.
    """
    width, height = int(width), int(height)
    if swapped:
        _check_swapped_size(width, height, texture_format)
    else:
        try:
            check_size(width, height, texture_format)
        except TextureEncodeError as error:
            raise IdentityError(str(error))

    tiles = load_texture_block(width, height, texture_format, clamp_s, clamp_t,
                               swapped)
    live_width, live_height, stride = live_rectangle(tiles)
    line_bytes = (live_width << tiles.siz) >> 1
    if line_bytes < 4:
        raise IdentityError(
            "a row of this texture is %d byte(s), and the CRC reads four at a "
            "time - it would hash nothing but the height, and every texture this "
            "tall would share one replacement" % line_bytes
        )
    read = (live_height - 1) * stride + line_bytes
    image = texel_bytes(width, height, texture_format)
    if read > image:
        raise IdentityError(
            "a row of this texture is %d bytes, narrower than the 8-byte word "
            "texture memory is loaded in, so the game reads it with a stride of "
            "%d and the CRC runs %d bytes past the image, into memory whose "
            "contents only exist at run time" % (line_bytes, stride, read - image)
        )
    return tiles.fmt, tiles.siz, live_width, live_height, stride


def hd_problem(width, height, texture_format, clamp_s=False,
               clamp_t=False, swapped=False) -> Optional[str]:
    """Why a texture this size cannot have a high-resolution version, or ``None``."""
    try:
        derive(width, height, texture_format, clamp_s, clamp_t, swapped)
    except IdentityError as error:
        return str(error)
    return None


def rice_identity(texels, width, height, texture_format, clamp_s=False,
                  clamp_t=False, swapped=False) -> str:
    """``<crc>#<fmt>#<siz>`` for these texels, exactly as RT64 will name them.

    ``texels`` are the bytes after the ``TextureHeader`` - what
    :func:`..textures.encode_texels` produces and ``textures/N.bin`` carries.
    For a ``swapped`` texture they are the payload's, odd rows already swapped.
    """
    fmt, siz, live_width, live_height, stride = derive(
        width, height, texture_format, clamp_s, clamp_t, swapped
    )
    if len(texels) < texel_bytes(width, height, texture_format):
        raise IdentityError(
            "%d bytes of texels for a %dx%d texture that needs %d"
            % (len(texels), width, height,
               texel_bytes(width, height, texture_format))
        )
    crc = rice_crc32(rice_word_order(texels), live_width, live_height, siz,
                     stride)
    return "%08x#%d#%d" % (crc, fmt, siz)


def free_nudge(texels, width, height, texture_format, taken, limit=4096):
    """``(nudge, identity)``: the first nudge whose identity nothing holds.

    ``texels`` are the un-nudged ones. Tried in turn rather than trusted,
    because a flip in the first word of a row can cancel out in the CRC - see
    :func:`..textures.nudge_texels`.
    """
    for nudge in range(1, int(limit) + 1):
        identity = rice_identity(
            nudge_texels(texels, width, height, texture_format, nudge),
            width, height, texture_format,
        )
        if identity not in taken:
            return nudge, identity
    raise IdentityError("no nudge of %d gave this texture a name of its own"
                        % limit)
