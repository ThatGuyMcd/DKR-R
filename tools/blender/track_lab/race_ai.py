"""What the race AI reads from a track, reproduced so an author can see it.

Two things decide how the computer racers drive a race: the checkpoints, which
they steer along, and a handful of level header bytes, which decide how fast and
how aggressive they are. Neither is visible in the data. This module turns both
into something a panel and a viewport overlay can show, and it reproduces the
game's own arithmetic rather than an approximation of it - a line that is almost
where the bots drive is worse than none.

**The line.** ``checkpoint_update_all`` (``objects.c``) keeps the checkpoints
whose ``vehicleType`` equals ``header.unk4F[vehicle]``, at most
:data:`MAX_CHECKPOINTS` of them, numbers an alternate-route gate ``index + 255``,
bubble-sorts on that, and pairs each alternate with the main gate of the same
``index``. ``func_80045C48`` (``racer.c``) then takes four consecutive gates,
moves each by the racer's lane -

    x += node.scale * cos(yaw) * lateral
    y += node.scale * vertical
    z -= node.scale * sin(yaw) * lateral

where ``node.scale = max(scale, 5) / 64 * 2`` - and steers along the
``cubic_spline_interpolation`` (a Catmull-Rom spline) through them. The lateral
direction is ``(cos yaw, 0, -sin yaw)`` because ``mtxf_transform_point`` treats
its matrix as row vectors: ``mtxf_from_transform`` with a Y rotation alone sends
``(0, 0, 1)`` to ``(sin yaw, 0, cos yaw)``, the gate's normal.

**The difficulty.** ``aitable_init`` (``game.c``) picks one of ten behaviour
tables from the header's ``ai-levels`` block, and ``func_80042D20`` reads each
racer's start skill out of ``unkC`` or ``unk16``. What each byte does is below,
next to the field it lives in.

Deliberately free of ``bpy``, like the other format modules, so it is tested on a
plain Python against every retail map.
"""

from __future__ import annotations

import math
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from . import level_header_template as template

CHECKPOINT = "ASSET_OBJECT_CHECKPOINT"

#: A racer is always in one of four lanes (``racer->unk1CA``).
LANES = 4

#: The checkpoint's per-lane bytes, lane 1 first (``checkpoint_update_all``).
LATERAL_FIELDS = ("unkB", "unkC", "unkD", "unkE")
VERTICAL_FIELDS = ("unkF", "unk10", "unk11", "unk12")

#: Every field the line depends on, for a caller deciding whether to redraw.
LINE_FIELDS = ("scale", "index", "angleY", "isAltCheckpoint", "vehicleType") \
    + LATERAL_FIELDS + VERTICAL_FIELDS

#: ``checkpointID += 255`` for a gate on the alternate route.
ALTERNATE_OFFSET = 255

#: ``gNumberOfMainCheckpoints < MAX_CHECKPOINTS`` (``objects.c``): gates past
#: this many in one set are never loaded, alternates included, and nothing says
#: so in game.
MAX_CHECKPOINTS = 60

#: ``obj_init_checkpoint`` clamps a smaller scale byte up to this.
MIN_SCALE = 5

#: Points drawn per stretch between two gates. The game evaluates the spline
#: continuously; this is only how finely the editor draws it.
SAMPLES = 12

Point = Tuple[float, float, float]


# ---------------------------------------------------------------------------
# One gate
# ---------------------------------------------------------------------------

class Node:
    """One checkpoint as the racers see it: a ``CheckpointNode``."""

    __slots__ = ("x", "y", "z", "scale", "sin_yaw", "cos_yaw", "lateral",
                 "vertical", "index", "alternate", "source")

    def __init__(self, position, angle_degrees, scale_byte, index, alternate,
                 lateral=(0, 0, 0, 0), vertical=(0, 0, 0, 0), source=None):
        self.x, self.y, self.z = (float(c) for c in position)
        # obj_init_checkpoint: trans.scale = max(scale & 0xFF, 5) / 64, and
        # checkpoint_update_all doubles it into node.scale.
        self.scale = max(int(scale_byte) & 0xFF, MIN_SCALE) / 64.0 * 2.0
        # angleY is stored as a byte, 64 steps to a turn, and the catalogue
        # hands it over in degrees. sin and cos do not care which turn it is on.
        yaw = math.radians(float(angle_degrees))
        self.sin_yaw = math.sin(yaw)
        self.cos_yaw = math.cos(yaw)
        self.lateral = tuple(int(v) for v in lateral)
        self.vertical = tuple(int(v) for v in vertical)
        self.index = int(index)
        self.alternate = bool(alternate)
        #: Whatever the caller built this from, handed back untouched.
        self.source = source

    @property
    def checkpoint_id(self) -> int:
        return self.index + (ALTERNATE_OFFSET if self.alternate else 0)

    def lane_point(self, lane: int) -> Point:
        """Where lane ``lane`` (0 to 3) crosses this gate, in map space."""
        lateral = self.scale * self.lateral[lane]
        return (
            self.x + lateral * self.cos_yaw,
            self.y + self.scale * self.vertical[lane],
            self.z - lateral * self.sin_yaw,
        )


