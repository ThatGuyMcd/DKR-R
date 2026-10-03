"""The race AI in the viewport and the sidebar.

The line is :mod:`track_lab.race_ai`'s, drawn as an overlay rather than
built into scene objects: it is a view of the checkpoints, never something to
export, select or save, and an overlay follows a checkpoint while it is being
dragged where a generated curve would lag until someone rebuilt it.

Rebuilding is keyed on a signature of everything the line depends on - each
checkpoint's position, turn and lane bytes, and the vehicle set shown - so a
redraw with nothing changed costs one pass over the checkpoints and no spline.
"""

from __future__ import annotations

import traceback

import bpy
from bpy.props import EnumProperty

from .. import catalog as catalog_module, level_types, prefs, race_ai, scene

#: Lane 1 to 4, warm to cold, so the outside lanes read as the two ends of the
#: road rather than four unrelated colours.
LANE_COLOURS = (
    (0.96, 0.36, 0.30, 1.0),
    (1.00, 0.78, 0.22, 1.0),
    (0.36, 0.86, 0.42, 1.0),
    (0.32, 0.62, 1.00, 1.0),
)
LANE_NAMES = ("red", "yellow", "green", "blue")

LINE_WIDTH = 3.0
ALTERNATE_WIDTH = 2.0
ALTERNATE_ALPHA = 0.6
POINT_SIZE = 7.0

#: Retail races, the only headers whose difficulty is worth copying: hubs,
#: challenges and cutscenes carry the survey's zeros.
RETAIL_RACE_TYPE = "RACETYPE_DEFAULT"


# ---------------------------------------------------------------------------
# Reading the scene
# ---------------------------------------------------------------------------

def checkpoint_empties(context):
    """The scene's checkpoints in document order, which is spawn order - the
    order the game counts to its 60-gate ceiling in."""
    found = [obj for obj in scene.iter_dkr_objects(context)
             if str(obj.get(scene.PROP_ID)) == race_ai.CHECKPOINT]
    found.sort(key=lambda obj: (int(obj.get("dkr_order", 1 << 30)), obj.name))
    return found


def set_counts(context):
    """``{vehicleType: checkpoints}`` in the scene, without reading the rest."""
    counts = {}
    for obj in checkpoint_empties(context):
        key = int(obj.get("vehicleType", 0))
        counts[key] = counts.get(key, 0) + 1
    return counts


def vehicle_index(settings) -> int:
    names = [vehicle for vehicle, _label in level_types.PLAYER_VEHICLES]
    try:
        return names.index(settings.ai_line_vehicle)
    except ValueError:
        return 0


def vehicle_set(context) -> int:
    """The checkpoint set the shown vehicle's racers load (``header.unk4F``)."""
    return int(getattr(context.scene.dkr_ai,
                       "set_%d" % vehicle_index(context.scene.dkr)))


def read_route(context, catalog=None):
    """``(route, map_objects)`` for the vehicle the panel is showing."""
    catalog = catalog or catalog_module.load()
    objects = [scene.read_object(obj, catalog) for obj in checkpoint_empties(context)]
    return race_ai.build_route(objects, vehicle_set(context)), objects


def geometry(route):
    """The overlay's vertices, in Blender space, per lane."""
    to_blender = scene.to_blender

    def pairs(lines):
        coords = []
        for line in lines:
            points = [tuple(to_blender(point)) for point in line]
            for start, end in zip(points, points[1:]):
                coords += (start, end)
        return coords

    gates = list(route.main) + [route.alternate_of[k] for k in sorted(route.alternate_of)]
    return {
        "lanes": [pairs([race_ai.lane_line(route, lane)])
                  for lane in range(race_ai.LANES)],
        "detours": [pairs(race_ai.alternate_lines(route, lane))
                    for lane in range(race_ai.LANES)],
        "points": [[tuple(to_blender(node.lane_point(lane))) for node in gates]
                   for lane in range(race_ai.LANES)],
    }


