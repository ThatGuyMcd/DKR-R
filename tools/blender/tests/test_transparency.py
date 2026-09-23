"""Gate for how the addon makes the game draw a face see-through.

Four things have to hold.

**The pass rule is the game's.** ``render_level_segment`` draws a batch only in
the pass its texture and flags put it in, and a batch on the wrong side of
``numberofOpaqueBatches`` is never drawn. The addon now derives that side
instead of carrying it, so the derivation is held to every textured batch in
every extracted retail model, in both revisions: it has to put each one where
retail did.

**A cut-out is what retail says it is.** ``RENDER_CUTOUT`` sits on 302 retail
batches, all over a transparent texture - the shape :mod:`transparency` writes
for a cut-out of the track's own.

**``material_init``'s table is transcribed faithfully**, format by format.

**The pixels agree with the look, and nothing else moves.** A cut-out hardens
at half and spreads colour into its holes; a blend keeps its soft alpha unless
the format cannot; an opaque texture is solid; and a texture with no look - one
added before transparency existed - encodes exactly as it always did.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_transparency.py
"""

from __future__ import annotations

import glob
import os
import struct
import sys
import tempfile
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import assets, level_model, textures, transparency  # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)
    return condition


def trees():
    found = []
    for version in sorted(glob.glob(os.path.join(VANILLA, "*"))):
        tree = assets.AssetTree.find(version)
        if tree is not None:
            found.append(tree)
    return found


def write_png(path, width, height, rgba):
    """An eight-bit RGBA PNG, unfiltered."""
    rows = b"".join(b"\x00" + bytes(rgba[y * width * 4:(y + 1) * width * 4])
                    for y in range(height))

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6,
                                                0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(rows)))
        handle.write(chunk(b"IEND", b""))


# ---------------------------------------------------------------------------

def test_material_init_table():
    print("material_init's table")
    codes = textures.FORMAT_CODES
    for name in ("RGBA32", "RGBA16", "CI4"):
        check(transparency.translucent(codes[name], "TRANSPARENT"),
              "%s TRANSPARENT is see-through" % name)
        check(transparency.translucent(codes[name], "TRANSPARENT_2"),
              "%s TRANSPARENT_2 is see-through" % name)
        check(not transparency.translucent(codes[name], "OPAQUE"),
              "%s OPAQUE is solid" % name)
        check(not transparency.translucent(codes[name], "OPAQUE_2"),
              "%s OPAQUE_2 is solid" % name)
    for name in ("IA16", "IA8", "IA4"):
        check(transparency.translucent(codes[name], "OPAQUE"),
              "%s is see-through whatever the render mode says" % name)
    for name in ("I8", "I4", "CI8"):
        check(not transparency.translucent(codes[name], "TRANSPARENT"),
              "%s is never see-through" % name)


def test_pass_rule():
    print("the pass rule, against every retail batch")
    found = trees()
    if not found:
        print("  skip: no extracted assets")
        return
    for tree in found:
        catalogue = {entry.index: entry for entry in textures.catalogue(tree)}
        textured = wrong = cutouts = cutouts_translucent = 0
        for path in sorted(glob.glob(os.path.join(tree.root, "levels", "models",
                                                  "*", "*.bin"))):
            model = level_model.load(path)
            for segment in model.segments:
                for index, batch in enumerate(segment.batches):
                    texture = model.texture_for(batch)
                    entry = catalogue.get(texture.texture_id) if texture else None
                    if entry is None:
                        continue
                    textured += 1
                    side = transparency.draws_in_opaque_pass(batch.flags,
                                                             entry.translucent)
                    if side != (index < segment.opaque_batches):
                        wrong += 1
                    if batch.flags & transparency.RENDER_CUTOUT:
                        cutouts += 1
                        cutouts_translucent += entry.translucent
        label = os.path.basename(tree.root)
        check(textured > 10000,
              "%s: %d textured batches to hold the rule to" % (label, textured))
        check(wrong == 0,
              "%s: every one sits on the side the rule puts it (%d do not)"
              % (label, wrong))
        check(cutouts > 0 and cutouts == cutouts_translucent,
              "%s: all %d cut-out batches draw a see-through texture (%d do)"
              % (label, cutouts, cutouts_translucent))


