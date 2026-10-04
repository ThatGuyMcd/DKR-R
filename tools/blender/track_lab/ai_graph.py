"""Build a DKR AI node graph from sampled curves.

**An AI node graph is not the racing line**, which this module and the addon's
UI both used to say. A normal race steers by interpolating a spline through the
*checkpoints* - ``func_80045C48``, ``src/racer.c:1360`` - and never reads a
node. The dispatch is explicit at ``src/racer.c:613``: only
``RACETYPE_CHALLENGE_BATTLE`` and ``RACETYPE_CHALLENGE_BANANAS`` route to
``racer_ai_challenge``, which is what walks the graph.

What the graph is actually for:

* the AI in the Battle and Bananas challenges
* hub NPCs - T.T., Taj, the golden balloon
* loop-de-loops, which ride it as an on-rails track
* teaching the game where an arena's floors are, through ``elevation``

So drawing one is worth doing for an arena or a hub, and does nothing at all for
a normal circuit. This module holds the part with no Blender in it: arc-length
sampling, adjacency, branch attachment and the structural limits.

What retail data says about the format, measured across the 16 shipped maps that
have an AI graph:

* ``adjacent`` holds neighbour ``nodeID`` values, with 255 meaning empty.
* Adjacency is perfectly reciprocal - 440 directed edges, not one of them
  one-way. So a link is always written into both nodes.
* No node exceeds four neighbours, which the four slots make structural anyway.
* ``nodeID`` is not the order nodes appear in the document, so ids are assigned
  independently of placement.
* Isolated nodes with no neighbours at all do occur (27 of them), so an
  unreachable node is worth warning about but is not corrupt.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import math
from typing import Iterable, List, Optional, Sequence, Tuple

#: Empty ``adjacent`` slot.
NO_NEIGHBOUR = 255

#: Slots per node, from ``u8 adjacent[4]``.
MAX_NEIGHBOURS = 4

#: The game keeps ``AINODE_COUNT`` nodes, and that is **128**, not 255:
#: ``gAINodes`` is allocated as that many pointers and ``ainode_update``'s
#: working arrays hold that many entries (``src/objects.c:50``, ``:313``,
#: ``:7342``). Both a ``nodeID`` and every ``adjacent`` slot are filtered with
#: ``!(index & AINODE_COUNT)`` - a bit-7 test - so an id of 128 or more is
#: dropped at load, and so is every link pointing at one
#: (``src/objects.c:7359``, ``:7378``). 255 reads as the empty sentinel through
#: that same bit rather than by being reserved.
#:
#: This was 255 until it was measured, which meant a track with 200 nodes
#: exported without complaint and silently lost a third of its graph in game.
#: Retail never exceeds ``nodeID`` 38.
MAX_NODES = 128

Vector3 = Tuple[float, float, float]


class AiGraphError(Exception):
    pass


class AiNode:
    __slots__ = ("node_id", "position", "elevation", "neighbours", "branch")

    def __init__(self, node_id: int, position: Vector3, elevation: int = 0,
                 branch: int = 0):
        self.node_id = node_id
        self.position = position
        self.elevation = elevation
        self.branch = branch
        self.neighbours: List[int] = []

    @property
    def adjacent(self) -> List[int]:
        """The four ``adjacent`` slots, padded with the empty sentinel."""
        slots = list(self.neighbours[:MAX_NEIGHBOURS])
        return slots + [NO_NEIGHBOUR] * (MAX_NEIGHBOURS - len(slots))

    @property
    def free_slots(self) -> int:
        return MAX_NEIGHBOURS - len(self.neighbours)

    def __repr__(self):
        return "AiNode(%d, neighbours=%r)" % (self.node_id, self.neighbours)


class AiGraph:
    """A whole AI racing line, ready to be written as object-map entries."""

    def __init__(self):
        self.nodes: List[AiNode] = []
        self._by_id = {}

    def add(self, position: Vector3, elevation: int = 0, branch: int = 0) -> AiNode:
        if len(self.nodes) >= MAX_NODES:
            raise AiGraphError(
                "a track cannot hold more than %d AI nodes (nodeID is a byte and "
                "255 marks an empty link)" % MAX_NODES
            )
        node = AiNode(len(self.nodes), position, elevation, branch)
        self.nodes.append(node)
        self._by_id[node.node_id] = node
        return node

    def get(self, node_id: int) -> Optional[AiNode]:
        return self._by_id.get(node_id)

    def link(self, a: AiNode, b: AiNode) -> None:
        """Join two nodes in both directions, as every retail graph does."""
        if a is b:
            raise AiGraphError("node %d cannot link to itself" % a.node_id)
        if b.node_id in a.neighbours:
            return
        for node, other in ((a, b), (b, a)):
            if node.free_slots == 0:
                raise AiGraphError(
                    "node %d already has %d neighbours and the format has no "
                    "room for a fifth" % (node.node_id, MAX_NEIGHBOURS)
                )
        a.neighbours.append(b.node_id)
        b.neighbours.append(a.node_id)

    def can_link(self, a: AiNode, b: AiNode) -> bool:
        if a is b or b.node_id in a.neighbours:
            return False
        return a.free_slots > 0 and b.free_slots > 0

    def nearest(self, position: Vector3, exclude: Iterable[int] = ()) -> Optional[AiNode]:
        excluded = set(exclude)
        best, best_distance = None, None
        for node in self.nodes:
            if node.node_id in excluded:
                continue
            distance = _distance_squared(node.position, position)
            if best_distance is None or distance < best_distance:
                best, best_distance = node, distance
        return best


# ---------------------------------------------------------------------------
# Sampling
# ---------------------------------------------------------------------------

def resample(points: Sequence[Vector3], spacing: float,
             closed: bool = False) -> List[Vector3]:
    """Walk a polyline and emit a point every ``spacing`` units of arc length.

    Blender hands us a curve already tessellated into many short segments; this
    turns that into the evenly spaced nodes an AI line wants, independent of how
    densely the author placed control points.
    """
    if spacing <= 0:
        raise AiGraphError("node spacing must be positive")
    path = [tuple(float(c) for c in p) for p in points]
    if closed and path and path[0] != path[-1]:
        path = path + [path[0]]
    if len(path) < 2:
        return path[:1]

    out = [path[0]]
    carried = 0.0
    for start, end in zip(path, path[1:]):
        segment = _distance(start, end)
        if segment <= 0.0:
            continue
        travelled = spacing - carried
        while travelled <= segment:
            out.append(_lerp(start, end, travelled / segment))
            travelled += spacing
        carried = segment - (travelled - spacing)

    # On a closed loop the last sample can land almost on top of the first, which
    # would make a zero length link once the loop is closed.
    if closed and len(out) > 2 and _distance(out[-1], out[0]) < spacing * 0.5:
        out.pop()
    return out


def build_from_path(points: Sequence[Vector3], spacing: float, closed: bool = True,
                    elevation: int = 0) -> AiGraph:
    """Sample one curve into a chain of nodes, closed into a loop if asked."""
    graph = AiGraph()
    samples = resample(points, spacing, closed=closed)
    if len(samples) < 2:
        raise AiGraphError("the racing line needs at least two nodes")
    if len(samples) > MAX_NODES:
        raise AiGraphError(
            "spacing of %g gives %d nodes, over the %d the format allows; "
            "increase the spacing" % (spacing, len(samples), MAX_NODES)
        )
    for sample in samples:
        graph.add(sample, elevation=elevation)
    for previous, node in zip(graph.nodes, graph.nodes[1:]):
        graph.link(previous, node)
    if closed and len(graph.nodes) > 2:
        graph.link(graph.nodes[-1], graph.nodes[0])
    return graph


def attach_branch(graph: AiGraph, points: Sequence[Vector3], spacing: float,
                  elevation: int = 0, branch: int = 1) -> List[AiNode]:
    """Sample an alternate route and splice it onto the nodes it starts and ends near.

    The branch's own nodes are chained as usual; its two ends then link to
    whichever existing node is closest and still has a free slot. A node already
    holding four neighbours is skipped rather than silently dropping a link.
    """
    samples = resample(points, spacing, closed=False)
    if len(samples) < 2:
        raise AiGraphError("a branch needs at least two nodes")
    if len(graph.nodes) + len(samples) > MAX_NODES:
        raise AiGraphError(
            "the branch would take the track to %d AI nodes, over the %d the "
            "format allows" % (len(graph.nodes) + len(samples), MAX_NODES)
        )

    existing = [node.node_id for node in graph.nodes]
    added = [graph.add(s, elevation=elevation, branch=branch) for s in samples]
    for previous, node in zip(added, added[1:]):
        graph.link(previous, node)

    for end in (added[0], added[-1]):
        anchor = _nearest_with_room(graph, end, existing)
        if anchor is not None:
            graph.link(anchor, end)
    return added


def _nearest_with_room(graph: AiGraph, node: AiNode, candidates: Sequence[int]):
    best, best_distance = None, None
    for node_id in candidates:
        other = graph.get(node_id)
        if other is None or not graph.can_link(node, other):
            continue
        distance = _distance_squared(other.position, node.position)
        if best_distance is None or distance < best_distance:
            best, best_distance = other, distance
    return best


# ---------------------------------------------------------------------------
# Inspection
# ---------------------------------------------------------------------------

def reachable_from(graph: AiGraph, start: int = 0) -> set:
    """Node ids reachable from ``start``, for the connectivity warning."""
    if not graph.nodes:
        return set()
    seen = {start}
    stack = [start]
    while stack:
        node = graph.get(stack.pop())
        if node is None:
            continue
        for neighbour in node.neighbours:
            if neighbour not in seen:
                seen.add(neighbour)
                stack.append(neighbour)
    return seen


def _distance(a: Vector3, b: Vector3) -> float:
    return math.sqrt(_distance_squared(a, b))


def _distance_squared(a: Vector3, b: Vector3) -> float:
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _lerp(a: Vector3, b: Vector3, t: float) -> Vector3:
    return (
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    )
