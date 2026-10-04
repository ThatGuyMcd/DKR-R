"""Gate for the high-resolution texture pack: the names in it are RT64's.

A pack entry only works if its name is the identity RT64 computes, live, for
the 64x32 the track ships - and a wrong name does not fail, it just finds no
replacement. So everything here is about the name being *exactly* right, from
two directions.

**The arithmetic is the patch's.** ``riceCRC32`` and ``reverseDXT`` are
extracted from ``patches/rt64/0011-enable-runtime-rice-texture-aliases.patch``
as the text of the patch itself, compiled, and run against the Python
transcription over thousands of pseudo-random buffers of every size and stride.
The same harness compiles DKR-R's own ``rice_texture_pack_policy.hpp`` and asks
its ``parse_filename`` to read back every name the pack writes. That needs a C++
compiler - ``DKR_CXX``, or ``g++``/``clang++``/``c++`` on the path - and is
reported as skipped without one. The known answers below were produced by the
compiled patch and keep the transcription honest either way.

**The rectangle is the renderer's.** The CRC runs over a width, height and row
stride the patch derives from the tiles ``material_init`` sets up - mask, clamp
bits, the DXT reversed by search - not from the image. Every size and format a
custom texture may take is pushed through that derivation, with every
combination of wrap flags, and it must land on the image's own size: that is
the invariant ``textures.py`` declares beside ``MAX_WRAP_SIZE``. The ones it
cannot land on - rows narrower than a word of texture memory - must be refused
with the reason, because their identity would be a hash of memory the addon
never wrote.

**The line-swapped load is retail's.** A minimap is loaded with
``gDPLoadTextureBlockS`` - a DXT of zero, odd rows swapped in RDRAM. Every
retail minimap is re-encoded from the extracted tree and must come out under the
name a published Rice pack (DKR REMASTERED) gives its replacement; skipped
without the tree.

    python tools/blender/tests/test_rice_identity.py
"""

from __future__ import annotations

import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import zipfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from track_lab import minimap, rice_identity, rice_pack, textures  # noqa: E402
from test_custom_textures import solid_rgba, write_png  # noqa: E402
from test_roundtrip import VANILLA  # noqa: E402

REPO = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
PATCH = os.path.join(REPO, "patches", "rt64",
                     "0011-enable-runtime-rice-texture-aliases.patch")
POLICY_DIR = os.path.join(REPO, "runtime-recomp", "src", "game")

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def equal(got, want, message):
    return check(got == want, "%s: got %r, wanted %r" % (message, got, want))


# ---------------------------------------------------------------------------
# Known answers, from the patch's own riceCRC32 compiled with g++
# ---------------------------------------------------------------------------

def known_buffer(length, seed):
    return bytes(((index * 37 + seed * 11 + (index >> 3)) & 0xFF)
                 for index in range(length))


#: ``(siz, width, height, stride, seed, crc)``. The buffer is
#: ``known_buffer((height - 1) * stride + bytes_per_line, seed)``.
KNOWN = [
    (2, 64, 32, 128, 1, 0xefd0e2c8),  # RGBA16 64x32, the ordinary custom texture
    (2, 32, 64, 64, 2, 0x06eb037a),   # the same on its side
    (3, 32, 32, 128, 3, 0xe55b5147),  # RGBA32
    (1, 64, 64, 64, 4, 0xcf44fa40),   # I8 / IA8 64x64
    (0, 64, 64, 32, 5, 0x25042155),   # I4 / IA4 64x64
    (2, 8, 4, 16, 6, 0x08841682),     # a small one
    (2, 16, 8, 40, 7, 0x53d9b379),    # a stride wider than the row
    (0, 4, 8, 8, 8, 0x0000001c),      # the degenerate row: height alone
]


def test_known_answers():
    for siz, width, height, stride, seed, crc in KNOWN:
        line = (width << siz) >> 1
        data = known_buffer((height - 1) * stride + line, seed)
        got = rice_identity.rice_crc32(data, width, height, siz, stride)
        if crc is None:
            print("  known answer missing for %r: %08x" % ((siz, width, height,
                                                             stride, seed), got))
            continue
        equal("%08x" % got, "%08x" % crc,
              "riceCRC32 siz %d %dx%d stride %d" % (siz, width, height, stride))


