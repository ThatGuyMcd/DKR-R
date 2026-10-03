"""Round-trip gate for the object-map reader and writer.

Step 1 of docs/BLENDER_ADDON_PLAN.md: read every retail object map, push it
through the in-memory representation the addon edits, write it back and require
the bytes to be identical. Nothing else in the addon is trustworthy until this
holds, because a single coerced integer silently changes an object's behaviour
in game.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_roundtrip.py
"""

from __future__ import annotations

import glob
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import gltf_io  # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(
    REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla",
)


def find_object_maps():
    """Every extracted retail object map, across whichever revisions are present."""
    pattern = os.path.join(VANILLA, "*", "levels", "objectMaps", "*", "*.gltf")
    return sorted(glob.glob(pattern))


def check_bytes(path):
    """Read -> model -> write must reproduce the file exactly."""
    with open(path, "rb") as handle:
        original = handle.read()
    object_map = gltf_io.parse(json.loads(original.decode("utf-8")))
    rewritten = gltf_io.dumps(object_map).encode("utf-8")
    if rewritten == original:
        return None
    for index, (a, b) in enumerate(zip(original, rewritten)):
        if a != b:
            return "byte %d: retail %r != rewritten %r" % (
                index,
                original[max(0, index - 40):index + 40],
                rewritten[max(0, index - 40):index + 40],
            )
    return "length %d != %d" % (len(original), len(rewritten))


def check_model(path):
    """The model must expose every field, not silently drop unknown ones."""
    object_map = gltf_io.load(path)
    document = json.load(open(path, "r", encoding="utf-8"))
    nodes = document["nodes"][1:]
    if len(object_map.objects) != len(nodes):
        return "parsed %d objects, document has %d nodes" % (
            len(object_map.objects), len(nodes),
        )
    for obj, node in zip(object_map.objects, nodes):
        if obj.extras != node["extras"]:
            return "extras differ for %s" % obj.name
    return None


def main():
    paths = find_object_maps()
    if not paths:
        print("SKIP: no extracted object maps under %s" % VANILLA)
        print("      run the decomp's extract.sh first")
        return 0

    failures = []
    objects = 0
    for path in paths:
        for check in (check_bytes, check_model):
            problem = check(path)
            if problem:
                failures.append("%s: %s" % (os.path.relpath(path, REPO_ROOT), problem))
        objects += len(gltf_io.load(path).objects)

    print("object maps : %d" % len(paths))
    print("objects     : %d" % objects)
    if failures:
        print("FAIL        : %d" % len(failures))
        for line in failures[:10]:
            print("  " + line)
        return 1
    print("PASS        : byte-exact round trip")
    return 0


if __name__ == "__main__":
    sys.exit(main())
