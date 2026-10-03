"""Rebuild every retail level header and compare against the shipped bytes.

Same gate as the object-map encoder, for the same reason: the correct answer
already exists. All 65 retail headers sit in ``assets.bin`` and their sources sit
in the decomp as JSON, so the encoder is right exactly when it reproduces them.

Header entries are 200 bytes and uncompressed - no ``[u32][0x09]`` container,
unlike object maps - and the table has 68 slots for 65 real headers.

Two offsets are excluded from the field comparison and checked separately: 0x36
and 0xBA belong to the runtime, which patches them when it builds the extended
asset table. The encoder must leave them **zero**, never -1: the game's bounds
check is a signed ``mapID >= i``, so -1 slips past the clamp and reads
``objMapTable[-1]``.

    python tools/blender/tests/test_header.py
"""

from __future__ import annotations

import collections
import json
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import (  # noqa: E402
    assets, catalog as catalog_module, level_header,
)

from test_binary_format import ASSETS, find_tree, sections  # noqa: E402

#: ``AssetSectionsEnum``.
SECTION_HEADERS_TABLE = 22
SECTION_HEADERS = 23

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def retail_headers(offsets, blob, tree_root):
    """Yield ``(asset_id, json_path, 200 bytes)`` per retail header."""
    meta_path = os.path.join(tree_root, "asset_level_headers.meta.json")
    if not os.path.isfile(meta_path):
        return
    with open(meta_path, "r", encoding="utf-8") as handle:
        meta = json.load(handle)
    files = meta["files"]
    folder = meta.get("folder", "")

    start, end = offsets[SECTION_HEADERS_TABLE], offsets[SECTION_HEADERS]
    table = [
        struct.unpack_from(">I", blob, start + i * 4)[0]
        for i in range((end - start) // 4)
    ]
    base = offsets[SECTION_HEADERS]

    for index, asset_id in enumerate(files["order"]):
        if index + 1 >= len(table):
            break
        span = table[index + 1] - table[index]
        if span != level_header.HEADER_SIZE:
            continue
        entry = files["sections"].get(asset_id, {}).get("filename")
        source = os.path.join(tree_root, folder, entry) if entry else None
        if source and os.path.isfile(source):
            yield asset_id, source, blob[base + table[index]: base + table[index + 1]]


def describe(offset):
    """Which field a differing byte belongs to."""
    best = None
    for field in level_header.LAYOUT:
        width = level_header._W[field.ctype]
        if field.offset <= offset < field.offset + width:
            return "%s (%s @0x%02X)" % (field.pointer, field.ctype, field.offset)
        if field.offset <= offset:
            best = field
    if offset in level_header.RUNTIME_OWNED:
        return "runtime-owned 0x%02X" % offset
    return "unmapped, after %s" % (best.pointer if best else "start")


def _check_unkc4_endianness():
    """Every extracted revision must agree that 0xC4 is little endian."""
    import glob

    vanilla = os.path.join(ASSETS, ".vanilla")
    if not os.path.isdir(vanilla):
        return
    field = next((f for f in level_header.LAYOUT if f.offset == 0xC4), None)
    if field is None:
        FAILURES.append("no field is mapped at 0xC4")
        return
    check(field.endian == level_header.LITTLE_ENDIAN,
          "0xC4 is declared little endian")

    for revision in sorted(os.listdir(vanilla)):
        headers = sorted(glob.glob(
            os.path.join(vanilla, revision, "levels", "headers", "*.json")
        ))
        if not headers:
            continue
        swapped = natural = 0
        for path in headers:
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    value = json.load(handle)["unknown"]["unkC4"] & 0xFFFFFFFF
            except (ValueError, OSError, KeyError):
                continue
            # A field the extractor read in the wrong order comes out as a large
            # value whose byte-swap is small. That is the signature.
            if struct.unpack(">I", struct.pack("<I", value))[0] < 0x10000:
                swapped += 1
            if value < 0x10000:
                natural += 1
        if not (swapped or natural):
            continue
        print("  %-8s unkC4 looks byte-swapped in %d of %d headers, natural in %d"
              % (revision, swapped, swapped + natural, natural))
        check(swapped >= natural,
              "%s agrees 0xC4 is little endian (%d swapped vs %d natural)"
              % (revision, swapped, natural))


def _check_cross_revision_indices():
    """An index written for one revision has to mean the same in the others.

    DKR-R runs whichever engine the player's ROM selects, so a package encoded
    against one revision's asset ordering would be wrong for players on another.
    The indices a track writes come from the level-object translation table and
    from the asset sections the header names.
    """
    import glob

    vanilla = os.path.join(ASSETS, ".vanilla")
    revisions = sorted(
        d for d in (os.listdir(vanilla) if os.path.isdir(vanilla) else [])
        if os.path.isfile(os.path.join(vanilla, d, "asset_objects.meta.json"))
    )
    if len(revisions) < 2:
        return

    trees = [assets.AssetTree(os.path.join(vanilla, r)) for r in revisions]
    first, rest = trees[0], trees[1:]

    # Object ids are indices into this, so a difference would repoint every
    # entry in every map.
    base_table = first.translation_table()
    for tree, name in zip(rest, revisions[1:]):
        check(tree.translation_table() == base_table,
              "%s and %s agree on the level-object translation table"
              % (revisions[0], name))
    print("  translation table: identical across %s" % ", ".join(revisions))

    # The sections a header resolves against. textures_2d legitimately differs
    # between revisions, so what matters is whether anything a header actually
    # names lands past the divergence.
    for meta in ("asset_level_models.meta.json", "asset_objects.meta.json",
                 "asset_misc.meta.json"):
        base = first.order(meta)
        for tree, name in zip(rest, revisions[1:]):
            check(tree.order(meta) == base,
                  "%s and %s agree on %s" % (revisions[0], name, meta))

    mismatched = []
    for tree, name in zip(rest, revisions[1:]):
        for path in sorted(glob.glob(
            os.path.join(tree.root, "levels", "headers", "*.json")
        )):
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    document = json.load(handle)
            except (ValueError, OSError):
                continue
            texture = (document.get("waves") or {}).get("texture-ID") or ""
            if not texture:
                continue
            if first.asset_index("ASSET_TEXTURES_2D", texture) !=                     tree.asset_index("ASSET_TEXTURES_2D", texture):
                mismatched.append((name, os.path.basename(path), texture))

    check(not mismatched,
          "every header's wave texture resolves to the same index in every "
          "revision (%d did not: %s)"
          % (len(mismatched), mismatched[:3]))
    print("  header asset indices: portable across %s" % ", ".join(revisions))


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
    encoder = level_header.LevelHeaderEncoder(
        catalog.raw.get("enumValues", {}), tree.asset_index
    )

    print("revision      : %s" % os.path.basename(tree_root))

    total = exact = 0
    reasons = collections.Counter()
    examples = []

    for asset_id, source, expected in retail_headers(offsets, blob, tree_root):
        with open(source, "r", encoding="utf-8") as handle:
            document = json.load(handle)
        total += 1

        # Reproduce the retail bytes by supplying the two runtime-owned fields;
        # the encoder itself never writes them.
        patch = {
            offset: struct.unpack_from(">h", expected, offset)[0]
            for offset in level_header.RUNTIME_OWNED
        }
        try:
            produced = encoder.encode(document, patch)
        except level_header.HeaderError as error:
            reasons[str(error)[:60]] += 1
            continue

        if produced == expected:
            exact += 1
            continue

        differing = [i for i in range(len(expected)) if produced[i] != expected[i]]
        first = differing[0]
        reasons[describe(first)] += 1
        if len(examples) < 8:
            examples.append(
                "%s: %d byte(s) differ, first at 0x%02X (%s): %02X != %02X"
                % (asset_id, len(differing), first, describe(first),
                   produced[first], expected[first])
            )

    print("headers built : %d" % total)
    print("byte-exact    : %d of %d" % (exact, total))

    if exact != total:
        print()
        print("divergences:")
        for reason, count in reasons.most_common(12):
            print("  x%-4d %s" % (count, reason))
        print()
        for line in examples:
            print("  " + line)

    check(total > 0, "found retail headers to compare against")
    check(exact == total,
          "every retail header rebuilds byte-exactly (%d of %d)" % (exact, total))

    # unkC4 is the one little-endian field in the header, and it is worth an
    # assertion of its own: the v77 bytes happen to look like a natural
    # big-endian value (00 00 07 a8), so a reader sampling one revision can
    # reasonably conclude the opposite. Checking every extracted revision
    # settles it without needing that revision's assets.bin, because the
    # extractor and the builder share one struct definition - whatever order
    # one reads with, the other writes with.
    _check_unkc4_endianness()
    _check_cross_revision_indices()

    # The two runtime-owned fields must be zero without a patch, never -1.
    if total:
        _asset_id, source, _expected = next(retail_headers(offsets, blob, tree_root))
        with open(source, "r", encoding="utf-8") as handle:
            document = json.load(handle)
        plain = encoder.encode(document)
        for offset in level_header.RUNTIME_OWNED:
            value = struct.unpack_from(">h", plain, offset)[0]
            check(value == 0,
                  "0x%02X is left zero for the runtime to patch (got %d); -1 "
                  "would slip past the game's signed bounds check and read "
                  "objMapTable[-1]" % (offset, value))
        check(len(plain) == level_header.HEADER_SIZE,
              "a header is %d bytes" % level_header.HEADER_SIZE)

    print()
    if FAILURES:
        print("FAIL: %d" % len(FAILURES))
        for line in FAILURES[:8]:
            print("  " + line)
        return 1
    print("PASS: the header encoder reproduces every retail level header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
