"""Adding gates to a retail course's checkpoint set.

Runs on any Python 3.8+; it does not need Blender or an asset tree.

    python tools/blender/tests/test_dense_checkpoint_track.py
"""

from __future__ import annotations

import copy
import math
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from make_dense_checkpoint_track import BLENDED, CHECKPOINT, densify  # noqa: E402
from track_lab import race_ai  # noqa: E402
from track_lab.gltf_io import MapObject  # noqa: E402

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def gate(index, x, z, angle=0.0, scale=64, lane=0):
    fields = {"index": index, "angleY": angle, "scale": scale, "isAltCheckpoint": 0,
              "vehicleType": 0, "unk18": -1, "unk19": 0, "unk1B": 0}
    fields.update({name: lane for name in BLENDED})
    return MapObject(CHECKPOINT, "checkpoint", [float(x), 50.0, float(z)], fields)


def ring(count, radius=4000.0):
    """Gates round a circle, numbered with gaps the way retail numbers them."""
    gates = []
    for position in range(count):
        turn = 2 * math.pi * position / count
        gates.append(gate(position * 2, round(radius * math.cos(turn)),
                          round(radius * math.sin(turn)),
                          angle=360.0 * position / count, scale=60 + position,
                          lane=position * 4 - 20))
    return gates


def test_the_full_table():
    gates = ring(18)
    before = copy.deepcopy(gates)
    dense = densify(gates, race_ai.MAX_CHECKPOINTS)
    check(len(dense) == 127, "18 gates become 127 (%d)" % len(dense))
    check([g.fields["index"] for g in dense] == list(range(127)),
          "numbered 0 to 126 with no gaps")
    check(all(a.translation == b.translation and a.fields == b.fields
              for a, b in zip(gates, before)), "the gates passed in are not changed")
    kept = [g for g in dense if g.translation in [o.translation for o in gates]]
    check([g.translation for g in kept] == [o.translation for o in gates],
          "every original gate is still there, in order")
    check(dense[0].translation == gates[0].translation,
          "the first gate, the finish line, is where it was")
    # 109 new gates over 18 gaps: one gap gets seven, the rest six.
    starts = [dense.index(g) for g in kept] + [127]
    gaps = [b - a - 1 for a, b in zip(starts, starts[1:])]
    check(sorted(gaps) == [6] * 17 + [7], "new gates are shared out evenly (%r)" % gaps)


def test_new_gates_follow_the_line():
    gates = ring(12)
    dense = densify(gates, 48)
    radii = [math.hypot(g.translation[0], g.translation[2]) for g in dense]
    check(all(abs(radius - 4000.0) < 40.0 for radius in radii),
          "gates added to a circle stay on it (%.0f to %.0f)" % (min(radii), max(radii)))
    turns = [math.atan2(g.translation[2], g.translation[0]) % (2 * math.pi) for g in dense]
    check(turns == sorted(turns), "and in racing order round it")
    check(all(g.translation == [float(round(v)) for v in g.translation] for g in dense),
          "positions are whole units, as an object map stores them")
    between = dense[1:4]
    check(all(60 <= g.fields["scale"] <= 61 for g in between)
          and all(-20 <= g.fields[BLENDED[0]] <= -16 for g in between),
          "width and lane offsets run from one neighbour to the other")


def test_headings_turn_the_short_way():
    gates = [gate(0, 0, 0, angle=350.0), gate(1, 0, 1000, angle=10.0),
             gate(2, 1000, 1000, angle=-170.0), gate(3, 1000, 0, angle=170.0)]
    dense = densify(gates, 8)
    check(abs(dense[1].fields["angleY"] - 360.0) < 1e-9,
          "350 to 10 passes 360, not 180 (%r)" % dense[1].fields["angleY"])
    check(abs(dense[5].fields["angleY"] + 180.0) < 1e-9,
          "-170 to 170 passes -180, not 0 (%r)" % dense[5].fields["angleY"])


def test_refusals():
    for count, what in ((17, "fewer gates than the set has"),
                        (race_ai.MAX_CHECKPOINTS + 1, "more than the runtime loads")):
        try:
            densify(ring(18), count)
            check(False, "asking for %s is refused" % what)
        except ValueError:
            pass
    try:
        densify(ring(3), 20)
        check(False, "a set too short for a racing line is refused")
    except ValueError:
        pass
    check(len(densify(ring(18), 18)) == 18, "asking for what is there changes nothing")


def main():
    test_the_full_table()
    test_new_gates_follow_the_line()
    test_headings_turn_the_short_way()
    test_refusals()
    if FAILURES:
        print("FAIL: %d" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: a checkpoint set grows to the count asked for along its racing line")
    return 0


if __name__ == "__main__":
    sys.exit(main())
