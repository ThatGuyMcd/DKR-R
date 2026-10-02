"""Everything that depends on what kind of level a track is.

The *Level Type* is the first thing an author answers and the switch the rest of
the addon obeys: what can be placed, how many start positions a grid gets,
which checks validation runs and what the header's race type says. Keeping all
of it here, rather than scattered across the panels and operators that use it,
means one table decides it and the tests can reach it without Blender. See
LEVEL_TYPE_PLAN.md for where each number comes from.

Two words are used carefully. A **family** is what the first dropdown offers:
Race, Boss Race, Challenge, Hub, Special. A **key** is what the game can tell
apart - the family, except that Challenge and Special each split into three,
because race types 64, 65 and 66 behave differently and the Egg Creator only
works in one of them. Every retail ``race_type`` maps to exactly one key, so an
import round-trips the value it came with.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Sequence, Tuple

# ---------------------------------------------------------------------------
# Families and keys
# ---------------------------------------------------------------------------

NONE = "NONE"
RACE = "RACE"
BOSS = "BOSS"
CHALLENGE = "CHALLENGE"
HUB = "HUB"
SPECIAL = "SPECIAL"

BATTLE = "BATTLE"
BANANAS = "BANANAS"
EGGS = "EGGS"
CUTSCENE = "CUTSCENE"
BACKDROP = "BACKDROP"
TEST_RACE = "TEST_RACE"

CHALLENGES = (BATTLE, BANANAS, EGGS)
SPECIALS = (CUTSCENE, BACKDROP, TEST_RACE)
KEYS = (RACE, BOSS, BATTLE, BANANAS, EGGS, HUB, CUTSCENE, BACKDROP, TEST_RACE)


class Family:
    __slots__ = ("key", "label", "blurb", "description", "more")

    def __init__(self, key, label, blurb, description, more=""):
        self.key = key
        self.label = label
        #: The second line of the big first-contact button.
        self.blurb = blurb
        self.description = description
        #: What changes in the addon, for the tooltip.
        self.more = more

    @property
    def tooltip(self) -> str:
        return (self.description + " " + self.more).strip()


FAMILIES: Dict[str, Family] = {
    RACE: Family(RACE, "Race", "Lap race, 8 racers",
                 "A lap race for up to eight racers.",
                 "8 start positions in two staggered rows · checkpoints "
                 "required · weapons, zippers and coins available."),
    BOSS: Family(BOSS, "Boss Race", "One-on-one vs a boss",
                 "A one-on-one race against one of the bosses.",
                 "2 start positions side by side · checkpoints required · "
                 "pick the boss below."),
    CHALLENGE: Family(CHALLENGE, "Challenge", "Battle · bananas · eggs",
                      "A four-racer arena game: battle, banana hunt or egg "
                      "collecting.",
                      "4 start positions around the arena, facing the centre "
                      "· no checkpoints."),
    HUB: Family(HUB, "Hub", "Overworld with doors",
                "An overworld that leads to other levels.",
                "One start position per entrance · doors, gates and signs "
                "available."),
    SPECIAL: Family(SPECIAL, "Special", "Advanced",
                    "Advanced: the level types the game uses for scenes, menus "
                    "and testing."),
}

#: The second selector, for the two families that split: (key, label, help).
SUBTYPES: Dict[str, Tuple[Tuple[str, str, str], ...]] = {
    CHALLENGE: (
        (BATTLE, "Battle",
         "Knock the others out. A racer left with no bananas is eliminated "
         "(Darkwater Beach, Icicle Pyramid)."),
        (BANANAS, "Bananas", "Collect bananas and bank them (Smokey Castle)."),
        (EGGS, "Eggs",
         "Take eggs from the Egg Creator back to your nest (Fire Mountain). "
         "The only mode where the Egg Creator works."),
    ),
    SPECIAL: (
        (CUTSCENE, "Cutscene",
         "No racers are spawned. The game uses it for character select, the "
         "title screen and the trophy scenes."),
        (BACKDROP, "Menu Backdrop",
         "No racers are spawned and the track geometry is not drawn - only "
         "objects. Used behind the main menus."),
        (TEST_RACE, "Test Race",
         "Horseshoe Gulch's type: a test race left in the game."),
    ),
}

TEST_RACE_WARNING = (
    "Not recommended. It runs like a race but has no time trial, does not "
    "count as a race and saves no records or progress."
)

#: The header byte each key writes (``level_header.py``, 0x4C).
RACE_TYPES: Dict[str, str] = {
    RACE: "RACETYPE_DEFAULT",
    BOSS: "RACETYPE_BOSS",
    BATTLE: "RACETYPE_CHALLENGE_BATTLE",
    BANANAS: "RACETYPE_CHALLENGE_BANANAS",
    EGGS: "RACETYPE_CHALLENGE_EGGS",
    HUB: "RACETYPE_HUBWORLD",
    CUTSCENE: "RACETYPE_CUTSCENE_1",
    BACKDROP: "RACETYPE_CUTSCENE_2",
    TEST_RACE: "RACETYPE_HORSESHOE_GULCH",
}

KEY_LABELS: Dict[str, str] = {
    RACE: "Race", BOSS: "Boss Race",
    BATTLE: "Challenge › Battle", BANANAS: "Challenge › Bananas",
    EGGS: "Challenge › Eggs", HUB: "Hub",
    CUTSCENE: "Special › Cutscene", BACKDROP: "Special › Menu Backdrop",
    TEST_RACE: "Special › Test Race",
}

KEY_PLURALS: Dict[str, str] = {
    RACE: "races", BOSS: "boss races", BATTLE: "battles",
    BANANAS: "banana challenges", EGGS: "egg challenges", HUB: "hubs",
    CUTSCENE: "cutscenes", BACKDROP: "menu backdrops",
    TEST_RACE: "the test race",
}


def key_of(family: str, challenge: str = BATTLE, special: str = CUTSCENE) -> Optional[str]:
    """The key a family and its sub-selectors add up to; ``None`` for no mode."""
    if family == CHALLENGE:
        return challenge if challenge in CHALLENGES else BATTLE
    if family == SPECIAL:
        return special if special in SPECIALS else CUTSCENE
    if family in (RACE, BOSS, HUB):
        return family
    return None


def family_of(key: str) -> Tuple[str, Optional[str]]:
    """``"EGGS"`` -> ``("CHALLENGE", "EGGS")``; ``"RACE"`` -> ``("RACE", None)``."""
    if key in CHALLENGES:
        return CHALLENGE, key
    if key in SPECIALS:
        return SPECIAL, key
    if key in (RACE, BOSS, HUB):
        return key, None
    return NONE, None


def label(key: Optional[str]) -> str:
    return KEY_LABELS.get(key or "", "not chosen")


def current_key(settings) -> Optional[str]:
    """The key a scene's settings add up to, read by attribute so a test can
    hand in any object."""
    return key_of(getattr(settings, "level_type", NONE),
                  getattr(settings, "challenge_type", BATTLE),
                  getattr(settings, "special_type", CUTSCENE))


def race_type(key: str) -> str:
    return RACE_TYPES[key]


def key_for_race_type(value: str) -> Optional[str]:
    """The key a header's ``race-type`` belongs to, or ``None`` for one no menu
    offers - ``RACETYPE_UNK1``, which no level uses, and the ``RACETYPE_CHALLENGE``
    mask, which is a question the code asks rather than a type."""
    for key, name in RACE_TYPES.items():
        if name == value:
            return key
    return None


# ---------------------------------------------------------------------------
# What each key asks of a track
# ---------------------------------------------------------------------------

#: Racers the game spawns (``objects.c``: 8 by default, 4 in a challenge, 2 in a
#: boss race, one per player in a hub; a cutscene returns before spawning).
SPAWN_COUNTS: Dict[str, int] = {
    RACE: 8, TEST_RACE: 8, BOSS: 2, BATTLE: 4, BANANAS: 4, EGGS: 4, HUB: 1,
    CUTSCENE: 0, BACKDROP: 0,
}

#: Where ``racerIndex`` stops meaning anything (``objects.c:1131``).
MAX_RACER_INDEX = 8


def spawn_count(key: Optional[str]) -> int:
    return SPAWN_COUNTS.get(key or "", 0)


def needs_checkpoints(key: Optional[str]) -> bool:
    """Retail has checkpoints in every race and boss race and in no challenge."""
    return key in (RACE, BOSS, TEST_RACE)


def is_challenge(key: Optional[str]) -> bool:
    return key in CHALLENGES


def has_vehicles(key: Optional[str]) -> bool:
    """Whether the vehicles a track allows mean anything: not where nobody races."""
    return bool(key) and key not in (CUTSCENE, BACKDROP)


# ---------------------------------------------------------------------------
# Bosses and vehicles
# ---------------------------------------------------------------------------

#: The ten boss races, with the vehicle every retail header for it names.
BOSSES: Tuple[Tuple[str, str, str], ...] = (
    ("BOSS_RACE_TRICKY1", "Tricky (first race)", "VEHICLE_CAR"),
    ("BOSS_RACE_TRICKY2", "Tricky (rematch)", "VEHICLE_CAR"),
    ("BOSS_RACE_BLUEY1", "Bluey (first race)", "VEHICLE_HOVERCRAFT"),
    ("BOSS_RACE_BLUEY2", "Bluey (rematch)", "VEHICLE_HOVERCRAFT"),
    ("BOSS_RACE_BUBBLER1", "Bubbler (first race)", "VEHICLE_HOVERCRAFT"),
    ("BOSS_RACE_BUBBLER2", "Bubbler (rematch)", "VEHICLE_HOVERCRAFT"),
    ("BOSS_RACE_SMOKEY1", "Smokey (first race)", "VEHICLE_PLANE"),
    ("BOSS_RACE_SMOKEY2", "Smokey (rematch)", "VEHICLE_PLANE"),
    ("BOSS_RACE_WIZPIG1", "Wizpig (first race)", "VEHICLE_CAR"),
    ("BOSS_RACE_WIZPIG2", "Wizpig (final race)", "VEHICLE_PLANE"),
)

#: The vehicles a player can drive, and so the only ones a track should allow.
PLAYER_VEHICLES: Tuple[Tuple[str, str], ...] = (
    ("VEHICLE_CAR", "Car"),
    ("VEHICLE_HOVERCRAFT", "Hover"),
    ("VEHICLE_PLANE", "Plane"),
)

#: Vehicles the enum holds that no racer picks. For debugging only.
DEBUG_VEHICLES: Tuple[str, ...] = (
    "VEHICLE_FLYING_CAR", "VEHICLE_LOOPDELOOP", "VEHICLE_TRICKY",
    "VEHICLE_BLUEY", "VEHICLE_SMOKEY", "VEHICLE_PTERODACTYL",
    "VEHICLE_SNOWBALL", "VEHICLE_CARPET", "VEHICLE_BUBBLER", "VEHICLE_WIZPIG",
    "VEHICLE_ROCKET",
)


def boss_label(boss: str) -> str:
    for boss_id, text, _vehicle in BOSSES:
        if boss_id == boss:
            return text
    return boss


def boss_vehicle(boss: str) -> str:
    for boss_id, _text, vehicle in BOSSES:
        if boss_id == boss:
            return vehicle
    return "VEHICLE_CAR"


def vehicle_name(vehicle: str) -> str:
    """``VEHICLE_HOVERCRAFT`` -> ``Hovercraft``."""
    return (vehicle or "").replace("VEHICLE_", "").replace("_", " ").title()


def allowed_vehicles(key: Optional[str], chosen: Sequence[str], default: str,
                     boss: str = "") -> List[str]:
    """What a track actually allows, which the Level Type narrows.

    A boss race is raced in the boss's vehicle, and a challenge allows exactly
    one so no racer has an advantage - in both cases the header must not
    offer a second.
    """
    order = [vehicle for vehicle, _name in PLAYER_VEHICLES]
    picked = [vehicle for vehicle in order if vehicle in set(chosen)] or [order[0]]
    if key == BOSS:
        return [boss_vehicle(boss)]
    if is_challenge(key):
        return [default if default in picked else picked[0]]
    return picked


def default_vehicle(allowed: Sequence[str], default: str) -> str:
    return default if default in allowed else (allowed[0] if allowed else "VEHICLE_CAR")


def header_overrides(key: Optional[str], boss: str = "", vehicles=(),
                     default: str = "VEHICLE_CAR", laps: int = 3,
                     vehicle_override: str = "") -> Dict[str, object]:
    """The header fields the Level Type owns, as template pointers.

    The header never has a second answer for these: the panel shows them as
    set by Level Type, and both an authored header and an inherited one take
    them from here. For a track imported and left alone every value equals the
    base header's, because the import filled the Level Type from that header.
    """
    if not key:
        return {}
    found: Dict[str, object] = {"/race-type": race_type(key)}
    if key == BOSS and boss:
        found["/boss-race-id"] = boss
    if has_vehicles(key):
        allowed = allowed_vehicles(key, vehicles, default, boss)
        found["/avaliable-vehicles"] = list(allowed)
        found["/default-vehicle"] = default_vehicle(allowed, default)
    if needs_checkpoints(key):
        found["/lap-count"] = int(laps)
    if vehicle_override:
        found["/default-vehicle"] = vehicle_override
    return found


def from_header(header) -> Optional[Dict[str, object]]:
    """The Level Type settings a level header implies, or ``None`` for a race
    type no menu offers.

    Everything the Level Type owns is read back - race type, boss, vehicles,
    laps - which is what makes :func:`header_overrides` of the result give that
    same header back: a track imported and exported untouched writes the bytes
    it came with.
    """
    key = key_for_race_type(str(header.get("race-type", "")))
    if key is None:
        return None
    family, sub = family_of(key)
    found: Dict[str, object] = {"level_type": family}
    if family == CHALLENGE:
        found["challenge_type"] = sub
    elif family == SPECIAL:
        found["special_type"] = sub
    boss = header.get("boss-race-id")
    if key == BOSS and boss in {b[0] for b in BOSSES}:
        found["boss"] = boss
    players = [vehicle for vehicle, _name in PLAYER_VEHICLES]
    listed = header.get("avaliable-vehicles")
    listed = [v for v in listed if v in players] if isinstance(listed, list) else []
    if listed:
        found["vehicles"] = set(listed)
    if header.get("default-vehicle") in players:
        found["default_vehicle"] = header["default-vehicle"]
    laps = header.get("lap-count")
    found["laps"] = laps if isinstance(laps, int) and 1 <= laps <= 9 else 3
    return found


def settings_overrides(settings) -> Dict[str, object]:
    """:func:`header_overrides` for a scene's settings."""
    override = getattr(settings, "vehicle_override", "NONE")
    return header_overrides(
        current_key(settings),
        boss=getattr(settings, "boss", ""),
        vehicles=set(getattr(settings, "vehicles", ()) or ()),
        default=getattr(settings, "default_vehicle", "VEHICLE_CAR"),
        laps=getattr(settings, "laps", 3),
        vehicle_override="" if override in ("", "NONE") else override,
    )


