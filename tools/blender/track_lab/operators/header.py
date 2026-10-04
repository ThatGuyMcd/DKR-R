"""Collect the level header for a track with no ancestor.

A remix inherits its header from the track it is built on: the world, the race
type and two hundred bytes of everything else come from the base track and only
the name changes. A track modelled from scratch has nothing to inherit from, and
the header is what points at the geometry, so without one the package has
nothing to load even once the geometry exists.

:mod:`level_header_template` owns what a header holds — 22 fields identical in
all 65 retail headers, 95 more with a surveyed default, and the handful with no
dominant value at all. This module owns only the asking: it renders
``template.CHOICES`` as a form and hands the answers back as overrides.

**Nothing here restates a field.** The pointers, kinds, enum subjects, ranges
and labels are all read from ``CHOICES`` at draw time, so a field added or
changed on the template side appears here with no edit. Hard-coding
``/background/skybox/id`` and a dozen siblings would have drifted the first time
the template moved, with nothing to catch it.

Answers live as custom properties on the scene rather than as registered
properties, because the set is decided by the template rather than by this file
and Blender wants its properties known at registration. The key is the pointer
flattened into an identifier, so the mapping is by construction rather than by a
table that could fall out of step.
"""

from __future__ import annotations

import bpy
from bpy.props import EnumProperty, IntProperty, StringProperty

from .. import catalog as catalog_module, level_header_template as template
from .. import level_types, prefs, race_ai, water

#: Prefix for the scene properties holding an author's answers.
PREFIX = "dkr_hdr_"

#: Fields the Level Type answers. The header panel shows them locked, since two
#: sources of truth for one byte is the thing that must not exist.
LEVEL_OWNED = ("/race-type", "/lap-count", "/avaliable-vehicles",
               "/default-vehicle")

MUSIC = "/music"
SKYBOX = "/background/skybox/id"

#: The header's wave bytes, described the way the header form describes its
#: own, so the scene answers, the inherited-header overlay and the encoder
#: treat them like any other field. The Water panel draws them.
WATER_CHOICES = tuple(
    template.Choice(field.pointer, field.label, field.help,
                    minimum=field.minimum, maximum=field.maximum)
    for field in water.HEADER_FIELDS
) + (
    template.Choice(water.DETAIL_POINTER, "Wave Detail Texture",
                    "The 2D texture translucent waves are overlaid with. "
                    "Every retail header but the trophy race's names "
                    "ASSET_TEX2D_WATER_DETAIL"),
)

#: Answers a remix keeps from the header it inherits, and can change. The
#: import fills them from that header and the export lays them back over it, so
#: an untouched remix writes what it came with and a new sky actually ships.
#: The race AI's bytes are among them: a remix starts with its base track's
#: difficulty and the AI Racers panel edits it. So are the waves'.
INHERITED = ((MUSIC, SKYBOX) + race_ai.HEADER_POINTERS
             + tuple(choice.pointer for choice in WATER_CHOICES))

#: Every field an answer can be kept for. The header form draws only
#: ``template.CHOICES``; the AI's own live in the AI Racers panel and the
#: waves' in the Water panel.
ALL_CHOICES = tuple(template.CHOICES) + race_ai.HEADER_CHOICES + WATER_CHOICES


def key_for(pointer: str) -> str:
    """The scene property holding the answer to ``pointer``."""
    return PREFIX + pointer.strip("/").replace("/", "_").replace("-", "_")


def choice_for(pointer: str):
    for choice in ALL_CHOICES:
        if choice.pointer == pointer:
            return choice
    return None


def overrides(context) -> dict:
    """Every answer the author has given, keyed the way the template wants."""
    scene = context.scene
    found = {}
    for choice in ALL_CHOICES:
        key = key_for(choice.pointer)
        if key not in scene:
            continue
        value = scene[key]
        if hasattr(value, "to_list"):
            value = value.to_list()
        elif hasattr(value, "__len__") and not isinstance(value, (str, bytes)):
            value = list(value)
        found[choice.pointer] = value
    return found


def surveyed(pointer: str):
    """What the template would use for ``pointer`` if the author says nothing."""
    return template.lookup(template.document({}), pointer)


def effective_overrides(context) -> dict:
    """The author's answers with the Level Type's fields laid over them."""
    found = overrides(context)
    found.update(level_types.settings_overrides(context.scene.dkr))
    return found