def test_flags_and_modes():
    print("flags and looks")
    flags = 0x4200
    check(transparency.with_mode(flags, transparency.CUTOUT) == flags | 0x10,
          "a cut-out sets RENDER_CUTOUT and nothing else")
    check(transparency.with_mode(flags | 0x10, transparency.BLEND) == flags,
          "a blend clears it")
    check(transparency.with_mode(0x80000000 | 0x10, transparency.OPAQUE)
          == 0x80000000, "the high bits survive")
    check(transparency.draws_in_opaque_pass(0x10, False),
          "a cut-out over a solid texture is drawn with the solid track")
    check(not transparency.draws_in_opaque_pass(0x10, True),
          "and over a see-through one, in the second pass, as retail does")
    check(not transparency.draws_in_opaque_pass(0x2000, False),
          "water is always drawn in the second pass")
    check(transparency.draws_in_opaque_pass(0x2000 | 0x800, True),
          "a decal is always drawn in the first")
    check(transparency.face_mode(0x10, True) == transparency.CUTOUT,
          "the cut-out bit reads back as a cut-out")
    check(transparency.face_mode(0, True) == transparency.BLEND,
          "a see-through texture without it reads as a blend")

    codes = textures.FORMAT_CODES
    check(transparency.own_modes(codes["RGBA16"]) == transparency.MODES,
          "a colour texture of the track's own can take any look")
    check(transparency.own_modes(codes["IA8"])
          == (transparency.CUTOUT, transparency.BLEND),
          "a greyscale-with-alpha one cannot be opaque")
    check(transparency.own_modes(codes["I8"]) == (transparency.OPAQUE,),
          "a plain greyscale one can only be opaque")
    check(transparency.own_mode(transparency.OPAQUE, codes["IA16"])
          == transparency.BLEND, "asking IA16 for opaque settles on a blend")
    check(transparency.face_modes(codes["RGBA32"], "TRANSPARENT")
          == (transparency.CUTOUT, transparency.BLEND),
          "a face on a see-through ROM texture can be cut out or blended")
    check(transparency.face_modes(codes["RGBA16"], "OPAQUE")
          == (transparency.OPAQUE, transparency.CUTOUT),
          "a face on a solid one can be solid or cut out")
    check(transparency.render_mode_for(transparency.CUTOUT) == "TRANSPARENT"
          and transparency.render_mode_for(transparency.BLEND) == "TRANSPARENT"
          and transparency.render_mode_for(transparency.OPAQUE) == "OPAQUE",
          "cut-outs and blends are written TRANSPARENT, as retail's are")


def test_suggest():
    print("reading a picture's alpha")
    check(transparency.suggest(100, 0, 0) == transparency.OPAQUE,
          "no transparency is opaque")
    check(transparency.suggest(70, 25, 5) == transparency.CUTOUT,
          "holes with a thin soft edge are a cut-out")
    check(transparency.suggest(0, 0, 100) == transparency.BLEND,
          "uniform half alpha is a blend")
    check(transparency.suggest(40, 20, 40) == transparency.BLEND,
          "a wide soft band is a blend")
    rgba = bytes([10, 20, 30, 255] * 3 + [0, 0, 0, 0])
    check(transparency.suggest_rgba(rgba) == transparency.CUTOUT,
          "suggest_rgba reads the fourth byte of each pixel")


