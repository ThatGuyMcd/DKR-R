"""Generate ``track_lab/data/catalog.json`` from the DKR decomp.

The addon needs to know, for all 85 object types that appear in retail tracks,
which fields each one carries, what type each field is and which enum members
are legal. docs/BLENDER_ADDON_PLAN.md is explicit that this must not be
transcribed by hand, so it is derived from two sources that already agree:

* ``include/level_object_entries.h`` - the ``LevelObjectEntry_*`` structs give
  field order, C type and the ``Hint(...)`` annotations that tell the asset tool
  how to decode a field (angles in degrees, enum members by name, asset ids).
* the extracted object-map glTFs - the same fields after decoding, which give
  the concrete JSON type and the value ranges retail actually uses.

The header gives the schema; the glTFs prove which parts of it are real. A field
the header declares but no retail map ever writes is marked unused, and a
field's editing widget comes from the observed range rather than from the C type
alone.

Usage::

    python tools/blender/generate_catalog.py
    python tools/blender/generate_catalog.py --decomp path/to/dkr-decomp --check
"""

from __future__ import annotations

import argparse
import collections
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_DECOMP = os.path.join(REPO_ROOT, "extern", "dkr-decomp")
DEFAULT_OUTPUT = os.path.join(HERE, "track_lab", "data", "catalog.json")

SCHEMA_VERSION = 1

# ---------------------------------------------------------------------------
# C header parsing
# ---------------------------------------------------------------------------

_STRUCT_RE = re.compile(
    r"typedef\s+struct\s+(?P<tag>\w+)\s*\{(?P<body>.*?)\}\s*(?P<name>\w+)\s*;",
    re.DOTALL,
)
_FIELD_RE = re.compile(
    r"^\s*(?:/\*\s*(?P<offset>0x[0-9A-Fa-f]+)\s*\*/\s*)?"
    r"(?P<ctype>u8|s8|u16|s16|u32|s32|f32|LevelObjectEntryCommon)\s+"
    r"(?P<decls>[^;]+);"
    r"(?P<rest>.*)$"
)
_DECL_RE = re.compile(r"^(?P<name>\w+)\s*(?:\[\s*(?P<count>[0-9A-Fa-fx]+)\s*\])?$")
_HINT_RE = re.compile(r"Hint\(\((?P<args>[^)]*)\)\)")

#: C types to (JSON kind, signed, byte width).
CTYPES = {
    "u8": ("int", False, 1),
    "s8": ("int", True, 1),
    "u16": ("int", False, 2),
    "s16": ("int", True, 2),
    "u32": ("int", False, 4),
    "s32": ("int", True, 4),
    "f32": ("float", True, 4),
}


def _strip_comments(text):
    """Remove ``//`` comments but keep ``/* 0x00 */`` offset markers."""
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def _parse_hint(rest):
    """Decode a ``Hint((Angle, DivideBy:64))`` annotation into a dict."""
    match = _HINT_RE.search(rest)
    if not match:
        return None
    parts = [p.strip() for p in match.group("args").split(",") if p.strip()]
    if not parts:
        return None
    # The leading term is either a bare kind (``Angle``) or a kind with its
    # subject attached (``Enum:Vehicle``, ``AssetId:ASSET_LEVEL_HEADERS``).
    kind, _, subject = parts[0].partition(":")
    hint = {"kind": kind}
    if subject:
        hint["subject"] = subject
    for part in parts[1:]:
        if ":" in part:
            key, value = part.split(":", 1)
            key, value = key.strip(), value.strip()
            hint[key[0].lower() + key[1:]] = int(value) if value.isdigit() else value
    return hint


