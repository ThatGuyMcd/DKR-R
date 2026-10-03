"""Gate for a track shipping artwork the ROM does not hold.

The neighbouring suite checks that a track can name any of the ROM's 1401
textures. This one checks the step past that: an image the author brings
becoming an ``ASSET_TEXTURES_3D`` payload the game can load. Nothing here needs
an extraction or a ROM, so unlike its neighbour it always runs.

What it holds, and why each one is the thing that would break first.

**The bytes are the asset tool's.** ``dkr_assets_tool``'s
``BuildTexture::build`` writes a 32-byte ``TextureHeader`` and then the image,
and every field's value is reproduced here from that source rather than from a
sample - width, height, the render mode in the high nibble of ``format``,
``numberOfInstances``, the wrap flags, ``numOfTextures`` as a *byte* at 0x12,
and ``textureSize`` as the 16-byte-aligned total. Getting 0x12 wrong is
invisible until the game reads ``numOfTextures >> 8`` and allocates space for
zero display lists.

**The texel conversions are n64graphics'.** ``rgba2raw``, ``i2raw`` and
``ia2raw`` are small and exact, including the rounding: RGBA16 is
``((v + 4) * 31) / 255`` a channel and one bit of alpha, and an intensity is
``(r + g + b + 1) / 3``. A conversion that is merely close produces a texture
that looks right and does not match what the same PNG produces through the
decomp, which is the only reference either has.

**The size limits are the hardware's.** The RDP has 4 KiB of texture memory and
``material_init`` loads a level texture as one block, so a 16-bit format stops
at 2048 texels; and its mask loop only walks powers of two up to 64, so a
larger side clamps rather than tiles however the flags are set. Both are
refusals rather than warnings, because both produce a track that looks broken
in a way that does not point at its cause.

**The PNG reader reads PNGs.** All five colour types, both byte depths and all
five scanline filters, checked by decoding images built here with each.

    python tools/blender/tests/test_custom_textures.py
"""

from __future__ import annotations

import os
import struct
import sys
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import textures  # noqa: E402

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def equal(got, want, message):
    return check(got == want, "%s: got %r, wanted %r" % (message, got, want))


# ---------------------------------------------------------------------------
# Building PNGs to read back
# ---------------------------------------------------------------------------

def write_png(path, width, height, rows, colour=6, depth=8, filters=None,
              palette=None, transparency=None):
    """A PNG with the scanline filters named, so the reader is held to all five.

    ``rows`` is one flat bytes-per-row list already in the file's own channel
    order; the filter is applied here rather than chosen by a library, which is
    the only way to reach filters 3 and 4 deliberately.
    """
    stride = len(rows[0]) if rows else 0
    step = max(1, (_channels(colour) * (2 if depth == 16 else 1)))
    filters = filters or [0] * height

    raw = bytearray()
    previous = bytearray(stride)
    for row, line in enumerate(rows):
        method = filters[row]
        encoded = bytearray(stride)
        for index in range(stride):
            left = line[index - step] if index >= step else 0
            up = previous[index]
            upleft = previous[index - step] if index >= step else 0
            if method == 0:
                encoded[index] = line[index]
            elif method == 1:
                encoded[index] = (line[index] - left) & 0xFF
            elif method == 2:
                encoded[index] = (line[index] - up) & 0xFF
            elif method == 3:
                encoded[index] = (line[index] - ((left + up) >> 1)) & 0xFF
            else:
                estimate = left + up - upleft
                near = [abs(estimate - left), abs(estimate - up),
                        abs(estimate - upleft)]
                if near[0] <= near[1] and near[0] <= near[2]:
                    nearest = left
                elif near[1] <= near[2]:
                    nearest = up
                else:
                    nearest = upleft
                encoded[index] = (line[index] - nearest) & 0xFF
        raw.append(method)
        raw += encoded
        previous = bytearray(line)

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    blob = b"\x89PNG\r\n\x1a\n"
    blob += chunk(b"IHDR",
                  struct.pack(">IIBBBBB", width, height, depth, colour, 0, 0, 0))
    if palette is not None:
        blob += chunk(b"PLTE", palette)
    if transparency is not None:
        blob += chunk(b"tRNS", transparency)
    blob += chunk(b"IDAT", zlib.compress(bytes(raw)))
    blob += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(blob)
    return path


