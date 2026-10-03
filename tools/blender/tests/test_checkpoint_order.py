"""Renumbering checkpoints by clicking them in driving order.

Runs on any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_checkpoint_order.py
"""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor.checkpoint_order import Chain, ChainError  # noqa: E402

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def by_index(indices):
    return [k for k, _ in sorted(indices.items(), key=lambda kv: kv[1])]


def test_the_example():
    """22 is right, the gate that should follow it is 56: click it, it is 23."""
    gates = [("g%d" % i, i) for i in range(0, 23)] + [("late", 56)] + \
            [("g%d" % i, i) for i in range(23, 30)]
    chain = Chain(gates, "g22")
    check(chain.next_index == 23, "the first click hands out 23 (%d)" % chain.next_index)
    indices = chain.take("late")
    check(indices["late"] == 23, "the gate clicked after 22 becomes 23 (%d)" % indices["late"])
    check([indices["g%d" % i] for i in range(23, 30)] == list(range(24, 31)),
          "the gates after it move up one, in their order (%r)"
          % [indices["g%d" % i] for i in range(23, 30)])
    check(all(indices["g%d" % i] == i for i in range(0, 23)),
          "the gates up to 22 keep their numbers")
    check(len(set(indices.values())) == len(indices), "no index is used twice")
    check(chain.changed() == {"late": 23, **{"g%d" % i: i + 1 for i in range(23, 30)}},
          "only the gates that changed are reported (%r)" % chain.changed())


def test_iterating():
    """Each click goes after the last one, so a whole stretch can be redone."""
    gates = [("a", 0), ("b", 2), ("c", 4), ("d", 6), ("e", 8), ("f", 10)]
    chain = Chain(gates, "b")
    chain.take("e")
    indices = chain.take("d")
    check(by_index(indices) == ["a", "b", "e", "d", "c", "f"],
          "clicked gates follow the anchor in click order (%r)" % by_index(indices))
    check([indices[k] for k in ("a", "b", "e", "d", "c", "f")] == [0, 2, 3, 4, 5, 6],
          "and are numbered on from the anchor (%r)" % indices)
    check(chain.cursor == "d" and chain.next_index == 5,
          "the next click goes after d, as 5")

    indices = chain.back()
    check(by_index(indices) == ["a", "b", "e", "c", "d", "f"] and chain.cursor == "e",
          "taking back a click puts that gate back (%r)" % by_index(indices))
    indices = chain.back()
    check(indices == {"a": 0, "b": 2, "c": 3, "d": 4, "e": 5, "f": 6},
          "taking back every click leaves the order, numbered on from the anchor (%r)"
          % indices)


def test_gate_from_before_the_anchor():
    """A gate numbered too low is moved, and the gates before keep their numbers."""
    gates = [("a", 0), ("stray", 3), ("b", 6), ("c", 8), ("d", 10)]
    chain = Chain(gates, "c")
    indices = chain.take("stray")
    check(by_index(indices) == ["a", "b", "c", "stray", "d"],
          "a gate clicked from before the anchor moves after it (%r)" % by_index(indices))
    check(indices["a"] == 0 and indices["b"] == 6 and indices["stray"] == 9,
          "the gates before keep their numbers, gaps and all (%r)" % indices)


def test_refusals():
    chain = Chain([("a", 0), ("b", 1), ("c", 2)], "a")
    for key, why in (("a", "the anchor"), ("nope", "a gate of another set")):
        try:
            chain.take(key)
        except ChainError:
            pass
        else:
            FAILURES.append("clicking %s is refused" % why)
    chain.take("c")
    try:
        chain.take("c")
    except ChainError:
        pass
    else:
        FAILURES.append("clicking a gate twice is refused")

    full = Chain([("a", 250), ("b", 251), ("c", 252), ("d", 253), ("e", 254), ("f", 255),
                  ("g", 100)], "f")
    try:
        full.take("g")
    except ChainError:
        check(full.taken == [], "a refused click is not kept")
    else:
        FAILURES.append("numbering past 255 is refused")


def test_alternates_follow():
    """An alternate gate pairs by index, so it follows its main gate."""
    gates = [("a", 0), ("b", 2), ("c", 4), ("d", 6)]
    chain = Chain(gates, "a", alternates=[("alt-c", 4), ("alt-lonely", 9)])
    indices = chain.take("c")
    check(indices["c"] == 1 and indices["alt-c"] == 1,
          "an alternate follows its main gate to its new number (%r)" % indices)
    check(indices["alt-lonely"] == 9,
          "an alternate that pairs with nothing keeps its number")


def main():
    test_the_example()
    test_iterating()
    test_gate_from_before_the_anchor()
    test_refusals()
    test_alternates_follow()
    if FAILURES:
        print("FAIL: %d" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: checkpoints renumber in the order they are clicked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