def test_degenerate_row_hashes_only_the_height():
    """The review's bug: a 4-bit texture four wide never enters the inner loop."""
    first = rice_identity.rice_crc32(bytes(16), 4, 8, 0, 2)
    second = rice_identity.rice_crc32(bytes(range(16)), 4, 8, 0, 2)
    equal(first, second, "two different 4x8 I4 images share one CRC")
    equal(first, sum(range(8)), "and it is the sum of the row numbers")


# ---------------------------------------------------------------------------
# The view riceCRC32 actually reads: RDRAM, not the payload's own bytes
# ---------------------------------------------------------------------------
#
# A live run of a track shipping two 64x32 RGBA16 textures found the game
# computing identities the addon's export never named - every HD pack was
# therefore invisible, in every export, silently: DKR-R accepted the pack
# and drew the 64x32 with no error anywhere. Root cause: riceCRC32 reads
# state->RDRAM, and N64Recomp stores RDRAM word-byte-swapped on a
# little-endian host (recomp.h's MEM_B: logical byte A lands at raw A ^ 3).
# rice_identity() was hashing the payload's own (unswapped) bytes.

def test_rice_word_order_matches_mem_b():
    """Byte j of a 32-bit word lands at raw position j ^ 3 - by hand, not by
    calling rice_word_order, so a sign error in it cannot hide."""
    logical = bytes(range(16))  # four words: 00 01 02 03 | 04 05 06 07 | ...
    expected = bytearray(len(logical))
    for offset, value in enumerate(logical):
        expected[(offset & ~3) + ((offset & 3) ^ 3)] = value
    equal(rice_identity.rice_word_order(logical), bytes(expected),
          "each 32-bit word is stored MEM_B-reversed (byte j at raw j ^ 3)")
    equal(rice_identity.rice_word_order(bytes(expected)), logical,
          "the swap is its own inverse")


def test_rice_identity_reads_the_rdram_view():
    """riceCRC32's answer for the RDRAM view differs from - and is what
    matters, not - its answer for the payload's own byte order.

    The unswapped CRC is KNOWN's existing 64x32 RGBA16 entry; the swapped one
    is the known answer rice_identity() must produce, since that is the
    bytes the game actually hashes. A regression that stops swapping, or
    swaps some other way, silently reproduces the bug this test file exists
    to catch.
    """
    data = known_buffer((32 - 1) * 128 + 128, 1)
    equal(rice_identity.rice_crc32(data, 64, 32, 2, 128), 0xefd0e2c8,
          "sanity: this is KNOWN's own 64x32 RGBA16 fixture")
    swapped_crc = rice_identity.rice_crc32(
        rice_identity.rice_word_order(data), 64, 32, 2, 128)
    equal(swapped_crc, 0x9ceb16c0,
          "the RDRAM-view CRC riceCRC32 actually produces for these texels")


# ---------------------------------------------------------------------------
# The rectangle, as the renderer derives it
# ---------------------------------------------------------------------------

CLAMPS = [(False, False), (True, False), (False, True), (True, True)]


def test_rectangle_is_the_image():
    """Every accepted size lands on itself, or is refused for a stated reason."""
    accepted = refused = 0
    for name in textures.CUSTOM_FORMATS:
        code = textures.FORMAT_CODES[name]
        fmt, siz = rice_identity.tile_format(code)
        for width, height in textures.usable_sizes(code):
            row = textures.texel_bytes(width, 1, code)
            for clamp_s, clamp_t in CLAMPS:
                label = "%s %dx%d clamp %s/%s" % (name, width, height,
                                                   clamp_s, clamp_t)
                problem = rice_identity.hd_problem(width, height, code,
                                                   clamp_s, clamp_t)
                if row < rice_identity_word():
                    check(problem is not None,
                          "%s: a %d-byte row must be refused" % (label, row))
                    refused += 1
                    continue
                if not check(problem is None, "%s: %s" % (label, problem)):
                    continue
                got = rice_identity.derive(width, height, code, clamp_s, clamp_t)
                equal(got, (fmt, siz, width, height, row),
                      "%s derives the image's own rectangle" % label)
                accepted += 1
    check(accepted > 100 and refused > 0,
          "the sweep covered both sides (%d accepted, %d refused)"
          % (accepted, refused))