def parse_structs(header_path):
    """Return ``{struct_name: [field, ...]}`` for every ``LevelObjectEntry_*``."""
    with open(header_path, "r", encoding="utf-8") as handle:
        source = _strip_comments(handle.read())

    structs = {}
    for match in _STRUCT_RE.finditer(source):
        name = match.group("name")
        if not name.startswith("LevelObjectEntry") or name == "LevelObjectEntry":
            continue
        fields = []
        # Track the running offset so entry sizes stay right even for the few
        # structs that omit a /* 0x?? */ marker on some lines.
        cursor = COMMON_SIZE
        for line in match.group("body").splitlines():
            field_match = _FIELD_RE.match(line)
            if not field_match:
                continue
            ctype = field_match.group("ctype")
            if ctype == "LevelObjectEntryCommon":
                continue  # objectID/size/x/y/z, carried by name + translation
            marked = field_match.group("offset")
            if marked:
                marked_offset = int(marked, 16)
                # The header carries offset-marker typos - AudioReverb marks two
                # consecutive fields as 0x0A - and trusting one that would
                # overlap the previous field shortens the entry. The retail
                # bytes say AudioReverb is 12 long, not 11.
                if marked_offset >= cursor:
                    cursor = marked_offset
            hint = _parse_hint(field_match.group("rest"))
            width = CTYPES[ctype][2]
            for decl in field_match.group("decls").split(","):
                decl_match = _DECL_RE.match(decl.strip())
                if not decl_match:
                    continue
                count = decl_match.group("count")
                count = int(count, 0) if count else 1
                cursor = _align(cursor, width)
                fields.append(
                    {
                        "name": decl_match.group("name"),
                        "ctype": ctype,
                        "count": count,
                        "offset": cursor,
                        "hint": hint,
                    }
                )
                cursor += width * count
        structs[name] = fields
    return structs


#: ``LevelObjectEntryCommon`` is 8 bytes and every entry starts with it.
COMMON_SIZE = 8

#: An entry's ``size`` byte is 7 bits wide, the top bit belonging to the 9-bit
#: object id, so no entry can exceed this.
MAX_ENTRY_SIZE = 0x7F


def _align(offset, width):
    return offset + (-offset % width)


def struct_size(fields):
    """Encoded byte length of one entry of this type.

    MIPS pads a struct out to its widest member, which is what the asset tool
    writes and what the ``size`` byte has to agree with.
    """
    if not fields:
        return COMMON_SIZE
    end = max(f["offset"] + CTYPES[f["ctype"]][2] * f["count"] for f in fields)
    alignment = max(CTYPES[f["ctype"]][2] for f in fields)
    return _align(end, alignment)


_ENUM_RE = re.compile(
    r"typedef\s+enum\s+(?P<name>\w+)\s*\{(?P<body>.*?)\}\s*(?P=name)\s*;", re.DOTALL
)

#: Not every enum is a typedef - ``enum BossSetupTypes { ... };`` is plain, and
#: the level header needs it.
_PLAIN_ENUM_RE = re.compile(
    r"(?<!typedef )enum\s+(?P<name>\w+)\s*\{(?P<body>[^}]*)\}\s*;", re.DOTALL
)


def parse_enums(*header_paths):
    """Return ``{enum_name: [member, ...]}`` in declaration order."""
    enums = {}
    for path in header_paths:
        if not os.path.exists(path):
            continue
        with open(path, "r", encoding="utf-8") as handle:
            source = _strip_comments(handle.read())
        for pattern in (_ENUM_RE, _PLAIN_ENUM_RE):
            for match in pattern.finditer(source):
                members = []
                for chunk in match.group("body").split(","):
                    member = chunk.split("=", 1)[0].strip()
                    if re.fullmatch(r"[A-Za-z_]\w*", member or ""):
                        members.append(member)
                enums.setdefault(match.group("name"), members)
    return enums


