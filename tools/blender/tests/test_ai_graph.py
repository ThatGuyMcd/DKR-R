"""Check the AI node graph helpers the arena tools and the overlay stand on.

What a bot does with the graph (``racer.c:682``, ``objects.c:7675``): it walks
links, and a bot without a weapon heads for the destination-class-1 node
nearest by path, never the one it stands on. The helpers that model that, the
slot arithmetic behind Link and Unlink, the balloon-reach checks, and the
overlay's geometry, all without Blender.

    python tools/blender/tests/test_ai_graph.py
"""

from __future__ import annotations

import math
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import ai_graph as ag, ai_graph_view as view  # noqa: E402

FAILURES = []
E = ag.NO_NEIGHBOUR


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def chain(count, spacing=100.0):
    """Nodes 0..count-1 in a line along X, each linked to the next."""
    nodes = []
    for i in range(count):
        adjacent = [n for n in (i - 1, i + 1) if 0 <= n < count]
        nodes.append((i, adjacent + [E] * (4 - len(adjacent))))
    positions = {i: (i * spacing, 0.0, 0.0) for i in range(count)}
    return nodes, positions


# ---------------------------------------------------------------------------

def test_adjacency():
    print("reading adjacency the way the scene may hold it")
    found = ag.adjacency([(0, [1, 255, 7, 1]), (1, [0, 1, 255, 255]),
                          (2, [255, 255, 255, 255]), (0, [2, 255, 255, 255])])
    check(found == {0: [1], 1: [0], 2: []},
          "empty slots, missing ids, self links, repeats and a second node 0 "
          "are dropped (%r)" % found)
    check(ag.adjacency([("x", [1]), (3, None)]) == {3: []},
          "a node with no id or no slots does not break it")
    check(ag.links({0: [1, 2], 1: [0], 2: []}) == [(0, 1), (0, 2)],
          "each link is listed once, one-way ones included")


def test_heat():
    print("how far each node is from the selected one")
    nodes, positions = chain(5)
    adjacent = ag.adjacency(nodes)
    check(ag.steps_from(adjacent, 2) == {2: 0, 1: 1, 3: 1, 0: 2, 4: 2},
          "steps go out both ways along a chain")
    check(ag.steps_from(adjacent, 9) == {}, "an unknown start reaches nothing")

    # A square with a long side: 0-1-2 is shorter than 0-3-2 by distance,
    # though both are two steps.
    positions = {0: (0, 0, 0), 1: (100, 0, 0), 2: (100, 100, 0), 3: (-400, 300, 0)}
    adjacent = {0: [1, 3], 1: [0, 2], 2: [1, 3], 3: [0, 2]}
    distance, previous = ag.path_distances(adjacent, positions, 0)
    check(abs(distance[2] - 200.0) < 1e-6 and previous[2] == 1,
          "Dijkstra takes the short way round (%r)" % distance)
    check(abs(distance[3] - 500.0) < 1e-6, "and measures each link's length")

    split = {0: [1], 1: [0], 2: [3], 3: [2]}
    check(set(ag.steps_from(split, 0)) == {0, 1},
          "a disconnected piece is not reached")


def test_bot_route():
    print("the route a bot without a weapon drives")
    nodes, positions = chain(6)
    adjacent = ag.adjacency(nodes)
    classes = {0: 1, 5: 1}
    check(ag.bot_route(adjacent, positions, classes, 2) == [2, 1, 0],
          "to the nearest weapon node by path")
    check(ag.bot_route(adjacent, positions, classes, 0) == [0, 1, 2, 3, 4, 5],
          "standing on one, it goes on to the next: never its own node")
    check(ag.bot_route(adjacent, positions, {}, 2) == [],
          "no weapon node, no route")
    check(ag.bot_route({0: [], 1: []}, {0: (0, 0, 0), 1: (9, 0, 0)}, {1: 1}, 0) == [],
          "an unreachable weapon node is no route")

    # Nearest by path, not by straight line: node 3 is beside node 0 in
    # space but four links away; node 2 is further in space but closer.
    positions = {0: (0, 0, 0), 1: (300, 0, 0), 2: (600, 0, 0),
                 4: (300, 400, 0), 5: (0, 400, 0), 3: (0, 60, 0)}
    adjacent = {0: [1], 1: [0, 2, 4], 2: [1], 4: [1, 5], 5: [4, 3], 3: [5]}
    check(ag.bot_route(adjacent, positions, {2: 1, 3: 1}, 0) == [0, 1, 2],
          "the pathfinder measures along the links, not across")