def _channels(colour):
    return {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]


def solid_rgba(width, height, colour):
    return [bytes(colour) * width for _row in range(height)]


# ---------------------------------------------------------------------------
# The reference conversions, transcribed from the decomp
# ---------------------------------------------------------------------------

def reference_rgba16(red, green, blue, alpha):
    """``rgba2raw`` at depth 16, including ``SCALE_8_5``'s rounding."""
    scale = lambda value: ((value + 4) * 0x1F) // 0xFF  # noqa: E731
    red, green, blue = scale(red), scale(green), scale(blue)
    return bytes(((red << 3) | (green >> 2),
                  ((green & 0x3) << 6) | (blue << 1) | (1 if alpha else 0)))


def reference_intensity(red, green, blue):
    """``png2ia``: the mean, rounded up where appropriate."""
    return (red + green + blue + 1) // 3


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_png_reader(tmp):
    """Every colour type, both depths and all five filters decode."""
    pixels = [(10, 20, 30, 255), (200, 100, 50, 128),
              (0, 0, 0, 0), (255, 255, 255, 255)]

    rows = [b"".join(bytes(p) for p in pixels)]
    path = write_png(os.path.join(tmp, "rgba8.png"), 4, 1, rows)
    width, height, rgba = textures.read_png(path)
    equal((width, height), (4, 1), "RGBA8 size")
    equal(bytes(rgba), rows[0], "RGBA8 pixels")

    rows = [b"".join(bytes(p[:3]) for p in pixels)]
    path = write_png(os.path.join(tmp, "rgb8.png"), 4, 1, rows, colour=2)
    _w, _h, rgba = textures.read_png(path)
    equal(bytes(rgba), b"".join(bytes(p[:3]) + b"\xff" for p in pixels),
          "RGB8 fills alpha opaque")

    rows = [bytes((10, 255, 90, 0, 200, 128, 255, 255))]
    path = write_png(os.path.join(tmp, "ga8.png"), 4, 1, rows, colour=4)
    _w, _h, rgba = textures.read_png(path)
    equal(bytes(rgba),
          bytes((10, 10, 10, 255, 90, 90, 90, 0,
                 200, 200, 200, 128, 255, 255, 255, 255)),
          "grey+alpha spreads the intensity over RGB")

    rows = [bytes((0, 128, 255, 64))]
    path = write_png(os.path.join(tmp, "g8.png"), 4, 1, rows, colour=0)
    _w, _h, rgba = textures.read_png(path)
    equal(bytes(rgba),
          bytes((0, 0, 0, 255, 128, 128, 128, 255,
                 255, 255, 255, 255, 64, 64, 64, 255)),
          "greyscale")

    palette = bytes((10, 20, 30, 40, 50, 60, 70, 80, 90))
    rows = [bytes((0, 1, 2, 1))]
    path = write_png(os.path.join(tmp, "pal.png"), 4, 1, rows, colour=3,
                     palette=palette, transparency=bytes((255, 0, 128)))
    _w, _h, rgba = textures.read_png(path)
    equal(bytes(rgba),
          bytes((10, 20, 30, 255, 40, 50, 60, 0,
                 70, 80, 90, 128, 40, 50, 60, 0)),
          "palette with tRNS")

    # 16 bits a channel: the high byte is what a texture keeps.
    rows = [struct.pack(">8H", 0x1234, 0x5678, 0x9ABC, 0xDEF0,
                        0x1111, 0x2222, 0x3333, 0x4444)]
    path = write_png(os.path.join(tmp, "rgba16.png"), 2, 1, rows, depth=16)
    _w, _h, rgba = textures.read_png(path)
    equal(bytes(rgba), bytes((0x12, 0x56, 0x9A, 0xDE, 0x11, 0x22, 0x33, 0x44)),
          "16-bit channels truncate to the high byte")

    # All five filters, on an image whose rows differ so a filter that was
    # skipped or applied twice cannot come out right by accident.
    rows = [bytes(((row * 40 + column * 7) & 0xFF for column in range(16)))
            for row in range(5)]
    path = write_png(os.path.join(tmp, "filters.png"), 4, 5, rows,
                     filters=[0, 1, 2, 3, 4])
    _w, _h, rgba = textures.read_png(path)
    equal(bytes(rgba), b"".join(rows), "all five scanline filters")