def scene_allowed_vehicles(settings) -> List[str]:
    return allowed_vehicles(current_key(settings),
                            set(getattr(settings, "vehicles", ()) or ()),
                            getattr(settings, "default_vehicle", "VEHICLE_CAR"),
                            getattr(settings, "boss", ""))


# ---------------------------------------------------------------------------
# What is visible where
# ---------------------------------------------------------------------------

GAMEPLAY_CATEGORIES = ("racing", "pickups", "hub")

#: Actors and effects that only make sense in one kind of level, though their
#: category says otherwise.
MODE_BOUND = frozenset({
    "ASSET_OBJECT_TREASURESUCKER", "ASSET_OBJECT_CHARACTERFLAG",
    "ASSET_OBJECT_FIREBALLATTRACT", "ASSET_OBJECT_PARKWARDEN",
    "ASSET_OBJECT_PIGHEADCOLOURS", "ASSET_OBJECT_RANGETRIGGER",
})

PLAYABLE = (RACE, BOSS, BATTLE, BANANAS, EGGS, HUB)

#: Hand-written exceptions for what the survey cannot prove: object id to the
#: keys it is visible in. Empty until a case needs it.
OVERRIDES: Dict[str, Tuple[str, ...]] = {}


def mode_bound(object_type) -> bool:
    return (object_type.category in GAMEPLAY_CATEGORIES
            or object_type.object_id in MODE_BOUND)


