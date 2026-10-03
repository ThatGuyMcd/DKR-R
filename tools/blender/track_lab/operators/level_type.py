"""The Level Type: set it, and say what it changes before changing it.

The property is only ever written from here and from the importers. A change
can leave objects the new kind of level does not use and grids with the wrong
number of racers, and an author should read that before it happens rather than
find it in a validation report later - so the operator asks first, with the
consequences listed and the grids offered for rebuilding in the same dialog.
"""

from __future__ import annotations

import collections
import json

import bpy
from bpy.props import BoolProperty, EnumProperty, StringProperty

from .. import catalog as catalog_module, level_types, prefs, scene
from . import start_grid


def apply_level_type(context, family: str, key: str) -> None:
    settings = context.scene.dkr
    if family == level_types.CHALLENGE:
        settings.challenge_type = key
    elif family == level_types.SPECIAL:
        settings.special_type = key
    settings.level_type = family
    # Results computed under the old rules would be read as the new ones'.
    settings.has_validated = False
    settings.results.clear()


def adopt_header(settings, header) -> str:
    """Fill the Level Type from a level header; returns the key, or ``""``.

    See :func:`level_types.from_header` for why everything it owns is read.
    """
    values = level_types.from_header(header)
    if values is None:
        return ""
    family = values.pop("level_type")
    for name, value in values.items():
        setattr(settings, name, value)
    # Last, because it resets the Place list's category.
    settings.level_type = family
    settings.has_validated = False
    settings.results.clear()
    return level_types.current_key(settings)


_HEADERS = {}


def _read_header(path):
    if path not in _HEADERS:
        try:
            with open(path, "r", encoding="utf-8") as handle:
                _HEADERS[path] = json.load(handle)
        except (OSError, ValueError):
            _HEADERS[path] = None
    return _HEADERS[path]


def imported_level(context):
    """``(level, header)`` of the retail track this scene came from, if any."""
    settings = context.scene.dkr
    source = settings.source_path or settings.geometry_path
    tree = prefs.resolve(context) if source else None
    level = tree.level_using(source) if tree else None
    if level is None or not level.header_path:
        return None, None
    return level, _read_header(level.header_path)


def suggestion(context):
    """What an old scene with no Level Type most likely is, and why.

    A panel cannot write data while drawing, and should not guess in silence,
    so the answer is offered as a button: from the header of the retail track
    the scene was imported from, or from a race type answered in the Level
    Header before the Level Type existed.
    """
    from . import header as header_ops  # noqa: PLC0415

    level, header = imported_level(context)
    if header:
        key = level_types.key_for_race_type(str(header.get("race-type", "")))
        if key:
            return {"key": key, "from": level.label, "header": header,
                    "boss": header.get("boss-race-id") if key == level_types.BOSS else ""}
    answered = context.scene.get(header_ops.key_for("/race-type"))
    if answered:
        key = level_types.key_for_race_type(str(answered))
        if key:
            return {"key": key, "from": "its Level Header",
                    "header": {"race-type": str(answered)}, "boss": ""}
    return None


def _redraw(context) -> None:
    if context.screen is None:
        return
    for area in context.screen.areas:
        area.tag_redraw()


