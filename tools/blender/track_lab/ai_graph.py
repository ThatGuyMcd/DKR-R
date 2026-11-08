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
    __slots__ = ("node_id", "position", "elevation", "neighbours", "branch",
                 "destination")

    def __init__(self, node_id: int, position: Vector3, elevation: int = 0,
                 branch: int = 0, destination: int = 0):
        self.node_id = node_id
        self.position = position
        self.elevation = elevation
        self.branch = branch
        #: The destination class (``unk8``) the arena AI's pathfinder targets
        #: a node by: 0 an ordinary node, 1 a weapon balloon, 4 + racer a
        #: banana chest. Kept per node, so rewriting the graph keeps it.
        self.destination = destination
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

    def add(self, position: Vector3, elevation: int = 0, branch: int = 0,
            destination: int = 0) -> AiNode:
        if len(self.nodes) >= MAX_NODES:
            raise AiGraphError(
                "a track cannot hold more than %d AI nodes (nodeID is a byte and "
                "255 marks an empty link)" % MAX_NODES
            )
        node = AiNode(len(self.nodes), position, elevation, branch, destination)
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
                    elevation: int = 0, destination: int = 0) -> AiGraph:
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
        graph.add(sample, elevation=elevation, destination=destination)
    for previous, node in zip(graph.nodes, graph.nodes[1:]):
        graph.link(previous, node)
    if closed and len(graph.nodes) > 2:
        graph.link(graph.nodes[-1], graph.nodes[0])
    return graph


def attach_branch(graph: AiGraph, points: Sequence[Vector3], spacing: float,
                  elevation: int = 0, branch: int = 1,
                  destination: int = 0) -> List[AiNode]:
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
    added = [graph.add(s, elevation=elevation, branch=branch,
                       destination=destination) for s in samples]
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


def nearest_index(positions: Sequence[Vector3], target: Vector3) -> Optional[int]:
    """The index of the position closest to ``target``, or ``None`` if empty."""
    best, best_distance = None, None
    for index, position in enumerate(positions):
        distance = _distance_squared(position, target)
        if best_distance is None or distance < best_distance:
            best, best_distance = index, distance
    return best


def destination_tags(nodes: Sequence[Vector3], balloons: Sequence[Vector3],
                     chests: Sequence[Tuple[int, Vector3]],
                     current: Sequence[int]) -> List[Tuple[int, int]]:
    """``(node index, destination class)`` to set so the arena AI has targets.

    The node nearest each weapon balloon becomes class 1, and the node
    nearest racer ``r``'s chest class 4 + r - what ``func_8001CD28`` asks for
    (``objects.c:7675``; ``racer.c:929``, ``:948``, ``:951``). A chest wins a
    node a balloon also wants, and a node already holding a class other than
    0 is left alone, since an author set it.
    """
    wanted = {}
    for position in balloons:
        index = nearest_index(nodes, position)
        if index is not None:
            wanted.setdefault(index, 1)
    for racer, position in chests:
        index = nearest_index(nodes, position)
        if index is not None and 0 <= racer <= 3:
            wanted[index] = 4 + racer
    return sorted((index, value) for index, value in wanted.items()
                  if current[index] in (0, value) and current[index] != value)


def balloons_off_their_node(nodes: Sequence[Vector3], balloons: Sequence[Vector3],
                            reach: float = None) -> List[Tuple[int, float]]:
    """``(balloon index, distance)`` for each balloon whose nearest node is
    further than ``reach``.

    :func:`destination_tags` tags that nearest node whatever its distance,
    and a bot drives to the node, not to the balloon: it switches to its
    next target 50 units out (``racer.c:682``), so a balloon further than
    that from its node is driven past rather than taken.
    """
    reach = WEAPON_REACH if reach is None else reach
    found = []
    if not nodes:
        return found
    for index, position in enumerate(balloons):
        nearest = nearest_index(nodes, position)
        distance = _distance(nodes[nearest], position)
        if distance > reach:
            found.append((index, distance))
    return found