def visible(object_type, key: Optional[str]) -> bool:
    """Whether a type belongs in the Place list for this kind of level.

    Visible where retail uses it, for the types a kind of level decides; always
    for scenery, audio, effects and camera; everywhere for a gameplay type
    retail never uses, since nothing proves it wrong; and with no filter at
    all in Special, which is the advanced escape and whose scenes use anything.
    """
    if not key or key in SPECIALS:
        return True
    if object_type.object_id in OVERRIDES:
        return key in OVERRIDES[object_type.object_id]
    if not mode_bound(object_type):
        return True
    modes = getattr(object_type, "modes", {}) or {}
    if not any(modes.get(k) for k in PLAYABLE):
        # Used nowhere a racer plays: a cutscene-only type stays hidden, and
        # one retail never used at all is left visible.
        return not modes
    return bool(modes.get(key))


def where_used(object_type) -> str:
    """``"340 in races, 87 in boss races"``, most first."""
    modes = getattr(object_type, "modes", {}) or {}
    ranked = sorted(modes.items(), key=lambda item: -item[1])
    return ", ".join("%d in %s" % (count, KEY_PLURALS.get(key, key))
                     for key, count in ranked)


def tab_of(object_type) -> str:
    """Pickups go to the collectables map; everything else is the structure."""
    return "collectables" if object_type.category == "pickups" else "structure"


