"""What the AI graph overlay draws, worked out without Blender.

:mod:`track_lab.operators.ai` turns this into GPU batches; everything that
decides *what* is drawn - which links, in what colour, how big each node, the
route a bot would take - is here, so it can be tested without a GPU and the
draw handler stays a thin loop.

Positions are taken and returned in Blender space (Z up). Distances do not
care, since the map-to-Blender mapping is a pure axis swap, but the ground
circles are drawn flat in Blender's XY plane.

What is drawn:

* every link as a line, every node as a dot;
* a weapon node (destination class 1) as a ring in its balloon's colour, with
  a ground circle of :data:`ai_graph.WEAPON_REACH` around it: green when a
  weapon balloon is inside it, red when the bot would drive up to the node and
  find nothing to take. At arena scale that circle is a few pixels across, so
  a node with no balloon in reach also says so in words beside it;
* a bank node (class 4-7) as a ring in its racer's colour, a re-entry node
  (class 3) in a quiet accent;
* with a node active, a heat map out from it - its neighbours red, then
  orange, yellow, green and blue the further away, by links or by distance -
  with whatever cannot be reached left dim grey, which is also how a
  disconnected piece of the graph shows up;
* and from that node, dashed, the route a bot without a weapon drives: to the
  nearest weapon node by path, never the one it is on.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

from . import ai_graph

Vector3 = Tuple[float, float, float]
Colour = Tuple[float, float, float, float]

STEPS = "STEPS"
DISTANCE = "DISTANCE"

#: Weapon balloon colours by ``balloonType``, matching the Place buttons.
BALLOON_COLOURS: Dict[str, Colour] = {
    "BALLOON_TYPE_MISSILE": (1.00, 0.24, 0.20, 1.0),
    "BALLOON_TYPE_BOOST": (0.25, 0.55, 1.00, 1.0),
    "BALLOON_TYPE_TRAP": (0.30, 0.90, 0.35, 1.0),
    "BALLOON_TYPE_SHIELD": (1.00, 0.88, 0.20, 1.0),
    "BALLOON_TYPE_MAGNET": (0.85, 0.40, 1.00, 1.0),
}
#: A weapon node whose balloon cannot be told.
UNKNOWN_BALLOON: Colour = (1.0, 1.0, 1.0, 1.0)

#: Bank node rings, racer 0-3: apart from every balloon colour and the heat.
RACER_COLOURS: Tuple[Colour, ...] = (
    (0.20, 0.90, 0.95, 1.0),
    (1.00, 0.55, 0.10, 1.0),
    (0.95, 0.45, 0.80, 1.0),
    (0.65, 0.60, 1.00, 1.0),
)
REENTRY_COLOUR: Colour = (0.85, 0.75, 0.50, 1.0)

NODE_COLOUR: Colour = (0.80, 0.80, 0.82, 1.0)
LINK_COLOUR: Colour = (0.80, 0.80, 0.82, 0.75)
#: The middle of a ringed node with no heat colour, dark so the ring reads.
RING_CORE: Colour = (0.08, 0.08, 0.09, 1.0)
UNREACHED: Colour = (0.40, 0.40, 0.42, 0.55)
ACTIVE_COLOUR: Colour = (1.0, 1.0, 1.0, 1.0)
ROUTE_COLOUR: Colour = (1.0, 1.0, 1.0, 0.95)
IN_REACH: Colour = (0.30, 0.95, 0.40, 0.95)
OUT_OF_REACH: Colour = (1.00, 0.20, 0.18, 0.95)

#: Point sizes in pixels, so the graph reads however far the view is out.
NODE_SIZE = 13.0
NEIGHBOUR_SIZE = 18.0
ACTIVE_SIZE = 20.0
#: A ring is three discs: the ring colour, a dark gap, then the node's own
#: dot. The gap is what keeps a red balloon's ring apart from a red heat dot.
RING_GAP = 4.0
RING_WIDTH = 12.0  #: how much wider the ring is than the dot inside it
#: A dark rim around every node, so a grey dot reads on a grey floor.
RIM = 3.0
RIM_COLOUR: Colour = (0.0, 0.0, 0.0, 0.6)

#: Beside a weapon node with no balloon in reach.
NO_BALLOON = "no balloon in reach"

#: The dashes of the bot route, in world units.
DASH = 70.0
GAP = 45.0
CIRCLE_SEGMENTS = 40

#: Heat stops, near to far: the 3ds Max soft-selection ramp.
HEAT_STOPS: Tuple[Tuple[float, Colour], ...] = (
    (0.00, (1.00, 0.15, 0.10, 1.0)),
    (0.25, (1.00, 0.55, 0.10, 1.0)),
    (0.50, (1.00, 0.95, 0.20, 1.0)),
    (0.75, (0.30, 0.90, 0.30, 1.0)),
    (1.00, (0.25, 0.50, 1.00, 1.0)),
)


class NodeView:
    """One AI node as the overlay sees it."""

    __slots__ = ("node_id", "position", "destination", "adjacent", "colour")

    def __init__(self, node_id: int, position: Vector3, destination: int = 0,
                 adjacent: Sequence[int] = (), colour: Optional[Colour] = None):
        self.node_id = node_id
        self.position = tuple(float(c) for c in position)
        self.destination = destination
        self.adjacent = list(adjacent or [])
        #: The ring colour of a weapon node, from the balloon it is parented
        #: to; ``None`` to take the nearest balloon's.
        self.colour = colour


def heat(t: float) -> Colour:
    """The ramp colour at ``t``, 0 near and 1 far."""
    t = min(1.0, max(0.0, t))
    for (low, a), (high, b) in zip(HEAT_STOPS, HEAT_STOPS[1:]):
        if t <= high:
            f = (t - low) / (high - low) if high > low else 0.0
            return tuple(a[i] + (b[i] - a[i]) * f for i in range(4))
    return HEAT_STOPS[-1][1]


def _distance(a: Vector3, b: Vector3) -> float:
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def ring_colour(node: NodeView, balloons) -> Optional[Colour]:
    """The ring a node is drawn with, or ``None`` for a plain dot."""
    value = node.destination
    if value == ai_graph.CLASS_WEAPON:
        if node.colour is not None:
            return node.colour
        near = [(_distance(node.position, where), colour) for where, colour in balloons]
        near = [entry for entry in near if entry[0] <= ai_graph.WEAPON_REACH]
        return min(near)[1] if near else UNKNOWN_BALLOON
    if isinstance(value, int) and ai_graph.CLASS_BANK <= value <= ai_graph.CLASS_BANK + 3:
        return RACER_COLOURS[value - ai_graph.CLASS_BANK]
    if value == ai_graph.CLASS_REENTRY:
        return REENTRY_COLOUR
    return None


def _dashes(points: Sequence[Vector3], dash: float = DASH, gap: float = GAP):
    """A polyline cut into dashes, as ``LINES`` vertex pairs."""
    out = []
    on, left = True, dash
    for start, end in zip(points, points[1:]):
        length = _distance(start, end)
        at = 0.0
        while length > 0.0 and at < length:
            step = min(left, length - at)
            if on:
                a = tuple(start[i] + (end[i] - start[i]) * at / length for i in range(3))
                b = tuple(start[i] + (end[i] - start[i]) * (at + step) / length
                          for i in range(3))
                out += (a, b)
            at += step
            left -= step
            if left <= 1e-6:
                on = not on
                left = dash if on else gap
    return out


def _circle(centre: Vector3, radius: float, segments: int = CIRCLE_SEGMENTS):
    points = []
    for i in range(segments):
        for k in (i, i + 1):
            angle = 2.0 * math.pi * k / segments
            points.append((centre[0] + radius * math.cos(angle),
                           centre[1] + radius * math.sin(angle), centre[2]))
    return points


def build(nodes: Sequence[NodeView], balloons=(), active: Optional[int] = None,
          mode: str = STEPS, show_ids: bool = False) -> dict:
    """Everything the overlay draws, as plain lists.

    ``balloons`` are ``(position, colour)`` of the weapon balloons. The
    result holds ``links`` and ``circles`` (``LINES`` vertex pairs with a
    colour per vertex), ``route`` (dash pairs, one colour), ``points`` -
    ``(size, positions, colours)`` layers, biggest first so a ring's dot
    lands on top of it - ``labels`` as ``(position, text, colour)``, and
    ``route_ids`` and ``reach`` (``{weapon node id: balloon inside}``) for
    the panel.
    """
    by_id: Dict[int, NodeView] = {}
    for node in nodes:
        if isinstance(node.node_id, int):
            by_id.setdefault(node.node_id, node)
    adjacent = ai_graph.adjacency((n.node_id, n.adjacent) for n in by_id.values())
    positions = {node_id: node.position for node_id, node in by_id.items()}
    classes = {node_id: node.destination for node_id, node in by_id.items()}
    if active not in by_id:
        active = None

    # The heat, 0 at the active node to 1 at the furthest it reaches.
    shade: Dict[int, float] = {}
    neighbours = set(adjacent.get(active, ())) if active is not None else set()
    route: List[int] = []
    if active is not None:
        if mode == DISTANCE:
            found, _previous = ai_graph.path_distances(adjacent, positions, active)
            far = max(found.values()) if found else 0.0
            shade = {k: (v / far if far > 0 else 0.0) for k, v in found.items()}
        else:
            found = ai_graph.steps_from(adjacent, active)
            far = max(found.values()) if found else 0
            shade = {k: (0.0 if v <= 1 or far <= 1 else (v - 1) / float(far - 1))
                     for k, v in found.items()}
        route = ai_graph.bot_route(adjacent, positions, classes, active)

    def fill(node_id):
        if active is None:
            return None
        if node_id == active:
            return ACTIVE_COLOUR
        if node_id not in shade:
            return UNREACHED
        return heat(shade[node_id])

    link_pos, link_col = [], []
    for a, b in ai_graph.links(adjacent):
        link_pos += (positions[a], positions[b])
        if active is None:
            link_col += (LINK_COLOUR, LINK_COLOUR)
        elif a in shade and b in shade:
            link_col += (fill(a), fill(b))
        else:
            link_col += (UNREACHED, UNREACHED)

    circle_pos, circle_col = [], []
    reach: Dict[int, bool] = {}
    layers: Dict[float, Tuple[list, list]] = {}

    def dot(size, where, colour):
        layer = layers.setdefault(size, ([], []))
        layer[0].append(where)
        layer[1].append(colour)

    labels = []
    for node_id, node in sorted(by_id.items()):
        ring = ring_colour(node, balloons)
        if node.destination == ai_graph.CLASS_WEAPON:
            inside = any(_distance(node.position, where) <= ai_graph.WEAPON_REACH
                         for where, _colour in balloons)
            reach[node_id] = inside
            points = _circle(node.position, ai_graph.WEAPON_REACH)
            circle_pos += points
            circle_col += [IN_REACH if inside else OUT_OF_REACH] * len(points)

        size = (ACTIVE_SIZE if node_id == active else
                NEIGHBOUR_SIZE if node_id in neighbours else NODE_SIZE)
        colour = fill(node_id)
        dot(size + (RING_WIDTH if ring is not None else 0.0) + RIM,
            node.position, RIM_COLOUR)
        if ring is not None:
            dot(size + RING_WIDTH, node.position, ring)
            dot(size + RING_GAP, node.position, RING_CORE)
            dot(size, node.position, colour or RING_CORE)
        else:
            dot(size, node.position, colour or NODE_COLOUR)
        if show_ids:
            labels.append((node.position, str(node_id),
                           UNREACHED if active is not None and node_id not in shade
                           else ACTIVE_COLOUR))
        if reach.get(node_id) is False:
            labels.append((node.position, NO_BALLOON, OUT_OF_REACH))

    route_pos = _dashes([positions[n] for n in route]) if len(route) > 1 else []
    return {
        "links": (link_pos, link_col),
        "circles": (circle_pos, circle_col),
        "route": route_pos,
        "points": [(size, layers[size][0], layers[size][1])
                   for size in sorted(layers, reverse=True)],
        "labels": labels,
        "route_ids": route,
        "reach": reach,
    }