def _plain(value):
    if hasattr(value, "to_list"):
        return tuple(value.to_list())
    return value


def _signature(context):
    items = []
    for obj in checkpoint_empties(context):
        items.append((
            obj.name,
            tuple(round(c, 4) for c in obj.matrix_world.translation),
            round(scene.world_yaw(obj), 6),
            tuple(_plain(obj.get(name)) for name in race_ai.LINE_FIELDS),
        ))
    return vehicle_set(context), tuple(items)


# ---------------------------------------------------------------------------
# Drawing
# ---------------------------------------------------------------------------

_HANDLE = None
_CACHE = {"signature": None, "geometry": None, "batches": None}
_LAST_ERROR = [None]


def _shader(*names):
    import gpu  # noqa: PLC0415 - absent in some background builds

    for name in names:
        try:
            return gpu.shader.from_builtin(name)
        except (ValueError, RuntimeError, SystemError):
            continue
    return None


def _build_batches(found):
    from gpu_extras.batch import batch_for_shader  # noqa: PLC0415

    line = _shader("POLYLINE_UNIFORM_COLOR")
    point = _shader("POINT_UNIFORM_COLOR", "UNIFORM_COLOR")
    batches = []
    for lane in range(race_ai.LANES):
        colour = LANE_COLOURS[lane]
        if line is not None and found["lanes"][lane]:
            batches.append(("line", line, batch_for_shader(
                line, "LINES", {"pos": found["lanes"][lane]}), colour, LINE_WIDTH))
        if line is not None and found["detours"][lane]:
            batches.append(("line", line, batch_for_shader(
                line, "LINES", {"pos": found["detours"][lane]}),
                colour[:3] + (ALTERNATE_ALPHA,), ALTERNATE_WIDTH))
        if point is not None and found["points"][lane]:
            batches.append(("point", point, batch_for_shader(
                point, "POINTS", {"pos": found["points"][lane]}), colour, POINT_SIZE))
    return batches


def _draw_batches(batches, region, on_top):
    import gpu  # noqa: PLC0415

    size = (float(region.width), float(region.height)) if region else (1.0, 1.0)
    gpu.state.blend_set("ALPHA")
    gpu.state.depth_test_set("NONE" if on_top else "LESS_EQUAL")
    try:
        for kind, shader, batch, colour, width in batches:
            shader.bind()
            shader.uniform_float("color", colour)
            if kind == "line":
                shader.uniform_float("viewportSize", size)
                shader.uniform_float("lineWidth", width)
            else:
                gpu.state.point_size_set(width)
            batch.draw(shader)
    finally:
        gpu.state.blend_set("NONE")
        gpu.state.depth_test_set("NONE")
        gpu.state.point_size_set(1.0)


def _draw():
    """The draw handler. It must never raise: Blender would print the
    traceback on every redraw, so a failure is printed once and then only when
    it changes."""
    context = bpy.context
    try:
        settings = getattr(context.scene, "dkr", None)
        if settings is None or not settings.show_ai_lines:
            return
        if not level_types.needs_checkpoints(level_types.current_key(settings)):
            return
        signature = _signature(context)
        if signature != _CACHE["signature"]:
            route, _objects = read_route(context)
            _CACHE.update(signature=signature, geometry=geometry(route), batches=None)
        if _CACHE["batches"] is None:
            _CACHE["batches"] = _build_batches(_CACHE["geometry"])
        _draw_batches(_CACHE["batches"], context.region, settings.ai_lines_on_top)
        _LAST_ERROR[0] = None
    except Exception:  # noqa: BLE001 - see the docstring
        message = traceback.format_exc()
        if message != _LAST_ERROR[0]:
            _LAST_ERROR[0] = message
            print("Track Lab: could not draw the AI lines\n" + message)