# ---------------------------------------------------------------------------
# Presets and help
# ---------------------------------------------------------------------------

WEAPON_BALLOON = "ASSET_OBJECT_WEAPONBALLOON"
SETUPPOINT = "ASSET_OBJECT_SETUPPOINT"
CHECKPOINT = "ASSET_OBJECT_CHECKPOINT"
MODECHANGE = "ASSET_OBJECT_MODECHANGE"


class Preset:
    """A type placed with some fields already chosen - a balloon's colour, or
    which way a loop's mode changer switches the racer."""

    __slots__ = ("pid", "label", "sub", "object_id", "fields", "icon", "help")

    def __init__(self, pid, label, sub, object_id, fields, icon, help_text):
        self.pid = pid
        self.label = label
        self.sub = sub
        self.object_id = object_id
        self.fields = dict(fields)
        #: A Blender icon name, so a list of balloons reads by colour.
        self.icon = icon
        self.help = help_text


#: The cheats name the colours (``object_functions.c:4658-4679``). The icons
#: are the collection colour tags, which Blender has had since 2.91 under the
#: same names - the sequencer's strip colours were renamed and are not.
PRESETS: Tuple[Preset, ...] = (
    Preset("RED", "Red Balloon", "Missile", WEAPON_BALLOON,
           {"balloonType": "BALLOON_TYPE_MISSILE"}, "COLLECTION_COLOR_01",
           "Weapon balloon, red: missiles."),
    Preset("BLUE", "Blue Balloon", "Boost", WEAPON_BALLOON,
           {"balloonType": "BALLOON_TYPE_BOOST"}, "COLLECTION_COLOR_05",
           "Weapon balloon, blue: a speed boost."),
    Preset("GREEN", "Green Balloon", "Trap", WEAPON_BALLOON,
           {"balloonType": "BALLOON_TYPE_TRAP"}, "COLLECTION_COLOR_04",
           "Weapon balloon, green: traps dropped behind the racer."),
    Preset("YELLOW", "Yellow Balloon", "Shield", WEAPON_BALLOON,
           {"balloonType": "BALLOON_TYPE_SHIELD"}, "COLLECTION_COLOR_03",
           "Weapon balloon, yellow: a shield."),
    Preset("RAINBOW", "Colored Balloon", "Magnet", WEAPON_BALLOON,
           {"balloonType": "BALLOON_TYPE_MAGNET"}, "COLOR",
           "Weapon balloon, rainbow: a magnet that pulls the racer to the one "
           "ahead."),
    # A racer crossing a mode changer takes its vehicleID (obj_loop_modechange,
    # object_functions.c). VEHICLE_LOOPDELOOP runs the loop physics in
    # racer.c func_8004CC20: gravity along the kart's own floor, unclamped
    # pitch, throttle held open. The plain car's physics cannot climb past
    # ~73 degrees whatever its speed, so a loop needs both of these.
    Preset("LOOP_ENTRY", "Loop Entry", "Loop physics on", MODECHANGE,
           {"vehicleID": "VEHICLE_LOOPDELOOP"}, "LOOP_FORWARDS",
           "Stand it across the road at the foot of the loop, like a gate: "
           "racers crossing it ride the loop glued to its floor. Its local Y "
           "must point the way they drive (rotation Z 0 drives toward +Y, as "
           "the start grid does), within 22.5 degrees, and the loop must run "
           "straight that way - turned further off, the push forward stops "
           "and the kart falls."),
    Preset("LOOP_EXIT", "Loop Exit", "Back to car", MODECHANGE,
           {"vehicleID": "VEHICLE_CAR"}, "LOOP_BACK",
           "Stand it across the road where the loop lands: racers crossing it "
           "drive as a car again. Its local Y points the way they drive. "
           "Without it the loop physics only end after about 10 seconds, or "
           "1 second airborne."),
)