def parse_enum_values(*header_paths):
    """Return ``{enum_name: {member: value}}`` with real C enum semantics.

    An encoder needs the declared value, not the position in the list, and the
    two differ where it matters most: ``VEHICLE_NO_OVERRIDE`` is -1 while sitting
    fifteenth, and ``WARP_FLAG_NORMAL`` is -1 while sitting first. Writing the
    index instead would put a plausible but wrong byte in the entry.

    Handles the three forms the headers use: implicit increment, an explicit
    integer, and an alias naming an earlier member (``VEHICLE_TRICKY =
    VEHICLE_BOSSES``).
    """
    values = {}
    for path in header_paths:
        if not os.path.exists(path):
            continue
        with open(path, "r", encoding="utf-8") as handle:
            source = _strip_comments(handle.read())
        for pattern in (_ENUM_RE, _PLAIN_ENUM_RE):
          for match in pattern.finditer(source):
            members = {}
            cursor = 0
            for chunk in match.group("body").split(","):
                name, _, literal = chunk.partition("=")
                name = name.strip()
                if not re.fullmatch(r"[A-Za-z_]\w*", name or ""):
                    continue
                literal = literal.strip()
                if literal:
                    try:
                        cursor = int(literal, 0)
                    except ValueError:
                        # An alias naming an earlier member of the same enum.
                        if literal in members:
                            cursor = members[literal]
                        else:
                            continue
                members[name] = cursor
                cursor += 1
            values.setdefault(match.group("name"), members)
    return values


# ---------------------------------------------------------------------------
# glTF survey
# ---------------------------------------------------------------------------

def survey_object_maps(decomp_root):
    """Observe every placed object in every extracted retail map.

    Returns ``{object_id: {...}}`` recording how often the type appears, the
    node name it is given, and per field the JSON types, value range and how
    many of the type's instances actually carry it.
    """
    pattern = os.path.join(
        decomp_root, "assets", ".vanilla", "*", "levels", "objectMaps", "*", "*.gltf"
    )
    paths = sorted(glob.glob(pattern))
    observed = {}
    for path in paths:
        with open(path, "r", encoding="utf-8") as handle:
            document = json.load(handle)
        for node in document.get("nodes", []):
            extras = node.get("extras")
            if not isinstance(extras, dict) or "id" not in extras:
                continue
            entry = observed.setdefault(
                extras["id"],
                {"count": 0, "names": collections.Counter(), "fields": {}},
            )
            entry["count"] += 1
            entry["names"][node.get("name", "")] += 1
            for key, value in extras.items():
                if key == "id":
                    continue
                info = entry["fields"].setdefault(
                    key,
                    {
                        "count": 0,
                        "kinds": set(),
                        "values": collections.Counter(),
                        "lengths": set(),
                        "min": None,
                        "max": None,
                    },
                )
                info["count"] += 1
                _record_value(info, value)
    return paths, observed


def survey_level_modes(decomp_root):
    """Which kinds of level place each type, and in which of the two maps.

    The survey above cannot say this, because an object map does not know what
    level it belongs to - its header does. So each retail header's race type
    decides the kind of level (a :data:`level_types.KEYS` member), and its
    ``map-2`` and ``map-collectables`` name the two maps. This is what lets the
    Place list hide the Egg Creator outside an egg challenge on evidence rather
    than taste. A level two extracted revisions both carry is counted once.

    Returns ``({object_id: Counter(key)}, {object_id: Counter(slot)})``.
    """
    sys.path.insert(0, HERE)
    from track_lab import assets, gltf_io, level_types  # noqa: PLC0415

    by_mode = collections.defaultdict(collections.Counter)
    by_slot = collections.defaultdict(collections.Counter)
    seen = set()
    pattern = os.path.join(decomp_root, "assets", ".vanilla", "*")
    for root in sorted(glob.glob(pattern)):
        if not os.path.isfile(os.path.join(root, assets.META_OBJECTS)):
            continue
        for level in assets.AssetTree(root).levels():
            if level.label in seen:
                continue
            seen.add(level.label)
            key = level_types.key_for_race_type(level.race_type)
            for slot, path in (("structure", level.objects_path),
                               ("collectables", level.collectables_path)):
                if not path or not os.path.isfile(path):
                    continue
                for obj in gltf_io.load(path).objects:
                    by_slot[obj.object_id][slot] += 1
                    if key:
                        by_mode[obj.object_id][key] += 1
    return by_mode, by_slot


