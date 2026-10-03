"""Placing objects and editing their fields."""

from __future__ import annotations

import bpy
from bpy.props import EnumProperty, StringProperty

from .. import catalog as catalog_module, level_types, prefs, scene
from ..gltf_io import MapObject
from . import snap


def _catalog():
    return catalog_module.load()


def _asset_tree(context):
    """Where to read artwork from, however the author configured it."""
    return prefs.resolve(context)


#: Types whose field must be unique among their peers, and what groups them.
#: A checkpoint index has to be unique within its (vehicleType, isAltCheckpoint)
#: chain - the rule holds across all 51 retail chains - and an AI node's id has
#: to be unique outright. A start position's racerIndex is unique within its
#: entrance, and a missing one starts that racer at the map origin.
UNIQUE_FIELDS = {
    "ASSET_OBJECT_CHECKPOINT": ("index", ("vehicleType", "isAltCheckpoint")),
    "ASSET_OBJECT_AINODE": ("nodeID", ()),
    "ASSET_OBJECT_SETUPPOINT": ("racerIndex", ("entranceID",)),
}


def _assign_free_index(context, object_id, fields):
    """Give a newly placed object an index nothing else is using.

    Without this the first two checkpoints an author places both take index 0
    and the track fails validation for a reason that has nothing to do with what
    they were trying to do.
    """
    spec = UNIQUE_FIELDS.get(object_id)
    if spec is None:
        return
    field, grouped_by = spec
    if field not in fields:
        return

    chain = tuple(fields.get(key) for key in grouped_by)
    taken = set()
    for obj in scene.iter_dkr_objects(context):
        if str(obj.get(scene.PROP_ID)) != object_id:
            continue
        if tuple(obj.get(key) for key in grouped_by) != chain:
            continue
        value = obj.get(field)
        if isinstance(value, int):
            taken.add(value)

    candidate = 0
    while candidate in taken:
        candidate += 1
    fields[field] = candidate


def object_type_items(self, context):
    """Every object type, grouped so the featured ones come first."""
    try:
        catalog = _catalog()
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        return [("NONE", "catalogue unavailable", "")]

    settings = context.scene.dkr if context and context.scene else None
    category = getattr(settings, "category", "ALL")

    types = list(catalog.types.values())
    if category and category not in ("ALL", ""):
        types = [t for t in types if t.category == category]
    types.sort(key=lambda t: (not t.featured, -t.retail_count, t.object_id))

    items = []
    for object_type in types:
        description = "%s | %s | %d in retail tracks" % (
            object_type.object_id, object_type.category, object_type.retail_count
        )
        label = object_type.label
        if object_type.featured:
            label += "  *"
        items.append((object_type.object_id, label, description))
    return _keep("object_type",
                 items or [("NONE", "no types in this category", "")])


#: Appended to every Place button's tooltip.
CLICK_HINT = ("Click on the track to place one per left click, snapping as the "
              "viewport's magnet says (hold Ctrl to flip it); Esc or "
              "right-click stops.")


def _inside(rect, x, y):
    return (rect.x <= x < rect.x + rect.width
            and rect.y <= y < rect.y + rect.height)


def _view_under(context, event):
    """The 3D viewport under the mouse, as ``(region, view, coordinate)``.

    ``None`` over anything else - the sidebar, a header, the toolbar, another
    editor - so those keep working while placing. The sidebar overlaps the
    viewport's own region rather than sitting beside it, so lying inside the
    viewport's rectangle is not enough: nothing else may be on top.
    """
    x, y = event.mouse_x, event.mouse_y
    for area in context.window.screen.areas:
        if area.type != "VIEW_3D" or not _inside(area, x, y):
            continue
        view = None
        for region in area.regions:
            if not _inside(region, x, y):
                continue
            if region.type != "WINDOW":
                return None
            view = region
        if view is None or view.data is None:
            return None
        return view, view.data, (x - view.x, y - view.y)
    return None