def preset(pid: str) -> Optional[Preset]:
    for entry in PRESETS:
        if entry.pid == pid:
            return entry
    return None


def preset_for(object_id: str, fields) -> Optional[Preset]:
    """The preset an object's fields match, so a balloon is named by colour."""
    for entry in PRESETS:
        if entry.object_id == object_id and all(
                fields.get(k) == v for k, v in entry.fields.items()):
            return entry
    return None


HELP: Dict[str, str] = {
    SETUPPOINT: "Where a racer starts. racerIndex picks the racer, entranceID "
                "the door or warp the level was entered through. Start Grid "
                "places a whole set at once.",
    CHECKPOINT: "Counts laps and places respawns. Racers pass them in index "
                "order.",
    "ASSET_OBJECT_AINODE": "A point on the AI racing line, linked to up to four "
                           "neighbours.",
    "ASSET_OBJECT_GROUNDZIPPER": "A speed pad on the ground.",
    "ASSET_OBJECT_AIRZIPPERS": "Boost rings in the air, for planes.",
    "ASSET_OBJECT_WATERZIPPERS": "Boost pads on water, for hovercraft.",
    MODECHANGE: "Switches the vehicle of each racer crossing it, inside radius. "
                "Retail uses it for the loop-de-loops; place one as Loop Entry "
                "and one as Loop Exit.",
    "ASSET_OBJECT_COIN": "A banana: each one collected makes a racer faster. In "
                         "a banana challenge it is what the racers collect.",
    "ASSET_OBJECT_COINCREATOR": "Spawns bananas during a banana challenge.",
    "ASSET_OBJECT_EGGCREATOR": "Spawns the eggs of an egg challenge. Only works "
                               "in Challenge › Eggs.",
    "ASSET_OBJECT_HEADFORPOINT": "A point the AI heads for in the egg "
                                 "challenge.",
    "ASSET_OBJECT_TREASURESUCKER": "The chest a racer banks bananas in, in the "
                                   "banana challenge.",
    "ASSET_OBJECT_GOLDENBALLOON": "A golden balloon, the reward a hub or a boss "
                                  "race hands out.",
    "ASSET_OBJECT_EXIT": "Warps the racer to another level.",
    "ASSET_OBJECT_LEVELDOOR": "The door from a hub into a race.",
    "ASSET_OBJECT_WORLDGATE": "A gate between worlds in the hub.",
    "ASSET_OBJECT_BOSSDOOR": "The door to a world's boss race.",
    "ASSET_OBJECT_TROPHYCAB": "The trophy cabinet in a hub.",
    "ASSET_OBJECT_WORLDKEY": "A world key, hidden in a race.",
}