def test_texel_formats():
    """Each conversion against the n64graphics routine it reproduces."""
    pixels = [(0, 0, 0, 255), (255, 255, 255, 255), (200, 100, 50, 255),
              (13, 200, 77, 0), (128, 128, 128, 200), (7, 3, 250, 255),
              (255, 0, 0, 1), (60, 61, 62, 255)]
    rgba = b"".join(bytes(p) for p in pixels)
    count = len(pixels)

    got = textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES["RGBA16"])
    want = b"".join(reference_rgba16(*p) for p in pixels)
    equal(got, want, "RGBA16")

    got = textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES["RGBA32"])
    equal(got, rgba, "RGBA32 is the pixels unchanged")

    got = textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES["I8"])
    want = bytes(reference_intensity(*p[:3]) for p in pixels)
    equal(got, want, "I8")

    got = textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES["IA16"])
    want = b"".join(bytes((reference_intensity(*p[:3]), p[3])) for p in pixels)
    equal(got, want, "IA16")

    got = textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES["IA8"])
    want = bytes(((reference_intensity(*p[:3]) // 0x11) << 4) | (p[3] // 0x11)
                 for p in pixels)
    equal(got, want, "IA8")

    # The two four-bit formats pack two texels a byte, high nibble first.
    got = textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES["I4"])
    nibbles = [reference_intensity(*p[:3]) // 0x11 for p in pixels]
    want = bytes((nibbles[i] << 4) | nibbles[i + 1]
                 for i in range(0, count, 2))
    equal(got, want, "I4")

    got = textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES["IA4"])
    nibbles = [((reference_intensity(*p[:3]) // 0x24) << 1) | (1 if p[3] else 0)
               for p in pixels]
    want = bytes((nibbles[i] << 4) | nibbles[i + 1]
                 for i in range(0, count, 2))
    equal(got, want, "IA4")

    # A colour-indexed texture would need a palette out of ASSET_EMPTY_14,
    # which a track cannot add, so it is refused rather than written wrong.
    for name in ("CI4", "CI8"):
        try:
            textures.encode_texels(rgba, count, 1, textures.FORMAT_CODES[name])
            check(False, "%s should have been refused" % name)
        except textures.TextureEncodeError:
            pass


def test_header(tmp):
    """The 32 bytes in front of the image, field by field."""
    path = write_png(os.path.join(tmp, "head.png"), 32, 16,
                     solid_rgba(32, 16, (200, 40, 90, 255)))
    payload = textures.encode_texture(path, textures.FORMAT_CODES["RGBA16"])

    equal(payload[0x00], 32, "width")
    equal(payload[0x01], 16, "height")
    # OPAQUE is render mode 1, so the byte is 0x10 | the format's low nibble.
    equal(payload[0x02], 0x11, "format byte is (render mode << 4) | format")
    equal(payload[0x03], 0, "spriteX")
    equal(payload[0x04], 0, "spriteY")
    equal(payload[0x05], 1, "numberOfInstances is always 1")
    equal(struct.unpack_from(">h", payload, 0x06)[0], 0, "flags, both wraps")
    equal(struct.unpack_from(">h", payload, 0x08)[0], 0, "ciPaletteOffset")
    equal(struct.unpack_from(">h", payload, 0x0A)[0], 0,
          "numberOfCommands is filled in RAM")
    equal(struct.unpack_from(">i", payload, 0x0C)[0], 0,
          "cmd is filled in RAM")
    # load_texture reads numOfTextures as a big-endian u16 and shifts it right
    # by eight, so the count is the byte at 0x12 and 0x13 must be clear.
    equal(payload[0x12], 1, "numOfTextures at 0x12")
    equal(payload[0x13], 0, "0x13 clear, or the count reads as animated")
    equal(struct.unpack_from(">H", payload, 0x12)[0] >> 8, 1,
          "load_texture's own reading of the frame count")
    equal(struct.unpack_from(">H", payload, 0x14)[0], 0, "frameAdvanceDelay")
    equal(struct.unpack_from(">h", payload, 0x16)[0], 32 + 32 * 16 * 2,
          "textureSize counts the header and is 16-aligned")
    equal(payload[0x1D], 0, "isCompressed")

    equal(len(payload), 32 + 32 * 16 * 2, "payload is header plus image")
    equal(len(payload) % 16, 0,
          "payload is 16-aligned, or load_texture's display list overruns "
          "its own allocation")
    check(len(payload) >= textures.TEMP_HEADER_SIZE,
          "payload must be at least the 40 bytes load_texture peeks")

    # The wrap flags are bits 6 and 7, and they are the only ones set.
    clamped = textures.texture_header(32, 16, 1, clamp_s=True, clamp_t=True)
    equal(struct.unpack_from(">h", clamped, 0x06)[0], 0xC0, "both clamp bits")
    transparent = textures.texture_header(32, 16, 1, render_mode="TRANSPARENT")
    equal(transparent[0x02], 0x01, "TRANSPARENT is render mode 0")


def test_pixels_reach_the_payload(tmp):
    """A known image comes out of the encoder as the texels it should be."""
    pixels = [(255, 0, 0, 255), (0, 255, 0, 255),
              (0, 0, 255, 255), (0, 0, 0, 0)]
    rows = [b"".join(bytes(p) for p in pixels)] * 4
    path = write_png(os.path.join(tmp, "known.png"), 4, 4, rows)

    payload = textures.encode_texture(path, textures.FORMAT_CODES["RGBA16"])
    image = payload[32:32 + 4 * 4 * 2]
    want = b"".join(reference_rgba16(*p) for p in pixels) * 4
    equal(image, want, "the image lands directly after the header")
    equal(len(payload), 32 + 32, "4x4 RGBA16 is 32 bytes of image")


def test_size_limits(tmp):
    """The two hardware limits are refusals, and they say which one was hit."""
    code = textures.FORMAT_CODES["RGBA16"]

    # 64x64 at 16 bits is 8192 bytes into 4096 of texture memory.
    path = write_png(os.path.join(tmp, "big.png"), 64, 64,
                     solid_rgba(64, 64, (1, 2, 3, 255)))
    try:
        textures.encode_texture(path, code)
        check(False, "64x64 RGBA16 should not fit texture memory")
    except textures.TextureEncodeError as error:
        check("64x32" in str(error),
              "the refusal should name a size that would fit: %s" % error)

    # The same pixels in an eight-bit format do fit.
    payload = textures.encode_texture(path, textures.FORMAT_CODES["I8"])
    equal(len(payload), 32 + 64 * 64, "64x64 I8 is exactly texture memory")

    path = write_png(os.path.join(tmp, "odd.png"), 24, 32,
                     solid_rgba(24, 32, (1, 2, 3, 255)))
    try:
        textures.encode_texture(path, code)
        check(False, "24 wide is not a power of two and should be refused")
    except textures.TextureEncodeError as error:
        check("power of two" in str(error), "refusal names the reason: %s" % error)

    path = write_png(os.path.join(tmp, "wide.png"), 128, 16,
                     solid_rgba(128, 16, (1, 2, 3, 255)))
    try:
        textures.encode_texture(path, code)
        check(False, "128 wide cannot tile and should be refused")
    except textures.TextureEncodeError as error:
        check("clamp" in str(error), "refusal names the reason: %s" % error)

    equal(textures.largest_size(code), (64, 32),
          "the largest tiling RGBA16 texture")
    equal(textures.largest_size(textures.FORMAT_CODES["I8"]), (64, 64),
          "eight bits a texel reaches 64x64")
    equal(textures.largest_size(textures.FORMAT_CODES["I4"]), (64, 64),
          "and four bits is capped by the wrap limit, not by memory")
    check((64, 64) not in textures.usable_sizes(code),
          "64x64 must not be offered for a 16-bit format")
    check(all(width <= 64 and height <= 64
              for width, height in textures.usable_sizes(code)),
          "no offered size may exceed the wrap limit")

    # Resampling picks the largest the format allows, then the shape closest to
    # the picture's - which for a 16-bit format is the whole of the choice
    # between a photograph laid along the road and the same one on its side.
    equal(textures.best_size(code, 2752, 1536), (64, 32), "a widescreen photo")
    equal(textures.best_size(code, 1536, 2752), (32, 64), "the same on its side")
    equal(textures.best_size(code, 1000, 1000), (64, 32),
          "a square picture still has to give up half its height")
    equal(textures.best_size(textures.FORMAT_CODES["I8"], 1000, 1000), (64, 64),
          "eight bits a texel can keep a square square")


def test_sentinel_ids():
    """The id a model stores for its own artwork, and its edges."""
    equal(textures.custom_id(0), 0x7000, "the first custom id")
    equal(textures.custom_id(5), 0x7005, "the sixth")
    check(textures.is_custom_id(0x7000), "0x7000 is custom")
    check(textures.is_custom_id(0x70FE), "the last one is custom")
    check(not textures.is_custom_id(0x70FF),
          "one past the last must not be, or a track could overrun the range")
    check(not textures.is_custom_id(1400), "a retail id is not custom")
    check(not textures.is_custom_id(-1), "no texture is not custom")
    check(not textures.is_custom_id(None), "a missing id is not custom")
    equal(textures.custom_ordinal(0x7003), 3, "the ordinal comes back")
    equal(textures.custom_ordinal(12), None, "a retail id has no ordinal")

    # load_texture masks the id with 0x7FFF after or-ing in 0x8000, so an id
    # that does not survive that would resolve to something else entirely.
    for ordinal in (0, 1, 254):
        identifier = textures.custom_id(ordinal)
        equal((identifier | 0x8000) & 0x7FFF, identifier,
              "id %d survives load_texture's masking" % identifier)

    try:
        textures.custom_id(textures.CUSTOM_ID_COUNT)
        check(False, "asking for one past the ceiling should be refused")
    except textures.TextureEncodeError:
        pass


def test_custom_texture_quacks(tmp):
    """A ``CustomTexture`` presents what the browser and the exporter read."""
    path = write_png(os.path.join(tmp, "quack.png"), 32, 32,
                     solid_rgba(32, 32, (9, 8, 7, 255)))
    entry = textures.CustomTexture(2, "Leaked", path, 32, 32,
                                   textures.FORMAT_CODES["RGBA16"])
    equal(entry.index, 0x7002, "index is the sentinel")
    equal(entry.frames, 1, "a still image has one frame")
    equal(entry.animated, False, "and is not animated")
    equal(entry.width, 32, "width")
    equal(entry.group, "yours", "grouped apart from the ROM's folders")
    check("Leaked" in entry.label, "the label names it")
    equal(len(entry.encode()), 32 + 32 * 32 * 2, "it encodes to its payload")

    # These five are what operators/textures.py reads off a browsed texture, so
    # a CustomTexture missing one would fail only when an author applied it.
    for field in ("index", "width", "height", "format", "frames", "png",
                  "name", "group", "animated", "label"):
        check(hasattr(entry, field), "a CustomTexture needs %s" % field)


def main():
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        test_png_reader(tmp)
        test_texel_formats()
        test_header(tmp)
        test_pixels_reach_the_payload(tmp)
        test_size_limits(tmp)
        test_sentinel_ids()
        test_custom_texture_quacks(tmp)

    if FAILURES:
        print("%d failure(s):" % len(FAILURES))
        for message in FAILURES:
            print("  - %s" % message)
        return 1
    print("an image becomes the ASSET_TEXTURES_3D payload the asset tool "
          "would have written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