def test_slots():
    print("linking and unlinking slots")
    check(ag.link_slots([4, E, E, E], 9) == [4, 9, E, E], "takes the first free slot")
    check(ag.link_slots([4, 9, E, E], 9) == [4, 9, E, E], "already linked: unchanged")
    check(ag.link_slots([], 3) == [3, E, E, E], "pads missing slots")
    try:
        ag.link_slots([1, 2, 3, 4], 5)
        check(False, "a fifth neighbour is refused")
    except ag.AiGraphError:
        check(True, "a fifth neighbour is refused")
    check(ag.unlink_slots([4, 9, 7, E], 9) == [4, 7, E, E],
          "unlinking moves the rest up, as retail packs them")
    check(ag.unlink_slots([4, E, E, E], 9) == [4, E, E, E], "unlinking a stranger does nothing")
    check(ag.free_slots([1, E, 2, E]) == 2 and ag.free_slots([1, 2]) == 2
          and ag.free_slots([1, 2, 3, 4]) == 0, "counts free slots")


def test_link_targets():
    print("where a new weapon node links")
    candidates = [(0, (0, 0, 0), 2), (1, (100, 0, 0), 2), (2, (200, 0, 0), 0),
                  (3, (5000, 0, 0), 3)]
    check(ag.link_targets((90, 0, 0), candidates) == [1, 0],
          "the two nearest, nearest first")
    check(ag.link_targets((190, 0, 0), candidates) == [1, 0],
          "skipping a node with no free slot")
    check(ag.link_targets((4000, 0, 0), candidates) == [3],
          "and anything beyond %d units" % ag.AUTO_LINK_RANGE)
    check(ag.link_targets((90, 0, 0), candidates, count=1) == [1],
          "as many as the new node has room for")
    check(ag.link_targets((0, 0, 0), []) == [], "no graph, no links")


def test_reach():
    print("balloons on their nodes")
    nodes = [(0, 0, 0), (1000, 0, 0)]
    balloons = [(30, 0, 0), (1000, 0, 80), (5000, 0, 0)]
    far = ag.balloons_off_their_node(nodes, balloons)
    check([index for index, _d in far] == [1, 2],
          "a balloon more than %d from its nearest node is reported (%r)"
          % (ag.WEAPON_REACH, far))
    check(abs(far[0][1] - 80.0) < 1e-6, "with the distance")
    check(ag.balloons_off_their_node([], balloons) == [], "no nodes, nothing to say")

    found = ag.weapon_reach([(4, (0, 0, 0), 1), (5, (500, 0, 0), 0),
                             (6, (900, 0, 0), 1)], [(20, 0, 0)])
    check([(n, round(d)) for n, d in found] == [(4, 20), (6, 880)],
          "weapon reach measures each class-1 node to its nearest balloon")
    check(ag.weapon_reach([(4, (0, 0, 0), 1)], []) == [(4, None)],
          "with no balloon the distance is None")
    check(ag.describe_class(5) == "Bank node of racer 1"
          and ag.describe_class(1) == "Weapon node", "classes read in words")


