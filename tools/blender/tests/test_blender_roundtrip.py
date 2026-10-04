"""Round-trip retail object maps through an actual Blender scene.

tests/test_roundtrip.py proves the reader and writer agree. This proves the part
that plan step 1 actually cares about: that a map survives becoming Blender
objects and coming back. It is where a coerced integer or a lost field would
show up, since IDProperties, the transform stack and float precision are all in
the path.

Run it with Blender, not with a plain Python:

    blender --background --python tools/blender/tests/test_blender_roundtrip.py

Exits non-zero if any map fails to reproduce byte for byte.
"""

from __future__ import annotations

import json
import os
import sys
import traceback

import bpy

_HERE = os.path.dirname(os.path.abspath(bpy.data.filepath or __file__))
if not os.path.isdir(os.path.join(_HERE, "..", "track_lab")):
    # Blender does not set __file__ the way a normal run does; fall back to the
    # path the script was invoked from.
    for argument in sys.argv:
        if argument.endswith("test_blender_roundtrip.py"):
            _HERE = os.path.dirname(os.path.abspath(argument))
            break

ADDON_ROOT = os.path.abspath(os.path.join(_HERE, ".."))
sys.path.insert(0, ADDON_ROOT)
sys.path.insert(0, _HERE)

import track_lab  # noqa: E402
from track_lab import catalog as catalog_module, gltf_io, scene  # noqa: E402

from test_roundtrip import find_object_maps  # noqa: E402

#: Enough maps to cover every object type without a slow full sweep. Overridden
#: with --all after a `--` separator.
DEFAULT_SAMPLE = 40


def fresh_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def round_trip(path, catalog, tree=None):
    """Import one map into a clean scene, export it, compare the bytes.

    With ``tree`` given, objects are built with the sprite or model artwork the
    game uses. That path has to reproduce the file just as exactly as the plain
    marker path does: the artwork is what an author looks at, never what the
    object map is made of, and a preview that nudged a transform or a field
    would corrupt every track it touched.
    """
    fresh_scene()
    with open(path, "rb") as handle:
        original = handle.read()

    object_map = gltf_io.parse(json.loads(original.decode("utf-8")))
    scene.import_object_map(bpy.context, object_map, catalog, tree)
    rebuilt = scene.export_object_map(bpy.context, catalog)
    rewritten = gltf_io.dumps(rebuilt).encode("utf-8")

    if rewritten == original:
        return None

    if len(rebuilt.objects) != len(object_map.objects):
        return "object count %d -> %d" % (len(object_map.objects), len(rebuilt.objects))

    for before, after in zip(object_map.objects, rebuilt.objects):
        if before.translation != after.translation:
            return "%s moved: %r -> %r" % (
                before.name, before.translation, after.translation
            )
        if before.name != after.name:
            return "name %r -> %r" % (before.name, after.name)
        for key in sorted(set(before.fields) | set(after.fields)):
            a, b = before.fields.get(key, "<missing>"), after.fields.get(key, "<missing>")
            if a != b or type(a) is not type(b):
                return "%s.%s: %r (%s) -> %r (%s)" % (
                    before.object_id, key, a, type(a).__name__, b, type(b).__name__
                )
    return "bytes differ but every field matches; check key order or formatting"


def choose(paths, want_all):
    if want_all:
        return paths
    # Spread the sample across the list so it covers different level kinds.
    if len(paths) <= DEFAULT_SAMPLE:
        return paths
    stride = len(paths) / float(DEFAULT_SAMPLE)
    return [paths[int(i * stride)] for i in range(DEFAULT_SAMPLE)]


def main():
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    want_all = "--all" in arguments

    track_lab.register()
    try:
        catalog = catalog_module.load()
        paths = find_object_maps()
        if not paths:
            print("SKIP: no extracted object maps found")
            return 0

        selected = choose(paths, want_all)
        failures = []
        objects = 0
        for path in selected:
            problem = round_trip(path, catalog)
            objects += len(gltf_io.load(path).objects)
            if problem:
                failures.append("markers %s: %s" % (os.path.basename(path), problem))

        # The same maps again, this time built with the artwork an author sees.
        from track_lab import assets  # noqa: PLC0415 - optional path

        with_artwork = 0
        for path in selected:
            tree = assets.AssetTree.find(path)
            if tree is None:
                continue
            with_artwork += 1
            problem = round_trip(path, catalog, tree)
            if problem:
                failures.append("artwork %s: %s" % (os.path.basename(path), problem))

        print()
        print("blender      : %s" % bpy.app.version_string)
        print("maps tested  : %d of %d" % (len(selected), len(paths)))
        print("with artwork : %d" % with_artwork)
        print("objects      : %d" % objects)
        if failures:
            print("FAIL         : %d" % len(failures))
            for line in failures[:15]:
                print("  " + line)
            return 1
        print("PASS         : byte-exact through a Blender scene, markers and artwork")
        return 0
    finally:
        track_lab.unregister()


if __name__ == "__main__":
    # What vanishes under Blender is an **uncaught exception**, not sys.exit:
    # measured on 5.2, a bare sys.exit(3) exits 3 and so does sys.exit(main()),
    # while a NameError makes Blender print the traceback and still exit 0. So a
    # crashing test read as a pass, stopped the suite where it stood, and hid
    # every check after it - which is what this catch repairs.
    try:
        _code = main()
    except BaseException:  # noqa: BLE001 - the point is to report anything
        traceback.print_exc()
        _code = 1
    sys.exit(_code)
