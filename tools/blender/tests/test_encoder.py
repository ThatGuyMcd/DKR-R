"""Encode every retail object map and compare against the bytes the game ships.

The correct answer already exists. For all 136 retail maps, ``assets.bin``
holds the section payload the game loads, and the decomp holds the glTF that
decodes to. So the encoder is correct exactly when, given the glTF, it produces
those same bytes - no play testing, no guessing.

This is the gate for ``object_map_encoder``: a change that alters one field's
encoding fails here rather than in game, where a wrong byte shows up as an
object silently behaving oddly.

Needs ``assets.bin``, which is not in every checkout, and skips cleanly without.

    python tools/blender/tests/test_encoder.py
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

from track_lab import (  # noqa: E402
    assets, catalog as catalog_module, gltf_io, object_map_encoder,
)

from test_roundtrip import REPO_ROOT  # noqa: E402
from test_binary_format import (  # noqa: E402
    ASSETS, SECTION_OBJECT_MAPS, SECTION_OBJECT_MAPS_TABLE, find_tree, sections,
)

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def retail_payloads(offsets, blob, tree):
    """Yield ``(asset_id, gltf_path, decompressed_payload)`` per retail map."""
    meta_path = os.path.join(tree, "asset_level_object_maps.meta.json")
    with open(meta_path, "r", encoding="utf-8") as handle:
        meta = json.load(handle)["files"]

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
        if len(raw) < 5 or raw[4] != object_map_encoder.CONTAINER_TAG:
            continue
        try:
            payload = zlib.decompress(raw[5:], -15)
        except zlib.error:
            continue
        entry = meta["sections"].get(asset_id, {}).get("filename")
        gltf = (
            os.path.join(tree, "levels", "objectMaps",
                         os.path.splitext(entry)[0] + ".gltf")
            if entry else None
        )
        if gltf and os.path.isfile(gltf):
            yield asset_id, gltf, payload


def first_difference(produced, expected, object_map, encoder):
    """Say which entry and which byte of it diverged."""
    if len(produced) != len(expected):
        return "length %d != %d" % (len(produced), len(expected))
    index = next(
        (i for i, (a, b) in enumerate(zip(produced, expected)) if a != b), None
    )
    if index is None:
        return None

    if index < object_map_encoder.HEADER_SIZE:
        return "header byte %d: %02X != %02X" % (index, produced[index], expected[index])

    offset = object_map_encoder.HEADER_SIZE
    for obj in object_map.objects:
        size = expected[offset + 1] & 0x7F
        if offset <= index < offset + size:
            within = index - offset
            field = describe(encoder, obj, within)
            return ("%s at entry byte %d (%s): produced %02X, retail %02X"
                    % (obj.object_id, within, field,
                       produced[index], expected[index]))
        offset += size or 1
    return "byte %d past the entries" % index


def describe(encoder, obj, within):
    """Which field of an entry a byte belongs to."""
    if within < 2:
        return "objectID/size"
    if within < 8:
        return "position"
    object_type = encoder.catalog.get(obj.object_id)
    if object_type is None:
        return "unknown type"
    for field in object_type.fields:
        if field.offset is None or field.ctype not in object_map_encoder._WIDTH:
            continue
        width = object_map_encoder._WIDTH[field.ctype] * (field.count or 1)
        if field.offset <= within < field.offset + width:
            return "%s %s%s" % (
                field.name, field.ctype,
                " " + field.hint.get("kind", "") if field.hint else "",
            )
    return "unmapped"


def main():
    offsets = sections()
    binary_path = os.path.join(ASSETS, "assets.bin")
    if offsets is None or not os.path.isfile(binary_path):
        print("SKIP: assets.bin is not in this checkout")
        return 0
    with open(binary_path, "rb") as handle:
        blob = handle.read()

    tree_root, matching = find_tree(offsets, blob)
    if tree_root is None or matching == 0:
        print("SKIP: no extracted revision matches assets.bin")
        return 0

    tree = assets.AssetTree(tree_root)
    catalog = catalog_module.load()
    translation = tree.translation_table()
    if not translation:
        print("SKIP: no level-object translation table")
        return 0

    encoder = object_map_encoder.ObjectMapEncoder(
        catalog, translation, tree.asset_index
    )

    print("revision           : %s" % os.path.basename(tree_root))
    print("translation table  : %d entries" % len(translation))

    maps = exact = objects = 0
    reasons = collections.Counter()
    examples = []

    for asset_id, gltf, expected in retail_payloads(offsets, blob, tree_root):
        object_map = gltf_io.load(gltf)
        maps += 1
        objects += len(object_map.objects)
        try:
            produced = encoder.encode(object_map)
        except object_map_encoder.EncodeError as error:
            reasons[str(error)[:60]] += 1
            continue

        if produced == expected:
            exact += 1
            continue
        problem = first_difference(produced, expected, object_map, encoder)
        reasons[problem.split(":")[0][:60] if problem else "unknown"] += 1
        if len(examples) < 8:
            examples.append("%s: %s" % (asset_id, problem))

    print("maps encoded       : %d" % maps)
    print("objects encoded    : %d" % objects)
    print("byte-exact         : %d of %d" % (exact, maps))

    if exact != maps:
        print()
        print("divergences:")
        for reason, count in reasons.most_common(10):
            print("  x%-5d %s" % (count, reason))
        print()
        for line in examples:
            print("  " + line)

    check(exact == maps,
          "every retail map re-encodes byte-exactly (%d of %d)" % (exact, maps))

    # The container has to round-trip too, not just the payload.
    if maps:
        asset_id, gltf, expected = next(retail_payloads(offsets, blob, tree_root))
        packed = encoder.pack(gltf_io.load(gltf))
        size, tag = struct.unpack_from("<IB", packed, 0)
        check(tag == object_map_encoder.CONTAINER_TAG, "container tag is 0x09")
        check(size == len(zlib.decompress(packed[5:], -15)),
              "container declares the decompressed size")
        check(zlib.decompress(packed[5:], -15) == encoder.encode(gltf_io.load(gltf)),
              "the container holds the payload")

    print()
    if FAILURES:
        print("FAIL: %d" % len(FAILURES))
        for line in FAILURES[:8]:
            print("  " + line)
        return 1
    print("PASS: the encoder reproduces every retail object map")
    return 0


if __name__ == "__main__":
    sys.exit(main())