def node_from(map_object, source=None) -> Node:
    """A :class:`Node` from a :class:`~track_lab.gltf_io.MapObject`."""
    fields = map_object.fields
    return Node(
        map_object.translation,
        fields.get("angleY", 0.0),
        fields.get("scale", 64),
        fields.get("index", 0),
        fields.get("isAltCheckpoint", 0),
        [fields.get(name, 0) for name in LATERAL_FIELDS],
        [fields.get(name, 0) for name in VERTICAL_FIELDS],
        source=map_object if source is None else source,
    )


# ---------------------------------------------------------------------------
# The route
# ---------------------------------------------------------------------------

class Route:
    """The checkpoints one vehicle's racers load, in the order they use them."""

    __slots__ = ("vehicle_set", "main", "alternates", "alternate_of",
                 "duplicates", "unpaired", "dropped")

    def __init__(self, vehicle_set: int):
        self.vehicle_set = vehicle_set
        #: The main loop, sorted as the game sorts it.
        self.main: List[Node] = []
        #: The alternate-route gates, sorted the same way.
        self.alternates: List[Node] = []
        #: Main position -> the alternate gate paired with it.
        self.alternate_of: Dict[int, Node] = {}
        #: Checkpoint ids that more than one gate carries. The game prints
        #: "Error: Multiple checkpoint no" over the race for these.
        self.duplicates: List[int] = []
        #: Alternate gates whose index names no main gate: loaded, never used.
        self.unpaired: List[Node] = []
        #: Gates in this set past :data:`MAX_CHECKPOINTS`, never loaded.
        self.dropped = 0

    def node_at(self, position: int, alternate: bool = False) -> Node:
        """``find_next_checkpoint_node``: the gate a racer uses at a position."""
        position %= len(self.main)
        if alternate and position in self.alternate_of:
            return self.alternate_of[position]
        return self.main[position]


def vehicle_sets(map_objects: Iterable) -> Dict[int, int]:
    """``{vehicleType: checkpoint count}`` for every set a map carries."""
    counts: Dict[int, int] = {}
    for obj in map_objects:
        if obj.object_id == CHECKPOINT:
            key = int(obj.fields.get("vehicleType", 0))
            counts[key] = counts.get(key, 0) + 1
    return counts


def build_route(map_objects: Iterable, vehicle_set: int = 0) -> Route:
    """What ``checkpoint_update_all`` loads for one vehicle set.

    ``map_objects`` are taken in the order given, which should be document
    order - the order the game spawned them in, and so the order it counts to
    :data:`MAX_CHECKPOINTS` in.
    """
    route = Route(int(vehicle_set))
    loaded: List[Node] = []
    for obj in map_objects:
        if obj.object_id != CHECKPOINT:
            continue
        if int(obj.fields.get("vehicleType", 0)) != route.vehicle_set:
            continue
        if len(loaded) >= MAX_CHECKPOINTS:
            route.dropped += 1
            continue
        loaded.append(node_from(obj))

    # A bubble sort that swaps only on strictly less is stable, and so is
    # Python's sort: gates sharing an id keep the order they were spawned in.
    loaded.sort(key=lambda node: node.checkpoint_id)
    route.duplicates = sorted({
        later.checkpoint_id for earlier, later in zip(loaded, loaded[1:])
        if earlier.checkpoint_id == later.checkpoint_id
    })

    route.main = [node for node in loaded if not node.alternate]
    route.alternates = [node for node in loaded if node.alternate]
    for alternate in route.alternates:
        # The game breaks at the first main gate with the same id and lets a
        # later alternate overwrite an earlier one's pairing.
        for position, node in enumerate(route.main):
            if node.index == alternate.index:
                route.alternate_of[position] = alternate
                break
        else:
            route.unpaired.append(alternate)
    return route


# ---------------------------------------------------------------------------
# The spline
# ---------------------------------------------------------------------------

def cubic_spline(data: Sequence[float], x: float) -> float:
    """``cubic_spline_interpolation`` (``objects.c``), term for term."""
    a = -0.5 * data[0] + 1.5 * data[1] - 1.5 * data[2] + 0.5 * data[3]
    b = data[0] - 2.5 * data[1] + 2.0 * data[2] - 0.5 * data[3]
    c = 0.5 * data[2] - 0.5 * data[0]
    return (((a * x) + b) * x + c) * x + data[1]


