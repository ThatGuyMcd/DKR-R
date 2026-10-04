"""Round-trip gate for the level model reader and writer.

Phase 2 step 1 of docs/BLENDER_ADDON_PLAN.md: read every retail level model,
push it through the in-memory representation the addon will edit, write it back
and require the bytes to be identical. Nothing built on top of the geometry
encoder is trustworthy until this holds - a single field the decoder quietly
dropped becomes a track the game misreads.

This mirrors tests/test_roundtrip.py, which does the same for object maps, and
it is possible for the same reason: the correct bytes already exist in the
extracted assets, so the encoder can be proved rather than argued.

Two extra checks ride along, because byte equality alone can be reached by a
decoder that understands nothing and copies everything:

* **Coverage.** Bytes no decoded structure claimed are counted. They are real
  alignment padding in retail, so the budget is small; a regression that drops
  a whole array would blow through it rather than passing as padding.
* **The PVS size rule.** ``segmentsBitfields`` should be one visibility bitmask
  per segment. Checking that against every model is what turns it from a
  plausible reading of the layout into a verified one.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_level_model_roundtrip.py
"""

from __future__ import annotations

import glob
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import level_model, level_model_encoder  # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")

#: Longest unclaimed run tolerated. What is left over in retail is alignment
#: padding between arrays, which is a few bytes at a time; the widest seen
#: across the extracted set is 12. The check is on the longest run rather than
#: on the total because that is what separates the two failures worth catching:
#: padding stays small however many segments a model has, while an array the
#: decoder stopped claiming shows up as one long run.
LONGEST_GAP = 16


def find_level_models():
    """Every extracted retail level model, across whichever revisions exist."""
    pattern = os.path.join(VANILLA, "*", "levels", "models", "*", "*.bin")
    return sorted(glob.glob(pattern))


def check(path):
    """Read -> model -> write must reproduce the decompressed blob exactly."""
    with open(path, "rb") as handle:
        container = handle.read()

    try:
        blob = level_model.decompress(container)
    except level_model.LevelModelError as error:
        return "container did not decompress: %s" % error

    model = level_model.parse(blob)

    try:
        rewritten = level_model_encoder.encode(model)
    except level_model_encoder.LevelModelEncodeError as error:
        return "encode refused the decoded model: %s" % error

    if rewritten != blob:
        return _describe_difference(blob, rewritten, model)

    for offset, data in model.gaps:
        if len(data) > LONGEST_GAP:
            return (
                "round trips, but %d bytes at 0x%X belong to no decoded "
                "structure; padding runs to %d, so this is a structure the "
                "decoder stopped claiming"
                % (len(data), offset, LONGEST_GAP)
            )

    expected = level_model.pvs_size(len(model.segments))
    actual = model.unk_c_ptr - model.bitfields_ptr
    if actual < expected:
        return (
            "segmentsBitfields spans %d bytes but one bitmask per segment needs "
            "%d; the PVS reading is wrong for this model" % (actual, expected)
        )
    return None


def _describe_difference(original, rewritten, model):
    """Say where the bytes first diverge, and what lives there."""
    if len(original) != len(rewritten):
        return "wrote %d bytes, expected %d" % (len(rewritten), len(original))
    for index, (left, right) in enumerate(zip(original, rewritten)):
        if left != right:
            return "first difference at 0x%X: expected 0x%02X, wrote 0x%02X (%s)" % (
                index, left, right, _region_at(index, model)
            )
    return "bytes differ but no differing offset was found"


def _region_at(offset, model):
    """Name the structure covering an offset, to make a failure diagnosable."""
    if offset < level_model.HEADER_SIZE:
        return "LevelModel header"
    named = [
        ("texture table", model.textures_ptr,
         len(model.textures) * level_model.TEXTURE_INFO_SIZE),
        ("segment array", model.segments_ptr,
         len(model.segments) * level_model.SEGMENT_SIZE),
        ("bounding boxes", model.bounding_boxes_ptr,
         len(model.bounding_boxes) * level_model.BOUNDING_BOX_SIZE),
        ("BSP tree", model.bsp_ptr,
         len(model.bsp) * level_model.BSP_NODE_SIZE),
    ]
    for segment in model.segments:
        named += [
            ("segment %d vertices" % segment.index, segment.vertices_ptr,
             len(segment.vertices) * level_model.VERTEX_SIZE),
            ("segment %d triangles" % segment.index, segment.triangles_ptr,
             len(segment.triangles) * level_model.TRIANGLE_SIZE),
            ("segment %d batches" % segment.index, segment.batches_ptr,
             (len(segment.batches) + 1) * level_model.BATCH_SIZE),
        ]
    for label, start, length in named:
        if start <= offset < start + length:
            return label
    if offset >= model.bitfields_ptr:
        return "carried-through region (PVS or collision facet scratch)"
    return "unclaimed"


def main():
    models = find_level_models()
    if not models:
        print("no extracted level models found under %s" % VANILLA)
        print("nothing to check; run the decomp extraction first")
        return 0

    failures = []
    total_gap = 0
    total_bytes = 0
    for path in models:
        problem = check(path)
        if problem:
            failures.append((path, problem))
        else:
            with open(path, "rb") as handle:
                blob = level_model.decompress(handle.read())
            model = level_model.parse(blob)
            total_gap += sum(len(data) for _offset, data in model.gaps)
            total_bytes += len(blob)

    print("level models checked: %d" % len(models))
    if total_bytes:
        print("bytes accounted for: %d of %d (%.4f%%)"
              % (total_bytes - total_gap, total_bytes,
                 100.0 * (total_bytes - total_gap) / total_bytes))
    for path, problem in failures:
        print("  FAIL %s: %s" % (os.path.basename(path), problem))

    if failures:
        print("\n%d of %d level models did not round trip" % (len(failures), len(models)))
        return 1
    print("every level model re-encoded to identical bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