# ---------------------------------------------------------------------------
# What the arena bots do with the graph
# ---------------------------------------------------------------------------
#
# ``racer_ai_challenge`` (``racer.c:682``) drives along straight lines between
# linked nodes. It starts at the node nearest the racer, picks its next node
# once within 300 units of the current one and turns toward it within 50. A
# bot with no weapon asks ``func_8001CD28`` (``objects.c:7675``) for the
# destination-class-1 node closest by path through the links, excluding the
# node it stands on, and takes the first step toward it; arriving still
# without a weapon, it asks again and so goes on to the next nearest one.
# Bots never look at a balloon itself: the balloon has to sit on the node.

#: How close a bot comes to a node before it turns for the next one, and so
#: how near a balloon must be to its weapon node to be driven through. The
#: overlay draws this ring around each weapon node.
WEAPON_REACH = 50.0

#: What the checklist accepts as "on its node". Wider than the 50 a bot
#: reliably touches because retail is: Darkwater Beach puts balloons up to 78
#: units from their class-1 node (most 20-50), Icicle Pyramid up to 28, and a
#: rule that flags retail would only teach authors to ignore it.
WEAPON_REACH_CHECK = 100.0

#: How far a new weapon node looks for graph nodes to link to. Retail arena
#: links are long - 330 to 3,600 units, the median 1,100-1,400 across
#: Darkwater Beach, Icicle Pyramid and Smokey Castle - so this reaches past
#: the median. It measures straight-line distance and does not know about
#: walls: a link through one makes the bot drive into it.
AUTO_LINK_RANGE = 1500.0
AUTO_LINK_COUNT = 2

#: Destination classes, as ``unk8`` holds them.
CLASS_PLAIN = 0
CLASS_WEAPON = 1
CLASS_REENTRY = 3
CLASS_BANK = 4  #: plus the racer, 0-3


def describe_class(value) -> str:
    """What a destination class means, in a few words."""
    if value == CLASS_PLAIN:
        return "Plain node"
    if value == CLASS_WEAPON:
        return "Weapon node"
    if value == CLASS_REENTRY:
        return "Re-entry node (after an elevation change)"
    if isinstance(value, int) and CLASS_BANK <= value <= CLASS_BANK + 3:
        return "Bank node of racer %d" % (value - CLASS_BANK)
    return "Destination class %s" % value


def adjacency(nodes) -> dict:
    """``{node id: [neighbour ids]}`` from ``(node id, adjacent slots)`` pairs.

    Forgiving, because it reads whatever the scene holds while the author is
    editing it: empty slots, ids no node has, a node naming itself and
    repeated ids are dropped, and a node id seen twice keeps its first entry.
    """
    present = []
    slots = {}
    for node_id, adjacent in nodes:
        if not isinstance(node_id, int) or node_id in slots:
            continue
        present.append(node_id)
        slots[node_id] = list(adjacent or [])
    known = set(present)
    found = {}
    for node_id in present:
        seen = []
        for value in slots[node_id]:
            if (isinstance(value, int) and value != NO_NEIGHBOUR and value in known
                    and value != node_id and value not in seen):
                seen.append(value)
        found[node_id] = seen
    return found


def links(adjacent: dict) -> List[Tuple[int, int]]:
    """Each link once, as ``(low id, high id)``, one-way links included."""
    found = set()
    for node_id, neighbours in adjacent.items():
        for other in neighbours:
            found.add((min(node_id, other), max(node_id, other)))
    return sorted(found)


def steps_from(adjacent: dict, start: int) -> dict:
    """``{node id: links away}`` for every node reachable from ``start``."""
    if start not in adjacent:
        return {}
    found = {start: 0}
    frontier = [start]
    while frontier:
        following = []
        for node_id in frontier:
            for other in adjacent.get(node_id, ()):
                if other not in found:
                    found[other] = found[node_id] + 1
                    following.append(other)
        frontier = following
    return found