def rice_identity_word():
    return 8


def test_named_cases():
    rgba16 = textures.FORMAT_CODES["RGBA16"]
    equal(rice_identity.derive(64, 32, rgba16), (0, 2, 64, 32, 128),
          "64x32 RGBA16: 16 words a row, DXT 128 reverses to 16")
    equal(rice_identity.material_tile(64, 32),
          (rice_identity.G_TX_WRAP, rice_identity.G_TX_WRAP, 6, 5),
          "material_init masks a 64x32 at 6 and 5 and wraps both ways")
    equal(rice_identity.material_tile(64, 32, clamp_s=True),
          (rice_identity.G_TX_CLAMP, rice_identity.G_TX_WRAP, 0, 5),
          "a clamp flag clamps with no mask")
    equal(rice_identity.reverse_dxt(128, 1, 2), 16, "reverseDXT(128)")
    equal(rice_identity.reverse_dxt(2048, 1, 2), 1, "reverseDXT(0x800)")

    problem = rice_identity.hd_problem(8, 8, textures.FORMAT_CODES["I4"])
    check(problem is not None and "zero" in problem,
          "an I4 texture 8 wide: CALC_DXT_4b divides by zero (%r)" % problem)
    problem = rice_identity.hd_problem(4, 8, textures.FORMAT_CODES["I8"])
    check(problem is not None and "stride of 8" in problem,
          "an I8 texture 4 wide is read with an 8-byte stride (%r)" % problem)
    check(rice_identity.hd_problem(16, 16, textures.FORMAT_CODES["I4"]) is None,
          "an I4 texture 16 wide is a whole word a row and is fine")
    try:
        rice_identity.tile_format(textures.FORMAT_CODES["CI8"])
        check(False, "a colour-indexed format has no three-part identity")
    except rice_identity.IdentityError:
        pass


# ---------------------------------------------------------------------------
# Line-swapped textures: the minimap
# ---------------------------------------------------------------------------

#: The CRC in each retail minimap's file name in DKR REMASTERED V0.2.0
#: (SR.GU's), ``Maps/Diddy Kong Racing#<crc>#3#1_all.png`` - a pack made on an
#: emulator, by Rice's own arithmetic, with no knowledge of this addon. The
#: three ``unused`` maps are not in it; the game never draws them.
RETAIL_MINIMAPS = {
    "ancient_lake": "328bf13c",
    "bluey": "0cb0c140",
    "boulder_canyon": "3ec8adf9",
    "bubbler": "8fb99a2d",
    "central_area": "a1818eb7",
    "crescent_island": "f3b43063",
    "darkmoon_caverns": "ffd2bd3d",
    "darkwater_beach": "dc5b3910",
    "everfrost_peak": "b8258f58",
    "fire_mountain": "aaa6fb06",
    "fossil_canyon": "bcb5cd36",
    "frosty_village": "818011de",
    "greenwood_village": "6bedc17b",
    "haunted_woods": "63c11aa7",
    "hot_top_volcano": "344f86e0",
    "icicle_pyramid": "147df67d",
    "jungle_falls": "ad85155c",
    "npc": "6690a158",
    "pirate_lagoon": "4e2a0b0a",
    "player": "d9fd385b",
    "smokey": "fdd7a53f",
    "smokey_castle": "5caf47f4",
    "snowball_valley": "781db064",
    "space_port_alpha": "11efe9bf",
    "spacedust_alley": "d6008756",
    "star_city": "f6d18432",
    "treasure_caves": "31dd1166",
    "trickytops": "7ecf700e",
    "walrus_cove": "5ed9a2a0",
    "whale_bay": "cec4c946",
    "windmill_plains": "98422f42",
    "wizpig1": "6fce8413",
    "wizpig2": "4d2adbe7",
}


