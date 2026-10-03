"""Gate for :mod:`race_ai`: the line the bots drive and the header's AI bytes.

The overlay is only worth drawing if it is where the bots actually go, so the
checks here hold the module to the game's arithmetic rather than to taste:

* **Lanes sit where ``func_80045C48`` puts them** - ``node.scale`` from the
  scale byte, the lateral offset along ``(cos yaw, 0, -sin yaw)``.
* **The spline is the game's.** It passes through every lane point, and a
  racer on the alternate route leaves and rejoins the main line at gates.
* **Loading follows ``checkpoint_update_all``** - vehicle set filter, sort on
  ``index + 255`` for alternates, pairing by index, the 60-gate ceiling.
* **Every retail map builds**, every set of it, into finite lines.
* **The header bytes are the right bytes.** Each pointer lands on the offset
  the game reads, the two mislabelled ``ai-levels`` slots included, and an
  answer reaches the encoded header.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_race_ai.py
"""

from __future__ import annotations

import glob
import json
import math
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from track_lab import (  # noqa: E402
    catalog, gltf_io, level_header, level_header_template as template, race_ai,
)

from test_roundtrip import VANILLA, find_object_maps  # noqa: E402

RETAIL_LANES = (-64, -22, 22, 64)


def _gate(x, z, angle=0.0, index=0, lateral=RETAIL_LANES, vertical=(0, 0, 0, 0),
          alternate=0, vehicle=0, scale=64, y=0.0):
    fields = {"scale": scale, "index": index, "angleY": angle,
              "isAltCheckpoint": alternate, "vehicleType": vehicle}
    fields.update(zip(race_ai.LATERAL_FIELDS, lateral))
    fields.update(zip(race_ai.VERTICAL_FIELDS, vertical))
    return gltf_io.MapObject(object_id=race_ai.CHECKPOINT, name="Checkpoint",
                             translation=[x, y, z], fields=fields)


def _close(a, b, tolerance=1e-6):
    return all(abs(p - q) <= tolerance for p, q in zip(a, b))


# ---------------------------------------------------------------------------

def check_lane_offsets():
    """A lane point is the gate moved by node.scale along the gate's plane."""
    node = race_ai.node_from(_gate(100.0, 200.0, 0.0, lateral=(10, 0, 0, 0),
                                   vertical=(5, 0, 0, 0)))
    if abs(node.scale - 2.0) > 1e-12:
        return "scale 64 should give node.scale 2, got %r" % node.scale
    if not _close(node.lane_point(0), (120.0, 10.0, 200.0)):
        return "facing +Z, lane 1 at +10 should sit 20 units along +X: %r" % (
            node.lane_point(0),)

    # angleY 16 is a quarter turn: sin 1, cos 0, so the lateral runs along -Z.
    turned = race_ai.node_from(_gate(0.0, 0.0, 90.0, lateral=(10, 0, 0, 0)))
    if not _close(turned.lane_point(0), (0.0, 0.0, -20.0)):
        return "a quarter turn should send lane 1 to -Z: %r" % (turned.lane_point(0),)

    # The same direction straight out of mtxf_from_transform with a Y rotation
    # alone: row 2 is (cos x * sin y, -sin x, cos x * cos y), the gate's normal,
    # and racer.c adds (rotationZFrac, 0, -rotationXFrac) * offset.
    for degrees in (0.0, 33.75, 90.0, 180.0, 309.375, -410.625):
        yaw = math.radians(degrees)
        normal = (math.sin(yaw), 0.0, math.cos(yaw))
        expected = (2.0 * normal[2] * 7, 0.0, -2.0 * normal[0] * 7)
        got = race_ai.node_from(_gate(0.0, 0.0, degrees, lateral=(7, 0, 0, 0))).lane_point(0)
        if not _close(got, expected):
            return "at %g degrees the lane point %r is not %r" % (degrees, got, expected)

    tiny = race_ai.node_from(_gate(0.0, 0.0, scale=0))
    if abs(tiny.scale - 5 / 64.0 * 2) > 1e-12:
        return "obj_init_checkpoint clamps the scale byte up to 5"
    return None


def _square(side=1000.0, **extra):
    corners = [(0.0, 0.0), (side, 0.0), (side, side), (0.0, side)]
    return [_gate(x, z, index=i * 2, **extra) for i, (x, z) in enumerate(corners)]


def check_spline_passes_the_gates():
    route = race_ai.build_route(_square())
    for lane in range(race_ai.LANES):
        line = race_ai.lane_line(route, lane)
        if len(line) != len(route.main) * race_ai.SAMPLES + 1:
            return "lane %d has %d points" % (lane, len(line))
        if not _close(line[0], line[-1]):
            return "lane %d does not close" % lane
        for position, node in enumerate(route.main):
            if not _close(line[position * race_ai.SAMPLES], node.lane_point(lane)):
                return "lane %d misses gate %d" % (lane, position)
    if race_ai.lane_line(race_ai.build_route(_square()[:1]), 0):
        return "one gate is not a line"
    return None


