"""What each non-race level type needs before the game can play it.

A race needs a grid and checkpoints, and the rest of the addon is built around
those. A challenge arena, a hub and a boss race each need a different handful
of objects, and missing one does not fail loudly: a banana arena whose racer 2
has no chest simply never ends, an egg arena with no weapon balloon freezes the
moment a bot runs out of things to do. This module is the one list of those
needs, per level type, each read out of the decomp:

* **Every challenge** spawns four racers (``objects.c:1175``) at entrance 0's
  ``racerIndex`` 0-3 (``objects.c:1121-1136``).
* **Battle and Bananas** bots walk the AI node graph: ``racer_AI_pathing_inputs``
  sends only those two race types to ``racer_ai_challenge`` (``racer.c:609-624``),
  which starts from the node nearest the racer (``racer.c:718``). Its
  pathfinder (``func_8001CD28``, ``objects.c:7675``) targets nodes by their
  destination class: 1 for weapon balloons (``racer.c:929``, ``:948``), and in
  Bananas 4 + racer for that racer's chest (``racer.c:760-773``, ``:951``).
* **Battle** eliminates a racer whose bananas reach 0 and ends when three are
  out (``objects.c:6166-6175``, ``:6192-6195``); the bananas go by being hit.
* **Bananas**: a racer banks at the chest whose ``playerID`` is its index + 1
  (``obj_init_treasuresucker`` stores ``(playerID - 1) & 3``,
  ``object_functions.c:4204``), from within 225 units (``:4231``), and wins at
  10 banked (``:4295``). A Coin Creator spawns a banana and another 20 seconds
  after it is taken (``:4318-4345``). The Character Flag over a chest shows whose
  it is, 0-based (``:1082-1084``).
* **Eggs** bots do not use the graph (``racer_ai_eggs``, ``racer.c:1052``): they
  steer straight at eggs, Head-For Points and weapon balloons. An Egg Creator
  keeps one egg on it (``object_functions.c:818-836``). An egg lands in a nest
  by touching collision surface ``SURFACE_EGG_01``..``04`` - the racer is the
  surface minus ``SURFACE_EGG_01`` - and anywhere outside ``SURFACE_EGG_SPAWN``
  ..``SURFACE_EGG_04`` it breaks and goes back to its creator
  (``object_functions.c:748-768``). Three hatched eggs win (``:797``). A bot
  carrying an egg heads for a Head-For Point of kind 0 and drops it within 100
  units (``racer.c:1158-1159``, ``:1187-1189``, ``:1219-1223``, ``:1324-1328``);
  kind 2 is a racer's way home (``racer.c:1190-1199``). The target search loops
  until it finds something (``racer.c:1102``, ``:1257-1260``), so with no
  kind-0 point, or no weapon balloon for an idle bot to fetch
  (``racer.c:1144-1154``), it never returns.
* **Hub**: only the players are spawned (``objects.c:1165``), at the entrance
  the level was entered through. An Exit fires when a player crosses its plane
  inside its radius (``object_functions.c:2441-2457``); its bytes become the
  level settings (``func_8006D968``, ``thread3_main.c:790-806``):
  ``destinationMapId`` the level,
  ``returnSpawnIndex`` the entrance of *this* hub the player comes back to.
  T.T. and Taj walk the AI node graph (``object_functions.c:1362``,
  ``:3025``); Taj's challenge races run
  around the hub's checkpoints (``mode_init_taj_race``, ``objects.c:9715``).
* **Boss race**: two racers, the player and the boss (``objects.c:1179``), in
  the vehicles the header's boss implies (``objects.c:1236-1258``); both steer
  along the checkpoints like a race (``racer.c:622``).

Each requirement is a :class:`Row`. The Mode Checklist panel draws them; the
validator reports the ones that are not met (:func:`issues`), so the two can
never disagree.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import math
from typing import Dict, Iterable, List, Optional, Sequence, Set

from . import ai_graph, level_links, level_types

AINODE = "ASSET_OBJECT_AINODE"
SETUPPOINT = level_types.SETUPPOINT
CHECKPOINT = level_types.CHECKPOINT
BALLOON = level_types.WEAPON_BALLOON
CHEST = "ASSET_OBJECT_TREASURESUCKER"
FLAG = "ASSET_OBJECT_CHARACTERFLAG"
COIN = "ASSET_OBJECT_COIN"
COIN_CREATOR = "ASSET_OBJECT_COINCREATOR"
EGG_CREATOR = "ASSET_OBJECT_EGGCREATOR"
HEAD_FOR = "ASSET_OBJECT_HEADFORPOINT"
EXIT = level_links.EXIT
TT = "ASSET_OBJECT_STOPWATCHMAN"
TAJ = "ASSET_OBJECT_PARKWARDEN"

#: Destination classes (AI node ``unk8``) the arena AI asks for.
CLASS_BALLOON = 1
CLASS_BANK = 4  #: plus the racer's index

#: Head-For Point kinds (``unk8``).
HEAD_DROP = 0
HEAD_HOME = 2

#: The challenge racers, 0-3.
RACERS = (0, 1, 2, 3)

#: Nest surfaces, by racer.
NEST_SURFACES = ("SURFACE_EGG_01", "SURFACE_EGG_02", "SURFACE_EGG_03",
                 "SURFACE_EGG_04")

#: A ``returnSpawnIndex`` that means "not coming back here".
NO_RETURN = 255

# Row states.
OK = "ok"
MISSING = "missing"
NOTE = "note"        #: Nothing to do; worth knowing.
#: Playable, but not what the author meant: reported as a warning and drawn
#: as one, yet counted as ready - retail ships levels like it.
ADVISE = "advise"
UNKNOWN = "unknown"  #: Cannot be checked from what is available.

ERROR = "error"
WARNING = "warning"

# What a row's button does; the panel maps each to an operator.
FIX_START = "START"
FIX_AI_LINE = "AI_LINE"
FIX_AI_TAG = "AI_TAG"
FIX_AI_LINK = "AI_LINK"
FIX_CHESTS = "CHESTS"
FIX_FLAGS = "FLAGS"
FIX_HEAD_FOR = "HEAD_FOR"
FIX_EGG_CREATOR = "EGG_CREATOR"
FIX_COIN_CREATOR = "COIN_CREATOR"
FIX_BALLOON = "BALLOON"
FIX_EXIT = "EXIT"
FIX_CHECKPOINT = "CHECKPOINT"


class Row:
    """One requirement: what it is, whether it is met, and why it matters."""

    __slots__ = ("rid", "label", "status", "detail", "problem", "severity",
                 "fix", "objects", "reported")

    def __init__(self, rid: str, label: str, status: str, detail: str,
                 problem: str = "", severity: str = WARNING, fix: str = "",
                 objects: Iterable[int] = (), reported: bool = True):
        self.rid = rid
        #: Short, with the count where there is one: ``"Treasure chests 3/4"``.
        self.label = label
        self.status = status
        #: One or two sentences: why the level needs it.
        self.detail = detail
        #: What validation says when it is missing; the label and detail if
        #: left empty.
        self.problem = problem
        self.severity = severity
        self.fix = fix
        #: Document positions of the objects the row is about.
        self.objects = list(objects)
        #: False where another check already reports the same thing - the
        #: start grid has its own, more detailed, validation.
        self.reported = reported

    @property
    def ok(self) -> bool:
        return self.status == OK

    @property
    def message(self) -> str:
        return self.problem or "%s: %s" % (self.label, self.detail)

    def __repr__(self):
        return "Row(%r, %r)" % (self.rid, self.status)


# ---------------------------------------------------------------------------
# Reading the map
# ---------------------------------------------------------------------------

class _Map:
    """The object map, indexed once for every rule."""

    def __init__(self, object_map):
        self.objects = list(object_map.objects)
        self.by_id: Dict[str, List[int]] = {}
        for position, obj in enumerate(self.objects):
            self.by_id.setdefault(obj.object_id, []).append(position)

    def positions(self, object_id: str) -> List[int]:
        return self.by_id.get(object_id, [])

    def count(self, object_id: str) -> int:
        return len(self.positions(object_id))

    def field(self, position: int, name: str, default=None):
        return self.objects[position].fields.get(name, default)

    def values(self, object_id: str, name: str) -> Dict[object, List[int]]:
        """``{value: [positions]}`` of one field across one type."""
        found: Dict[object, List[int]] = {}
        for position in self.positions(object_id):
            found.setdefault(self.field(position, name), []).append(position)
        return found

    def starts(self, entrance: int = 0) -> Set[int]:
        return {self.field(p, "racerIndex") for p in self.positions(SETUPPOINT)
                if self.field(p, "entranceID", 0) == entrance}

    def entrances(self) -> Set[int]:
        return {self.field(p, "entranceID", 0) for p in self.positions(SETUPPOINT)}

    def linked_nodes(self) -> int:
        """AI nodes holding at least one link."""
        return sum(1 for p in self.positions(AINODE) if self.is_linked(p))

    def is_linked(self, position: int) -> bool:
        return any(a != ai_graph.NO_NEIGHBOUR
                   for a in (self.field(position, "adjacent") or []))

    def where(self, position: int):
        return tuple(self.objects[position].translation or (0.0, 0.0, 0.0))


def _nodes(ids: Sequence[object]) -> str:
    """``[14, 20]`` -> ``"nodes 14 and 20"``."""
    names = [str(v) for v in ids]
    if not names:
        return "no node"
    if len(names) == 1:
        return "node %s" % names[0]
    shown = names[:6] + (["%d more" % (len(names) - 6)] if len(names) > 6 else [])
    return "nodes %s and %s" % (", ".join(shown[:-1]), shown[-1])


def _players(values: Sequence[int]) -> str:
    """``[0, 2]`` -> ``"racers 0 and 2"``."""
    names = [str(v) for v in values]
    if not names:
        return "no racer"
    if len(names) == 1:
        return "racer %s" % names[0]
    return "racers %s and %s" % (", ".join(names[:-1]), names[-1])


# ---------------------------------------------------------------------------
# The rules
# ---------------------------------------------------------------------------

def _start_row(found: _Map, key: str) -> Row:
    count = level_types.spawn_count(key)
    present = found.starts(0)
    missing = [i for i in range(count) if i not in present]
    if key == level_types.BOSS:
        label = "Start: player and boss"
        detail = ("racerIndex 0 is the player, 1 the boss, both at entrance 0. "
                  "Generate Start Grid places the pair side by side.")
    else:
        label = "Start ring %d/%d" % (count - len(missing), count)
        detail = ("Four racers start at entrance 0, racerIndex 0-3. Generate "
                  "Start Grid places a ring facing the centre.")
    return Row("start", label, MISSING if missing else OK, detail,
               severity=ERROR, fix=FIX_START, reported=False)


def _graph_rows(found: _Map, key: str) -> List[Row]:
    nodes = found.count(AINODE)
    linked = found.linked_nodes()
    rows = [Row(
        "ai_graph",
        "AI node graph: %d nodes" % nodes if nodes else "AI node graph",
        OK if linked >= 2 else MISSING,
        "Battle and banana bots steer only along this graph. Draw a curve "
        "through the arena, select it, then sample it with AI Line From "
        "Curve.",
        problem=("No connected AI node graph: the AI racers will not move. "
                 "%s bots steer only along the AI node graph; draw one with "
                 "AI Node Graph > AI Line From Curve."
                 % ("Battle" if key == level_types.BATTLE else "Banana")),
        severity=ERROR, fix=FIX_AI_LINE,
    )]
    if linked < 2:
        return rows

    classes = found.values(AINODE, "unk8")
    tagged = len(classes.get(CLASS_BALLOON, []))
    rows.append(Row(
        "balloon_nodes", "Weapon nodes: %d" % tagged, OK if tagged else MISSING,
        "Nodes with destination class 1 are where a bot goes for a weapon. "
        "Tag Destinations marks the node nearest each balloon.",
        problem=("No AI node has destination class 1, so the bots never go "
                 "for a weapon balloon. Tag Destinations in AI Node Graph "
                 "marks the node nearest each balloon."),
        fix=FIX_AI_TAG if found.count(BALLOON) else FIX_BALLOON,
    ))
    rows += _weapon_node_rows(found, classes.get(CLASS_BALLOON, []))
    if key == level_types.BANANAS:
        missing = [r for r in RACERS if not classes.get(CLASS_BANK + r)]
        rows.append(Row(
            "bank_nodes", "Bank nodes %d/4" % (4 - len(missing)),
            MISSING if missing else OK,
            "A node with destination class 4 + racer, by that racer's chest, "
            "is where a bot carrying two or more bananas drives to bank them.",
            problem=("No AI node has destination class %s, so %s never drive "
                     "home to bank. Tag Destinations marks the node nearest "
                     "each chest." % (
                         " or ".join(str(CLASS_BANK + r) for r in missing),
                         _players(missing).replace("racer", "bot"))),
            fix=FIX_AI_TAG if found.count(CHEST) else FIX_CHESTS,
        ))
    return rows


def _weapon_node_rows(found: _Map, weapons: Sequence[int]) -> List[Row]:
    """Whether each weapon node has a balloon on it, and a link to the graph.

    Bots never look at a balloon: they drive to the class-1 node and turn
    for the next node 50 units out (``racer.c:682``), so a balloon off its
    node is driven past and a node with no balloon is a wasted trip. That is
    advice rather than a fault - Smokey Castle ships two class-1 nodes 215
    and 762 units from any balloon - so it is never an error, and the reach
    accepted is :data:`ai_graph.WEAPON_REACH_CHECK`, which retail's
    placements pass, rather than the 50 the overlay draws.
    """
    rows = []
    balloons = [found.where(p) for p in found.positions(BALLOON)]
    if weapons and balloons:
        far = [p for p in weapons
               if min(math.dist(found.where(p), b) for b in balloons)
               > ai_graph.WEAPON_REACH_CHECK]
        ids = [found.field(p, "nodeID", "?") for p in far]
        # Shown only while some node is off its balloon: a met row hides it.
        detail = ("Bots never look at balloons, only at weapon nodes. %s %s no "
                  "balloon within %d units, so a bot drives there for nothing: "
                  "put a balloon on it, or set its class to 0."
                  % (_nodes(ids).capitalize(), "has" if len(far) == 1 else "have",
                     ai_graph.WEAPON_REACH_CHECK))
        rows.append(Row(
            "weapon_reach", "Weapon nodes on a balloon %d/%d"
            % (len(weapons) - len(far), len(weapons)),
            ADVISE if far else OK, detail,
            problem=("Weapon %s %s no weapon balloon within %d units: a bot "
                     "without a weapon drives there, finds nothing to take and "
                     "goes on to the next. Put a balloon on it, or set its "
                     "destination class to 0."
                     % (_nodes(ids), "has" if len(far) == 1 else "have",
                        ai_graph.WEAPON_REACH_CHECK)),
            objects=far,
        ))

    loose = [p for p in weapons if not found.is_linked(p)]
    if loose:
        ids = [found.field(p, "nodeID", "?") for p in loose]
        rows.append(Row(
            "weapon_links", "Weapon nodes linked %d/%d"
            % (len(weapons) - len(loose), len(weapons)), MISSING,
            "A weapon node with no link is off the graph: the pathfinder "
            "never reaches it, so no bot comes for its balloon.",
            problem=("Weapon %s %s not linked to the AI graph, so no bot ever "
                     "comes for %s balloon. Link Weapon Nodes in AI Node Graph "
                     "joins each to the nodes nearest it."
                     % (_nodes(ids), "is" if len(loose) == 1 else "are",
                        "its" if len(loose) == 1 else "their")),
            fix=FIX_AI_LINK, objects=loose,
        ))
    return rows


def _balloon_row(found: _Map, key: str) -> Row:
    count = found.count(BALLOON)
    if key == level_types.EGGS:
        detail = ("A bot with nothing else to do fetches a weapon. With no "
                  "balloon to find, it searches forever and the game freezes.")
        problem = ("No weapon balloon: an egg bot with nothing else to do "
                   "searches for one forever and the game freezes. Place at "
                   "least one.")
        severity = ERROR
    elif key == level_types.BATTLE:
        detail = ("Racers lose their bananas to weapons, and a racer left with "
                  "none is out. Without balloons nobody can be knocked out.")
        problem = ("No weapon balloon: nobody can be hit, so nobody runs out "
                   "of bananas and the battle never ends.")
        severity = WARNING
    else:
        detail = "Weapons let racers knock bananas out of each other."
        problem = "No weapon balloon: the racers cannot attack each other."
        severity = WARNING
    return Row("balloons", "Weapon balloons: %d" % count,
               OK if count else MISSING, detail, problem=problem,
               severity=severity, fix=FIX_BALLOON)


def owner(object_id: str, value) -> Optional[int]:
    """The racer an object's racer field names, as the game reads it.

    A chest stores ``playerID - 1`` masked to two bits
    (``object_functions.c:4204``), so 1-4 are racers 0-3 and a 0 is racer 3,
    not racer 0. A flag's index and a Head-For Point's are taken as they are.
    """
    if not isinstance(value, int):
        return None
    if object_id == CHEST:
        return (value - 1) & 3
    return value


def _per_racer(found: _Map, object_id: str, field: str):
    """``(present, missing, positions)`` of one per-racer type, by racer 0-3."""
    owners = {owner(object_id, found.field(p, field))
              for p in found.positions(object_id)}
    present = [r for r in RACERS if r in owners]
    missing = [r for r in RACERS if r not in present]
    return present, missing, found.positions(object_id)


def _flag_row(found: _Map, over: str) -> Row:
    present, missing, where = _per_racer(found, FLAG, "playerIndex")
    return Row(
        "flags", "Racer flags %d/4" % len(present), MISSING if missing else OK,
        "A Character Flag shows a racer's portrait over their %s, so players "
        "can tell whose is whose." % over,
        problem="No Character Flag for %s: nothing shows whose %s is whose."
        % (_players(missing), over),
        fix=FIX_FLAGS, objects=where,
    )


def _battle(found: _Map) -> List[Row]:
    return [_start_row(found, level_types.BATTLE)] + \
        _graph_rows(found, level_types.BATTLE) + \
        [_balloon_row(found, level_types.BATTLE)]


def _bananas(found: _Map) -> List[Row]:
    present, missing, where = _per_racer(found, CHEST, "playerID")
    rows = [_start_row(found, level_types.BANANAS)]
    rows.append(Row(
        "chests", "Treasure chests %d/4" % len(present),
        MISSING if missing else OK,
        "Each racer banks bananas at their own chest - playerID is the racer "
        "plus 1 - from within 225 units. Ten banked wins.",
        problem=("No treasure chest for %s: they can never bank a banana, so "
                 "they can never win. Each racer needs a chest whose playerID "
                 "is its racerIndex + 1." % _players(missing)),
        severity=ERROR, fix=FIX_CHESTS, objects=where,
    ))
    creators = found.count(COIN_CREATOR)
    loose = found.count(COIN)
    rows.append(Row(
        "bananas", "Banana creators: %d" % creators,
        OK if creators else MISSING,
        "A Coin Creator holds a banana and makes another 20 seconds after it "
        "is taken. Loose bananas never come back.",
        problem=("No banana to collect: place Coin Creators, which make a new "
                 "banana 20 seconds after one is taken." if not loose else
                 "No Coin Creator: the %d loose banana(s) never come back, so "
                 "the challenge can run dry before anyone banks ten." % loose),
        severity=ERROR if not loose else WARNING, fix=FIX_COIN_CREATOR,
    ))
    rows += _graph_rows(found, level_types.BANANAS)
    rows.append(_flag_row(found, "chest"))
    rows.append(_balloon_row(found, level_types.BANANAS))
    return rows


def _eggs(found: _Map, surfaces: Optional[Set[str]]) -> List[Row]:
    rows = [_start_row(found, level_types.EGGS)]
    creators = found.count(EGG_CREATOR)
    rows.append(Row(
        "egg_creator", "Egg creators: %d" % creators, OK if creators else MISSING,
        "An Egg Creator keeps one egg on it and makes a new one whenever it "
        "is taken. Retail's Fire Mountain has three in the middle.",
        problem="No Egg Creator: no egg ever appears.",
        severity=ERROR, fix=FIX_EGG_CREATOR,
    ))

    if surfaces is None:
        rows.append(Row(
            "nests", "Nest floors", UNKNOWN,
            "A nest is ground, not an object: racer N's eggs count on faces "
            "with surface Egg %s. Load the track geometry to check." %
            "/".join(str(r + 1) for r in RACERS),
            reported=False,
        ))
    else:
        missing = [r for r in RACERS if NEST_SURFACES[r] not in surfaces]
        rows.append(Row(
            "nests", "Nest floors %d/4" % (4 - len(missing)),
            MISSING if missing else OK,
            "A nest is ground, not an object: an egg dropped on surface "
            "SURFACE_EGG_0N counts for racer N-1, and anywhere else it breaks. "
            "Set it in Geometry > Surface.",
            problem=("No geometry uses %s, so %s have no nest: every egg they "
                     "drop breaks and goes back to the creator. Set the nest "
                     "floors' surface in Geometry > Surface."
                     % (", ".join(NEST_SURFACES[r] for r in missing),
                        _players(missing))),
            severity=ERROR,
        ))

    values = found.values(HEAD_FOR, "unk8")
    drops = [p for p in values.get(HEAD_DROP, [])]
    homes = [p for p in values.get(HEAD_HOME, [])]
    owners_drop = {found.field(p, "unk9") for p in drops}
    owners_home = {found.field(p, "unk9") for p in homes}
    missing = [r for r in RACERS if r not in owners_drop or r not in owners_home]
    status = OK if not missing else MISSING
    if not drops:
        problem = ("No Head-For Point of kind 0: a bot carrying an egg looks "
                   "for one to drop it at, never finds it, and the game "
                   "freezes. Add Head-For Points places a pair at each nest.")
        severity = ERROR
    else:
        problem = ("%s %s no Head-For Point pair (kind 0 at the nest, kind 2 "
                   "on the way to it): their bots drop eggs at another nest "
                   "or stall." % (_players(missing).capitalize(),
                                  "has" if len(missing) == 1 else "have"))
        severity = WARNING
    rows.append(Row(
        "head_for", "Head-For Points %d/4 racers" % (4 - len(missing)), status,
        "Where the bots aim: kind 0 over a nest is where they drop an egg, "
        "kind 2 their way home. Racer is the nest's owner, 0-3.",
        problem=problem, severity=severity, fix=FIX_HEAD_FOR,
        objects=found.positions(HEAD_FOR),
    ))
    rows.append(_flag_row(found, "nest"))
    rows.append(_balloon_row(found, level_types.EGGS))
    rows.append(Row(
        "no_graph", "No AI node graph needed", NOTE,
        "Egg bots steer straight at eggs, nests, Head-For Points and "
        "balloons; AI nodes do nothing here.",
    ))
    return rows


def _hub(found: _Map) -> List[Row]:
    rows = []
    present = found.starts(0)
    rows.append(Row(
        "entrance0", "Entrance 0", OK if 0 in present else MISSING,
        "Where a new game and a plain warp put the player: a start position "
        "with entranceID 0 and racerIndex 0. Each door back needs its own "
        "entrance.",
        severity=ERROR, fix=FIX_START, reported=False,
    ))

    exits = found.positions(EXIT)
    rows.append(Row(
        "exits", "Exits: %d" % len(exits), OK if exits else MISSING,
        "An Exit warps the player when they drive through it, inside its "
        "radius and from its front.",
        problem="No Exit: the player can never leave this hub.",
        fix=FIX_EXIT,
    ))
    if exits:
        aimless = [p for p in exits
                   if not level_links.linked_track(found.objects[p])
                   and not found.field(p, level_links.DESTINATION_FIELD)]
        rows.append(Row(
            "destinations", "Exit destinations %d/%d"
            % (len(exits) - len(aimless), len(exits)),
            MISSING if aimless else OK,
            "Each Exit names a retail level or another custom track; pick it "
            "in the DKR Object panel.",
            problem=("%d Exit(s) lead nowhere. Pick a destination in the DKR "
                     "Object panel: a retail level, or another custom track by "
                     "its id." % len(aimless)),
            severity=ERROR, objects=aimless,
        ))
        entrances = found.entrances()
        stranded = []
        for p in exits:
            back = found.field(p, "returnSpawnIndex", 0)
            if isinstance(back, int) and back != NO_RETURN and back not in entrances:
                stranded.append(p)
        wanted = sorted({found.field(p, "returnSpawnIndex") for p in stranded})
        rows.append(Row(
            "returns", "Return entrances", MISSING if stranded else OK,
            "returnSpawnIndex is the entrance of this hub the player comes "
            "back to after the race; 255 means they do not come back here.",
            problem=("%d Exit(s) bring the player back to entrance %s, which "
                     "this hub does not have: they would start at the map "
                     "origin. Add Entrance Point, then set its entranceID."
                     % (len(stranded), ", ".join(str(w) for w in wanted))),
            fix=FIX_START, objects=stranded,
        ))

    walkers = [label for object_id, label in ((TT, "T.T."), (TAJ, "Taj"))
               if found.count(object_id)]
    if walkers:
        nodes = found.linked_nodes()
        rows.append(Row(
            "npc_graph", "AI node graph: %d nodes" % found.count(AINODE),
            OK if nodes >= 2 else MISSING,
            "%s walk%s the AI node graph." % (
                " and ".join(walkers), "s" if len(walkers) == 1 else ""),
            problem=("%s walk%s the AI node graph, which this hub has none of: "
                     "draw one in AI Node Graph." % (
                         " and ".join(walkers),
                         "s" if len(walkers) == 1 else "")),
            fix=FIX_AI_LINE,
        ))
    if found.count(TAJ):
        count = found.count(CHECKPOINT)
        rows.append(Row(
            "taj_course", "Taj's course: %d checkpoints" % count,
            OK if count else MISSING,
            "Taj's challenge races run around this hub's checkpoints.",
            problem="Taj is here but the hub has no checkpoints: his challenge "
                    "races have no course.",
            fix=FIX_CHECKPOINT,
        ))
    return rows


def _boss(found: _Map, boss: Optional[str]) -> List[Row]:
    rows = [_start_row(found, level_types.BOSS)]
    known = {b[0] for b in level_types.BOSSES}
    if boss is not None:
        chosen = boss in known
        rows.append(Row(
            "boss", "Boss: %s" % level_types.boss_label(boss) if chosen else "Boss",
            OK if chosen else MISSING,
            "Chosen above. The race is run in the boss's vehicle (%s)."
            % level_types.vehicle_name(level_types.boss_vehicle(boss)).lower(),
            problem="No boss chosen: the header's boss-race-id decides who "
                    "the player races and in what.",
            severity=ERROR,
        ))
    count = found.count(CHECKPOINT)
    rows.append(Row(
        "checkpoints", "Checkpoints: %d" % count,
        OK if count >= 3 else MISSING,
        "The boss steers along the checkpoints and laps count through them, "
        "as in a race. AI Racers shows the line it drives.",
        problem=("No checkpoints: the boss has no line to drive and no lap "
                 "ever counts." if not count else
                 "Only %d checkpoint(s): with fewer than three the boss's line "
                 "folds back on itself." % count),
        severity=ERROR if not count else WARNING, fix=FIX_CHECKPOINT,
    ))
    return rows


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------

#: The level types the checklist speaks for.
KEYS = (level_types.BATTLE, level_types.BANANAS, level_types.EGGS,
        level_types.HUB, level_types.BOSS)


def applies(key: Optional[str]) -> bool:
    return key in KEYS


def rows(object_map, key: Optional[str], boss: Optional[str] = None,
         surfaces: Optional[Iterable[str]] = None) -> List[Row]:
    """The checklist for one level type; empty for a race or a special.

    ``boss`` is the scene's boss, or ``None`` when it is not known.
    ``surfaces`` names the ``SurfaceType`` members the track geometry uses, or
    ``None`` when there is no geometry to look at.
    """
    found = _Map(object_map)
    surface_set = set(surfaces) if surfaces is not None else None
    if key == level_types.BATTLE:
        return _battle(found)
    if key == level_types.BANANAS:
        return _bananas(found)
    if key == level_types.EGGS:
        return _eggs(found, surface_set)
    if key == level_types.HUB:
        return _hub(found)
    if key == level_types.BOSS:
        return _boss(found, boss)
    return []


def issues(object_map, key: Optional[str], boss: Optional[str] = None,
           surfaces: Optional[Iterable[str]] = None):
    """``(severity, message, object_id, positions)`` per unmet requirement."""
    found = []
    for row in rows(object_map, key, boss, surfaces):
        if row.status not in (MISSING, ADVISE) or not row.reported:
            continue
        severity = WARNING if row.status == ADVISE else row.severity
        found.append((severity, row.message, "", row.objects))
    return found


def summary(checklist: Sequence[Row]) -> str:
    """``"5 of 7 ready"`` for the panel header."""
    counted = [r for r in checklist if r.status in (OK, MISSING, ADVISE)]
    done = sum(1 for r in counted if r.status != MISSING)
    return "%d of %d ready" % (done, len(counted))


def missing_racers(object_map, object_id: str, field: str,
                   match: Optional[Dict[str, object]] = None) -> List[int]:
    """Racers 0-3 with no object of a type yet, for the Add buttons.

    ``match`` narrows the objects counted, e.g. Head-For Points of one kind.
    """
    owned = set()
    for obj in object_map.objects:
        if obj.object_id != object_id:
            continue
        if match and any(obj.fields.get(k) != v for k, v in match.items()):
            continue
        owned.add(owner(object_id, obj.fields.get(field)))
    return [r for r in RACERS if r not in owned]