def test_prepare():
    print("making the pixels agree with the look")
    codes = textures.FORMAT_CODES
    # A 4x4: red where solid, black in the holes, one half-covered pixel.
    rgba = bytearray()
    for y in range(4):
        for x in range(4):
            if x < 2:
                rgba += bytes((200, 0, 0, 255))
            elif (x, y) == (2, 0):
                rgba += bytes((200, 0, 0, 90))
            else:
                rgba += bytes((0, 0, 0, 0))
    rgba = bytes(rgba)

    same = transparency.prepare(rgba, 4, 4, None, codes["RGBA16"])
    check(same == rgba, "no look leaves the picture exactly as it was")

    solid = transparency.prepare(rgba, 4, 4, transparency.OPAQUE, codes["RGBA16"])
    check(all(solid[i] == 255 for i in range(3, len(solid), 4)),
          "opaque makes every pixel solid")

    cut = transparency.prepare(rgba, 4, 4, transparency.CUTOUT, codes["RGBA32"])
    alphas = set(cut[3::4])
    check(alphas <= {0, 255}, "a cut-out has only solid and clear pixels")
    check(cut[2 * 4 + 3] == 0, "and a pixel under half alpha is a hole")
    reds = [cut[i * 4] for i in range(16) if cut[i * 4 + 3] == 0]
    check(all(red == 200 for red in reds),
          "every hole took the colour of the red beside it (%r)" % reds)

    blended = transparency.prepare(rgba, 4, 4, transparency.BLEND, codes["RGBA32"])
    check(blended[2 * 4 + 3] == 90,
          "a blend in RGBA32 keeps the soft alpha")
    check(all(blended[i + 3] == rgba[i + 3] for i in range(0, 64, 4)),
          "and never changes any alpha")
    one_bit = transparency.prepare(rgba, 4, 4, transparency.BLEND, codes["RGBA16"])
    check(set(one_bit[3::4]) <= {0, 255},
          "a blend in RGBA16, which keeps one bit, is hardened too")

    grey = transparency.prepare(rgba, 4, 4, transparency.OPAQUE, codes["I8"])
    check(grey == rgba, "a format with no alpha is left alone")


def test_bleed_wraps():
    print("colour spreads across the wrap")
    rgba = bytearray(bytes((0, 0, 0, 0)) * 16)
    # One green pixel at the right edge; the hole at the left edge touches it
    # only through the wrap.
    rgba[(1 * 4 + 3) * 4:(1 * 4 + 3) * 4 + 4] = bytes((0, 250, 0, 255))
    transparency.bleed(rgba, 4, 4, passes=1)
    check(rgba[(1 * 4 + 0) * 4 + 1] == 250,
          "the texel across the wrap took the green")
    check(rgba[(1 * 4 + 3) * 4 + 3] == 255 and rgba[(1 * 4 + 0) * 4 + 3] == 0,
          "and no alpha changed")


def test_encoding():
    print("the encoder with a look")
    codes = textures.FORMAT_CODES
    rgba = bytes([255, 255, 255, 255] * 2 + [255, 255, 255, 60] * 2 + [0, 0, 0, 0] * 12)
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "fence.png")
        write_png(path, 4, 4, rgba)
        plain = textures.encode_texture(path, codes["RGBA16"])
        check(plain[2] == 0x11, "no look: OPAQUE render mode, as before")
        # rgba2raw: alpha 60 is "any alpha", so the bit is set.
        check(plain[32 + 2 * 2 + 1] & 1 == 1,
              "and the soft pixel's alpha bit is set, as the asset tool sets it")

        cut = textures.encode_texture(path, codes["RGBA16"], "TRANSPARENT",
                                      transparency_mode=transparency.CUTOUT)
        check(cut[2] == 0x01, "a cut-out is written TRANSPARENT RGBA16")
        check(cut[32 + 2 * 2 + 1] & 1 == 0,
              "and the soft pixel became a hole")
        check(cut[32 + 1] & 1 == 1, "while a solid one stayed solid")

        entry = textures.CustomTexture(0, "fence", path, 4, 4, codes["RGBA16"],
                                       transparency_mode=transparency.CUTOUT)
        check(entry.render_mode == "TRANSPARENT" and entry.translucent,
              "a cut-out texture of the track's own is see-through to the game")
        check(entry.encode() == cut,
              "and encodes to the same bytes through the entry")
        check(entry.texels() == cut[32:32 + 32],
              "and the texels the HD pack is named by are those bytes")

        legacy = textures.CustomTexture(0, "fence", path, 4, 4, codes["RGBA16"])
        check(legacy.mode is None and legacy.render_mode == "OPAQUE"
              and legacy.encode() == plain,
              "a texture from before transparency encodes as it always did")
        check(legacy.transparency == transparency.OPAQUE,
              "and reads as opaque")

        grey = textures.CustomTexture(0, "smoke", path, 4, 4, codes["IA8"],
                                      transparency_mode=transparency.OPAQUE)
        check(grey.transparency == transparency.BLEND,
              "an IA8 texture asked to be opaque is a blend, which it is")