def test_swapped_rectangle():
    """A DXT of zero takes the row from the render tile, and any clamped size
    the header holds is fine - the minimap's sizes are not powers of two."""
    ia8 = textures.FORMAT_CODES["IA8"]
    tiles = rice_identity.load_texture_block(80, 50, ia8, True, True, swapped=True)
    equal(tiles.load_lrt, 0, "gDPLoadTextureBlockS passes gDPLoadBlock a DXT of 0")
    equal(rice_identity.derive(80, 50, ia8, True, True, swapped=True),
          (3, 1, 80, 50, 80), "an 80x50 IA8 minimap is its own rectangle")
    check(rice_identity.hd_problem(80, 50, ia8, True, True) is not None,
          "and the level-texture rules would refuse it")
    problem = rice_identity.hd_problem(12, 4, ia8, True, True, swapped=True)
    check(problem is not None and "stride of 16" in problem,
          "a row that is not whole TMEM words is read past its end (%r)" % problem)
    problem = rice_identity.hd_problem(80, 60, ia8, True, True, swapped=True)
    check(problem is not None and "texture memory" in problem,
          "past 4096 bytes does not fit TMEM (%r)" % problem)


def test_retail_minimaps():
    folder = None
    for version in ("us.v77", "us.v80"):
        candidate = os.path.join(VANILLA, version, "textures", "2d", "minimap")
        if os.path.isdir(candidate):
            folder = candidate
            break
    if folder is None:
        print("  skip: no extracted asset tree for the retail minimaps")
        return
    found = 0
    for name, crc in sorted(RETAIL_MINIMAPS.items()):
        try:
            with open(os.path.join(folder, name + ".json"), "r",
                      encoding="utf-8") as handle:
                meta = json.load(handle)
            width, height, rgba = textures.read_png(
                os.path.join(folder, meta["images"][0]))
        except (OSError, ValueError, KeyError) as error:
            check(False, "%s could not be read: %s" % (name, error))
            continue
        payload = minimap.texture_payload(rgba, width, height)
        equal(minimap.texture_identity(payload), "%s#3#1" % crc,
              "%s (%dx%d) is named as the published pack names it"
              % (name, width, height))
        found += 1
    equal(found, len(RETAIL_MINIMAPS), "every retail minimap was checked")


# ---------------------------------------------------------------------------
# Collisions
# ---------------------------------------------------------------------------

def test_nudge():
    """Two equal reductions can be told apart, by a bit nobody can see.

    Not every nudge gives a new name, and that is the CRC's doing: the word at
    the start of each row is added twice, the second time XORed with the row
    number, so a flip there whose bit the row number also has cancels out. So
    the export does not trust a nudge - it tries them in turn until the name is
    one nothing else holds, which is what :func:`free_nudge` is held to here.
    """
    rng = random.Random(7)
    for name in textures.CUSTOM_FORMATS:
        code = textures.FORMAT_CODES[name]
        width, height = textures.largest_size(code)
        texels = bytes(rng.randrange(256)
                       for _ in range(textures.texel_bytes(width, height, code)))
        for nudge in range(0, 12):
            moved = textures.nudge_texels(texels, width, height, code, nudge)
            changed = sum(bin(a ^ b).count("1") for a, b in zip(texels, moved))
            equal(changed, 0 if nudge == 0 else 1,
                  "%s nudge %d flips exactly one bit" % (name, nudge))

        taken = {rice_identity.rice_identity(texels, width, height, code)}
        for _round in range(12):
            nudge, identity = rice_identity.free_nudge(texels, width, height,
                                                       code, taken)
            check(nudge >= 1 and identity not in taken,
                  "%s: a free name was found (%d, %s)" % (name, nudge, identity))
            equal(identity, rice_identity.rice_identity(
                textures.nudge_texels(texels, width, height, code, nudge),
                width, height, code), "%s: and it is that nudge's" % name)
            taken.add(identity)
        equal(len(taken), 13, "%s: twelve textures told apart from one" % name)

    # RGBA16's lowest bit is alpha; the nudge must never touch it.
    code = textures.FORMAT_CODES["RGBA16"]
    texels = bytes(64 * 32 * 2)
    for nudge in range(1, 40):
        moved = textures.nudge_texels(texels, 64, 32, code, nudge)
        check(all(moved[at] & 1 == 0 for at in range(1, len(moved), 2)),
              "RGBA16 nudge %d leaves every alpha bit alone" % nudge)


