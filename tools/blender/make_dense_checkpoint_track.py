"""Build a .dkrmap that is a retail course with more checkpoints.

A course to race when what's under test is the checkpoint table, not the
track: DKR-R loads 127 gates per vehicle set where retail loads 60
(``docs/CUSTOM_TRACKS.md``), and no retail course has more than 60. This takes
one, keeps its model, objects and every gate it has, and adds gates between
them along the racing line until one vehicle set holds exactly ``--count``.

    python tools/blender/make_dense_checkpoint_track.py \\
        --assets extern/dkr-decomp/assets/.vanilla/us.v77 \\
        --level AncientLake --count 127 --out build/check-tracks

``--assets`` is an extracted asset tree: the decomp's, or the one
``python -m track_lab.rom_extract ROM DEST`` writes. The package lands in
``<out>/<id>.dkrmap``, in the Custom Tracks world, ready for
``DKR_OWNED_CHECK_TRACKS_DIR`` (``docs/ROLLBACK-SELF-TEST.md``).

Needs no Blender. The header and both object maps go through the encoders the
addon exports with.
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

from track_lab import (  # noqa: E402
    assets, catalog as catalog_module, dkrmap, gltf_io, race_ai, validate,
)

CHECKPOINT = "ASSET_OBJECT_CHECKPOINT"

#: A gate's lane offsets. A new gate takes them from the gates either side of it.
BLENDED = ("unkB", "unkC", "unkD", "unkE", "unkF", "unk10", "unk11", "unk12",
           "unk13", "unk14", "unk15", "unk16")


def _spline(p0, p1, p2, p3, t):
    """Catmull-Rom from ``p1`` to ``p2``, the curve the game's racers follow
    between two gates (``func_80045C48``, ``racer.c``)."""
    return [0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t
                   + (3 * b - a - 3 * c + d) * t * t * t)
            for a, b, c, d in zip(p0, p1, p2, p3)]


def _turn(start, end, t):
    """From one heading to another the short way round, in degrees."""
    return start + ((end - start + 180.0) % 360.0 - 180.0) * t


def densify(gates, count):
    """``count`` gates from the main-route ``gates`` of one vehicle set.

    ``gates`` are in racing order. Every one is kept where it is, the new ones
    are shared out evenly between them, and all are numbered 0 to ``count - 1``
    so the first gate stays the finish line. The input is not changed.
    """
    have = len(gates)
    if have < 4:
        raise ValueError("a set needs four gates for a racing line, and has %d" % have)
    if count < have:
        raise ValueError("the set already has %d gates" % have)
    if count > race_ai.MAX_CHECKPOINTS:
        raise ValueError("DKR-R loads at most %d gates per set" % race_ai.MAX_CHECKPOINTS)
    extra = count - have
    dense = []
    for position, gate in enumerate(gates):
        dense.append(copy.deepcopy(gate))
        before = gates[position - 1]
        after = gates[(position + 1) % have]
        beyond = gates[(position + 2) % have]
        added = extra // have + (1 if position < extra % have else 0)
        for step in range(1, added + 1):
            t = step / (added + 1)
            made = copy.deepcopy(gate)
            made.translation = [float(round(value)) for value in _spline(
                before.translation, gate.translation, after.translation,
                beyond.translation, t)]
            made.fields["angleY"] = _turn(gate.fields["angleY"], after.fields["angleY"], t)
            for name in ("scale",) + BLENDED:
                made.fields[name] = int(round(
                    gate.fields[name] * (1 - t) + after.fields[name] * t))
            dense.append(made)
    for index, gate in enumerate(dense):
        gate.fields["index"] = index
    return dense


def build(tree, level_name, count, out, vehicle_type=0, track_id=""):
    """Write the package and return ``(manifest path, validation issues)``."""
    level = tree.level(level_name)
    if level is None or not level.objects_path or not level.collectables_path:
        raise SystemExit("no level %r in %s; it has %s" % (
            level_name, tree.root, ", ".join(sorted(entry.name for entry in tree.levels()))))
    with open(level.header_path, "r", encoding="utf-8") as handle:
        header = json.load(handle)
    structure = gltf_io.load(level.objects_path)

    def in_set(obj):
        return obj.object_id == CHECKPOINT and obj.fields["vehicleType"] == vehicle_type

    gates = [obj for obj in structure.objects if in_set(obj)]
    if any(gate.fields["isAltCheckpoint"] for gate in gates):
        raise SystemExit("vehicle type %d of %s has an alternate route; pick a set "
                         "without one" % (vehicle_type, level_name))
    gates.sort(key=lambda gate: gate.fields["index"])
    try:
        dense = densify(gates, count)
    except ValueError as error:
        raise SystemExit(str(error)) from error

    # Everything else keeps its place in the map; the set goes where its first gate was.
    objects = []
    for obj in structure.objects:
        if not in_set(obj):
            objects.append(obj)
        elif dense:
            objects.extend(dense)
            dense = None
    structure.objects = objects

    catalog = catalog_module.load()
    header["world"] = "WORLD_CUSTOM_TRACKS"
    name = "%s %d Gates" % (header["name"]["LANGUAGE_ENGLISH"], count)
    track_id = track_id or dkrmap.normalise_id(name)
    package = dkrmap.TrackPackage(
        dkrmap.package_path(out, track_id), track_id, name,
        author="make_dense_checkpoint_track.py",
        revision=os.path.basename(os.path.normpath(tree.root)))
    package.encode_header(header, catalog.raw.get("enumValues", {}), tree.asset_index)
    table = tree.translation_table()
    package.encode_object_map("structure", structure, catalog, table, tree.asset_index)
    # The header encoder leaves both map indices to the runtime, which fills
    # them from what the package ships: so the retail collectables ship too.
    package.encode_object_map("collectables", gltf_io.load(level.collectables_path),
                              catalog, table, tree.asset_index)
    return package.write(), validate.validate(structure, catalog).issues


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--assets", required=True, help="an extracted asset tree, e.g. .vanilla/us.v77")
    parser.add_argument("--level", default="AncientLake",
                        help="the level header's file name, without .json")
    parser.add_argument("--count", type=int, default=race_ai.MAX_CHECKPOINTS,
                        help="gates the vehicle set ends up with")
    parser.add_argument("--vehicle-type", type=int, default=0,
                        help="which set: the checkpoints' vehicleType")
    parser.add_argument("--id", default="", help="the package id; made from the name by default")
    parser.add_argument("--out", required=True, help="the folder to write <id>.dkrmap into")
    args = parser.parse_args(argv)

    tree = assets.AssetTree(os.path.abspath(args.assets))
    if not tree.is_usable():
        raise SystemExit("%s is not an extracted asset tree" % args.assets)
    manifest, issues = build(tree, args.level, args.count, os.path.abspath(args.out),
                             args.vehicle_type, args.id)
    for issue in issues:
        print(issue)
    print("wrote %s" % os.path.dirname(manifest))
    return 1 if any(issue.severity == validate.ERROR for issue in issues) else 0


if __name__ == "__main__":
    sys.exit(main())
