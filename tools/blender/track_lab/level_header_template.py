"""A level header for a track with no ancestor.

A remix inherits its header from the track it reworks: `operators/pack.py` reads
the extracted JSON of the base level and hands it to :mod:`level_header`. A
track modelled from scratch has nothing to inherit, so `_base_header` returns
``None`` and no header is written at all - and since the header is what names a
track's geometry, the package has nothing to load even once the geometry exists.
That is the gap this closes.

:mod:`level_header` was never the missing part; it already reproduces all 65
retail headers byte for byte. What was missing is a *document* for it to encode.

**Where the values come from.** Not from taste. `generate_header_defaults.py`
surveys the 65 retail headers field by field and splits them three ways, and the
result is committed as ``data/level_header_defaults.json``:

* **fixed** (22 fields) - the same value in all 65. Nobody chooses these.
* **defaulted** (96) - varies, but one value holds in at least 33 of the 65.
  Fog, the wave simulation, the weather block, the AI difficulty curve: a track
  that says nothing about them gets what most retail tracks have.
* **author choices** (8) - no dominant value, so defaulting one would be
  choosing for the author while looking like a default.

:data:`CHOICES` is what an author fills in. It is deliberately a little wider
than those 8: `lap-count` and the fog distances have dominant defaults but are
obviously things a track author sets, and `background/colour` splits three ways
in the survey only because green varies more than red and blue do.

Every descriptor takes its type, enum and default **from
:data:`level_header.LAYOUT` itself** rather than restating them, so a UI
generated from this list cannot drift from what the encoder will accept.

Deliberately free of ``bpy``: the panel that collects these is the addon's, the
decision about what they are is this module's.
"""

from __future__ import annotations

import copy
import json
import os
from typing import Any, Dict, List, Optional, Tuple

from . import level_header

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data",
                    "level_header_defaults.json")

SUPPORTED_SCHEMA = 1

#: Offsets the runtime fills in as it serves the header, so nothing in the
#: document has to wait for the geometry or the object maps to exist. ``0x34``
#: is patched from a shipped ``LEVEL_MODELS`` payload, ``0x36`` and ``0xBA``
#: from the two object map slots. An author never sets any of them, and a panel
#: has no ordering constraint because of them.
RUNTIME_PATCHED = ("/model",)


class TemplateError(Exception):
    pass


class Choice:
    """One field an author fills in, described well enough to build a form.

    ``kind``, ``subject`` and ``ctype`` mirror the matching
    :class:`level_header.Field`; they are read from it at import rather than
    written down twice.
    """

    __slots__ = ("pointer", "label", "help", "kind", "subject", "ctype",
                 "default", "minimum", "maximum")

    def __init__(self, pointer, label, help="", default=None,
                 minimum=None, maximum=None):
        field = _field(pointer)
        self.pointer = pointer
        self.label = label
        self.help = help
        #: ``enum``, ``bitfield``, ``asset``, ``int`` or ``float``.
        self.kind = field.kind
        #: The enum or asset section name, for populating a dropdown from
        #: ``catalog.raw["enumValues"]``. ``None`` for plain numbers.
        self.subject = field.subject
        self.ctype = field.ctype
        self.default = default if default is not None else field.default
        self.minimum = minimum
        self.maximum = maximum

    def as_dict(self) -> Dict[str, Any]:
        """The descriptor as plain data, for a UI or a test to walk."""
        return {
            "pointer": self.pointer, "label": self.label, "help": self.help,
            "kind": self.kind, "subject": self.subject, "ctype": self.ctype,
            "default": self.default,
            "minimum": self.minimum, "maximum": self.maximum,
        }

    def __repr__(self):
        return "Choice(%r, kind=%r, subject=%r)" % (
            self.pointer, self.kind, self.subject
        )


def _field(pointer: str) -> level_header.Field:
    for field in level_header.LAYOUT:
        if field.pointer == pointer:
            return field
    raise TemplateError(
        "%s is not a field of the level header layout; a choice cannot "
        "describe something the encoder will not write" % pointer
    )