def _record_value(info, value):
    if isinstance(value, bool):
        info["kinds"].add("bool")
    elif isinstance(value, int):
        info["kinds"].add("int")
        info["values"][value] += 1
        info["min"] = value if info["min"] is None else min(info["min"], value)
        info["max"] = value if info["max"] is None else max(info["max"], value)
    elif isinstance(value, float):
        info["kinds"].add("float")
        info["values"][value] += 1
        info["min"] = value if info["min"] is None else min(info["min"], value)
        info["max"] = value if info["max"] is None else max(info["max"], value)
    elif isinstance(value, str):
        info["kinds"].add("enum")
        info["values"][value] += 1
    elif isinstance(value, list):
        info["kinds"].add("list")
        info["lengths"].add(len(value))
        info["values"][json.dumps(value)] += 1


# ---------------------------------------------------------------------------
# Matching an observed object type to its struct
# ---------------------------------------------------------------------------

_WORD_RE = re.compile(r"[A-Z]+(?![a-z])|[A-Z][a-z]*|[0-9]+")


def _name_affinity(object_id, struct_name):
    """Crude similarity, used only to break ties between identical layouts.

    ``ASSET_OBJECT_AIRZIPPERS`` and ``ASSET_OBJECT_ROCKETSIGNPOST`` have byte
    identical structs, so field matching alone cannot separate
    ``LevelObjectEntry_AirZippers_WaterZippers`` from
    ``LevelObjectEntry_Lighthouse_RocketSignpost``. Compare the words instead.
    """
    short = object_id.replace("ASSET_OBJECT_", "").replace("_", "").lower()
    score = 0
    for word in _WORD_RE.findall(struct_name.split("_", 1)[-1]):
        word = word.lower()
        if word and word in short:
            score = max(score, len(word))
    return score


def match_struct(object_id, observed_fields, structs):
    """Find the struct whose fields cover the ones the glTF actually wrote.

    A retail map omits a field whose decoded value has no representation (an
    out-of-range enum, say), so the observed names are a subset of the struct's,
    never a superset. Prefer an exact match, then the smallest superset, then
    name affinity.

    Array lengths have to be part of the comparison, not just names.
    ``ASSET_OBJECT_FIREBALLATTRACT`` writes ``unk8`` as four values; matching on
    names alone picks ``LevelObjectEntry_AudioSeqLine``, whose ``unk8`` is
    ``u8[0xC]``, and the entry comes out 20 bytes where retail says 12.
    """
    observed = set(observed_fields)
    lengths = {
        name: max(info["lengths"])
        for name, info in observed_fields.items()
        if isinstance(info, dict) and info.get("lengths")
    }

    candidates = []
    for name, fields in structs.items():
        declared = {f["name"] for f in fields}
        if not observed <= declared:
            continue
        by_name = {f["name"]: f for f in fields}
        if any(by_name[field]["count"] != length
               for field, length in lengths.items() if field in by_name):
            continue
        candidates.append(
            (len(declared - observed), -_name_affinity(object_id, name), name)
        )
    if not candidates:
        return None
    candidates.sort()
    return candidates[0][2]


# ---------------------------------------------------------------------------
# Category assignment
# ---------------------------------------------------------------------------