def _segment(route: Route, start: int, lane: int, alternate: bool,
             samples: int, closing: bool) -> List[Point]:
    """The stretch from gate ``start`` to the next, as ``func_80045C48`` sees
    it: control points ``start - 1`` to ``start + 2``, evaluated between the
    middle two."""
    control = [route.node_at(start + offset, alternate).lane_point(lane)
               for offset in (-1, 0, 1, 2)]
    axes = [[point[axis] for point in control] for axis in range(3)]
    steps = range(samples + 1) if closing else range(samples)
    return [
        tuple(cubic_spline(axes[axis], step / float(samples)) for axis in range(3))
        for step in steps
    ]


def lane_line(route: Route, lane: int, samples: int = SAMPLES) -> List[Point]:
    """One lane of the main route as a closed polyline, first point repeated.

    Closed even for a point-to-point boss race: the game wraps its gate index
    whatever the track looks like.
    """
    count = len(route.main)
    if count < 2:
        return []
    points: List[Point] = []
    for start in range(count):
        points += _segment(route, start, lane, False, samples, False)
    points.append(points[0])
    return points


def alternate_lines(route: Route, lane: int,
                    samples: int = SAMPLES) -> List[List[Point]]:
    """Where a racer on the alternate route leaves the main line, per lane.

    Only the stretches whose four control gates include an alternate differ
    from the main line; each run of them comes back as its own polyline.
    """
    count = len(route.main)
    if count < 2 or not route.alternate_of:
        return []

    def differs(start):
        return any((start + offset) % count in route.alternate_of
                   for offset in (-1, 0, 1, 2))

    runs: List[List[int]] = []
    for start in range(count):
        if not differs(start):
            continue
        if runs and runs[-1][-1] == start - 1:
            runs[-1].append(start)
        else:
            runs.append([start])
    # A run that reaches the last stretch and one that starts at the first are
    # the same run, once the loop closes.
    if len(runs) > 1 and runs[0][0] == 0 and runs[-1][-1] == count - 1:
        runs[0] = runs.pop() + runs[0]

    lines = []
    for run in runs:
        points: List[Point] = []
        for position, start in enumerate(run):
            points += _segment(route, start, lane, True, samples,
                               closing=position == len(run) - 1)
        lines.append(points)
    return lines


# ---------------------------------------------------------------------------
# Difficulty: the level header's AI bytes
# ---------------------------------------------------------------------------

#: The two ``ai-levels`` blocks; Adventure 2 reads the second
#: (``aiLevel += 5``).
ADVENTURES = (("adv1", "Adventure"), ("adv2", "Adventure 2"))

#: The five slots of an ``ai-levels`` block, **in byte order**, with what
#: ``aitable_init`` picks each one for. The JSON names are the asset tool's and
#: two of them are the wrong way round: byte 1 is chosen when the course flags
#: carry ``RACE_CLEARED`` (``1 << 1``) and byte 2 when they carry
#: ``RACE_CLEARED_SILVER_COINS`` (``1 << 2``), but the tool calls byte 1
#: ``silver-coins`` and byte 2 ``completed``. The names stay - they are the keys
#: of every extracted header - and the labels say what the game does.
AI_LEVEL_SLOTS = (
    ("base", "Not won yet",
     "Adventure, before this race has been won on the save"),
    ("silver-coins", "Race won",
     "Adventure, once the race has been won. The byte the asset tool calls "
     "silver-coins"),
    ("completed", "Silver coins",
     "Adventure, once the silver coin challenge is done; it wins over Race "
     "won. The byte the asset tool calls completed"),
    ("tracks-mode", "Tracks mode",
     "Any race started from the Tracks menu"),
    ("trophy-race", "Trophy race",
     "The race as part of a trophy race; it wins over everything above"),
)

#: The ten ``ASSET_AI_BEHAVIOUR`` tables' first two floats: the target speed of
#: the bot at the back and of the bot leading the bots, before
#: ``sqrt((x * 0.025 + 0.561) / 0.004)``. Read out of the extracted
#: ``asset_ai_behaviour_*.bin``; the tables are the ROM's, not the track's.
BEHAVIOUR_SPEEDS = (
    (-6.0, -1.5), (-5.0, 0.0), (-4.0, 1.5), (-3.0, 2.5), (-1.5, 3.5),
    (-0.5, 4.0), (0.0, 6.0), (2.0, 8.0), (5.0, 10.0), (7.0, 10.0),
)

#: What ``aitable_init`` falls back to for a level past the tables.
BEHAVIOUR_LEVELS = len(BEHAVIOUR_SPEEDS)


def target_speed(raw: float) -> float:
    """The speed a bot aims for, as ``func_80042D20`` converts it."""
    return math.sqrt(max(0.0, raw * 0.025 + 0.561) / 0.004)


def describe_levels() -> str:
    """Every behaviour level's speed range, for a tooltip."""
    return "; ".join(
        "%d: %.1f-%.1f" % (level, target_speed(slow), target_speed(fast))
        for level, (slow, fast) in enumerate(BEHAVIOUR_SPEEDS)
    )


