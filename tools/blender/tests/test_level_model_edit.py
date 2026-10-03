"""Gate for the geometry edit path: derived values, and edits that survive.

Phase 2 step 2 of docs/BLENDER_ADDON_PLAN.md. Two things have to hold before an
author can reshape a track.

**The derivation rules are right.** ``recompute_bounds`` is run over every
extracted retail model without changing a single vertex, and must report nothing
to change. That is a strong claim rather than a tautology: the bounding box and
header bound values already in the file were produced by Rare's tools, so
reproducing all of them from the vertices alone - 1146 segments, including the
138 flat ones that need the zero-height guard - is what says the rule is the
rule. Get it wrong and the re-encode stops being byte-exact, which the second
half of each case checks.

**An edit goes in and comes back out.** A model is edited, encoded, decoded
again, and has to carry exactly the edit that was made and nothing else.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_level_model_edit.py
"""

from __future__ import annotations

import glob
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import (  # noqa: E402
    level_model, level_model_edit, level_model_encoder,
)

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")


def find_level_models():
    pattern = os.path.join(VANILLA, "*", "levels", "models", "*", "*.bin")
    return sorted(glob.glob(pattern))


def load(path):
    with open(path, "rb") as handle:
        blob = level_model.decompress(handle.read())
    return blob, level_model.parse(blob)


# ---------------------------------------------------------------------------
# The derivation rules
# ---------------------------------------------------------------------------

def check_derivation(path):
    """Recomputing the derived values must reproduce what retail already holds."""
    blob, model = load(path)

    changed = level_model_edit.recompute_bounds(model)
    if changed:
        for index, segment in enumerate(model.segments):
            if index >= len(model.bounding_boxes):
                break
            want = level_model_edit.segment_box(segment)
            if want is None:
                continue
            flat = (segment.vertices
                    and min(v[1] for v in segment.vertices)
                    == max(v[1] for v in segment.vertices))
            return (
                "recompute_bounds changed %d value(s) on an unedited model; "
                "segment %d derives to %r%s"
                % (changed, index, want, " (flat in Y)" if flat else "")
            )
        return "recompute_bounds changed %d value(s) on an unedited model" % changed

    if level_model_encoder.encode(model) != blob:
        return "recomputing bounds broke byte equality"
    return None


# ---------------------------------------------------------------------------
# Edits
# ---------------------------------------------------------------------------

def _first_populated(model):
    for index, segment in enumerate(model.segments):
        if segment.vertices and segment.batches:
            return index, segment
    return None, None


def check_edit_round_trip(path):
    """An edit must survive encode and decode, and change nothing else."""
    blob, model = load(path)
    index, segment = _first_populated(model)
    if segment is None:
        return None

    original = segment.vertices[0]
    original_colour = segment.colours[0]
    original_flags = segment.batches[0].flags

    moved = (original[0], original[1] + 40, original[2])
    summary = level_model_edit.apply(
        model,
        positions={(index, 0): moved},
        colours={(index, 0): (1, 2, 3, 4)},
        batch_flags={(index, 0): original_flags ^ level_model.RENDER_HIDDEN},
    )
    if summary.moved != 1:
        return "moving one vertex reported %d moved" % summary.moved
    if summary.painted != 1:
        return "recolouring one vertex reported %d painted" % summary.painted
    if summary.flags != 1:
        return "reflagging one batch reported %d changed" % summary.flags

    payload = level_model_encoder.encode(model)
    if payload == blob:
        return "an edited model encoded to the same bytes as the original"
    if len(payload) != len(blob):
        return "an edit changed the model size from %d to %d" % (len(blob), len(payload))

    again = level_model.parse(payload)
    back = again.segments[index]
    if tuple(back.vertices[0]) != moved:
        return "vertex came back as %r, not %r" % (back.vertices[0], moved)
    if tuple(back.colours[0]) != (1, 2, 3, 4):
        return "colour came back as %r" % (back.colours[0],)
    if back.batches[0].flags != original_flags ^ level_model.RENDER_HIDDEN:
        return "batch flags came back as 0x%X" % back.batches[0].flags

    # Everything not addressed has to be untouched.
    if len(back.vertices) > 1 and tuple(back.vertices[1]) != tuple(segment.vertices[1]):
        return "an edit to vertex 0 also changed vertex 1"

    # And the box has to have followed the vertex out.
    want = level_model_edit.segment_box(back)
    if tuple(again.bounding_boxes[index]) != want:
        return ("the bounding box did not follow the moved vertex: file has %r, "
                "vertices imply %r" % (tuple(again.bounding_boxes[index]), want))
    return None


def check_refusals(path):
    """Edits the format cannot hold must stop the export, not wrap around."""
    _blob, model = load(path)
    index, segment = _first_populated(model)
    if segment is None:
        return None

    try:
        level_model_edit.set_vertex_positions(model, {(index, 0): (40000, 0, 0)})
    except level_model_edit.EditError:
        pass
    else:
        return "a vertex outside s16 was accepted"

    try:
        level_model_edit.set_batch_textures(model, {(index, 0): len(model.textures)})
    except level_model_edit.EditError:
        pass
    else:
        return "a texture index past the model's table was accepted"

    try:
        level_model_edit.set_vertex_positions(model, {(len(model.segments), 0): (0, 0, 0)})
    except level_model_edit.EditError:
        pass
    else:
        return "an edit to a segment that does not exist was accepted"
    return None


def check_flat_guard(path):
    """A segment flattened onto one height still gets a box with height."""
    _blob, model = load(path)
    index, segment = _first_populated(model)
    if segment is None:
        return None

    height = segment.vertices[0][1]
    level_model_edit.apply(model, positions={
        (index, i): (v[0], height, v[2]) for i, v in enumerate(segment.vertices)
    })
    box = model.bounding_boxes[index]
    if box[4] - box[1] != level_model_edit.FLAT_SEGMENT_HEIGHT:
        return ("a segment flattened to one height got a box %d tall, expected %d"
                % (box[4] - box[1], level_model_edit.FLAT_SEGMENT_HEIGHT))
    return None


CASES = (
    ("derivation rules reproduce retail", check_derivation),
    ("edits survive a round trip", check_edit_round_trip),
    ("impossible edits are refused", check_refusals),
    ("flat segments keep a height", check_flat_guard),
)


def main():
    models = find_level_models()
    if not models:
        print("no extracted level models found under %s" % VANILLA)
        return 0

    failures = []
    for label, case in CASES:
        bad = []
        for path in models:
            problem = case(path)
            if problem:
                bad.append((os.path.basename(path), problem))
        print("%-36s %s" % (label, "PASS" if not bad else "FAIL (%d)" % len(bad)))
        for name, problem in bad[:5]:
            print("    %s: %s" % (name, problem))
        failures += bad

    print("")
    print("level models checked: %d" % len(models))
    if failures:
        print("%d case failure(s)" % len(failures))
        return 1
    print("every model derives its own bounds, and every edit round trips")
    return 0


if __name__ == "__main__":
    sys.exit(main())