def register_overlay():
    global _HANDLE
    if _HANDLE is None:
        _HANDLE = bpy.types.SpaceView3D.draw_handler_add(
            _draw, (), "WINDOW", "POST_VIEW")
    register_labels()


def unregister_overlay():
    global _HANDLE
    if _HANDLE is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_HANDLE, "WINDOW")
        _HANDLE = None
    unregister_labels()
    _CACHE.update(signature=None, geometry=None, batches=None)


def is_drawing() -> bool:
    return _HANDLE is not None


# ---------------------------------------------------------------------------
# Copying a retail track's difficulty
# ---------------------------------------------------------------------------

def retail_races(context):
    tree = prefs.resolve(context)
    if tree is None:
        return []
    return sorted((level for level in tree.levels()
                   if level.race_type == RETAIL_RACE_TYPE),
                  key=lambda level: level.label)


#: Blender hands an enum callback's strings to C without taking a reference; a
#: list built fresh each call can be collected while the menu still uses it.
_ITEMS = {}


class DKR_OT_ai_copy_difficulty(bpy.types.Operator):
    """Copy a retail race's AI difficulty - the behaviour levels and every
    character's start skill - onto this track. The checkpoint sets stay: they
    name this track's own checkpoints"""

    bl_idname = "dkr.ai_copy_difficulty"
    bl_label = "Copy Difficulty From Retail"
    bl_options = {"REGISTER", "UNDO"}

    def _items(self, context):
        found = [(level.name, level.label, "") for level in retail_races(context)]
        _ITEMS["levels"] = found or [(
            "NONE", "no retail races",
            "Set the decomp assets in Preferences > Add-ons to copy from them")]
        return _ITEMS["levels"]

    level: EnumProperty(name="Track", items=_items)

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def draw(self, context):
        self.layout.label(text="Behaviour levels and start skills from:")
        self.layout.prop(self, "level", text="")

    def execute(self, context):
        from . import header as header_ops  # noqa: PLC0415
        from .level_type import _read_header  # noqa: PLC0415

        level = next((entry for entry in retail_races(context)
                      if entry.name == self.level), None)
        if level is None:
            self.report({"ERROR"}, "no retail race to copy from; set the decomp "
                                   "assets in Preferences > Add-ons")
            return {"CANCELLED"}
        found = race_ai.difficulty_from(_read_header(level.header_path) or {})
        if not found:
            self.report({"ERROR"}, "%s's header carries no AI bytes" % level.label)
            return {"CANCELLED"}
        for pointer, value in found.items():
            context.scene[header_ops.key_for(pointer)] = value
        header_ops._redraw(context)
        self.report({"INFO"}, "copied %s's difficulty (%d bytes)"
                    % (level.label, len(found)))
        return {"FINISHED"}


#: How close, in pixels, a click has to land to a checkpoint to pick it.
PICK_RADIUS = 40.0


def checkpoint_set(context, vehicle_type):
    """``(main, alternates)`` of one set, each as ``[(name, index)]``."""
    main, alternates = [], []
    for obj in checkpoint_empties(context):
        if int(obj.get("vehicleType", 0)) != vehicle_type:
            continue
        entry = (obj.name, int(obj.get("index", 0)))
        (alternates if int(obj.get("isAltCheckpoint", 0)) else main).append(entry)
    return main, alternates


def is_checkpoint(obj) -> bool:
    return obj is not None and str(obj.get(scene.PROP_ID, "")) == race_ai.CHECKPOINT


def apply_indices(context, indices):
    """Write ``{name: index}`` onto the checkpoints; how many changed."""
    changed = 0
    for name, index in indices.items():
        obj = context.scene.objects.get(name)
        if obj is not None and int(obj.get("index", -1)) != index:
            obj["index"] = index
            changed += 1
    _redraw(context)
    return changed


