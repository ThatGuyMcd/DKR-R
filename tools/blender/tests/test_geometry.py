"""Decode every retail level model.

The decoder is what gives an author something to place objects against, so it
has to cope with all 110 shipped models, not just the one it was written
against. This checks that each decodes, that the structure is self-consistent -
batch windows inside their segment, triangle indices inside their batch - and
that Ancient Lake still matches the figures in docs/LEVEL_MODEL_FORMAT.md.

Runs on any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_geometry.py
"""

from __future__ import annotations

import glob
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import level_model  # noqa: E402

from test_roundtrip import REPO_ROOT, VANILLA  # noqa: E402

#: The worked example in the format document, so the decoder is pinned to
#: numbers that were verified against the retail asset by hand.
ANCIENT_LAKE = {
    "texture_count": 25,
    "segment_count": 24,
    "animated_texture_count": 7,
    "bounds": (-5918, -23, -56, 885, -12559, -2048),
}

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def find_models():
    pattern = os.path.join(VANILLA, "*", "levels", "models", "*", "*.bin")
    return sorted(glob.glob(pattern))


def check_structure(path, model):
    """Batch windows must stay inside their segment, indices inside their batch."""
    name = os.path.basename(path)
    for segment in model.segments:
        for index, batch in enumerate(segment.batches):
            if batch.vertex_count < 0 or batch.face_count < 0:
                FAILURES.append(
                    "%s seg%d batch%d has a negative span" % (name, segment.index, index)
                )
                continue
            end = batch.vertex_offset + batch.vertex_count
            if end > len(segment.vertices):
                FAILURES.append(
                    "%s seg%d batch%d addresses vertex %d of %d"
                    % (name, segment.index, index, end, len(segment.vertices))
                )
            if batch.vertex_count > 256:
                FAILURES.append(
                    "%s seg%d batch%d spans %d vertices; the index is a u8"
                    % (name, segment.index, index, batch.vertex_count)
                )
            for face in range(batch.face_offset,
                              min(batch.face_offset + batch.face_count,
                                  len(segment.triangles))):
                _flags, vi0, vi1, vi2 = segment.triangles[face]
                if max(vi0, vi1, vi2) >= max(batch.vertex_count, 1):
                    FAILURES.append(
                        "%s seg%d batch%d triangle %d indexes past its window"
                        % (name, segment.index, index, face)
                    )
                    break


def check_ancient_lake(model):
    for key, expected in ANCIENT_LAKE.items():
        if key == "segment_count":
            actual = len(model.segments)
        elif key == "bounds":
            actual = tuple(model.bounds)
        else:
            actual = getattr(model, key)
        check(actual == expected,
              "ancient_lake %s is %r, the format document says %r"
              % (key, actual, expected))
    print("  ancient_lake matches docs/LEVEL_MODEL_FORMAT.md")


def main():
    paths = find_models()
    if not paths:
        print("SKIP: no extracted level models under %s" % VANILLA)
        return 0

    decoded = 0
    vertices = triangles = batches = 0
    hidden = walls = 0

    for path in paths:
        try:
            model = level_model.load(path)
        except level_model.LevelModelError as error:
            FAILURES.append("%s: %s" % (os.path.basename(path), error))
            continue
        decoded += 1
        vertices += model.vertex_count
        triangles += model.triangle_count
        batches += model.batch_count
        for segment in model.segments:
            for batch in segment.batches:
                hidden += batch.hidden
                walls += batch.invisible_wall
        check_structure(path, model)
        if os.path.basename(path) == "ancient_lake.bin" and "us.v77" in path:
            check_ancient_lake(model)

    print()
    print("models        : %d of %d decoded" % (decoded, len(paths)))
    print("vertices      : %d" % vertices)
    print("triangles     : %d" % triangles)
    print("batches       : %d" % batches)
    print("hidden batches: %d, of which still solid (invisible walls): %d"
          % (hidden, walls))

    if FAILURES:
        print("FAIL          : %d" % len(FAILURES))
        for line in FAILURES[:15]:
            print("  " + line)
        return 1
    print("PASS          : every retail level model decodes and is self-consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