def test_custom_texture_texels_are_the_payloads(tmp):
    path = write_png(os.path.join(tmp, "same.png"), 64, 32,
                     solid_rgba(64, 32, (90, 140, 30, 255)))
    for nudge in (0, 3):
        entry = textures.CustomTexture(0, "same", path, 64, 32,
                                       textures.FORMAT_CODES["RGBA16"],
                                       nudge=nudge)
        payload = entry.encode()
        equal(payload[32:32 + 64 * 32 * 2], entry.texels(),
              "texels() is what encode() ships, nudge %d" % nudge)


# ---------------------------------------------------------------------------
# The pack
# ---------------------------------------------------------------------------

def test_pack(tmp):
    first = write_png(os.path.join(tmp, "first.png"), 300, 170,
                      solid_rgba(300, 170, (200, 10, 10, 255)))
    second = write_png(os.path.join(tmp, "second.png"), 170, 300,
                       solid_rgba(170, 300, (10, 200, 10, 255)))
    ids = ["0badf00d#0#2", "12345678#4#1"]
    target = os.path.join(tmp, "track-hd.zip")
    written = rice_pack.write_pack(target, list(zip(ids, (first, second))),
                                   stamp={"track": "t", "textureDigest": "ab"})
    equal(written["count"], 2, "two replacements")
    with zipfile.ZipFile(target) as archive:
        names = sorted(archive.namelist())
        equal(names, sorted([rice_pack.entry_name(i) for i in ids]
                            + [rice_pack.STAMP_NAME]),
              "one Rice PNG per identity, and the stamp")
        with open(first, "rb") as handle:
            equal(archive.read(rice_pack.entry_name(ids[0])), handle.read(),
                  "the original goes in untouched")
    equal(rice_pack.read_stamp(target).get("textureDigest"), "ab",
          "the stamp reads back")
    equal(rice_pack.entry_name(ids[0]),
          "Diddy Kong Racing#0badf00d#0#2_all.png", "the file name")

    # The same texture twice, with the same original, is one entry.
    rice_pack.write_pack(target, [(ids[0], first), (ids[0], first)])
    with zipfile.ZipFile(target) as archive:
        equal(len(archive.namelist()), 1, "a repeated identity is written once")

    # With two different originals it is a choice nobody made - refused.
    try:
        rice_pack.write_pack(target, [(ids[0], first), (ids[0], second)])
        check(False, "two originals for one identity must be refused")
    except rice_pack.PackError:
        pass
    with zipfile.ZipFile(target) as archive:
        equal(len(archive.namelist()), 1,
              "and the refusal leaves the previous pack in place")

    jpeg = os.path.join(tmp, "photo.jpg")
    with open(jpeg, "wb") as handle:
        handle.write(b"\xff\xd8\xff\xe0 not a png")
    check(rice_pack.image_problem(jpeg) is not None, "a JPEG is not a pack PNG")
    check(rice_pack.image_problem(first) is None, "a PNG is")

    one = rice_pack.texture_digest([b"a", b"bc"])
    equal(one, rice_pack.texture_digest([b"a", b"bc"]), "the digest is stable")
    check(one != rice_pack.texture_digest([b"bc", b"a"]),
          "and depends on order, which is identity")
    check(one != rice_pack.texture_digest([b"ab", b"c"]),
          "and on where one payload ends")