def _snap_summary(context, snapping):
    """What a click snaps to, in the viewport's own words."""
    if not snapping:
        return "no snapping (Ctrl snaps)"
    names = {item.identifier: item.name for item in
             bpy.types.ToolSettings.bl_rna.properties["snap_elements"].enum_items}
    chosen = context.scene.tool_settings.snap_elements
    useful = [names[e] for e in names if e in chosen and e not in
              ("FACE", "FACE_PROJECT", "FACE_NEAREST", "VOLUME",
               "EDGE_PERPENDICULAR")]
    return "snapping to %s (Ctrl: off)" % (", ".join(useful) or "the surface")


class DKR_OT_place_object(bpy.types.Operator):
    """Place DKR objects by clicking on the track, one per left click"""

    bl_idname = "dkr.place_object"
    bl_label = "Place DKR Object"
    bl_options = {"REGISTER", "UNDO"}

    object_id: StringProperty(
        name="Object Type",
        description="ASSET_OBJECT_* identifier to place",
    )
    preset: StringProperty(
        name="Preset",
        description="A preset of the type, such as a weapon balloon's colour",
        default="", options={"SKIP_SAVE"},
    )

    #: The placing session running now. Pressing another Place button while
    #: one runs hands it the new type rather than starting a second session on
    #: top, which would swallow the first one's clicks and its Esc.
    _session = None

    @classmethod
    def description(cls, context, properties):
        """Each Place button says what it places, not what placing is."""
        try:
            object_type = _catalog().get(properties.object_id)
        except Exception:  # noqa: BLE001 - a tooltip must not raise
            object_type = None
        if object_type is None:
            return cls.__doc__
        return "%s %s" % (level_types.place_description(
            object_type, level_types.current_key(context.scene.dkr),
            level_types.preset(properties.preset) if properties.preset else None,
        ), CLICK_HINT)

    @classmethod
    def placing(cls):
        """``(object_id, preset)`` the running session places, or ``None``."""
        session = cls._session
        if session is None:
            return None
        try:
            return session.object_id, session.preset
        except ReferenceError:  # freed without being told, e.g. an add-on reload
            cls._session = None
            return None

    def execute(self, context):
        """Scripts and tests: one object at the 3D cursor, no clicking."""
        object_type = self._type(context)
        if object_type is None:
            return {"CANCELLED"}
        self._place(context, object_type, context.scene.cursor.location)
        return {"FINISHED"}

    def invoke(self, context, event):
        object_type = self._type(context)
        if object_type is None:
            return {"CANCELLED"}
        self.object_id = object_type.object_id

        running = DKR_OT_place_object._session
        if running is not None:
            try:
                running.object_id = self.object_id
                running.preset = self.preset
                running._show_status(context)
            except ReferenceError:
                DKR_OT_place_object._session = None
            else:
                return {"CANCELLED"}

        window = context.window
        if window is None or not any(
                area.type == "VIEW_3D" for area in window.screen.areas):
            return self.execute(context)  # nowhere to click

        self._placed = []
        self._snapper = snap.Snapper()
        self._snapping = None
        DKR_OT_place_object._session = self
        window.cursor_modal_set("CROSSHAIR")
        snap.start_marker()
        self._hover(context, event)
        context.window_manager.modal_handler_add(self)
        return {"RUNNING_MODAL"}

    def modal(self, context, event):
        if event.type in {"MOUSEMOVE", "LEFT_CTRL", "RIGHT_CTRL"}:
            # Ctrl flips snapping the moment it goes down, not on the next
            # move, as it does while moving an object.
            self._hover(context, event)
            return {"PASS_THROUGH"}
        if event.value != "PRESS":
            return {"PASS_THROUGH"}
        if event.type in {"ESC", "RIGHTMOUSE", "RET", "NUMPAD_ENTER"}:
            self._stop(context)
            # Each click pushed its own undo step. Finishing would push one
            # more, and offer a redo panel whose re-run places at the cursor.
            return {"CANCELLED"}
        if event.type == "Z" and (event.ctrl or event.oskey) and not event.shift:
            # Blender's undo is not safe to run under a modal operator, and
            # taking back the last click is what an author means here anyway.
            self._remove_last(context)
            return {"RUNNING_MODAL"}
        if event.type != "LEFTMOUSE" or event.alt:
            return {"PASS_THROUGH"}  # navigation, including Alt+click orbit

        if _view_under(context, event) is None:
            return {"PASS_THROUGH"}
        found = self._pick(context, event)
        if found is None:
            self.report({"WARNING"}, "no track under the mouse; nothing placed")
            return {"RUNNING_MODAL"}
        location, _kind = found

        object_type = self._type(context)
        if object_type is None:
            self._stop(context)
            return {"CANCELLED"}
        empty = self._place(context, object_type, location)
        # By name: a Python reference would not survive an undo run from the
        # Edit menu, which a click on the header still reaches.
        self._placed.append(empty.name)
        bpy.ops.ed.undo_push(message="Place %s" % self._label())
        return {"RUNNING_MODAL"}

    def cancel(self, context):
        """Blender ending the session itself: a file load, a closed window."""
        self._stop(context)

    def _pick(self, context, event):
        """``(location, kind)`` a click here would place at, or ``None``."""
        view = _view_under(context, event)
        if view is None:
            return None
        region, view3d, coordinate = view
        return self._snapper.pick(context, region, view3d, coordinate,
                                  self._snaps(context, event))

    @staticmethod
    def _snaps(context, event):
        """The magnet, flipped while Ctrl is held."""
        return context.scene.tool_settings.use_snap != bool(event.ctrl)

    def _hover(self, context, event):
        """Move the marker to where a click would land, and say how it snaps."""
        if snap.show_marker(self._pick(context, event)):
            for area in context.screen.areas if context.screen else ():
                if area.type == "VIEW_3D":
                    area.tag_redraw()
        snapping = self._snaps(context, event)
        if snapping != self._snapping:
            self._snapping = snapping
            self._show_status(context)

    def _stop(self, context):
        if DKR_OT_place_object._session is self:
            DKR_OT_place_object._session = None
        snap.stop_marker()
        if context.window is not None:
            context.window.cursor_modal_restore()
        if context.workspace is not None:
            context.workspace.status_text_set(None)
        for area in context.screen.areas if context.screen else ():
            area.tag_redraw()  # the Place button stops showing pressed

    def _show_status(self, context):
        if context.workspace is not None:
            context.workspace.status_text_set(
                "Placing %s: left-click on the track  ·  %s  ·  Ctrl+Z removes "
                "the last  ·  Esc or right-click to finish"
                % (self._label(), _snap_summary(context, self._snapping)))
        for area in context.screen.areas if context.screen else ():
            area.tag_redraw()

    def _remove_last(self, context):
        while self._placed:
            obj = bpy.data.objects.get(self._placed.pop())
            if obj is None:
                continue  # already deleted by hand
            name = obj.name
            bpy.data.objects.remove(obj, do_unlink=True)
            bpy.ops.ed.undo_push(message="Remove %s" % name)
            self.report({"INFO"}, "removed %s" % name)
            return
        self.report({"INFO"}, "nothing placed this time to remove")

    def _label(self):
        chosen = level_types.preset(self.preset) if self.preset else None
        if chosen is not None and chosen.object_id == self.object_id:
            return chosen.label
        object_type = _catalog().get(self.object_id)
        return object_type.label if object_type else self.object_id

    def _type(self, context):
        object_id = self.object_id or context.scene.dkr.object_type
        object_type = _catalog().get(object_id)
        if object_type is None:
            self.report({"ERROR"}, "unknown object type %r" % object_id)
        return object_type

    def _place(self, context, object_type, location):
        """One object of this type at a world position; the object."""
        catalog = _catalog()
        object_id = object_type.object_id
        fields = object_type.fresh_fields()
        chosen = level_types.preset(self.preset) if self.preset else None
        if chosen is not None and chosen.object_id == object_id:
            fields.update(chosen.fields)
        else:
            chosen = None
        _assign_free_index(context, object_id, fields)

        placed = MapObject(
            object_id=object_id,
            name=object_type.node_name,
            translation=scene.to_map(location),
            fields=fields,
        )

        root = scene.ensure_root(context)
        empty = scene.create_empty(context, placed, catalog, root,
                                   _asset_tree(context),
                                   context.scene.dkr.slot)
        empty["dkr_order"] = scene.next_order(context)
        if chosen is not None:
            # A label for the outliner only; the node name the export writes
            # stays the type's own, in dkr_node_name.
            empty.name = chosen.label.replace(" ", "")

        for obj in context.selected_objects:
            obj.select_set(False)
        empty.select_set(True)
        context.view_layer.objects.active = empty

        shown = chosen.label if chosen else object_type.label
        key = level_types.current_key(context.scene.dkr)
        if empty.type == "EMPTY" and _asset_tree(context) is None:
            self.report(
                {"WARNING"},
                "placed %s as a marker; set the decomp asset path in the addon "
                "preferences to draw objects with their real artwork" % shown,
            )
        elif not level_types.visible(object_type, key):
            self.report({"WARNING"}, "placed %s, which a %s level does not use"
                        % (shown, level_types.label(key)))
        else:
            unique = UNIQUE_FIELDS.get(object_id, (None,))[0]
            index = (" (%s %d)" % (unique, fields[unique])
                     if unique in fields else "")
            self.report({"INFO"}, "placed %s%s in the %s map"
                        % (shown, index, scene.slot_of(empty)))
        return empty