def auto_help(object_type) -> str:
    """A description built from the survey, for types with no written help."""
    slots = getattr(object_type, "slots", {}) or {}
    structure = slots.get("structure", 0)
    collectables = slots.get("collectables", 0)
    total = structure + collectables
    where = where_used(object_type)
    text = "%s, %s. " % (object_type.label, object_type.category)
    text += ("Retail uses it %s." % where) if where else "No retail level uses it."
    if total:
        text += " Usually in the %s map (%d%%)." % (
            "collectables" if collectables > structure else "structure",
            round(100.0 * max(structure, collectables) / total),
        )
    return text


def place_description(object_type, key: Optional[str],
                      chosen: Optional[Preset] = None) -> str:
    """The tooltip of one Place button: what it is, and whether it fits here."""
    parts = []
    if chosen is not None:
        parts.append("%s (%s). %s %s."
                     % (chosen.label, chosen.sub, chosen.help,
                        ", ".join("%s = %s" % item for item in chosen.fields.items())))
    written = HELP.get(object_type.object_id)
    if written:
        parts.append(written)
    if chosen is None:
        parts.append(auto_help(object_type))
    if not visible(object_type, key):
        parts.append("Not used in a %s level. It can still be placed; "
                     "validation will warn." % label(key))
    return " ".join(parts)