def unanswered(context) -> list:
    """Pointers that have no answer and no default, so the header cannot say."""
    return template.missing(effective_overrides(context))


def _lookup(document, pointer):
    # List-aware: the AI bytes are array elements (``/unknown/unkC/3``), and a
    # dict-only walk read every one of them as absent.
    return template.lookup(document, pointer)


def adopt_answers(context, header) -> None:
    """Answer :data:`INHERITED` from a level header, as an import does."""
    for pointer in INHERITED:
        key = key_for(pointer)
        value = _lookup(header, pointer)
        if value is None:
            if key in context.scene:
                del context.scene[key]
            continue
        context.scene[key] = value
        choice = choice_for(pointer)
        if choice is not None:
            _apply_ui(context.scene, choice)


def inherited_overrides(context) -> dict:
    """The :data:`INHERITED` answers, for laying over an inherited header."""
    found = overrides(context)
    result = {pointer: found[pointer] for pointer in INHERITED if pointer in found}
    # A remix joins Custom Tracks too, unless the author chooses another world.
    result["/world"] = found.get("/world", surveyed("/world"))
    return result


def music_index(context) -> int:
    """What ``/music`` will be: the author's answer, or the surveyed default."""
    value = context.scene.get(key_for(MUSIC))
    if value is None:
        value = surveyed(MUSIC)
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def enum_members(choice) -> list:
    """The values an enum or bitfield choice can take."""
    try:
        catalog = catalog_module.load()
    except Exception:  # noqa: BLE001 - a picker must not take the panel down
        return []
    return sorted(catalog.raw.get("enumValues", {}).get(choice.subject, {}))


def asset_members(context, choice) -> list:
    """The build ids an asset choice can name, or empty with no asset tree."""
    tree = prefs.resolve(context)
    if tree is None:
        return []
    meta = "asset_%s.meta.json" % choice.subject.replace("ASSET_", "").lower()
    try:
        return list(tree.order(meta))
    except Exception:  # noqa: BLE001 - an unreadable manifest is not fatal here
        return []


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

#: Blender's enum callbacks hand their strings to C without taking a reference,
#: so a list built fresh on every call can be collected while the menu is still
#: using it - the documented symptom being a picker that opens empty. Holding the
#: last list returned for each key is what keeps them alive.
_ITEMS = {}


def _keep(key, items):
    """Hold a reference to what an enum callback returned, and return it."""
    _ITEMS[key] = items
    return items


class DKR_OT_set_header_choice(bpy.types.Operator):
    """Pick a value for one of the header's enumerated fields"""

    bl_idname = "dkr.set_header_choice"
    bl_label = "Set Header Field"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    pointer: StringProperty(options={"HIDDEN"})

    def _items(self, context):
        choice = choice_for(self.pointer)
        if choice is None:
            return _keep("choice", [("NONE", "no field selected", "")])
        members = (asset_members(context, choice) if choice.kind == "asset"
                   else enum_members(choice))
        if not members:
            return _keep("choice", [(
                "NONE",
                "nothing to choose from",
                "the asset tree is not configured" if choice.kind == "asset"
                else "the catalogue carries no %s enum" % choice.subject,
            )])
        return _keep("choice", [(m, m, "") for m in members])

    value: EnumProperty(name="Value", items=_items)

    def invoke(self, context, event):
        # A dialog rather than a search popup: it draws the enum as an ordinary
        # dropdown, which types-to-filter the same way and does not depend on
        # the search UI reading the operator's other properties.
        return context.window_manager.invoke_props_dialog(self)

    def draw(self, context):
        """Say which field is being set; the dialog otherwise shows only Value."""
        choice = choice_for(self.pointer)
        self.layout.label(text=choice.label if choice else self.pointer)
        if choice is not None and choice.help:
            for line in choice.help.split(". ")[:2]:
                self.layout.label(text=line.strip().rstrip(".") + ".")
        self.layout.prop(self, "value", text="")

    def execute(self, context):
        choice = choice_for(self.pointer)
        if choice is None:
            return {"CANCELLED"}
        if self.value in ("", "NONE"):
            self.report({"WARNING"}, "nothing was chosen")
            return {"CANCELLED"}
        key = key_for(choice.pointer)
        if choice.kind == "bitfield":
            # A bitfield holds several members at once, so picking one adds it
            # rather than replacing what is there.
            current = list(context.scene.get(key, []))
            if self.value not in current:
                current.append(self.value)
            context.scene[key] = sorted(current)
        else:
            context.scene[key] = self.value
        _redraw(context)
        return {"FINISHED"}


