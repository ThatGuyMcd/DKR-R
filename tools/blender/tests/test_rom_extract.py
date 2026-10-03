"""Extract the asset tree from a ROM and hold it to the decomp's own extraction.

:mod:`dkr_track_editor.rom_extract` is a port of the decomp's asset tool, so the
decomp's extracted tree is the reference it has to reproduce. For every ROM
found in ``extern/dkr-decomp/baseroms`` this extracts into a temporary folder
and compares, for the asset types the addon reads:

* every JSON and glTF file, as data (and counts how many are byte-identical)
* every ``.bin``, byte for byte
* every PNG, pixel for pixel - the encoders differ, the pictures must not

and checks no file the reference holds for those types is missing. It also runs
the ROM-free parts: byte-order normalisation and refusing what is not a DKR ROM.

Runs on any Python 3.8+ with numpy; it does not need Blender.

    python tools/blender/tests/test_rom_extract.py
"""

from __future__ import annotations

import glob
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import assets, rom_extract, textures  # noqa: E402

from test_roundtrip import REPO_ROOT, VANILLA  # noqa: E402

BASEROMS = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "baseroms")

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


# ---------------------------------------------------------------------------
# ROM-free
# ---------------------------------------------------------------------------

def test_normalise():
    z64 = b"\x80\x37\x12\x40" + bytes(range(12))
    v64 = bytearray(z64)
    v64[0::2], v64[1::2] = z64[1::2], z64[0::2]
    n64 = bytearray(len(z64))
    for index in range(4):
        n64[index::4] = z64[3 - index::4]
    for raw, order in ((z64, "z64"), (bytes(v64), "v64"), (bytes(n64), "n64")):
        data, found = rom_extract.normalise(raw)
        check(found == order, "byte order %s detected as %s" % (order, found))
        check(data == z64, "%s normalises to z64" % order)
    try:
        rom_extract.normalise(b"PK\x03\x04" + bytes(12))
        check(False, "a zip is refused")
    except rom_extract.RomError as error:
        check("unzip" in str(error), "a zip is refused with a hint to unzip it")


def test_refuses_other_roms():
    folder = tempfile.mkdtemp()
    try:
        path = os.path.join(folder, "other.z64")
        header = bytearray(0x2000)
        header[:4] = b"\x80\x37\x12\x40"
        header[0x20:0x34] = b"SUPER MARIO 64      "
        with open(path, "wb") as handle:
            handle.write(header)
        try:
            rom_extract.identify(path)
            check(False, "another game is refused")
        except rom_extract.RomError as error:
            check("not a Diddy Kong Racing ROM" in str(error), "another game is named as such")
        header[0x20:0x34] = b"Diddy Kong Racing   "
        with open(path, "wb") as handle:
            handle.write(header)
        try:
            rom_extract.identify(path)
            check(False, "a modified DKR is refused")
        except rom_extract.RomError as error:
            check("clean dump" in str(error), "a modified DKR asks for a clean dump")
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def test_path_length():
    if os.name != "nt":
        return
    deep = os.path.join(tempfile.gettempdir(), "d" * 200, "us.v77")
    try:
        rom_extract.check_path_length(deep)
        refused = False
    except rom_extract.RomError as error:
        refused = "too long" in str(error)
    check(refused, "a folder too deep for Windows is refused up front")
    rom_extract.check_path_length(os.path.join(tempfile.gettempdir(), "us.v77"))


def test_tables():
    tables = rom_extract.load_tables()
    versions = {entry["dkr_version"] for entry in tables["inputs"]}
    check({"us.v77", "us.v80"} <= versions, "the tables know both USA revisions")
    check(len(tables["sections"]) == 50, "the tables know the 50 asset sections")
    common = tables["structs"]["LevelObjectEntryCommon"]
    check(common["size"] == 8, "LevelObjectEntryCommon is 8 bytes")


# ---------------------------------------------------------------------------
# Against the reference tree
# ---------------------------------------------------------------------------

#: Folders whose files the extractor writes for the types the addon reads.
COMPARED = [
    "textures", "sprites", "objects/headers", "objects/models", "levels/headers",
    "levels/models", "levels/objectMaps", "audio",
    "objects/level_object_translation_table.json",
]


def _png_pixels(path):
    width, height, rgba = textures.read_png(path)
    return width, height, bytes(rgba)


def _reference_files(reference):
    found = set()
    for entry in COMPARED:
        path = os.path.join(reference, entry)
        if os.path.isfile(path):
            found.add(entry)
            continue
        for root, _, files in os.walk(path):
            for name in files:
                found.add(os.path.relpath(os.path.join(root, name), reference).replace("\\", "/"))
    return found


