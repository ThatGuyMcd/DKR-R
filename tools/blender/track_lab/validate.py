"""Pre-export checks on an object map.

These are the cheap checks that catch the failures which are confusing to
diagnose in game: an AI graph the racers cannot follow, a track with nowhere to
start, an exit pointing at a level that does not exist. Everything here runs on
an :class:`~track_lab.gltf_io.ObjectMap`, so it can be run over retail
maps as a sanity check on the rules themselves.

Severities are separated on purpose. An ``error`` means the map is structurally
wrong and the game will misbehave. A ``warning`` means something is unusual but
retail does it too - isolated AI nodes, for instance, appear 27 times in shipped
tracks, so refusing them would be wrong.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import collections
from typing import List, Optional

from . import ai_graph, catalog as catalog_module, level_types
from .gltf_io import ObjectMap

AINODE = "ASSET_OBJECT_AINODE"
SETUPPOINT = "ASSET_OBJECT_SETUPPOINT"
CHECKPOINT = "ASSET_OBJECT_CHECKPOINT"
EXIT = "ASSET_OBJECT_EXIT"

ERROR = "error"
WARNING = "warning"
INFO = "info"


class Issue:
    __slots__ = ("severity", "message", "object_id", "index", "objects")

    def __init__(self, severity: str, message: str, object_id: str = "",
                 index: Optional[int] = None, objects=None):
        self.severity = severity
        self.message = message
        self.object_id = object_id
        self.index = index
        #: Document positions of the objects this is about, so the UI can select
        #: them. Finding one duplicate checkpoint among ninety by hand is the
        #: difference between a useful message and a discouraging one.
        self.objects = list(objects or [])

    def __str__(self):
        where = " [%s]" % self.object_id if self.object_id else ""
        return "%s: %s%s" % (self.severity, self.message, where)

    def __repr__(self):
        return "Issue(%r, %r)" % (self.severity, self.message)


class Report:
    def __init__(self, issues: List[Issue]):
        self.issues = issues

    @property
    def errors(self) -> List[Issue]:
        return [i for i in self.issues if i.severity == ERROR]

    @property
    def warnings(self) -> List[Issue]:
        return [i for i in self.issues if i.severity == WARNING]

    @property
    def infos(self) -> List[Issue]:
        return [i for i in self.issues if i.severity == INFO]

    @property
    def ok(self) -> bool:
        return not self.errors

    def __len__(self):
        return len(self.issues)

    def __iter__(self):
        return iter(self.issues)


def validate(object_map: ObjectMap, catalog=None, require_racing_track=True,
             level_key: Optional[str] = None) -> Report:
    """Run every check over an object map.

    ``level_key`` is the track's :mod:`level_types` key, and when given it
    decides the rules: how many start positions there must be, whether
    checkpoints are needed, which object types belong. ``level_types.NONE``
    means the author has not chosen, and that is the only thing reported.

    Without one, ``require_racing_track`` is the older and blunter switch, which
    the retail sanity check still uses: it turns off the checks that only make
    sense for a track people race on.
    """
    if catalog is None:
        catalog = catalog_module.load()
    if level_key is not None and level_key not in level_types.KEYS:
        return Report([Issue(
            ERROR,
            "Choose the level type first. Nothing can be checked or exported "
            "without it.",
        )])

    issues: List[Issue] = []
    issues += _check_known_types(object_map, catalog)
    issues += _check_ai_graph(object_map)
    if level_key is None:
        issues += _check_checkpoints(object_map, require_racing_track)
        issues += _check_setup_points(object_map, require_racing_track)
    else:
        issues += _check_checkpoints(object_map,
                                     level_types.needs_checkpoints(level_key))
        issues += _check_start_grid(object_map, level_key)
        issues += _check_level_type(object_map, catalog, level_key)
    issues += _check_exits(object_map, catalog)
    issues += _check_budget(object_map, catalog)
    return Report(issues)


def _check_start_grid(object_map: ObjectMap, key: str) -> List[Issue]:
    """Start positions against the number of racers this kind of level spawns.

    The game zeroes every start position before reading the map and never
    zeroes the angle, so a missing ``racerIndex`` below the racer count starts
    that racer at the map origin facing an undefined direction - that is an
    error, not a style point (``objects.c:1121-1136``). An index of 8 or more
    is ignored (``objects.c:1131``).
    """
    count = level_types.spawn_count(key)
    if not count:
        # The game returns before reading start positions at all.
        return []

    positions = {id(obj): position for position, obj in enumerate(object_map.objects)}
    points = object_map.by_id(SETUPPOINT)
    if not points:
        return [Issue(
            ERROR,
            "No start positions: racers have nowhere to start. Use Generate "
            "Start Grid.",
            SETUPPOINT,
        )]

    issues = []
    by_entrance = collections.defaultdict(list)
    for point in points:
        by_entrance[point.fields.get("entranceID", 0)].append(
            (point.fields.get("racerIndex"), positions.get(id(point)))
        )

    first = {index for index, _ in by_entrance.get(0, [])}
    missing = [k for k in range(count) if k not in first]
    if missing:
        issues.append(Issue(
            ERROR,
            "Entrance 0 has no racerIndex %s: %s would start at the map origin, "
            "facing an undefined direction."
            % (", ".join(str(k) for k in missing),
               "those racers" if len(missing) > 1 else "that racer"),
            SETUPPOINT,
        ))

    for entrance, racers in sorted(by_entrance.items()):
        seen = collections.defaultdict(list)
        for index, position in racers:
            if index is not None:
                seen[index].append(position)
        duplicated = sorted(i for i, where in seen.items() if len(where) > 1)
        if duplicated:
            issues.append(Issue(
                ERROR,
                "entrance %s puts more than one racer on index %s"
                % (entrance, ", ".join(str(d) for d in duplicated)),
                SETUPPOINT,
                objects=[p for i in duplicated for p in seen[i] if p is not None],
            ))
        over = [p for i, p in racers
                if isinstance(i, int) and i >= level_types.MAX_RACER_INDEX]
        if over:
            issues.append(Issue(
                WARNING,
                "%d start position(s) at entrance %s use racerIndex 8 or more, "
                "which the game ignores." % (len(over), entrance),
                SETUPPOINT, objects=over,
            ))
        unused = [p for i, p in racers if isinstance(i, int)
                  and count <= i < level_types.MAX_RACER_INDEX]
        if unused and key != level_types.HUB:
            issues.append(Issue(
                INFO,
                "%d start position(s) at entrance %s are beyond the %d a %s "
                "uses." % (len(unused), entrance, count, level_types.label(key)),
                SETUPPOINT, objects=unused,
            ))

    issues.append(Issue(
        INFO,
        "%d start position(s) across %d entrance(s)"
        % (len(points), len(by_entrance)),
        SETUPPOINT,
    ))
    return issues


def _check_level_type(object_map: ObjectMap, catalog, key: str) -> List[Issue]:
    """Types the level does not use, and the level types that need a word."""
    issues = []
    unused = collections.defaultdict(list)
    for position, obj in enumerate(object_map.objects):
        object_type = catalog.get(obj.object_id)
        if object_type is not None and not level_types.visible(object_type, key):
            unused[obj.object_id].append(position)
    for object_id, where in sorted(unused.items()):
        issues.append(Issue(
            WARNING,
            "%d %s not used in a %s level."
            % (len(where), catalog.get(object_id).label, level_types.label(key)),
            object_id, objects=where,
        ))
    if key == level_types.TEST_RACE:
        issues.append(Issue(WARNING, "Test Race: " + level_types.TEST_RACE_WARNING))
    if key == level_types.BACKDROP:
        issues.append(Issue(
            WARNING,
            "Menu Backdrop does not draw the track geometry, only the objects.",
        ))
    return issues


def _check_known_types(object_map: ObjectMap, catalog) -> List[Issue]:
    issues = []
    for index, obj in enumerate(object_map.objects):
        object_type = catalog.get(obj.object_id)
        if object_type is None:
            issues.append(Issue(
                ERROR,
                "unknown object type %r; the asset tool will not be able to "
                "encode it" % obj.object_id,
                obj.object_id, index,
            ))
            continue
        for field in object_type.fields:
            if field.optional or field.unused:
                continue
            if field.name not in obj.fields:
                issues.append(Issue(
                    ERROR,
                    "%s is missing required field %r" % (obj.name, field.name),
                    obj.object_id, index,
                ))
    return issues


def _check_ai_graph(object_map: ObjectMap) -> List[Issue]:
    nodes = object_map.by_id(AINODE)
    if not nodes:
        return []

    issues = []
    if len(nodes) > ai_graph.MAX_NODES:
        issues.append(Issue(
            ERROR,
            "%d AI nodes, but the game keeps %d and drops any id from %d up "
            "at load, along with every link pointing at one - the track would "
            "lose them in silence"
            % (len(nodes), ai_graph.MAX_NODES, ai_graph.MAX_NODES),
            AINODE,
        ))

    by_id = {}
    for node in nodes:
        node_id = node.fields.get("nodeID")
        if node_id is None:
            issues.append(Issue(ERROR, "an AI node has no nodeID", AINODE))
            continue
        if node_id == ai_graph.NO_NEIGHBOUR:
            issues.append(Issue(
                ERROR,
                "AI node uses id %d, which is the empty-link sentinel and can "
                "never be referenced" % ai_graph.NO_NEIGHBOUR,
                AINODE,
            ))
        by_id.setdefault(node_id, []).append(node)

    for node_id, sharing in sorted(by_id.items()):
        if len(sharing) > 1:
            issues.append(Issue(
                ERROR,
                "%d AI nodes share nodeID %d" % (len(sharing), node_id),
                AINODE,
            ))

    links = {}
    elevations = {}
    for node_id, sharing in by_id.items():
        adjacent = sharing[0].fields.get("adjacent") or []
        neighbours = [a for a in adjacent if a != ai_graph.NO_NEIGHBOUR]
        links[node_id] = neighbours
        elevations[node_id] = sharing[0].fields.get("elevation")
        if len(neighbours) > ai_graph.MAX_NEIGHBOURS:
            issues.append(Issue(
                ERROR,
                "AI node %d lists %d neighbours; the format has %d slots"
                % (node_id, len(neighbours), ai_graph.MAX_NEIGHBOURS),
                AINODE,
            ))
        for neighbour in neighbours:
            if neighbour not in by_id:
                issues.append(Issue(
                    ERROR,
                    "AI node %d links to %d, which does not exist"
                    % (node_id, neighbour),
                    AINODE,
                ))

    for node_id, neighbours in sorted(links.items()):
        for neighbour in neighbours:
            if neighbour in links and node_id not in links[neighbour]:
                issues.append(Issue(
                    ERROR,
                    "AI link %d -> %d is one-way; every link in retail data is "
                    "written into both nodes" % (node_id, neighbour),
                    AINODE,
                ))

    # Connectivity, reported as a warning: retail ships isolated nodes.
    if links and not any(i.severity == ERROR for i in issues):
        graph = ai_graph.AiGraph()
        order = sorted(links)
        index_of = {node_id: i for i, node_id in enumerate(order)}
        for node_id in order:
            graph.add((0.0, 0.0, 0.0))
        for node_id in order:
            for neighbour in links[node_id]:
                a, b = graph.nodes[index_of[node_id]], graph.nodes[index_of[neighbour]]
                if graph.can_link(a, b):
                    graph.link(a, b)
        reached = ai_graph.reachable_from(graph, 0)
        stranded = [order[i] for i in range(len(order)) if i not in reached]
        # A node with no neighbours and elevation -1 is not a broken graph, it
        # is an inert marker: of the 27 isolated nodes in retail, 26 carry -1,
        # and every one of the 181 connected nodes carries 0 or more. Ancient
        # Lake's four are a discarded draft of the dinosaur's path, sitting
        # under the water and read by nothing. Warning about those is noise.
        stranded = [
            node_id for node_id in stranded
            if links.get(node_id) or elevations.get(node_id) != -1
        ]
        if stranded:
            issues.append(Issue(
                WARNING,
                "%d AI node(s) cannot be reached from node %d: %s"
                % (len(stranded), order[0],
                   ", ".join(str(s) for s in stranded[:12])),
                AINODE,
            ))
    return issues


def _check_checkpoints(object_map: ObjectMap, require_racing_track: bool) -> List[Issue]:
    """Checkpoints are chains, one per vehicle type, not a single dense sequence.

    docs/BLENDER_ADDON_PLAN.md proposed requiring indices to run contiguously
    from zero. Retail data disproves that: of 51 checkpoint chains in shipped
    maps, indices typically step by 2 and a chain may start at 2, 8 or 12. A
    map like Ancient Lake carries three independent chains, one each for car,
    hovercraft and plane, each with its own numbering.

    What does hold, in all 51 chains, is that an index is unique within its
    ``(vehicleType, isAltCheckpoint)`` group. That is the rule worth enforcing.
    """
    positions = {
        id(obj): position
        for position, obj in enumerate(object_map.objects)
    }
    checkpoints = object_map.by_id(CHECKPOINT)
    if not checkpoints:
        if require_racing_track:
            return [Issue(
                WARNING,
                "no checkpoints; laps and respawns will not work on a race track",
                CHECKPOINT,
            )]
        return []

    issues = []
    chains = collections.defaultdict(lambda: collections.defaultdict(list))
    for checkpoint in checkpoints:
        index = checkpoint.fields.get("index")
        if index is None:
            issues.append(Issue(
                ERROR, "a checkpoint has no index", CHECKPOINT,
                objects=[positions.get(id(checkpoint))],
            ))
            continue
        chain = (
            checkpoint.fields.get("vehicleType", 0),
            checkpoint.fields.get("isAltCheckpoint", 0),
        )
        chains[chain][index].append(positions.get(id(checkpoint)))

    for (vehicle_type, is_alt), indices in sorted(chains.items()):
        duplicated = sorted(i for i, where in indices.items() if len(where) > 1)
        if duplicated:
            offenders = [
                position
                for index in duplicated
                for position in indices[index]
                if position is not None
            ]
            issues.append(Issue(
                ERROR,
                "vehicle type %s%s reuses checkpoint index %s; a racer crossing "
                "it cannot tell which one it was"
                % (vehicle_type, " (alt route)" if is_alt else "",
                   ", ".join(str(d) for d in duplicated[:12])),
                CHECKPOINT,
                objects=offenders,
            ))
        issues.append(Issue(
            INFO,
            "vehicle type %s%s: %d checkpoints, indices %s..%s"
            % (vehicle_type, " (alt route)" if is_alt else "",
               sum(len(w) for w in indices.values()), min(indices), max(indices)),
            CHECKPOINT,
        ))
    return issues


def _check_setup_points(object_map: ObjectMap, require_racing_track: bool) -> List[Issue]:
    """Start positions are grouped by entrance, and a group is not always a grid.

    Retail entrance groups hold 1, 2, 4 or 8 racers: eight is a race grid, one
    is a hub arrival point. So the count carries no rule. What does hold across
    all 92 retail entrance groups is that a ``racerIndex`` is never reused
    within an entrance, which would leave two racers on the same square.
    """
    points = object_map.by_id(SETUPPOINT)
    if not points:
        if require_racing_track:
            return [Issue(
                ERROR,
                "no %s; racers have nowhere to start" % SETUPPOINT,
                SETUPPOINT,
            )]
        return []

    issues = []
    by_entrance = collections.defaultdict(list)
    for point in points:
        by_entrance[point.fields.get("entranceID", 0)].append(
            point.fields.get("racerIndex")
        )
    for entrance, racers in sorted(by_entrance.items()):
        present = sorted(r for r in racers if r is not None)
        duplicated = sorted(
            r for r, n in collections.Counter(present).items() if n > 1
        )
        if duplicated:
            issues.append(Issue(
                ERROR,
                "entrance %s puts more than one racer on index %s"
                % (entrance, ", ".join(str(d) for d in duplicated)),
                SETUPPOINT,
            ))
    issues.append(Issue(
        INFO,
        "%d start position(s) across %d entrance(s)"
        % (len(points), len(by_entrance)),
        SETUPPOINT,
    ))
    return issues


def _check_exits(object_map: ObjectMap, catalog) -> List[Issue]:
    known = set(catalog.levels)
    if not known:
        return []
    issues = []
    for exit_object in object_map.by_id(EXIT):
        destination = exit_object.fields.get("destinationMapId")
        if destination is None:
            issues.append(Issue(
                WARNING,
                "an exit has no destinationMapId and will not lead anywhere",
                EXIT,
            ))
        elif destination not in known:
            # A custom track adds level ids beyond the retail list, so this is a
            # warning: it is only certainly wrong if the id is not published.
            issues.append(Issue(
                WARNING,
                "exit targets %r, which is not a retail level; make sure the "
                "track that defines it is installed" % destination,
                EXIT,
            ))
    return issues


def _check_budget(object_map: ObjectMap, catalog) -> List[Issue]:
    """Report the encoded size, so an author sees a map growing before it breaks."""
    return [Issue(
        INFO,
        "%d objects, roughly %d bytes encoded"
        % (len(object_map.objects), estimate_size(object_map, catalog)),
    )]


def estimate_size(object_map: ObjectMap, catalog=None) -> int:
    """Approximate encoded byte length of the object map.

    Summed from each type's struct size as read out of the decomp header. It is
    an estimate rather than the exact figure the asset tool produces, which is
    enough to warn an author that a map is growing, and is never used to reject
    one.
    """
    if catalog is None:
        catalog = catalog_module.load()
    total = 0
    for obj in object_map.objects:
        entry = catalog.raw.get("objects", {}).get(obj.object_id)
        total += entry.get("entry_size", 8) if entry else 8
    return total