class DKR_OT_renumber_checkpoints(bpy.types.Operator):
    """Renumber a checkpoint set by clicking the gates in driving order. Select a
    checkpoint whose number is right first; each checkpoint clicked becomes the
    next number after it, and the ones not clicked yet follow in their order"""

    bl_idname = "dkr.renumber_checkpoints"
    bl_label = "Renumber Checkpoints"
    bl_options = {"REGISTER", "UNDO"}

    #: The checkpoint the numbering starts from; the active object if empty.
    anchor: bpy.props.StringProperty(options={"HIDDEN", "SKIP_SAVE"})
    #: The checkpoints clicked after it, comma separated. Filled in when a click
    #: session ends, so a redo replays it; given up front, no clicking happens.
    sequence: bpy.props.StringProperty(options={"HIDDEN", "SKIP_SAVE"})

    #: The session running, so the label overlay can show it.
    _session = None

    @classmethod
    def poll(cls, context):
        active = context.view_layer.objects.active if context.view_layer else None
        if is_checkpoint(active):
            return True
        cls.poll_message_set("Select the checkpoint whose number is right; "
                             "numbering goes on from it")
        return False

    # -- the session -----------------------------------------------------

    def _begin(self, context):
        """Set up :attr:`chain` from the anchor; an error message, or ``None``."""
        from ..checkpoint_order import Chain  # noqa: PLC0415

        anchor = (context.scene.objects.get(self.anchor) if self.anchor
                  else context.view_layer.objects.active)
        if not is_checkpoint(anchor):
            return "select the checkpoint whose number is right to start from"
        if int(anchor.get("isAltCheckpoint", 0)):
            return ("%s is on the alternate route; start from a main-route "
                    "checkpoint, and the alternates follow it" % anchor.name)
        self.vehicle_type = int(anchor.get("vehicleType", 0))
        main, alternates = checkpoint_set(context, self.vehicle_type)
        self.chain = Chain(main, anchor.name, alternates)
        self.anchor = anchor.name
        return None

    def _take(self, context, name):
        """Number ``name`` next; the report, as ``(level, text)``."""
        from ..checkpoint_order import ChainError  # noqa: PLC0415

        number = self.chain.next_index
        try:
            indices = self.chain.take(name)
        except ChainError as error:
            return "WARNING", "%s: %s" % (name, error)
        apply_indices(context, indices)
        return "INFO", "%s is now %d" % (name, number)

    def execute(self, context):
        problem = self._begin(context)
        if problem:
            self.report({"ERROR"}, problem)
            return {"CANCELLED"}
        names = [n.strip() for n in self.sequence.split(",") if n.strip()]
        for name in names:
            level, text = self._take(context, name)
            if level != "INFO":
                self.report({"ERROR"}, text)
                return {"CANCELLED"}
        apply_indices(context, self.chain.indices())
        self.report({"INFO"}, "renumbered %d checkpoint(s) of set %d from %s (%d)"
                    % (len(self.chain.changed()), self.vehicle_type, self.anchor,
                       self.chain.start[self.anchor]))
        return {"FINISHED"}

    def invoke(self, context, event):
        if self.sequence:
            return self.execute(context)
        window = context.window
        if window is None or not any(area.type == "VIEW_3D" for area in window.screen.areas):
            self.report({"ERROR"}, "renumbering is done by clicking in a 3D viewport")
            return {"CANCELLED"}
        problem = self._begin(context)
        if problem:
            self.report({"ERROR"}, problem)
            return {"CANCELLED"}
        self.hover = None
        DKR_OT_renumber_checkpoints._session = self
        window.cursor_modal_set("EYEDROPPER")
        self._show_status(context)
        context.window_manager.modal_handler_add(self)
        return {"RUNNING_MODAL"}

    def modal(self, context, event):
        if event.type == "MOUSEMOVE":
            hover = self._pick(context, event)
            if hover != self.hover:
                self.hover = hover
                _redraw(context)
            return {"PASS_THROUGH"}
        if event.value != "PRESS":
            return {"PASS_THROUGH"}
        if event.type in {"ESC", "RIGHTMOUSE", "RET", "NUMPAD_ENTER"}:
            return self._finish(context)
        if event.type == "BACK_SPACE" or (
                event.type == "Z" and (event.ctrl or event.oskey) and not event.shift):
            # Blender's undo is not safe to run under a modal operator; taking
            # back the last click is what an author means here anyway.
            if self.chain.taken:
                name = self.chain.taken[-1]
                apply_indices(context, self.chain.back())
                self.report({"INFO"}, "took back %s" % name)
            self._show_status(context)
            return {"RUNNING_MODAL"}
        if event.type != "LEFTMOUSE" or event.alt:
            return {"PASS_THROUGH"}  # navigation, including Alt+click orbit

        from .edit import _view_under  # noqa: PLC0415
        if _view_under(context, event) is None:
            return {"PASS_THROUGH"}  # the sidebar, a header, another editor
        name = self._pick(context, event, any_set=True)
        if name is None:
            self.report({"WARNING"}, "no checkpoint under the mouse")
            return {"RUNNING_MODAL"}
        obj = context.scene.objects.get(name)
        if int(obj.get("vehicleType", 0)) != self.vehicle_type:
            self.report({"WARNING"}, "%s is in set %d; this pass renumbers set %d"
                        % (name, int(obj.get("vehicleType", 0)), self.vehicle_type))
        elif int(obj.get("isAltCheckpoint", 0)):
            self.report({"WARNING"}, "%s is on the alternate route; it follows the "
                        "main checkpoint with its number" % name)
        else:
            level, text = self._take(context, name)
            self.report({level}, text)
        self._show_status(context)
        return {"RUNNING_MODAL"}

    def cancel(self, context):
        """Blender ending the session itself: a file load, a closed window."""
        self._stop(context)

    def _finish(self, context):
        self._stop(context)
        # Recorded so the redo panel, and Repeat Last, replay this pass.
        self.sequence = ",".join(self.chain.taken)
        if not self.chain.taken:
            return {"CANCELLED"}
        self.report({"INFO"}, "renumbered %d checkpoint(s) of set %d from %s (%d)"
                    % (len(self.chain.changed()), self.vehicle_type, self.anchor,
                       self.chain.start[self.anchor]))
        return {"FINISHED"}

    def _stop(self, context):
        if DKR_OT_renumber_checkpoints._session is self:
            DKR_OT_renumber_checkpoints._session = None
        if context.window is not None:
            context.window.cursor_modal_restore()
        if context.workspace is not None:
            context.workspace.status_text_set(None)
        _redraw(context)

    def _show_status(self, context):
        if context.workspace is not None:
            cursor = self.chain.cursor
            context.workspace.status_text_set(
                "Renumbering set %d: click the checkpoint after %s (%d) to make it "
                "%d  ·  Backspace takes back the last  ·  Esc, Enter or right-click "
                "to finish" % (self.vehicle_type, cursor,
                               self.chain.indices()[cursor], self.chain.next_index))
        _redraw(context)

    def _pick(self, context, event, any_set=False):
        """The checkpoint nearest the mouse on screen, if one is close enough."""
        from bpy_extras.view3d_utils import location_3d_to_region_2d  # noqa: PLC0415
        from .edit import _view_under  # noqa: PLC0415

        view = _view_under(context, event)
        if view is None:
            return None
        region, view3d, (x, y) = view
        best, best_distance = None, PICK_RADIUS
        for obj in checkpoint_empties(context):
            if not any_set and int(obj.get("vehicleType", 0)) != self.vehicle_type:
                continue
            point = location_3d_to_region_2d(region, view3d, obj.matrix_world.translation)
            if point is None:
                continue
            distance = ((point.x - x) ** 2 + (point.y - y) ** 2) ** 0.5
            if distance < best_distance:
                best, best_distance = obj.name, distance
        return best