def path_distances(adjacent: dict, positions: dict, start: int):
    """``(distance, previous)`` from ``start`` along the links, as the
    pathfinder measures it: Dijkstra over each link's straight length."""
    import heapq  # noqa: PLC0415

    if start not in adjacent or start not in positions:
        return {}, {}
    distance = {start: 0.0}
    previous = {}
    queue = [(0.0, start)]
    while queue:
        travelled, node_id = heapq.heappop(queue)
        if travelled > distance.get(node_id, math.inf):
            continue
        for other in adjacent.get(node_id, ()):
            if other not in positions:
                continue
            through = travelled + _distance(positions[node_id], positions[other])
            if through < distance.get(other, math.inf):
                distance[other] = through
                previous[other] = node_id
                heapq.heappush(queue, (through, other))
    return distance, previous


def bot_route(adjacent: dict, positions: dict, classes: dict, start: int,
              wanted: int = CLASS_WEAPON) -> List[int]:
    """The node ids a weapon-less bot standing on ``start`` drives through:
    to the class-``wanted`` node nearest by path, never ``start`` itself.
    Empty when no such node can be reached."""
    distance, previous = path_distances(adjacent, positions, start)
    targets = [(d, node_id) for node_id, d in distance.items()
               if node_id != start and classes.get(node_id) == wanted]
    if not targets:
        return []
    _d, target = min(targets)
    route = [target]
    while route[-1] != start:
        route.append(previous[route[-1]])
    route.reverse()
    return route


def link_slots(slots: Sequence[int], other: int) -> List[int]:
    """``slots`` with ``other`` written into the first empty one.

    Raises :class:`AiGraphError` when all four are taken. Already linked, the
    slots come back unchanged.
    """
    found = list(slots or [])[:MAX_NEIGHBOURS]
    found += [NO_NEIGHBOUR] * (MAX_NEIGHBOURS - len(found))
    if other in found:
        return found
    if NO_NEIGHBOUR not in found:
        raise AiGraphError("already has %d neighbours, the most the format "
                           "holds" % MAX_NEIGHBOURS)
    found[found.index(NO_NEIGHBOUR)] = other
    return found


def unlink_slots(slots: Sequence[int], other: int) -> List[int]:
    """``slots`` without ``other``, the rest moved up, as retail packs them."""
    kept = [value for value in list(slots or [])[:MAX_NEIGHBOURS]
            if value != other and value != NO_NEIGHBOUR]
    return kept + [NO_NEIGHBOUR] * (MAX_NEIGHBOURS - len(kept))


def free_slots(slots: Sequence[int]) -> int:
    """Empty ``adjacent`` slots, counting missing ones as empty."""
    filled = [value for value in list(slots or [])[:MAX_NEIGHBOURS]
              if value != NO_NEIGHBOUR]
    return MAX_NEIGHBOURS - len(filled)


def link_targets(position: Vector3, candidates, count: int = AUTO_LINK_COUNT,
                 reach: float = AUTO_LINK_RANGE) -> List[int]:
    """The ids a new node at ``position`` should link to.

    ``candidates`` are ``(node id, position, free slots)`` of the nodes
    already in the graph; the nearest ``count`` within ``reach`` with a free
    slot are taken. Straight-line distance only: it does not know about walls.
    """
    ranked = sorted((_distance(where, position), node_id)
                    for node_id, where, room in candidates
                    if room > 0 and _distance(where, position) <= reach)
    return [node_id for _d, node_id in ranked[:count]]


def weapon_reach(nodes, balloons: Sequence[Vector3]) -> List[Tuple[int, Optional[float]]]:
    """``(node id, distance to the nearest balloon)`` per class-1 node.

    ``nodes`` are ``(node id, position, destination class)``. The distance is
    ``None`` when there is no balloon at all.
    """
    found = []
    for node_id, position, value in nodes:
        if value != CLASS_WEAPON:
            continue
        nearest = min((_distance(position, b) for b in balloons), default=None)
        found.append((node_id, nearest))
    return found


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