def _loader_alpha(colour):
    """What ``track_init_level_model`` makes of a vertex: ``(alpha, faded)``."""
    r, g, b, a = colour
    return (b, True) if r == 1 and g == 1 else (a, False)


def test_opacity():
    print("opacity, through the loader's vertex alpha")
    check(transparency.opacity_byte(1.0) == 255
          and transparency.opacity_byte(0.5) == 128
          and transparency.opacity_byte(0.0) == 0
          and transparency.opacity_byte(2.0) == 255,
          "an opacity becomes a byte, clamped")
    check(_loader_alpha(transparency.vertex_alpha_colour(77)) == (77, True),
          "the colour written is the one the loader reads as that alpha")

    from types import SimpleNamespace as NS
    lit = (200, 180, 160, 255)
    segment = NS(
        colours=[lit] * 7,
        batches=[NS(texture_index=0, vertex_offset=0, vertex_count=3, flags=0),
                 NS(texture_index=1, vertex_offset=3, vertex_count=4, flags=0)])
    model = NS(segments=[segment])
    faded = transparency.apply_opacity(
        model, lambda batch: {1: 100}.get(batch.texture_index))
    check(faded == (1, 4), "only the faded batch is touched: %r" % (faded,))
    check(segment.colours[:3] == [lit] * 3,
          "the other batch keeps its baked light")
    check(all(_loader_alpha(c) == (100, True) for c in segment.colours[3:]),
          "every vertex of the faded one reads back as its alpha")
    check(transparency.apply_opacity(model, lambda batch: 255) == (0, 0)
          and transparency.apply_opacity(model, lambda batch: None) == (0, 0),
          "full opacity, or none asked for, changes nothing")
    check(transparency.draws_in_opaque_pass(transparency.RENDER_VTX_ALPHA,
                                            False),
          "a faded batch over an opaque texture stays in the opaque pass, "
          "as retail's 195 do")

    found = trees()
    if not found:
        print("  skip: no extracted assets for the file round trip")
        return
    from dkr_track_editor import level_model_encoder  # noqa: PLC0415
    path = sorted(glob.glob(os.path.join(found[0].root, "levels", "models",
                                         "*", "*.bin")))[0]
    model = level_model.load(path)
    target = model.segments[0].batches[0]
    transparency.apply_opacity(
        model, lambda batch: 64 if batch is target else None)
    with tempfile.TemporaryDirectory() as folder:
        out = os.path.join(folder, "faded.bin")
        with open(out, "wb") as handle:
            handle.write(level_model_encoder.pack(model))
        again = level_model.load(out)
    segment = again.segments[0]
    window = segment.colours[target.vertex_offset:
                             target.vertex_offset + target.vertex_count]
    check(window and all(_loader_alpha(c) == (64, True) for c in window),
          "the fade survives being written and read back")


def main():
    test_material_init_table()
    test_opacity()
    test_pass_rule()
    test_flags_and_modes()
    test_suggest()
    test_prepare()
    test_bleed_wraps()
    test_encoding()
    print()
    if FAILURES:
        print("FAIL: %d check(s)" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: transparency")
    return 0


if __name__ == "__main__":
    sys.exit(main())