#: Blender's enum callbacks hand their strings to C without taking a reference,
#: so a list built fresh on every call can be collected while the menu is still
#: using it - the documented symptom being a picker that opens empty. Holding the
#: last list returned for each key is what keeps them alive.
_ITEMS = {}


def _keep(key, items):
    """Hold a reference to what an enum callback returned, and return it."""
    _ITEMS[key] = items
    return items


class DKR_OT_set_enum_field(bpy.types.Operator):
    """Pick a value for an enum field from the full set the game defines"""

    bl_idname = "dkr.set_enum_field"
    bl_label = "Set Field"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    field: StringProperty(options={"HIDDEN"})

    def _items(self, context):
        obj = context.active_object
        if obj is None or scene.PROP_ID not in obj:
            return _keep("field", [("NONE", "no object selected", "")])
        catalog = _catalog()
        object_type = catalog.get(str(obj[scene.PROP_ID]))
        if object_type is None:
            return _keep("field", [("NONE", "type not in the catalogue", "")])
        field = object_type.field(self.field)
        if field is None:
            return _keep("field", [("NONE", "no such field", "")])
        members = catalog.enum_members(field)
        seen = set(field.values)
        return _keep("field", [
            (m, m, "used by retail tracks" if m in seen else "")
            for m in members
        ] or [("NONE", "this enum has no members", "")])

    value: EnumProperty(name="Value", items=_items)

    def invoke(self, context, event):
        # A dialog rather than a search popup: it draws the enum as an ordinary
        # dropdown, which types-to-filter the same way and does not depend on
        # the search UI reading the operator's other properties.
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        obj = context.active_object
        if obj is None or not self.field:
            return {"CANCELLED"}
        obj[self.field] = self.value
        # Nudge the UI so the new value shows without the author clicking away.
        obj.update_tag()
        for area in context.screen.areas:
            area.tag_redraw()
        return {"FINISHED"}