# ---------------------------------------------------------------------------
# Against the patch's own code
# ---------------------------------------------------------------------------

HARNESS = r"""
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "rice_texture_pack_policy.hpp"

namespace patch {
%s
}

static int nibble(char c) {
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

int main() {
    std::string op;
    while (std::cin >> op) {
        if (op == "crc") {
            int siz, width, height, stride;
            std::string hex;
            std::cin >> siz >> width >> height >> stride >> hex;
            std::vector<uint8_t> bytes(hex.size() / 2);
            for (size_t i = 0; i < bytes.size(); i++) {
                bytes[i] = uint8_t((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
            }
            std::printf("%%08x\n", patch::riceCRC32(bytes.data(), width, height, siz, stride));
        } else if (op == "dxt") {
            unsigned value, width, size;
            std::cin >> value >> width >> size;
            std::printf("%%d\n", patch::reverseDXT(value, width, size));
        } else if (op == "name") {
            std::string name;
            std::getline(std::cin >> std::ws, name);
            const auto parsed = dkr::runtime::rice_texture::parse_filename(name);
            std::printf("%%s\n", parsed ? parsed->identity.c_str() : "-");
        }
    }
    return 0;
}
"""


def patch_functions():
    """``calculateDXT`` through ``riceCRC32``, as the patch adds them."""
    with open(PATCH, "r", encoding="utf-8") as handle:
        lines = handle.read().splitlines()
    start = next(i for i, line in enumerate(lines) if "uint32_t calculateDXT(" in line)
    end = next(i for i, line in enumerate(lines) if "uint8_t maxCI8(" in line)
    body = lines[start:end]
    if not all(line.startswith("+") for line in body):
        raise RuntimeError("the patch moved; the extracted span is not all added lines")
    return "\n".join(line[1:] for line in body)


def find_compiler():
    explicit = os.environ.get("DKR_CXX")
    if explicit:
        return explicit if os.path.isfile(explicit) or shutil.which(explicit) else None
    for name in ("g++", "clang++", "c++"):
        found = shutil.which(name)
        if found:
            return found
    return None


def _compile(compiler, source, exe, *flags):
    # A toolchain unpacked somewhere (w64devkit, a MinGW zip) finds its own
    # assembler and linker on the path, so its directory goes first.
    environment = dict(os.environ)
    directory = os.path.dirname(os.path.abspath(shutil.which(compiler) or compiler))
    environment["PATH"] = directory + os.pathsep + environment.get("PATH", "")
    return subprocess.run([compiler, *flags, source, "-o", exe],
                          capture_output=True, text=True, env=environment)


def build_harness(tmp):
    compiler = find_compiler()
    if compiler is None:
        return None, "no C++ compiler (set DKR_CXX to one)"
    suffix = ".exe" if os.name == "nt" else ""

    # A compiler that cannot build an empty program is an environment problem,
    # not evidence about the patch - skipped, and said so.
    probe = os.path.join(tmp, "probe.cpp")
    with open(probe, "w", encoding="utf-8") as handle:
        handle.write("int main() { return 0; }\n")
    completed = _compile(compiler, probe, os.path.join(tmp, "probe" + suffix))
    if completed.returncode != 0:
        return None, "%s cannot build a program here: %s" % (
            compiler, (completed.stderr.strip().splitlines() or ["?"])[-1])

    source = os.path.join(tmp, "rice_harness.cpp")
    exe = os.path.join(tmp, "rice_harness" + suffix)
    with open(source, "w", encoding="utf-8") as handle:
        handle.write(HARNESS % patch_functions())
    completed = _compile(compiler, source, exe, "-std=c++20", "-O2",
                         "-I", POLICY_DIR)
    if completed.returncode != 0:
        FAILURES.append("the harness did not compile:\n%s" % completed.stderr[-2000:])
        return None, "compile failed"
    return exe, None


def run_harness(exe, script):
    completed = subprocess.run([exe], input=script, capture_output=True, text=True)
    if completed.returncode != 0:
        FAILURES.append("the harness crashed: %s" % completed.stderr[-500:])
        return []
    return completed.stdout.split("\n")