class DKR_OT_clear_header_choice(bpy.types.Operator):
    """Drop an answer, so the field falls back to the surveyed default"""

    bl_idname = "dkr.clear_header_choice"
    bl_label = "Clear Header Field"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    pointer: StringProperty(options={"HIDDEN"})
    member: StringProperty(options={"HIDDEN"})

    def execute(self, context):
        key = key_for(self.pointer)
        if key not in context.scene:
            return {"CANCELLED"}
        if self.member:
            remaining = [v for v in list(context.scene[key]) if v != self.member]
            context.scene[key] = remaining
        else:
            del context.scene[key]
        _redraw(context)
        return {"FINISHED"}


class DKR_OT_header_defaults(bpy.types.Operator):
    """Fill every header field with the value the retail tracks favour"""

    bl_idname = "dkr.header_defaults"
    bl_label = "Fill Defaults"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        filled = 0
        for choice in template.CHOICES:
            if choice.pointer in LEVEL_OWNED:
                continue
            value = choice.default
            if value is None:
                value = surveyed(choice.pointer)
            if value is None:
                continue
            context.scene[key_for(choice.pointer)] = value
            _apply_ui(context.scene, choice)
            filled += 1

        # Two defaults name an asset, which only an extracted tree resolves, so
        # they are offered rather than written into the template - a track that
        # never wanted water should not need the decomp to produce a header.
        tree = prefs.resolve(context)
        assets_filled = 0
        if tree is not None:
            for pointer, build_id in template.asset_defaults().items():
                if choice_for(pointer) is not None:
                    context.scene[key_for(pointer)] = build_id
                    assets_filled += 1

        left = unanswered(context)
        if left:
            self.report(
                {"WARNING"},
                "filled %d field(s); %s still need an answer, because retail "
                "has no dominant value for them and zero is a real setting "
                "rather than an absence" % (filled, ", ".join(left)),
            )
        else:
            self.report({"INFO"}, "filled %d header field(s)%s"
                        % (filled, ", %d from the asset tree" % assets_filled
                           if assets_filled else ""))
        _redraw(context)
        return {"FINISHED"}


class DKR_OT_step_music(bpy.types.Operator):
    """Step through the game's music list. The track showing when you export is
    the one the header gets (/music)"""

    bl_idname = "dkr.step_music"
    bl_label = "Music"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    step: IntProperty(default=1, options={"SKIP_SAVE"})

    @classmethod
    def description(cls, context, properties):
        return "Next track" if properties.step > 0 else "Previous track"

    def execute(self, context):
        tracks = level_types.music_tracks(_catalog_or_none())
        count = len(tracks) or 256
        context.scene[key_for(MUSIC)] = (music_index(context) + self.step) % count
        choice = choice_for(MUSIC)
        if choice is not None:
            _apply_ui(context.scene, choice)
        _redraw(context)
        return {"FINISHED"}


def _catalog_or_none():
    try:
        return catalog_module.load()
    except Exception:  # noqa: BLE001 - the music list is a nicety
        return None


def _apply_ui(scene, choice) -> None:
    """Teach Blender the field's range, so the slider behaves."""
    if choice.kind not in ("int", "float"):
        return
    try:
        ui = scene.id_properties_ui(key_for(choice.pointer))
    except (TypeError, KeyError):
        return
    settings = {}
    if choice.minimum is not None:
        settings["min"] = choice.minimum
        settings["soft_min"] = choice.minimum
    if choice.maximum is not None:
        settings["max"] = choice.maximum
        settings["soft_max"] = choice.maximum
    if choice.help:
        settings["description"] = choice.help
    if settings:
        try:
            ui.update(**settings)
        except (TypeError, ValueError):
            pass


def _redraw(context) -> None:
    if context.screen is None:
        return
    for area in context.screen.areas:
        area.tag_redraw()


CLASSES = (
    DKR_OT_set_header_choice,
    DKR_OT_clear_header_choice,
    DKR_OT_header_defaults,
    DKR_OT_step_music,
)