def test_view():
    print("the overlay's geometry")
    nodes, positions = chain(5)
    views = [view.NodeView(i, positions[i], 1 if i in (0, 4) else 0, slots)
             for i, slots in nodes]
    red = view.BALLOON_COLOURS["BALLOON_TYPE_MISSILE"]
    balloons = [((10.0, 0.0, 0.0), red)]

    plain = view.build(views, balloons)
    check(len(plain["links"][0]) == 8 and len(plain["links"][1]) == 8,
          "four links, two vertices each, one colour per vertex")
    check(all(c == view.LINK_COLOUR for c in plain["links"][1]),
          "with no node active, links are neutral")
    check(plain["reach"] == {0: True, 4: False},
          "node 0 has a balloon in reach, node 4 not (%r)" % plain["reach"])
    circles = plain["circles"][1]
    half = len(circles) // 2
    check(circles[:half] == [view.IN_REACH] * half
          and circles[half:] == [view.OUT_OF_REACH] * half,
          "ground circles green where a balloon is in reach, red where not")
    radius = math.dist(plain["circles"][0][0][:2], positions[0][:2])
    check(abs(radius - ag.WEAPON_REACH) < 1e-6, "of radius %d" % ag.WEAPON_REACH)
    sizes = [layer[0] for layer in plain["points"]]
    check(sizes == sorted(sizes, reverse=True), "point layers biggest first")
    ring = [c for size, _p, colours in plain["points"]
            if size == view.NODE_SIZE + view.RING_WIDTH for c in colours]
    gap = [c for size, _p, colours in plain["points"]
           if size == view.NODE_SIZE + view.RING_GAP for c in colours]
    check(gap and all(c == view.RING_CORE for c in gap),
          "a dark gap separates each ring from its dot")
    check(red in ring, "node 0's ring takes the nearest balloon's colour")
    check(view.UNKNOWN_BALLOON in ring, "node 4, with none near, is white")
    check(plain["route"] == [], "no node active, no route")
    check([text for _p, text, _c in plain["labels"]] == [view.NO_BALLOON]
          and plain["labels"][0][0] == positions[4],
          "the only words are under node 4, which has no balloon in reach")

    lit = view.build(views, balloons, active=2, show_ids=True)
    colours = dict(zip(lit["links"][0], lit["links"][1]))
    check(colours[positions[1]] == view.heat(0.0),
          "the active node's neighbours are the hot end of the ramp")
    check(colours[positions[0]] == view.heat(1.0), "the furthest the cold end")
    check(lit["route_ids"] == [2, 1, 0], "the route goes to the nearest weapon node")
    check(lit["route"] and len(lit["route"]) % 2 == 0, "drawn as dash pairs")
    dashed = sum(math.dist(a, b) for a, b in zip(lit["route"][::2], lit["route"][1::2]))
    check(0 < dashed < 200.0, "dashes cover part of the 200-unit route (%.0f)" % dashed)
    ids = [text for _p, text, _c in lit["labels"] if text != view.NO_BALLOON]
    check(ids == ["0", "1", "2", "3", "4"], "an id per node (%r)" % ids)
    big = [p for size, pos, _c in lit["points"] if size == view.NEIGHBOUR_SIZE for p in pos]
    check(positions[1] in big and positions[3] in big,
          "direct neighbours are drawn bigger")

    far = view.build(views, balloons, active=2, mode=view.DISTANCE)
    check(far["route_ids"] == [2, 1, 0], "by distance too")

    broken = [view.NodeView(0, (0, 0, 0), 0, [1, 99, 255, 0]),
              view.NodeView(1, (100, 0, 0), 0, [0]),
              view.NodeView(2, (500, 0, 0), 0, []),
              view.NodeView(None, (9, 9, 9), 0, [0])]
    found = view.build(broken, (), active=0)
    check(len(found["links"][0]) == 2, "links to missing ids and to itself are skipped")
    grey = [c for size, pos, colours in found["points"] for p, c in zip(pos, colours)
            if p == (500.0, 0.0, 0.0) and c != view.RIM_COLOUR]
    check(grey == [view.UNREACHED], "an unreachable node is dim grey (%r)" % grey)
    check(view.build([], ())["points"] == [], "an empty scene draws nothing")
    check(view.build(views, (), active=77)["route_ids"] == [],
          "an active id that is not a node is ignored")
    check(view.heat(-1) == view.HEAT_STOPS[0][1] and view.heat(2) == view.HEAT_STOPS[-1][1],
          "the ramp clamps")


def main():
    test_adjacency()
    test_heat()
    test_bot_route()
    test_slots()
    test_link_targets()
    test_reach()
    test_view()
    print()
    if FAILURES:
        print("%d FAILURE(S)" % len(FAILURES))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