#: What a from-scratch track asks its author for. Order is the order a panel
#: should show them: what the track *is*, then how it is raced, then how it
#: looks.
CHOICES: Tuple[Choice, ...] = (
    Choice("/world", "World",
           "Custom Tracks groups this course with legacy tracks in Track Select",
           default="WORLD_CUSTOM_TRACKS"),
    Choice("/race-type", "Race type",
           "A normal race, a battle, a challenge or a boss"),
    Choice("/lap-count", "Laps", "Laps in a race", minimum=1, maximum=9),
    Choice("/course-height", "Course height",
           "How far above the track the camera and the void sit. Retail spans "
           "roughly 1000 to 20000; 5000 is the most common",
           default=5000.0),
    Choice("/avaliable-vehicles", "Vehicles",
           "Which vehicles can be picked. A list, because a track can allow "
           "more than one",
           default=["VEHICLE_CAR"]),
    Choice("/default-vehicle", "Default vehicle",
           "The vehicle a racer starts in"),
    Choice("/music", "Music", "Index into the game's music list",
           default=0, minimum=0, maximum=255),
    # No default on purpose. An asset reference is a name that only an
    # extracted asset tree can resolve to an index, and a panel collecting this
    # form may have none configured. Left unset it encodes as -1, meaning no
    # skybox, which is honest; defaulting it would make a header that cannot be
    # written without the decomp present. ASSET_OBJECT_DOME1 is what 23 of the
    # 65 retail tracks use and is the sensible thing for a UI to preselect
    # once it can resolve it.
    Choice("/background/skybox/id", "Skybox",
           "The object drawn as the sky. Needs the decomp assets configured; "
           "most retail tracks use ASSET_OBJECT_DOME1"),
    Choice("/background/colour/red", "Background red",
           "Cleared to this behind the skybox", default=0,
           minimum=0, maximum=255),
    Choice("/background/colour/green", "Background green", "", default=0,
           minimum=0, maximum=255),
    Choice("/background/colour/blue", "Background blue", "", default=0,
           minimum=0, maximum=255),
    Choice("/fog/near", "Fog near", "Where fog starts"),
    Choice("/fog/far", "Fog far", "Where fog reaches full strength"),
    Choice("/fog/colour/red", "Fog red", "", minimum=0, maximum=255),
    Choice("/fog/colour/green", "Fog green", "", minimum=0, maximum=255),
    Choice("/fog/colour/blue", "Fog blue", "", minimum=0, maximum=255),
)


def choices() -> List[Dict[str, Any]]:
    """:data:`CHOICES` as plain dicts, for a UI to build a form from."""
    return [choice.as_dict() for choice in CHOICES]


# ---------------------------------------------------------------------------
# The document
# ---------------------------------------------------------------------------

_cache: Dict[str, Any] = {}


def load(path: str = DATA) -> Dict[str, Any]:
    """The surveyed defaults, as ``generate_header_defaults.py`` wrote them."""
    if _cache.get("path") == path and _cache.get("data") is not None:
        return _cache["data"]
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except OSError as error:
        raise TemplateError("could not read %s: %s" % (path, error))
    if data.get("schemaVersion") != SUPPORTED_SCHEMA:
        raise TemplateError(
            "%s is schema %r, this module speaks %r"
            % (path, data.get("schemaVersion"), SUPPORTED_SCHEMA)
        )
    _cache["path"], _cache["data"] = path, data
    return data


def _set(document: Dict[str, Any], pointer: str, value) -> None:
    """Write a value at a JSON-pointer-ish path, making dicts and lists on the
    way. A path component that parses as an integer means a list index, which
    is how ``/misc-assets/0`` and the AI difficulty arrays are addressed."""
    parts = [p for p in str(pointer).strip("/").split("/") if p]
    if not parts:
        raise TemplateError("an empty pointer addresses nothing")
    current: Any = document
    for index, part in enumerate(parts[:-1]):
        nxt = parts[index + 1]
        want_list = nxt.lstrip("-").isdigit()
        if part.lstrip("-").isdigit() and isinstance(current, list):
            slot = int(part)
            while len(current) <= slot:
                current.append(None)
            if not isinstance(current[slot], (dict, list)):
                current[slot] = [] if want_list else {}
            current = current[slot]
            continue
        if not isinstance(current.get(part), (dict, list)):
            current[part] = [] if want_list else {}
        current = current[part]
    last = parts[-1]
    if last.lstrip("-").isdigit() and isinstance(current, list):
        slot = int(last)
        while len(current) <= slot:
            current.append(None)
        current[slot] = value
    else:
        current[last] = value