# ---------------------------------------------------------------------------
# The Place list
# ---------------------------------------------------------------------------

class Entry:
    """One row of the Place list: a type, perhaps as a preset."""

    __slots__ = ("object_type", "preset", "ok")

    def __init__(self, object_type, chosen=None, ok=True):
        self.object_type = object_type
        self.preset = chosen
        self.ok = ok

    @property
    def label(self) -> str:
        return self.preset.label if self.preset else self.object_type.label


def visible_types(catalog, key: Optional[str], tab: str, category: str = "ALL",
                  show_all: bool = False) -> List[Entry]:
    """The Place list for one tab: usable types first, featured, then common.

    A type with presets becomes them - a weapon balloon its five colours, a
    mode changer the loop's entry and exit - since which one is placed is the
    whole point of placing it.
    """
    rows = []
    for object_type in catalog.types.values():
        if tab_of(object_type) != tab:
            continue
        if category not in ("ALL", "") and object_type.category != category:
            continue
        ok = visible(object_type, key)
        if not ok and not show_all:
            continue
        rows.append((object_type, ok))
    rows.sort(key=lambda row: (not row[1], not row[0].featured,
                               -row[0].retail_count, row[0].object_id))
    out = []
    for object_type, ok in rows:
        presets = [p for p in PRESETS if p.object_id == object_type.object_id]
        if presets:
            out += [Entry(object_type, p, ok) for p in presets]
        else:
            out.append(Entry(object_type, None, ok))
    return out