def compare(ours, reference):
    stats = {"json": 0, "json_bytes": 0, "bin": 0, "png": 0, "missing": 0, "extra": 0}
    wanted = _reference_files(reference)
    written = set()
    for root, _, files in os.walk(ours):
        for name in files:
            written.add(os.path.relpath(os.path.join(root, name), ours).replace("\\", "/"))

    manifests = {p for p in written if p.endswith(".meta.json") and "/" not in p}
    for relative in sorted(wanted | (written - manifests)):
        mine = os.path.join(ours, relative)
        theirs = os.path.join(reference, relative)
        if not os.path.isfile(mine):
            stats["missing"] += 1
            if stats["missing"] <= 10:
                FAILURES.append("missing %s" % relative)
            continue
        if not os.path.isfile(theirs):
            stats["extra"] += 1
            if stats["extra"] <= 10:
                FAILURES.append("%s is not in the decomp's extraction" % relative)
            continue
        extension = os.path.splitext(relative)[1].lower()
        if extension in (".json", ".gltf"):
            with open(mine, "rb") as handle:
                mine_bytes = handle.read()
            with open(theirs, "rb") as handle:
                their_bytes = handle.read()
            if json.loads(mine_bytes) != json.loads(their_bytes):
                FAILURES.append("%s differs from the decomp's" % relative)
            else:
                stats["json"] += 1
                stats["json_bytes"] += mine_bytes == their_bytes
        elif extension == ".png":
            if _png_pixels(mine) != _png_pixels(theirs):
                FAILURES.append("%s pixels differ from the decomp's" % relative)
            else:
                stats["png"] += 1
        else:
            with open(mine, "rb") as a, open(theirs, "rb") as b:
                if a.read() != b.read():
                    FAILURES.append("%s bytes differ from the decomp's" % relative)
                else:
                    stats["bin"] += 1

    # Every section manifest, and the main one, must match too.
    for path in sorted(glob.glob(os.path.join(reference, "*.meta.json"))):
        name = os.path.basename(path)
        mine = os.path.join(ours, name)
        if not check(os.path.isfile(mine), "manifest %s written" % name):
            continue
        with open(mine, "r", encoding="utf-8") as a, open(path, "r", encoding="utf-8") as b:
            check(json.load(a) == json.load(b), "manifest %s matches" % name)

    return stats


def test_rom(rom_path, scratch):
    data, identity = rom_extract.read_rom(rom_path)
    reference = os.path.join(VANILLA, identity.version)
    print("  %s: %s, %s byte order" % (os.path.basename(rom_path), identity.name,
                                        identity.byte_order))
    if not os.path.isdir(reference):
        print("    skip: no reference extraction for %s" % identity.version)
        return
    started = time.time()
    seen = []
    path = rom_extract.extract(rom_path, scratch,
                               progress=lambda fraction, message: seen.append(fraction))
    elapsed = time.time() - started
    check(os.path.basename(path) == identity.version, "extracted into <dest>/%s" % identity.version)
    check(seen and seen[-1] == 1.0, "progress reaches 100%")
    check(not [n for n in os.listdir(scratch) if n.startswith(".")],
          "no staging folder is left behind")

    tree = assets.AssetTree.find(path)
    check(tree is not None, "AssetTree accepts the extracted tree")
    if tree is not None:
        check(len(tree.levels()) == 65, "all 65 levels resolve (%d)" % len(tree.levels()))
        kind, preview, _ = tree.preview_for("ASSET_OBJECT_PALMTREETOP")
        check(kind == "sprite" and preview, "a palm tree resolves to its sprite")

    stats = compare(path, reference)
    print("    %.1f s; %d JSON/glTF equal (%d byte-identical), %d .bin, %d PNG; "
          "%d missing, %d extra" % (elapsed, stats["json"], stats["json_bytes"],
                                    stats["bin"], stats["png"], stats["missing"],
                                    stats["extra"]))
    check(stats["missing"] == 0, "%s: no reference file is missing" % identity.version)
    check(stats["extra"] == 0, "%s: nothing the decomp does not write" % identity.version)


def test_cancel(rom_path, scratch):
    calls = [0]

    def cancelled():
        calls[0] += 1
        return calls[0] > 50

    try:
        rom_extract.extract(rom_path, scratch, cancelled=cancelled)
        check(False, "a cancelled extraction raises Cancelled")
    except rom_extract.Cancelled:
        pass
    check(not os.listdir(scratch), "a cancelled extraction leaves nothing behind")


def main():
    print("ROM-free")
    test_normalise()
    test_refuses_other_roms()
    test_tables()
    test_path_length()

    roms = []
    for path in sorted(glob.glob(os.path.join(BASEROMS, "*"))):
        if not path.lower().endswith((".z64", ".n64", ".v64")):
            continue
        try:
            identity = rom_extract.identify(path)
        except rom_extract.RomError:
            continue
        roms.append((path, identity))
    orders = {identity.byte_order for _path, identity in roms}
    if roms:
        print("  byte orders identified: %s" % ", ".join(sorted(orders)))

    if not roms:
        print("skip: no DKR ROM in %s" % BASEROMS)
    else:
        print("ROMs")
        done = set()
        for path, identity in roms:
            # One extraction per revision: every byte order normalises to the
            # same bytes, which identify() has already proved by its SHA1.
            key = identity.version
            if key in done:
                continue
            done.add(key)
            scratch = tempfile.mkdtemp(prefix="dkr-rom-extract-")
            try:
                test_rom(path, scratch)
            finally:
                shutil.rmtree(scratch, ignore_errors=True)
        scratch = tempfile.mkdtemp(prefix="dkr-rom-cancel-")
        try:
            test_cancel(roms[0][0], scratch)
        finally:
            shutil.rmtree(scratch, ignore_errors=True)

    if FAILURES:
        print("\n%d failure(s):" % len(FAILURES))
        for failure in FAILURES[:60]:
            print("  FAIL %s" % failure)
        return 1
    print("\nall ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