def check_loading_rules():
    gates = [
        _gate(0, 0, index=4), _gate(0, 0, index=0), _gate(0, 0, index=2),
        _gate(5, 5, index=2, alternate=1), _gate(9, 9, index=9, alternate=1),
        _gate(0, 0, index=1, vehicle=1),
    ]
    route = race_ai.build_route(gates, 0)
    if [n.index for n in route.main] != [0, 2, 4]:
        return "main gates are not sorted by index: %r" % [n.index for n in route.main]
    if set(route.alternate_of) != {1} or route.alternate_of[1].index != 2:
        return "the index 2 alternate should pair with the second main gate"
    if [n.index for n in route.unpaired] != [9]:
        return "an alternate naming no main gate is unpaired"
    if route.node_at(1, True) is not route.alternate_of[1] or route.node_at(1) is not route.main[1]:
        return "node_at does not follow find_next_checkpoint_node"
    if [n.index for n in race_ai.build_route(gates, 1).main] != [1]:
        return "another vehicle set's gates leaked in"
    if race_ai.vehicle_sets(gates) != {0: 5, 1: 1}:
        return "vehicle_sets miscounts: %r" % race_ai.vehicle_sets(gates)

    doubled = race_ai.build_route([_gate(0, 0, index=0), _gate(1, 1, index=0)])
    if doubled.duplicates != [0]:
        return "two gates on index 0 are a duplicate"

    many = race_ai.build_route([_gate(i, 0, index=i) for i in range(65)])
    if len(many.main) != race_ai.MAX_CHECKPOINTS or many.dropped != 5:
        return "the game loads 60 gates a set and drops the rest"
    return None


def check_alternate_line_rejoins():
    gates = [_gate(i * 500.0, 0.0, index=i) for i in range(6)]
    gates.append(_gate(2 * 500.0, 800.0, index=2, alternate=1))
    route = race_ai.build_route(gates)
    lines = race_ai.alternate_lines(route, 0)
    if len(lines) != 1:
        return "one alternate gate makes one detour, got %d" % len(lines)
    detour = lines[0]
    # Stretches 0..3 have gate 2 among their control points.
    if len(detour) != 4 * race_ai.SAMPLES + 1:
        return "the detour covers %d points" % len(detour)
    if not _close(detour[0], route.main[0].lane_point(0)):
        return "the detour does not leave the main line at a gate"
    if not _close(detour[-1], route.main[4].lane_point(0)):
        return "the detour does not rejoin the main line at a gate"
    if not _close(detour[2 * race_ai.SAMPLES], route.alternate_of[2].lane_point(0)):
        return "the detour does not pass the alternate gate"
    if race_ai.alternate_lines(race_ai.build_route(gates[:-1]), 0):
        return "no alternate gate, no detour"
    return None


def check_retail_routes():
    paths = find_object_maps()
    if not paths:
        print("  skip: no extracted object maps")
        return None
    maps = sets = largest = dropped = unpaired = duplicated = 0
    for path in paths:
        object_map = gltf_io.load(path)
        counts = race_ai.vehicle_sets(object_map.objects)
        if not counts:
            continue
        maps += 1
        for vehicle_set in counts:
            route = race_ai.build_route(object_map.objects, vehicle_set)
            sets += 1
            largest = max(largest, len(route.main) + len(route.alternates))
            dropped += route.dropped
            unpaired += len(route.unpaired)
            duplicated += len(route.duplicates)
            for lane in range(race_ai.LANES):
                lines = [race_ai.lane_line(route, lane)]
                lines += race_ai.alternate_lines(route, lane)
                for line in lines:
                    if any(not math.isfinite(c) for point in line for c in point):
                        return "%s set %d lane %d is not finite" % (
                            os.path.basename(path), vehicle_set, lane)
                if len(route.main) >= 2 and len(lines[0]) != len(route.main) * race_ai.SAMPLES + 1:
                    return "%s set %d lane %d is the wrong length" % (
                        os.path.basename(path), vehicle_set, lane)
    print("  %d retail maps with checkpoints, %d sets, largest set %d gates, "
          "%d dropped, %d unpaired alternates, %d duplicate ids"
          % (maps, sets, largest, dropped, unpaired, duplicated))
    if dropped:
        return "a retail set passes the 60-gate ceiling, so the ceiling is wrong"
    return None