def category_counts(catalog, key: Optional[str], tab: str,
                    show_all: bool = False) -> Dict[str, int]:
    """Types per category in a tab, counting only what the list would show."""
    counts: Dict[str, int] = {}
    for object_type in catalog.types.values():
        if tab_of(object_type) != tab:
            continue
        if not show_all and not visible(object_type, key):
            continue
        counts[object_type.category] = counts.get(object_type.category, 0) + 1
    return counts


def hidden_count(catalog, key: Optional[str], tab: str) -> int:
    return sum(1 for t in catalog.types.values()
               if tab_of(t) == tab and not visible(t, key))


# ---------------------------------------------------------------------------
# The start grid
# ---------------------------------------------------------------------------

#: The step a ``u8`` angleY can hold.
ANGLE_STEP = 5.625

#: Median of the 20 retail race grids, centred; local +Y is forward, +X right.
RACE_TEMPLATE: Tuple[Tuple[float, float], ...] = (
    (-199, 39), (-78, 36), (30, 34), (137, 34),
    (195, -38), (81, -36), (-29, -35), (-139, -32),
)

#: Retail radii for a challenge ring: Icicle Pyramid, Fire Mountain, Darkwater.
CHALLENGE_RADIUS = 1200.0
BOSS_HALF_GAP = 70.0


def snap_angle(degrees: float) -> float:
    return round(degrees / ANGLE_STEP) * ANGLE_STEP


def grid_template(key: Optional[str], spacing: float = 1.0,
                  radius: float = CHALLENGE_RADIUS,
                  facing: float = 0.0) -> List[Tuple[float, float, float]]:
    """``(x, y, yaw)`` per racerIndex, in the grid root's local frame.

    A challenge ring faces the centre - 0 at -Y, 1 at +X, 2 at +Y, 3 at -X, as
    retail has them - and ``facing`` turns every racer further, which is how
    Smokey Castle's pinwheel is made.
    """
    if key in (RACE, TEST_RACE):
        return [(x * spacing, y * spacing, 0.0) for x, y in RACE_TEMPLATE]
    if key == BOSS:
        return [(-BOSS_HALF_GAP * spacing, 0.0, 0.0),
                (BOSS_HALF_GAP * spacing, 0.0, 0.0)]
    if is_challenge(key):
        ring = ((0.0, -radius, 0.0), (radius, 0.0, 90.0),
                (0.0, radius, 180.0), (-radius, 0.0, -90.0))
        return [(x, y, yaw + facing) for x, y, yaw in ring]
    if key == HUB:
        return [(0.0, 0.0, 0.0)]
    return []


def grid_blurb(key: Optional[str]) -> str:
    if key in (RACE, TEST_RACE):
        return "8 start positions in two staggered rows of four"
    if key == BOSS:
        return "2 start positions side by side"
    if is_challenge(key):
        return "4 start positions around the arena, facing the centre"
    if key == HUB:
        return "One start position per entrance"
    count = spawn_count(key)
    return ("%d start positions" % count) if count else \
        "No racers are spawned in this level type"


# ---------------------------------------------------------------------------
# Music
# ---------------------------------------------------------------------------

def music_tracks(catalog) -> List[str]:
    """The game's music list, in the order the header's ``/music`` indexes it."""
    members = list(catalog.enums.get("SequenceID", [])) if catalog else []
    return [m for m in members if not m.startswith("NUM_")]


def music_label(name: str) -> str:
    """``SEQUENCE_ANCIENT_LAKE`` -> ``Ancient Lake``."""
    return name.replace("SEQUENCE_", "").replace("_", " ").title()
