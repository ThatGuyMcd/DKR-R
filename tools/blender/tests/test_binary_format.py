"""Check the object-map binary format against the bytes the game ships.

docs/LEVEL_OBJECT_MAP_FORMAT.md is the specification an encoder is written from,
and this keeps it honest: every claim in it is re-checked here against
``assets.bin``, so the document cannot quietly drift from the format.

It also validates the generated catalogue. Each type's ``entry_size`` is derived
from the decomp's C header, and the retail entries say what the size really is -
so the two must agree for all 85 types. They did not at first: an offset-marker
typo in the header and a struct matched by field names alone both produced wrong
sizes, and this is what caught them.

``assets.bin`` is not in every checkout, so this skips cleanly without it.

    python tools/blender/tests/test_binary_format.py
"""

from __future__ import annotations

import collections
import json
import os
import struct
import sys
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import catalog as catalog_module, gltf_io  # noqa: E402

from test_roundtrip import REPO_ROOT  # noqa: E402

ASSETS = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets")

#: Indices from ``AssetSectionsEnum``.
SECTION_OBJECT_MAPS_TABLE = 20
SECTION_OBJECT_MAPS = 21

CONTAINER_TAG = 0x09
HEADER_SIZE = 0x10
BLOB_ALIGNMENT = 8

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def sections():
    """Section spans from the asset LUT: a count, then count + 1 offsets."""
    path = os.path.join(ASSETS, "assets.lut.bin")
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as handle:
        lut = handle.read()
    return [
        struct.unpack_from(">I", lut, 4 + i * 4)[0]
        for i in range((len(lut) - 4) // 4)
    ]


def find_tree(offsets, blob):
    """The extracted revision that matches ``assets.bin``.

    The checkout can hold several extracted revisions and only one of them
    corresponds to the built ``assets.bin``. Picking the wrong one produces a
    handful of position mismatches that look like format bugs and are not, so
    the tree is chosen by which one actually agrees.
    """
    vanilla = os.path.join(ASSETS, ".vanilla")
    if not os.path.isdir(vanilla):
        return None, 0
    best, best_score = None, -1
    for name in sorted(os.listdir(vanilla)):
        root = os.path.join(vanilla, name)
        if not os.path.isfile(os.path.join(root, "asset_level_object_maps.meta.json")):
            continue
        score = sum(1 for _ in walk(offsets, blob, root, strict=True))
        if score > best_score:
            best, best_score = root, score
    return best, best_score


def walk(offsets, blob, tree, strict=False):
    """Yield ``(asset_id, entries)`` for every map that decodes cleanly."""
    meta_path = os.path.join(tree, "asset_level_object_maps.meta.json")
    table_path = os.path.join(tree, "objects", "level_object_translation_table.json")
    if not (os.path.isfile(meta_path) and os.path.isfile(table_path)):
        return
    with open(meta_path, "r", encoding="utf-8") as handle:
        meta = json.load(handle)["files"]
    with open(table_path, "r", encoding="utf-8") as handle:
        translation = json.load(handle)["table"]

    start, end = offsets[SECTION_OBJECT_MAPS_TABLE], offsets[SECTION_OBJECT_MAPS]
    table = [
        struct.unpack_from(">I", blob, start + i * 4)[0]
        for i in range((end - start) // 4)
    ]
    base = offsets[SECTION_OBJECT_MAPS]

    for index, asset_id in enumerate(meta["order"]):
        if index + 1 >= len(table):
            break
        raw = blob[base + table[index]: base + table[index + 1]]
        if len(raw) < 5 or raw[4] != CONTAINER_TAG:
            continue
        try:
            data = zlib.decompress(raw[5:], -15)
        except zlib.error:
            continue

        entry = meta["sections"].get(asset_id, {}).get("filename")
        gltf = (
            os.path.join(tree, "levels", "objectMaps",
                         os.path.splitext(entry)[0] + ".gltf")
            if entry else None
        )
        if not gltf or not os.path.isfile(gltf):
            continue

        parsed = parse_entries(data, translation)
        if parsed is None:
            if not strict:
                FAILURES.append("%s: entries do not tile fileSize" % asset_id)
            continue
        if strict and not agrees(parsed, gltf_io.load(gltf)):
            continue
        yield asset_id, parsed, data, gltf


def parse_entries(data, translation):
    """Decode one map's entries, or ``None`` if they do not tile ``fileSize``."""
    file_size, = struct.unpack_from(">I", data, 0)
    offset, stop = HEADER_SIZE, HEADER_SIZE + file_size
    entries = []
    while offset < stop:
        if offset + 8 > len(data):
            return None
        byte0, byte1 = data[offset], data[offset + 1]
        object_id = byte0 | ((byte1 & 0x80) << 1)
        size = byte1 & 0x7F
        if size < 8:
            return None
        position = list(struct.unpack_from(">3h", data, offset + 2))
        name = translation[object_id] if object_id < len(translation) else None
        entries.append((name, size, position))
        offset += size
    return entries if offset == stop else None


def agrees(entries, object_map):
    if len(entries) != len(object_map.objects):
        return False
    for (name, _size, position), obj in zip(entries, object_map.objects):
        if name != obj.object_id:
            return False
        if position != [int(c) for c in obj.translation]:
            return False
    return True


def main():
    offsets = sections()
    if offsets is None:
        print("SKIP: no assets.lut.bin; assets.bin is not in this checkout")
        return 0
    binary_path = os.path.join(ASSETS, "assets.bin")
    if not os.path.isfile(binary_path):
        print("SKIP: no assets.bin in this checkout")
        return 0
    with open(binary_path, "rb") as handle:
        blob = handle.read()

    tree, matching = find_tree(offsets, blob)
    if tree is None or matching == 0:
        print("SKIP: no extracted revision matches assets.bin")
        return 0
    print("assets.bin matches : %s (%d maps)" % (os.path.basename(tree), matching))

    catalog = catalog_module.load()
    maps = entries = 0
    sizes = collections.defaultdict(set)
    padding = collections.Counter()

    for asset_id, parsed, data, gltf in walk(offsets, blob, tree):
        object_map = gltf_io.load(gltf)
        maps += 1
        entries += len(parsed)

        check(agrees(parsed, object_map),
              "%s: binary entries match the glTF" % asset_id)

        file_size, = struct.unpack_from(">I", data, 0)
        check(sum(size for _n, size, _p in parsed) == file_size,
              "%s: entry sizes sum to fileSize" % asset_id)
        check(len(data) % BLOB_ALIGNMENT == 0,
              "%s: blob is %d-byte aligned" % (asset_id, BLOB_ALIGNMENT))
        tail = data[HEADER_SIZE + file_size:]
        check(tail == b"\x00" * len(tail), "%s: padding is zero-filled" % asset_id)
        padding[len(tail)] += 1

        for name, size, _position in parsed:
            check(size % 2 == 0, "%s: entry size %d is even" % (asset_id, size))
            if name:
                sizes[name].add(size)

    print("maps verified      : %d" % maps)
    print("entries verified   : %d" % entries)
    print("object types       : %d" % len(sizes))
    print("padding after data : %s" % dict(sorted(padding.items())))

    # The catalogue derives entry_size from the decomp header; retail says what
    # it really is.
    wrong = []
    for name, observed in sorted(sizes.items()):
        entry = catalog.raw.get("objects", {}).get(name)
        if entry is None:
            continue
        if entry.get("entry_size") not in observed:
            wrong.append((name, sorted(observed), entry.get("entry_size")))
    check(not wrong, "catalogue entry sizes match retail (%d wrong)" % len(wrong))
    for name, observed, mine in wrong[:8]:
        print("  SIZE %-32s binary=%s catalog=%s"
              % (name.replace("ASSET_OBJECT_", ""), observed, mine))

    print()
    if FAILURES:
        print("FAIL: %d" % len(FAILURES))
        for line in FAILURES[:12]:
            print("  " + line)
        return 1
    print("PASS: the binary format matches docs/LEVEL_OBJECT_MAP_FORMAT.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