#: ``Character`` (``enums.h``), which is how ``unkC`` and ``unk16`` are indexed.
CHARACTERS = ("Krunch", "Bumper", "Tiptup", "Conker", "Timber", "Banjo",
              "Drumstick", "Pipsy", "T.T.", "Diddy")

#: ``AISkill`` (``racer.h``).
SKILLS = ("Master", "Expert", "Hard", "Medium", "Easy")


def start_delay(skill: int) -> int:
    """Frames after the start a bot of this skill waits before accelerating.

    ``func_80042D20`` holds A once ``(skill - 2) * 4 <= 300 - D_8011D544``, and
    that timer counts down from 300 as the race starts, so Hard and better go
    on the beep.
    """
    return max(0, (int(skill) - 2) * 4)


def describe_skill(skill: int) -> str:
    """A skill value in words, including the ones past ``AI_EASY`` retail uses."""
    skill = int(skill)
    name = SKILLS[skill] if 0 <= skill < len(SKILLS) else str(skill)
    if skill == 0:
        return "%s: always gets the start boost" % name
    if skill == 1:
        return "%s: gets the start boost when the player does" % name
    delay = start_delay(skill)
    return "%s: %s" % (name, "goes on the beep" if not delay
                       else "%d frames late off the line" % delay)


def ai_level_pointer(adventure: str, slot: str) -> str:
    return "/ai-levels/%s/%s" % (adventure, slot)


def skill_pointer(character: int, trophy: bool = False) -> str:
    return "/unknown/%s/%d" % ("unk16" if trophy else "unkC", character)


def vehicle_set_pointer(vehicle: int) -> str:
    return "/unknown/unk4F/%d" % vehicle


AI_LEVEL_POINTERS = tuple(ai_level_pointer(adventure, slot)
                          for adventure, _label in ADVENTURES
                          for slot, _name, _help in AI_LEVEL_SLOTS)
SKILL_POINTERS = tuple(skill_pointer(i) for i in range(len(CHARACTERS)))
TROPHY_SKILL_POINTERS = tuple(skill_pointer(i, True) for i in range(len(CHARACTERS)))
VEHICLE_SET_POINTERS = tuple(vehicle_set_pointer(i) for i in range(3))

#: The difficulty an author copies from one track to another. The vehicle sets
#: are left out: they name checkpoint sets, which belong to the track's own map.
DIFFICULTY_POINTERS = AI_LEVEL_POINTERS + SKILL_POINTERS + TROPHY_SKILL_POINTERS

#: Every header byte this module describes.
HEADER_POINTERS = DIFFICULTY_POINTERS + VEHICLE_SET_POINTERS


def _choices() -> Tuple[template.Choice, ...]:
    """The header bytes above, described the way the header form describes its
    own - so the scene answers, the inherited-header overlay and the encoder
    treat them like any other field."""
    found = []
    for adventure, adventure_label in ADVENTURES:
        for slot, name, help_text in AI_LEVEL_SLOTS:
            found.append(template.Choice(
                ai_level_pointer(adventure, slot),
                "%s, %s" % (adventure_label, name),
                "Behaviour level 0 to 9. " + help_text,
                minimum=0, maximum=BEHAVIOUR_LEVELS - 1,
            ))
    for index, character in enumerate(CHARACTERS):
        found.append(template.Choice(
            skill_pointer(index), "%s start skill" % character,
            "0 Master to 4 Easy, in single-player races, boss races and Taj's "
            "challenges", minimum=0, maximum=255,
        ))
        found.append(template.Choice(
            skill_pointer(index, True), "%s start skill, trophy" % character,
            "0 Master to 4 Easy, when the race is part of a trophy race",
            minimum=0, maximum=255,
        ))
    for index, vehicle in enumerate(("Car", "Hovercraft", "Plane")):
        found.append(template.Choice(
            vehicle_set_pointer(index), "%s checkpoint set" % vehicle,
            "Which checkpoints a %s's racers load: the ones whose vehicleType "
            "equals this" % vehicle.lower(), minimum=0, maximum=255,
        ))
    return tuple(found)


HEADER_CHOICES: Tuple[template.Choice, ...] = _choices()


def header_default(pointer: str):
    """What a from-scratch header carries for ``pointer`` if nobody says."""
    data = template.load()
    for section in ("defaulted", "fixed"):
        if pointer in data.get(section, {}):
            return data[section][pointer]
    return None


def difficulty_from(header: Dict) -> Dict[str, int]:
    """The difficulty bytes of a header document, keyed by pointer."""
    found = {}
    for pointer in DIFFICULTY_POINTERS:
        value = template.lookup(header, pointer)
        if isinstance(value, int):
            found[pointer] = value
    return found