CATEGORIES = [
    ("racing", {
        "AINODE", "CHECKPOINT", "SETUPPOINT", "GROUNDZIPPER", "AIRZIPPERS",
        "WATERZIPPERS", "TRIGGER", "MODECHANGE", "BOOST", "OVERRIDEPOS",
        "HEADFORPOINT", "POSARROW", "RAMPSWITCH", "STOPWATCHMAN",
    }),
    ("pickups", {
        "COIN", "SILVERCOIN", "GOLDCOIN", "FLYCOIN", "SILVERCOINADV2",
        "COINCREATOR", "WEAPONBALLOON", "GOLDENBALLOON", "BANANA",
        "BANANACREATOR", "COLLECTEGG", "EGGCREATOR", "WORLDKEY",
    }),
    ("hub", {
        "EXIT", "LEVELDOOR", "WORLDGATE", "CHALDOOR", "BOSSDOOR", "BIGBOSSDOOR",
        "TTDOOR", "TELEPORT", "LEVELNAME", "TROPHYCAB", "PWSAFETELEPOINT",
    }),
    ("camera", {"CAMERA_CONTROL", "CAMERAANIMATION"}),
    ("audio", {
        "AUDIO", "AUDIOLINE", "AUDIOREVERB", "AUDIOSEQLINE", "MIDIFADE",
        "MIDIFADEPOINT", "MIDICHSET",
    }),
    ("effects", {
        "FOGCHANGER", "WEATHER", "LENSFLARE", "LENSFLARESWITCH", "RGBALIGHT",
        "SKYCONTROL", "EFFECTBOX", "BUBBLER", "TEXSCROLL", "FLAMINGTORCH",
        "RANGETRIGGER", "WAVEGENERATOR", "WAVEPOWER", "ANIMATOR", "ANIMATION",
        "FIREBALLATTRACT",
    }),
    ("actors", {
        "FISH", "BUTTERFLY", "FROG", "PARKWARDEN", "LASERGUN", "LAVASPURT",
        "CHARACTERFLAG", "SEAMONSTER", "SNOWMEN", "PIGHEADCOLOURS",
        "TREASURESUCKER", "BUOY", "PIRATESHIP", "WIZPIGSHIP",
    }),
]

#: Types worth putting in front of a track author first.
FEATURED = [
    "ASSET_OBJECT_GROUNDZIPPER",
    "ASSET_OBJECT_AIRZIPPERS",
    "ASSET_OBJECT_WATERZIPPERS",
    "ASSET_OBJECT_WEAPONBALLOON",
    "ASSET_OBJECT_COIN",
    "ASSET_OBJECT_SILVERCOIN",
    "ASSET_OBJECT_GOLDCOIN",
    "ASSET_OBJECT_CHECKPOINT",
    "ASSET_OBJECT_SETUPPOINT",
    "ASSET_OBJECT_AINODE",
    "ASSET_OBJECT_CAMERA_CONTROL",
    "ASSET_OBJECT_EXIT",
    "ASSET_OBJECT_LEVELDOOR",
]


def categorise(object_id, struct_name):
    short = object_id.replace("ASSET_OBJECT_", "")
    for category, members in CATEGORIES:
        if short in members:
            return category
    if struct_name == "LevelObjectEntry_Scenery":
        return "scenery"
    return "misc"


# ---------------------------------------------------------------------------
# Field description
# ---------------------------------------------------------------------------

def _hint_step(hint):
    """The value one raw unit represents, for hints that rescale a raw byte.

    ``Hint((Angle, DivideBy:64))`` means 64 raw steps span a full turn, so one
    step is 360/64 degrees. Other kinds divide directly.
    """
    if not hint:
        return None
    divide_by = hint.get("divideBy")
    if not divide_by:
        return None
    if hint["kind"] == "Angle":
        return 360.0 / float(divide_by)
    return 1.0 / float(divide_by)