def lookup(document: Any, pointer: str) -> Any:
    """Read a value at a pointer, the inverse of :func:`_set`: an integer
    component indexes a list. ``None`` when the path is not there."""
    node = document
    for step in [p for p in str(pointer).strip("/").split("/") if p]:
        if isinstance(node, list) and step.lstrip("-").isdigit():
            slot = int(step)
            if not -len(node) <= slot < len(node):
                return None
            node = node[slot]
        elif isinstance(node, dict) and step in node:
            node = node[step]
        else:
            return None
    return node


def document(overrides: Optional[Dict[str, Any]] = None,
             path: str = DATA) -> Dict[str, Any]:
    """A header document for a track with no ancestor.

    ``overrides`` maps pointers to values and is where an author's answers go;
    anything it does not mention keeps the surveyed default. A pointer the
    layout does not know is refused rather than silently dropped, because a
    typo in a panel would otherwise look like a setting that did nothing.

    The result is what :func:`level_header.encode` takes. Nothing in it depends
    on the geometry or the object maps existing - see :data:`RUNTIME_PATCHED`.
    """
    data = load(path)
    built: Dict[str, Any] = {}

    for pointer, value in sorted(data.get("fixed", {}).items()):
        _set(built, pointer, copy.deepcopy(value))
    for pointer, value in sorted(data.get("defaulted", {}).items()):
        _set(built, pointer, copy.deepcopy(value))
    # asset-defaults are held back on purpose; see asset_defaults().
    for choice in CHOICES:
        if choice.default is not None:
            _set(built, choice.pointer, copy.deepcopy(choice.default))

    for pointer, value in (overrides or {}).items():
        _field(pointer)  # refuses a pointer the encoder would ignore
        _set(built, pointer, copy.deepcopy(value))
    return built


def apply_overrides(document: Dict[str, Any],
                    overrides: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    """Write ``overrides`` into an existing header document, in place.

    For a header inherited from a retail track: the fields the Level Type owns
    have to reach it as well, and nothing else about it may change. A pointer
    the layout does not know is refused, as in :func:`document`.
    """
    for pointer, value in (overrides or {}).items():
        _field(pointer)
        _set(document, pointer, copy.deepcopy(value))
    return document


def asset_defaults(path: str = DATA) -> Dict[str, str]:
    """Defaults that name an asset, which the template deliberately omits.

    An ``asset`` field stores an index, and the retail headers carry a build id
    - a string only an extracted asset tree turns into one. Writing those into
    the template would make a from-scratch track need the decomp configured
    before it could produce a header at all, to inherit things like the water
    texture that most tracks do not use.

    So they are held back and offered here instead: a UI with an asset tree
    available can preselect them, and one without simply does not, and the
    field encodes as -1 meaning nothing. Retail's only one is the wave detail
    texture, which matters solely to a track with water.
    """
    return dict(load(path).get("asset-defaults", {}))


def missing(overrides: Optional[Dict[str, Any]] = None,
            path: str = DATA) -> List[str]:
    """Author choices with no value and no default, which a panel must collect.

    A header still encodes without them - the encoder leaves a field it has no
    value for at zero - but zero is a real world and a real race type, so a
    track that never answered would quietly become a Central Area default race.
    """
    given = set(overrides or {})
    absent = []
    for choice in CHOICES:
        if choice.pointer in given or choice.default is not None:
            continue
        pointer = choice.pointer
        data = load(path)
        if pointer in data.get("fixed", {}) or pointer in data.get("defaulted", {}):
            continue
        absent.append(pointer)
    return absent