class DKR_OT_reset_field(bpy.types.Operator):
    """Put a field back to the value a freshly placed object would have"""

    bl_idname = "dkr.reset_field"
    bl_label = "Reset Field"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    field: StringProperty(options={"HIDDEN"})

    def execute(self, context):
        obj = context.active_object
        if obj is None or scene.PROP_ID not in obj:
            return {"CANCELLED"}
        object_type = _catalog().get(str(obj[scene.PROP_ID]))
        field = object_type.field(self.field) if object_type else None
        if field is None:
            return {"CANCELLED"}
        obj[self.field] = field.fresh()
        for area in context.screen.areas:
            area.tag_redraw()
        return {"FINISHED"}


class DKR_OT_select_by_type(bpy.types.Operator):
    """Select every object of the active object's type"""

    bl_idname = "dkr.select_by_type"
    bl_label = "Select Same Type"
    bl_options = {"REGISTER", "UNDO"}

    object_id: StringProperty(options={"HIDDEN"})

    @classmethod
    def description(cls, context, properties):
        if not properties.object_id:
            return cls.__doc__
        try:
            object_type = _catalog().get(properties.object_id)
        except Exception:  # noqa: BLE001 - a tooltip must not raise
            object_type = None
        name = object_type.label if object_type else properties.object_id
        return "Select every %s in the scene" % name

    def execute(self, context):
        target = self.object_id
        if not target:
            active = context.active_object
            if active is None or scene.PROP_ID not in active:
                self.report({"ERROR"}, "no active DKR object")
                return {"CANCELLED"}
            target = str(active[scene.PROP_ID])

        count = 0
        for obj in scene.iter_dkr_objects(context):
            match = str(obj[scene.PROP_ID]) == target
            obj.select_set(match)
            count += int(match)
        self.report({"INFO"}, "selected %d %s" % (count, target))
        return {"FINISHED"}