# ---------------------------------------------------------------------------
# Checkpoint numbers in the viewport
# ---------------------------------------------------------------------------

_LABEL_HANDLE = None

LABEL_SIZE = 15
LABEL_PLAIN = (1.0, 1.0, 1.0, 0.95)
LABEL_ALTERNATE = (0.75, 0.75, 0.75, 0.8)
LABEL_DONE = (0.45, 0.95, 0.5, 1.0)
LABEL_CURSOR = (1.0, 0.85, 0.2, 1.0)
LABEL_HOVER = (0.4, 0.75, 1.0, 1.0)


def _draw_labels():
    """Each checkpoint's index over it, so the order can be read off the track.

    Shown with the bot lines, for the set they show; and always while a
    renumbering pass runs, for the set it renumbers, with the gates already
    numbered in green, the one the next click follows in yellow and the one
    under the mouse in blue.
    """
    context = bpy.context
    try:
        session = DKR_OT_renumber_checkpoints._session
        settings = getattr(context.scene, "dkr", None)
        if session is None and (settings is None or not settings.show_ai_lines):
            return
        import blf  # noqa: PLC0415
        from bpy_extras.view3d_utils import location_3d_to_region_2d  # noqa: PLC0415

        region, view3d = context.region, context.region_data
        if region is None or view3d is None:
            return
        shown = session.vehicle_type if session is not None else vehicle_set(context)
        done = set(session.chain.taken) | {session.anchor} if session else set()
        cursor = session.chain.cursor if session else None
        hover = session.hover if session else None

        font = 0
        blf.size(font, LABEL_SIZE)
        blf.enable(font, blf.SHADOW)
        blf.shadow(font, 3, 0.0, 0.0, 0.0, 1.0)
        blf.shadow_offset(font, 1, -1)
        try:
            for obj in checkpoint_empties(context):
                if int(obj.get("vehicleType", 0)) != shown:
                    continue
                point = location_3d_to_region_2d(region, view3d, obj.matrix_world.translation)
                if point is None:
                    continue
                alternate = int(obj.get("isAltCheckpoint", 0))
                text = ("alt %d" if alternate else "%d") % int(obj.get("index", 0))
                colour = LABEL_ALTERNATE if alternate else LABEL_PLAIN
                if obj.name in done:
                    colour = LABEL_DONE
                if obj.name == cursor:
                    colour, text = LABEL_CURSOR, text + "  >"
                if obj.name == hover and obj.name not in done:
                    colour, text = LABEL_HOVER, "%s -> %d" % (text, session.chain.next_index)
                width, height = blf.dimensions(font, text)
                blf.position(font, point.x - width / 2.0, point.y + 12.0, 0.0)
                blf.color(font, *colour)
                blf.draw(font, text)
        finally:
            blf.disable(font, blf.SHADOW)
    except Exception:  # noqa: BLE001 - a draw handler must never raise
        message = traceback.format_exc()
        if message != _LAST_ERROR[0]:
            _LAST_ERROR[0] = message
            print("Track Lab: could not draw the checkpoint numbers\n" + message)


def register_labels():
    global _LABEL_HANDLE
    if _LABEL_HANDLE is None:
        _LABEL_HANDLE = bpy.types.SpaceView3D.draw_handler_add(
            _draw_labels, (), "WINDOW", "POST_PIXEL")


def unregister_labels():
    global _LABEL_HANDLE
    if _LABEL_HANDLE is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_LABEL_HANDLE, "WINDOW")
        _LABEL_HANDLE = None


def _redraw(context):
    for area in context.screen.areas if context.screen else ():
        if area.type in ("VIEW_3D", "PROPERTIES"):
            area.tag_redraw()


CLASSES = (
    DKR_OT_ai_copy_difficulty,
    DKR_OT_renumber_checkpoints,
)