class DKR_OT_set_level_type(bpy.types.Operator):
    """Choose what kind of level this is"""

    bl_idname = "dkr.set_level_type"
    bl_label = "Set Level Type"
    bl_options = {"REGISTER", "UNDO"}

    mode: EnumProperty(
        name="Level Type",
        items=[(key, family.label, family.tooltip)
               for key, family in level_types.FAMILIES.items()],
        default=level_types.RACE,
    )
    sub: StringProperty(
        name="Sub-type",
        description="Challenge or Special kind; blank keeps the current one",
        default="", options={"HIDDEN", "SKIP_SAVE"},
    )
    regenerate_grids: BoolProperty(
        name="Regenerate Start Grids",
        description=(
            "Rebuild each grid for the new level type, in the same place and "
            "facing the same way"
        ),
        default=True, options={"SKIP_SAVE"},
    )

    @classmethod
    def description(cls, context, properties):
        for key, text, help_text in level_types.SUBTYPES.get(properties.mode, ()):
            if key == properties.sub:
                if key == level_types.TEST_RACE:
                    return "%s %s" % (help_text, level_types.TEST_RACE_WARNING)
                return help_text
        return level_types.FAMILIES[properties.mode].tooltip

    def _key(self, context):
        settings = context.scene.dkr
        challenge = self.sub if self.sub in level_types.CHALLENGES else settings.challenge_type
        special = self.sub if self.sub in level_types.SPECIALS else settings.special_type
        return level_types.key_of(self.mode, challenge, special)

    @staticmethod
    def _affected(context, key):
        catalog = catalog_module.load()
        unused = collections.Counter()
        for obj in scene.iter_dkr_objects(context):
            object_type = catalog.get(str(obj.get(scene.PROP_ID)))
            if object_type is not None and not level_types.visible(object_type, key):
                unused[object_type.label] += 1
        grids = [root for root in scene.grid_roots(context)
                 if root.get(scene.PROP_GRID_KEY) != key]
        return unused, grids

    def invoke(self, context, event):
        settings = context.scene.dkr
        key = self._key(context)
        if settings.level_type == self.mode and level_types.current_key(settings) == key:
            return {"CANCELLED"}
        unused, grids = self._affected(context, key)
        if not unused and not grids:
            return self.execute(context)
        return context.window_manager.invoke_props_dialog(
            self, width=400,
            title="Change the level type to %s?" % level_types.label(key),
            confirm_text="Change Level Type",
        )

    def draw(self, context):
        key = self._key(context)
        unused, grids = self._affected(context, key)
        layout = self.layout
        if unused:
            layout.label(text="%d object(s) are not used in a %s level:"
                         % (sum(unused.values()), level_types.label(key)),
                         icon="ERROR")
            column = layout.column(align=True)
            for name, count in sorted(unused.items()):
                column.label(text="%s × %d" % (name, count), icon="DOT")
            row = layout.row()
            row.active = False
            row.label(text="They stay in the scene; validation will warn.")
        if grids:
            layout.separator()
            layout.label(text="%d start grid(s) were built for another level type:"
                         % len(grids), icon="ERROR")
            for root in grids:
                layout.label(
                    text="%s: %d → %d start positions" % (
                        root.name,
                        level_types.spawn_count(root.get(scene.PROP_GRID_KEY)),
                        level_types.spawn_count(key)),
                    icon="DOT",
                )
            if level_types.spawn_count(key):
                layout.prop(self, "regenerate_grids")
            else:
                row = layout.row()
                row.active = False
                row.label(text="A %s spawns no racers; the grids are left as they are."
                          % level_types.label(key))

    def execute(self, context):
        key = self._key(context)
        apply_level_type(context, self.mode, key)

        rebuilt = 0
        if self.regenerate_grids and level_types.spawn_count(key):
            for root in scene.grid_roots(context):
                if root.get(scene.PROP_GRID_KEY) != key:
                    start_grid.regenerate(context, root)
                    rebuilt += 1

        message = "Level type: %s" % level_types.label(key)
        if key == level_types.TEST_RACE:
            message += " (not recommended)"
        if rebuilt:
            message += "; rebuilt %d start grid(s)" % rebuilt
        self.report({"WARNING"} if key == level_types.TEST_RACE else {"INFO"}, message)
        _redraw(context)
        return {"FINISHED"}


class DKR_OT_use_imported_level_type(bpy.types.Operator):
    """Use the level type of the track this scene came from, read from its header"""

    bl_idname = "dkr.use_imported_level_type"
    bl_label = "Use Imported Level Type"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return suggestion(context) is not None

    def execute(self, context):
        found = suggestion(context)
        if found is None:
            return {"CANCELLED"}
        key = adopt_header(context.scene.dkr, found["header"])
        self.report({"INFO"}, "Level type: %s, from %s"
                    % (level_types.label(key), found["from"]))
        _redraw(context)
        return {"FINISHED"}


class DKR_OT_toggle_vehicle(bpy.types.Operator):
    """Allow or disallow a vehicle on this track"""

    bl_idname = "dkr.toggle_vehicle"
    bl_label = "Toggle Vehicle"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    vehicle: EnumProperty(
        items=[(vehicle, name, "") for vehicle, name in level_types.PLAYER_VEHICLES],
    )

    @classmethod
    def description(cls, context, properties):
        key = level_types.current_key(context.scene.dkr)
        name = level_types.vehicle_name(properties.vehicle).lower()
        if key == level_types.BOSS:
            return "In a Boss Race the vehicle is set by the boss"
        if level_types.is_challenge(key):
            return ("Race this challenge in the %s. A challenge allows one "
                    "vehicle, so no racer has an advantage" % name)
        return ("Allow the %s on this track. The player picks from the allowed "
                "vehicles, and the bots race in the player's vehicle" % name)

    def execute(self, context):
        settings = context.scene.dkr
        key = level_types.current_key(settings)
        if key == level_types.BOSS:
            self.report({"WARNING"}, "In a Boss Race the vehicle is set by the boss")
            return {"CANCELLED"}
        if level_types.is_challenge(key):
            settings.vehicles = {self.vehicle}
            settings.default_vehicle = self.vehicle
        else:
            chosen = set(settings.vehicles)
            if self.vehicle in chosen:
                if len(chosen) == 1:
                    self.report({"WARNING"}, "A track needs at least one vehicle")
                    return {"CANCELLED"}
                chosen.discard(self.vehicle)
            else:
                chosen.add(self.vehicle)
            settings.vehicles = chosen
        _redraw(context)
        return {"FINISHED"}


class DKR_OT_set_default_vehicle(bpy.types.Operator):
    """The vehicle selected when the player enters the track"""

    bl_idname = "dkr.set_default_vehicle"
    bl_label = "Default Vehicle"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    vehicle: EnumProperty(
        items=[(vehicle, level_types.vehicle_name(vehicle), "")
               for vehicle, _name in level_types.PLAYER_VEHICLES],
    )

    def execute(self, context):
        context.scene.dkr.default_vehicle = self.vehicle
        _redraw(context)
        return {"FINISHED"}


CLASSES = (
    DKR_OT_set_level_type,
    DKR_OT_use_imported_level_type,
    DKR_OT_toggle_vehicle,
    DKR_OT_set_default_vehicle,
)