def test_against_the_patch(tmp, print_known=False):
    exe, why = build_harness(tmp)
    if exe is None:
        print("  skip: %s - the arithmetic is held to the known answers only" % why)
        return

    rng = random.Random(0x0DDC0DE)
    cases = []
    for _case in range(3000):
        siz = rng.randrange(4)
        width = 1 << rng.randrange(1, 8)
        height = rng.randrange(1, 65)
        line = (width << siz) >> 1
        stride = line + rng.choice((0, 0, 0, 1, 3, 8, 16, 40))
        length = max(1, (height - 1) * stride + line)
        data = bytes(rng.randrange(256) for _ in range(length))
        cases.append((siz, width, height, stride, data))
    for siz, width, height, stride, seed, _crc in KNOWN:
        line = (width << siz) >> 1
        cases.append((siz, width, height, stride,
                      known_buffer((height - 1) * stride + line, seed)))

    script = "".join("crc %d %d %d %d %s\n" % (siz, w, h, s, data.hex())
                     for siz, w, h, s, data in cases)
    lines = run_harness(exe, script)
    wrong = 0
    for index, (siz, w, h, s, data) in enumerate(cases):
        want = lines[index] if index < len(lines) else "<none>"
        got = "%08x" % rice_identity.rice_crc32(data, w, h, siz, s)
        if got != want:
            wrong += 1
            if wrong <= 5:
                FAILURES.append("riceCRC32 siz %d %dx%d stride %d: python %s, "
                                "patch %s" % (siz, w, h, s, got, want))
    equal(wrong, 0, "the transcription agrees with the patch on %d buffers"
          % len(cases))
    if print_known:
        for index, known in enumerate(KNOWN):
            print("    %r -> 0x%s" % (known[:5], lines[3000 + index]))

    dxt_cases = [(value, width, size)
                 for value in range(0, 2049)
                 for width in (1, 2, 4, 8, 16, 32, 64, 128)
                 for size in range(4)]
    lines = run_harness(exe, "".join("dxt %d %d %d\n" % case for case in dxt_cases))
    wrong = sum(1 for index, case in enumerate(dxt_cases)
                if index >= len(lines)
                or str(rice_identity.reverse_dxt(*case)) != lines[index])
    equal(wrong, 0, "reverseDXT agrees with the patch on %d cases" % len(dxt_cases))

    # Every name a pack can hold, read back by DKR-R's own importer policy.
    identities = []
    for name in textures.CUSTOM_FORMATS:
        fmt, siz = rice_identity.tile_format(textures.FORMAT_CODES[name])
        identities += ["%08x#%d#%d" % (rng.getrandbits(32), fmt, siz)
                       for _ in range(5)]
    names = [rice_pack.entry_name(identity) for identity in identities]
    lines = run_harness(exe, "".join("name %s\n" % n for n in names)
                        + "name %s/%s\n" % ("nested dir", names[0]))
    for index, identity in enumerate(identities):
        equal(lines[index] if index < len(lines) else None, identity,
              "parse_filename reads %s back" % names[index])
    equal(lines[len(identities)] if len(lines) > len(identities) else None,
          identities[0], "and the same inside one directory of the zip")


def main(argv):
    with tempfile.TemporaryDirectory() as tmp:
        test_known_answers()
        test_degenerate_row_hashes_only_the_height()
        test_rice_word_order_matches_mem_b()
        test_rice_identity_reads_the_rdram_view()
        test_rectangle_is_the_image()
        test_named_cases()
        test_swapped_rectangle()
        test_retail_minimaps()
        test_nudge()
        test_custom_texture_texels_are_the_payloads(tmp)
        test_pack(tmp)
        test_against_the_patch(tmp, print_known="--print-known" in argv)

    if FAILURES:
        print("%d failure(s):" % len(FAILURES))
        for message in FAILURES:
            print("  - %s" % message)
        return 1
    print("a track's own textures get the names RT64 will look them up by")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