#: Human names for fields the decomp still calls ``unkB`` and friends, so the
#: N panel can say what a value does.
#:
#: **A label, never a rename.** A catalogue field's ``name`` is the Blender
#: custom property name: ``scene.create_empty`` writes ``empty[field.name]`` and
#: ``read_object`` skips any field the object does not carry. Rename one and
#: every existing ``.blend`` silently loses that value on its next export - the
#: field is skipped, never coerced, and the encoder writes whatever absence
#: means. So the raw name stays and this rides alongside it.
#:
#: Each entry was read out of the decomp, not guessed. The checkpoint block is
#: three groups of four, one slot per racer lane, interleaved in
#: ``checkpoint_update_all`` (``objects.c`` ~5675):
#:
#:     unk2E[0..3] <- unkB,  unkC,  unkD,  unkE
#:     unk32[0..3] <- unkF,  unk10, unk11, unk12
#:     unk36[0..3] <- unk13, unk14, unk15, unk16
#:
#: and ``racer.c`` ~1424 says what each group is: ``unk2E`` is added to the
#: spline's X and Z scaled by the gate's rotation fractions, which is a lateral
#: offset in the gate's own plane; ``unk32`` is added to Y alone; ``unk36`` is
#: compared against constants when the AI picks a route.
FIELD_LABELS = {
    "ASSET_OBJECT_CHECKPOINT": dict(
        [("unk%s" % name, "Lateral offset, lane %d" % (index + 1))
         for index, name in enumerate(("B", "C", "D", "E"))]
        + [("unk%s" % name, "Vertical offset, lane %d" % (index + 1))
           for index, name in enumerate(("F", "10", "11", "12"))]
        + [("unk%s" % name, "Route flag, lane %d" % (index + 1))
           for index, name in enumerate(("13", "14", "15", "16"))]
    ),
}


def label_for(object_id, field_name):
    """The human name for a field, or ``None`` when its own name is clear."""
    return FIELD_LABELS.get(object_id, {}).get(field_name)


def describe_field(field, info, enums):
    """Merge a struct field with what retail maps were seen to put in it."""
    ctype = field["ctype"] if field else None
    kinds = set(info["kinds"]) if info else set()
    hint = field.get("hint") if field else None

    if "list" in kinds or (field and field["count"] > 1):
        kind = "list"
    elif "enum" in kinds:
        kind = "enum"
    elif "float" in kinds:
        kind = "float"
    else:
        kind = "int"

    described = {
        "name": field["name"] if field else info["name"],
        "kind": kind,
    }
    if field:
        # The encoder writes each field at its own offset, so the layout has to
        # travel with the catalogue rather than being re-derived from the header.
        described["offset"] = field["offset"]
        described["count"] = field["count"]
    if ctype:
        described["ctype"] = ctype
    if hint:
        described["hint"] = hint

    if kind == "list":
        length = field["count"] if field and field["count"] > 1 else None
        if info and info["lengths"]:
            length = max(info["lengths"])
        described["length"] = length or 1
        described["default"] = (
            _most_common_list(info) if info else [0] * described["length"]
        )
        return described

    if kind == "enum":
        members = sorted(info["values"]) if info else []
        described["values"] = members
        described["enum"] = _enum_name_for(hint, members, enums)
        described["default"] = info["values"].most_common(1)[0][0] if info else ""
        return described

    step = _hint_step(hint)
    if step:
        described["step"] = step
    described["min"], described["max"] = _ctype_range(ctype, hint, step)

    if info and info["values"]:
        # An angle wants to start at zero on a newly placed object; every other
        # field is better off starting at whatever retail uses most, which keeps
        # the pad and unk bytes at the values the game expects to read.
        if hint and hint.get("kind") == "Angle":
            described["default"] = 0.0
        else:
            described["default"] = info["values"].most_common(1)[0][0]
        described["seen_min"] = info["min"]
        described["seen_max"] = info["max"]
        if len(info["values"]) <= 12:
            described["seen"] = sorted(info["values"])
    else:
        described["default"] = 0.0 if kind == "float" else 0
    return described


def _most_common_list(info):
    if not info["values"]:
        return []
    return json.loads(info["values"].most_common(1)[0][0])