def check_header_bytes():
    layout = {field.pointer: field for field in level_header.LAYOUT}
    expected = {}
    for number, (slot, _name, _help) in enumerate(race_ai.AI_LEVEL_SLOTS):
        expected[race_ai.ai_level_pointer("adv1", slot)] = 0x20 + number
        expected[race_ai.ai_level_pointer("adv2", slot)] = 0x25 + number
    for index in range(len(race_ai.CHARACTERS)):
        expected[race_ai.skill_pointer(index)] = 0x0C + index
        expected[race_ai.skill_pointer(index, True)] = 0x16 + index
    for index in range(3):
        expected[race_ai.vehicle_set_pointer(index)] = 0x4F + index
    if set(expected) != set(race_ai.HEADER_POINTERS):
        return "HEADER_POINTERS is not the 33 bytes the game reads"
    for pointer, offset in expected.items():
        field = layout.get(pointer)
        if field is None or field.offset != offset:
            return "%s should be byte 0x%X" % (pointer, offset)

    # aitable_init picks byte 1 for RACE_CLEARED; the asset tool names it
    # silver-coins. The label has to follow the game.
    labels = {slot: name for slot, name, _help in race_ai.AI_LEVEL_SLOTS}
    if layout[race_ai.ai_level_pointer("adv1", "silver-coins")].offset != 0x21 \
            or labels["silver-coins"] != "Race won":
        return "byte 0x21 is the race-won level, whatever the JSON calls it"

    for choice in race_ai.HEADER_CHOICES:
        field = layout[choice.pointer]
        if (choice.kind, choice.subject, choice.ctype) != (field.kind, field.subject, field.ctype):
            return "%s does not describe its field" % choice.pointer
        if not choice.label:
            return "%s has no label" % choice.pointer
    return None


def check_answers_encode():
    answers = {
        race_ai.ai_level_pointer("adv1", "silver-coins"): 4,
        race_ai.ai_level_pointer("adv2", "trophy-race"): 9,
        race_ai.skill_pointer(3): 0,
        race_ai.skill_pointer(9, True): 4,
        race_ai.vehicle_set_pointer(2): 1,
    }
    document = template.document(answers)
    payload = level_header.encode(document, catalog.load().raw.get("enumValues", {}))
    for offset, value in ((0x21, 4), (0x29, 9), (0x0F, 0), (0x1F, 4), (0x51, 1)):
        if payload[offset] != value:
            return "byte 0x%X is %d, not %d" % (offset, payload[offset], value)
    for pointer in race_ai.HEADER_POINTERS:
        if race_ai.header_default(pointer) is None:
            return "%s has no surveyed default" % pointer
    return None


def check_retail_difficulty():
    headers = sorted(glob.glob(os.path.join(VANILLA, "*", "levels", "headers", "*.json")))
    if not headers:
        print("  skip: no extracted level headers")
        return None
    lake = [p for p in headers if os.path.basename(p).startswith("AncientLake")]
    if lake:
        with open(lake[0], "r", encoding="utf-8") as handle:
            found = race_ai.difficulty_from(json.load(handle))
        if found.get(race_ai.skill_pointer(2)) != 1 or \
                found.get(race_ai.ai_level_pointer("adv1", "trophy-race")) != 6:
            return "Ancient Lake's difficulty did not read back: %r" % found
    races = 0
    for path in headers:
        with open(path, "r", encoding="utf-8") as handle:
            header = json.load(handle)
        if header.get("race-type") != "RACETYPE_DEFAULT":
            continue
        races += 1
        found = race_ai.difficulty_from(header)
        if len(found) != len(race_ai.DIFFICULTY_POINTERS):
            return "%s is missing AI bytes" % os.path.basename(path)
        for pointer in race_ai.AI_LEVEL_POINTERS:
            if not 0 <= found[pointer] < race_ai.BEHAVIOUR_LEVELS:
                return "%s uses level %d, past the tables" % (
                    os.path.basename(path), found[pointer])
    print("  %d retail races read" % races)
    if template.lookup({"a": [1, 2]}, "/a/5") is not None:
        return "lookup past the end of a list should be None"
    return None


def check_skill_words():
    if race_ai.start_delay(0) or race_ai.start_delay(2) or race_ai.start_delay(4) != 8:
        return "start delays are (skill - 2) * 4 frames, never negative"
    if "12 frames" not in race_ai.describe_skill(5):
        return "retail's skill 5 is 12 frames late"
    if "always" not in race_ai.describe_skill(0):
        return "Master always gets the start boost"
    if len(race_ai.describe_levels().split(";")) != race_ai.BEHAVIOUR_LEVELS:
        return "every behaviour level is described"
    return None


CHECKS = [
    ("lane offsets", check_lane_offsets),
    ("spline passes the gates", check_spline_passes_the_gates),
    ("loading rules", check_loading_rules),
    ("alternate line rejoins", check_alternate_line_rejoins),
    ("retail routes", check_retail_routes),
    ("header bytes", check_header_bytes),
    ("answers encode", check_answers_encode),
    ("retail difficulty", check_retail_difficulty),
    ("skill words", check_skill_words),
]


def main():
    failures = 0
    for name, check in CHECKS:
        problem = check()
        if problem:
            failures += 1
            print("FAIL %s: %s" % (name, problem))
        else:
            print("ok   %s" % name)
    print()
    print("FAIL: %d check(s)" % failures if failures else "PASS: race AI")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