class DKR_OT_refresh_artwork(bpy.types.Operator):
    """Redraw every placed object with its sprite or model"""

    bl_idname = "dkr.refresh_artwork"
    bl_label = "Refresh Object Artwork"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        from .. import gltf_io, preview

        prefs.invalidate()
        tree = prefs.resolve(context)
        if tree is None:
            self.report(
                {"ERROR"},
                "no extracted decomp assets found; set the path in the addon "
                "preferences",
            )
            return {"CANCELLED"}

        catalog = _catalog()
        root = scene.ensure_root(context)
        context.view_layer.update()

        # Everything each object is has to be read before any is removed: the
        # slot and the parent live on the object, and reading them afterwards
        # reads an empty scene and rebuilds nothing. A parent that is itself a
        # DKR object is about to be replaced, so only a grid root is kept.
        existing = sorted(scene.iter_dkr_objects(context),
                          key=lambda o: o.get("dkr_order", 1 << 30))
        records = []
        for obj in existing:
            parent = obj.parent
            if parent is not None and scene.is_dkr_object(parent):
                parent = None
            records.append((scene.read_object(obj, catalog), scene.slot_of(obj),
                            parent, obj.matrix_world.copy(), obj.get("dkr_order")))

        # An object's Blender type is fixed at creation, so an Empty cannot grow
        # a mesh. Rebuilding is the only way to give artwork to objects that
        # were placed before the asset path was known.
        for obj in existing:
            bpy.data.objects.remove(obj, do_unlink=True)

        rebuilt = []
        for placed, slot, parent, matrix, order in records:
            obj = scene.create_empty(context, placed, catalog, root, tree, slot)
            if parent is not None:
                obj.parent = parent
            obj.matrix_world = matrix
            if order is not None:
                obj["dkr_order"] = order
            rebuilt.append(obj)

        drawn = sum(1 for o in rebuilt if preview.PROP_PREVIEW in o)
        self.report(
            {"INFO"},
            "redrew %d object(s), %d with artwork from %s"
            % (len(rebuilt), drawn, tree.label),
        )
        return {"FINISHED"}


class DKR_OT_set_slot(bpy.types.Operator):
    """Move the selected objects to the other object map"""

    bl_idname = "dkr.set_slot"
    bl_label = "Object Map"
    bl_options = {"REGISTER", "UNDO"}

    slot: EnumProperty(
        name="Object Map",
        items=[
            ("structure", "Structure", "The track: checkpoints, spawns, scenery"),
            ("collectables", "Collectables", "Pickups: coins, balloons"),
            ("TOGGLE", "Toggle", "Swap each selected object to the other map"),
        ],
        default="TOGGLE",
    )

    def execute(self, context):
        targets = [o for o in context.selected_objects if scene.is_dkr_object(o)]
        if not targets:
            active = context.active_object
            if active is None or not scene.is_dkr_object(active):
                self.report({"ERROR"}, "no DKR object selected")
                return {"CANCELLED"}
            targets = [active]

        for obj in targets:
            if self.slot == "TOGGLE":
                current = scene.slot_of(obj)
                obj[scene.PROP_SLOT] = (
                    scene.SLOT_COLLECTABLES
                    if current == scene.SLOT_STRUCTURE
                    else scene.SLOT_STRUCTURE
                )
            else:
                obj[scene.PROP_SLOT] = self.slot

        for area in context.screen.areas:
            area.tag_redraw()
        self.report({"INFO"}, "moved %d object(s)" % len(targets))
        return {"FINISHED"}


CLASSES = (
    DKR_OT_place_object,
    DKR_OT_set_slot,
    DKR_OT_set_enum_field,
    DKR_OT_reset_field,
    DKR_OT_select_by_type,
    DKR_OT_refresh_artwork,
)