def _enum_name_for(hint, members, enums):
    """Name the enum a string field draws from, so the UI can offer all of it."""
    if hint and hint.get("kind") in ("Enum", "AssetId"):
        subject = hint.get("subject")
        if subject and subject in enums:
            return subject
        # ``AssetId:ASSET_LEVEL_HEADERS`` names a section, not the enum that
        # indexes it, so fall through to the subset search below.
    for name, declared in enums.items():
        if members and set(members) <= set(declared):
            return name
    return None


def _raw_range(ctype):
    """The range of the underlying integer, before any hint rescales it."""
    _, signed, width = CTYPES[ctype]
    bits = width * 8
    if signed:
        return -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    return 0, (1 << bits) - 1


def _ctype_range(ctype, hint, step):
    """The range a field can legally hold, in the units the glTF stores.

    This is what the C type permits, not what retail happens to use. Limiting a
    field to observed values would stop an author using a value the game accepts
    perfectly well - a zipper scaled past any retail zipper, say.

    Angles need care. ``get_hint_angle`` in the asset tool
    (``helpers/c/cStructGltfHelper.cpp``) decodes ``value / divideBy * 360``,
    then, only for an unsigned field and only when that lands above 360, wraps
    it negative by subtracting a whole turn's worth of raw steps. So a ``u8``
    with ``DivideBy:64`` yields 0..360 and -1074..-5.625, while the same type
    with ``DivideBy:256`` never exceeds 360 and so is never wrapped, giving
    0..358.59. Reproducing that rule is what keeps a legal angle from being
    clipped.
    """
    if ctype not in CTYPES:
        return None, None
    low, high = _raw_range(ctype)
    if not step:
        return low, high

    if not (hint and hint.get("kind") == "Angle"):
        return low * step, high * step

    _, signed, _width = CTYPES[ctype]
    angles = [raw * step for raw in (low, high)]
    if not signed:
        # Mirror the wrap the tool applies to values past a full turn.
        max_angle = (high + 1) * step
        angles = []
        for raw in range(low, high + 1):
            angle = raw * step
            if angle > 360.0:
                angle -= max_angle
            angles.append(angle)
    return min(angles), max(angles)


# ---------------------------------------------------------------------------
# Assembly
# ---------------------------------------------------------------------------

#: Enums the addon offers as full dropdowns rather than only observed values.
EXPORTED_ENUMS = [
    "Vehicle", "BalloonType", "WarpFlag", "World", "RaceType", "CameraMode",
    # The level header needs these too.
    "BossSetupTypes", "Language",
    # Not reachable from any object field: SurfaceType lives on a level model's
    # texture table entry, not on a LevelObjectEntry. It is exported anyway so
    # the addon has one source for it - what a surface behaves like is format
    # knowledge, and a fallback table copied into a bpy module is exactly the
    # kind of thing that drifts from the decomp without anything noticing.
    "SurfaceType",
    # The header's /music is an index into this list, so the music player can
    # name what it plays rather than show a number.
    "SequenceID",
]


def build_catalog(decomp_root):
    header = os.path.join(decomp_root, "include", "level_object_entries.h")
    structs = parse_structs(header)
    header_paths = (
        os.path.join(decomp_root, "include", "enums.h"),
        os.path.join(decomp_root, "include", "asset_enums.h"),
        os.path.join(decomp_root, "include", "sequence_ids.h"),
    )
    enums = parse_enums(*header_paths)
    enum_values = parse_enum_values(*header_paths)
    paths, observed = survey_object_maps(decomp_root)
    if not observed:
        raise SystemExit(
            "no extracted object maps found under %s; run the decomp's extract.sh"
            % decomp_root
        )
    by_mode, by_slot = survey_level_modes(decomp_root)

    objects = {}
    unmatched = []
    for object_id in sorted(observed):
        entry = observed[object_id]
        struct_name = match_struct(object_id, entry["fields"], structs)
        if struct_name is None and entry["fields"]:
            unmatched.append(object_id)
        struct_fields = structs.get(struct_name, []) if struct_name else []
        by_name = {f["name"]: f for f in struct_fields}

        described = []
        # Struct order first: it is the in-memory order and reads better in a
        # panel than the alphabetical order the glTF happens to store.
        for field in struct_fields:
            info = entry["fields"].get(field["name"])
            if info is None:
                # Declared but never written by any retail map. Keep it, an
                # author may still want it, but mark it so the UI can hide it.
                item = describe_field(field, None, enums)
                item["unused"] = True
            else:
                item = describe_field(field, info, enums)
                item["optional"] = info["count"] < entry["count"]
            described.append(item)
        for name in sorted(entry["fields"]):
            if name not in by_name:
                info = dict(entry["fields"][name], name=name)
                item = describe_field(None, info, enums)
                item["optional"] = entry["fields"][name]["count"] < entry["count"]
                item["undeclared"] = True
                described.append(item)

        for item in described:
            label = label_for(object_id, item["name"])
            if label:
                item["label"] = label

        objects[object_id] = {
            "node_name": entry["names"].most_common(1)[0][0],
            "struct": struct_name,
            "category": categorise(object_id, struct_name),
            "retail_count": entry["count"],
            "featured": object_id in FEATURED,
            "entry_size": struct_size(struct_fields),
            "fields": described,
        }
        if by_mode.get(object_id):
            objects[object_id]["modes"] = dict(sorted(by_mode[object_id].items()))
        if by_slot.get(object_id):
            objects[object_id]["slots"] = {
                slot: by_slot[object_id][slot]
                for slot in ("structure", "collectables")
            }

    catalog = {
        "schemaVersion": SCHEMA_VERSION,
        "source": {
            "maps_surveyed": len(paths),
            "objects_surveyed": sum(e["count"] for e in observed.values()),
            "header": os.path.relpath(header, decomp_root).replace("\\", "/"),
        },
        "enums": {name: enums[name] for name in EXPORTED_ENUMS if name in enums},
        # The encoder writes the declared value, which is not the position:
        # VEHICLE_NO_OVERRIDE is -1 while sitting fifteenth in the list.
        "enumValues": {
            name: enum_values[name] for name in EXPORTED_ENUMS if name in enum_values
        },
        "levels": _level_ids(enums),
        "categories": [name for name, _ in CATEGORIES] + ["scenery", "misc"],
        "objects": objects,
    }
    return catalog, unmatched


def _level_ids(enums):
    """``ASSET_LEVEL_*`` members, which is what ``Exit.destinationMapId`` picks."""
    members = enums.get("AssetLevelHeadersEnum", [])
    return [m for m in members if m.startswith("ASSET_LEVEL_")]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decomp", default=DEFAULT_DECOMP)
    parser.add_argument("--output", default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the generated catalogue differs from the one on disk",
    )
    args = parser.parse_args(argv)

    catalog, unmatched = build_catalog(args.decomp)
    text = json.dumps(catalog, indent=2, sort_keys=True) + "\n"

    if args.check:
        if not os.path.exists(args.output):
            print("FAIL: %s does not exist" % args.output)
            return 1
        with open(args.output, "r", encoding="utf-8") as handle:
            if handle.read() != text:
                print("FAIL: %s is stale, re-run generate_catalog.py" % args.output)
                return 1
        print("catalogue up to date (%d object types)" % len(catalog["objects"]))
        return 0

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

    print("maps surveyed  : %d" % catalog["source"]["maps_surveyed"])
    print("objects seen   : %d" % catalog["source"]["objects_surveyed"])
    print("object types   : %d" % len(catalog["objects"]))
    matched = sum(1 for o in catalog["objects"].values() if o["struct"])
    print("matched struct : %d" % matched)
    if unmatched:
        print("UNMATCHED      : %s" % ", ".join(unmatched))
    print("wrote          : %s" % os.path.relpath(args.output, REPO_ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
