"""The DKR sidebar: Level Type first, everything else after it.

The layout is the one ``tools/blender/ui_mockup.html`` draws. A new scene has no
level type and shows only the *Level Type* card, because nothing can be placed,
checked or exported before the addon knows what the track is; once chosen, the
card shrinks to a dropdown and the rest appears. Every panel but that one starts
collapsed, so the sidebar fits without scrolling.

Blender labels do not wrap, so long text goes through :func:`lines`, which
reflows it against the region's width rather than a fixed column - it tracks
the sidebar as it is resized.
"""

from __future__ import annotations

import textwrap

import bpy

from .. import (catalog as catalog_module, level_types, prefs, race_ai, scene,
                skyboxes, transparency as looks)
from ..operators import geometry as geometry_ops
from ..operators import header as header_ops
from ..operators import level_type as level_type_ops
from ..operators import new_track as new_track_ops
from ..operators import race_ai as race_ai_ops
from ..operators import skybox as skybox_ops
from ..operators import start_grid
from ..operators.edit import DKR_OT_place_object
from ..operators.edit import object_type_items  # noqa: F401 - kept for callers

CATEGORY = "DKR"


class DkrPanel:
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = CATEGORY


class LevelPanel(DkrPanel):
    """A panel that waits for the Level Type: there is nothing to do before it."""

    @classmethod
    def poll(cls, context):
        return context.scene.dkr.level_type != level_types.NONE


def _catalog_or_none():
    try:
        return catalog_module.load()
    except Exception:  # noqa: BLE001 - the panel must still draw
        return None


def _key(context):
    return level_types.current_key(context.scene.dkr)


# ---------------------------------------------------------------------------
# Drawing helpers
# ---------------------------------------------------------------------------

def _wrap_width(context, tight=False, icon=False) -> int:
    """Characters that fit on one label, measured on Blender 5.2's sidebar.

    An estimate on the safe side: a line that is too long is cut with an
    ellipsis, which loses words, while one a little short only wraps early.
    """
    region = context.region
    width = region.width if region is not None else 300
    scale = max(context.preferences.view.ui_scale, 0.5)
    usable = width / scale - 34 - (26 if tight else 0) - (24 if icon else 0)
    return max(20, int(usable / 6.6))


def lines(layout, context, text, icon=None, dim=False, tight=False):
    """A paragraph as several labels, wrapped to the sidebar's width.

    ``tight`` is for text inside a box, which has less room.
    """
    column = layout.column(align=True)
    column.active = not dim
    width = _wrap_width(context, tight, bool(icon))
    for number, line in enumerate(textwrap.wrap(text, width)):
        if icon:
            column.label(text=line, icon=icon if number == 0 else "BLANK1")
        else:
            column.label(text=line)
    return column


def info_box(layout, context, text, icon="INFO", alert=False):
    box = layout.box()
    box.alert = alert
    lines(box, context, text, icon=icon, tight=True)
    return box


def _rule(layout):
    try:
        layout.separator(type="LINE")
    except TypeError:
        layout.separator()


def _dim_label(layout, text, icon="NONE"):
    row = layout.row()
    row.active = False
    row.label(text=text, icon=icon)
    return row


def _labelled(layout, text, factor=0.4):
    """A label on the left and a slot for the widget on the right."""
    split = layout.split(factor=factor, align=True)
    split.alignment = "RIGHT"
    split.label(text=text)
    return split


# ---------------------------------------------------------------------------
# Menus
# ---------------------------------------------------------------------------

class DKR_MT_level_type(bpy.types.Menu):
    bl_idname = "DKR_MT_level_type"
    bl_label = "Level Type"

    def draw(self, context):
        layout = self.layout
        current = context.scene.dkr.level_type
        layout.label(text="Level Type")
        for key in (level_types.RACE, level_types.BOSS, level_types.CHALLENGE,
                    level_types.HUB):
            op = layout.operator("dkr.set_level_type",
                                 text=level_types.FAMILIES[key].label,
                                 icon="LAYER_ACTIVE" if key == current else "BLANK1")
            op.mode = key
        layout.separator()
        layout.label(text="Advanced")
        op = layout.operator("dkr.set_level_type", text="Special",
                             icon="LAYER_ACTIVE" if current == level_types.SPECIAL
                             else "BLANK1")
        op.mode = level_types.SPECIAL


class _SubMenu:
    family = level_types.CHALLENGE

    def draw(self, context):
        current = _key(context)
        for key, text, _help in level_types.SUBTYPES[self.family]:
            icon = "LAYER_ACTIVE" if key == current else (
                "ERROR" if key == level_types.TEST_RACE else "BLANK1")
            op = self.layout.operator("dkr.set_level_type", text=text, icon=icon)
            op.mode = self.family
            op.sub = key


class DKR_MT_challenge(_SubMenu, bpy.types.Menu):
    bl_idname = "DKR_MT_challenge"
    bl_label = "Challenge"
    family = level_types.CHALLENGE


class DKR_MT_special(_SubMenu, bpy.types.Menu):
    bl_idname = "DKR_MT_special"
    bl_label = "Kind"
    family = level_types.SPECIAL


class DKR_MT_default_vehicle(bpy.types.Menu):
    bl_idname = "DKR_MT_default_vehicle"
    bl_label = "Default Vehicle"

    def draw(self, context):
        settings = context.scene.dkr
        allowed = level_types.scene_allowed_vehicles(settings)
        current = level_types.default_vehicle(allowed, settings.default_vehicle)
        for vehicle in allowed:
            op = self.layout.operator(
                "dkr.set_default_vehicle", text=level_types.vehicle_name(vehicle),
                icon="LAYER_ACTIVE" if vehicle == current else "BLANK1",
            )
            op.vehicle = vehicle


# ---------------------------------------------------------------------------
# Level Type
# ---------------------------------------------------------------------------

class DKR_PT_level_type(DkrPanel, bpy.types.Panel):
    """Choose what kind of level this is. The rest of the addon appears once
    you do"""

    bl_label = "Level Type"
    bl_idname = "DKR_PT_level_type"
    bl_order = 0

    def draw_header_preset(self, context):
        key = _key(context)
        if key:
            _dim_label(self.layout, level_types.label(key))

    def draw(self, context):
        if context.scene.dkr.level_type == level_types.NONE:
            self._first_contact(context)
        else:
            self._chosen(context)

    def _first_contact(self, context):
        layout = self.layout
        settings = context.scene.dkr

        found = level_type_ops.suggestion(context)
        if found:
            box = layout.box()
            if found["from"] == "its Level Header":
                text = ("This scene answered a race type in its Level Header "
                        "before Level Type existed. Its objects are safe.")
            else:
                text = ("This scene was imported from %s and saved before "
                        "Level Type existed. Its objects are safe." % found["from"])
            lines(box, context, text, icon="INFO", tight=True)
            label = "Use %s" % level_types.label(found["key"])
            if found["boss"]:
                label += " · %s" % level_types.boss_label(found["boss"])
            row = box.row()
            row.scale_y = 1.2
            row.operator("dkr.use_imported_level_type", text=label, icon="CHECKMARK")

        layout.label(text="What kind of level is this?")
        lines(layout, context, "Everything else follows this choice: what can be "
              "placed, the start grid, validation and the header.", dim=True)

        grid = layout.grid_flow(row_major=True, columns=2, even_columns=True,
                                even_rows=True, align=False)
        for key in (level_types.RACE, level_types.BOSS, level_types.CHALLENGE,
                    level_types.HUB):
            family = level_types.FAMILIES[key]
            cell = grid.column(align=True)
            button = cell.row()
            button.scale_y = 1.8
            button.operator("dkr.set_level_type", text=family.label).mode = key
            blurb = cell.row()
            blurb.alignment = "CENTER"
            blurb.active = False
            blurb.label(text=family.blurb)

        layout.separator()
        layout.prop(settings, "show_special", emboss=False,
                    icon="DOWNARROW_HLT" if settings.show_special else "RIGHTARROW")
        if settings.show_special:
            column = layout.column(align=True)
            for key, text, _help in level_types.SUBTYPES[level_types.SPECIAL]:
                op = column.operator(
                    "dkr.set_level_type", text=text,
                    icon="ERROR" if key == level_types.TEST_RACE else "DOT",
                )
                op.mode = level_types.SPECIAL
                op.sub = key

        _rule(layout)
        split = layout.split(factor=0.18, align=True)
        _dim_label(split, "or")
        split.operator("dkr.import_level", text="Import Retail Track", icon="IMPORT")

    def _chosen(self, context):
        layout = self.layout
        settings = context.scene.dkr
        key = _key(context)

        layout.menu("DKR_MT_level_type",
                    text=level_types.FAMILIES[settings.level_type].label)

        if settings.level_type == level_types.BOSS:
            layout.prop(settings, "boss")
            _dim_label(layout, "%s, as in the retail race" % level_types.vehicle_name(
                level_types.boss_vehicle(settings.boss)), icon="INFO")
        elif settings.level_type == level_types.CHALLENGE:
            _labelled(layout, "Challenge").menu(
                "DKR_MT_challenge", text=dict(
                    (k, t) for k, t, _h in level_types.SUBTYPES[level_types.CHALLENGE]
                )[key])
            lines(layout, context, _sub_help(key), dim=True)
        elif settings.level_type == level_types.SPECIAL:
            _labelled(layout, "Kind").menu(
                "DKR_MT_special", text=dict(
                    (k, t) for k, t, _h in level_types.SUBTYPES[level_types.SPECIAL]
                )[key])
            if key == level_types.TEST_RACE:
                info_box(layout, context, level_types.TEST_RACE_WARNING,
                         icon="ERROR", alert=True)
            else:
                lines(layout, context, _sub_help(key), icon="INFO")

        if level_types.has_vehicles(key):
            layout.separator()
            _draw_vehicles(layout, context, key)

        spawns = level_types.spawn_count(key)
        if spawns or level_types.needs_checkpoints(key):
            layout.separator()
        if spawns:
            _draw_grid_button(layout, context, key)
        if level_types.needs_checkpoints(key):
            layout.prop(settings, "laps")


def _sub_help(key):
    for family in (level_types.CHALLENGE, level_types.SPECIAL):
        for sub, _text, help_text in level_types.SUBTYPES[family]:
            if sub == key:
                return help_text
    return ""


def _draw_vehicles(layout, context, key):
    settings = context.scene.dkr
    allowed = level_types.scene_allowed_vehicles(settings)
    row = _labelled(layout, "Vehicles").row(align=True)
    row.enabled = key != level_types.BOSS
    for vehicle, name in level_types.PLAYER_VEHICLES:
        op = row.operator("dkr.toggle_vehicle", text=name, depress=vehicle in allowed)
        op.vehicle = vehicle
    if len(allowed) > 1:
        _labelled(layout, "Default").menu(
            "DKR_MT_default_vehicle", text=level_types.vehicle_name(
                level_types.default_vehicle(allowed, settings.default_vehicle)))


def _draw_grid_button(layout, context, key):
    hub = key == level_types.HUB
    existing = None if hub else start_grid.root_for(context, 0)
    label = ("Add Entrance Point" if hub else
             "Regenerate Start Grid" if existing else "Generate Start Grid")
    op = layout.operator("dkr.generate_start_grid", text=label, icon="EMPTY_ARROWS")
    if existing is not None:
        op.root_name = existing.name
    for entrance, count in sorted(start_grid.loose_start_positions(context).items()):
        if hub:
            continue
        lines(layout, context,
              "%d loose start position(s) at entrance %d, not in a grid. "
              "Generating at that entrance replaces them." % (count, entrance),
              icon="INFO", dim=True)


# ---------------------------------------------------------------------------
# Track, with Geometry, Textures, Place and Minimap under it
# ---------------------------------------------------------------------------

class DKR_PT_track(LevelPanel, bpy.types.Panel):
    bl_label = "Track"
    bl_idname = "DKR_PT_track"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr

        catalog = _catalog_or_none()
        if catalog is None:
            box = layout.box()
            box.label(text="Catalogue not loaded", icon="ERROR")
            box.label(text="Run tools/blender/generate_catalog.py")
            return

        layout.operator("dkr.import_level", icon="IMPORT", text="Import Track")

        column = layout.column(align=True)
        column.operator("dkr.import_object_map", text="Import Object Map", icon="IMPORT")
        row = column.row(align=True)
        row.operator("dkr.export_object_map", icon="EXPORT")
        row.operator("dkr.export_over_source", text="", icon="FILE_TICK")

        level, _header = level_type_ops.imported_level(context)
        if level is not None:
            layout.label(text="%s (retail)" % level.label, icon="FILE")
        elif settings.source_path:
            layout.label(text=bpy.path.basename(settings.source_path), icon="FILE")

        counts = _counts(context)
        if counts:
            layout.separator()
            layout.label(text="%d objects, %d types" % (sum(counts.values()), len(counts)))
            slots = scene.slot_counts(context)
            layout.label(
                text="structure %d  |  collectables %d"
                % (slots[scene.SLOT_STRUCTURE], slots[scene.SLOT_COLLECTABLES]),
                icon="MOD_BUILD",
            )


class DKR_PT_geometry(DkrPanel, bpy.types.Panel):
    bl_label = "Geometry"
    bl_idname = "DKR_PT_geometry"
    bl_parent_id = "DKR_PT_track"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr

        layout.operator("dkr.import_geometry", icon="MESH_DATA")

        if new_track_ops.convertible(context):
            box = info_box(layout, context,
                           "A mesh of your own is in the scene. Track From Mesh "
                           "turns it into geometry, and the pictures its "
                           "materials draw become the track's own - or start "
                           "from a shipped track's texture table.")
            column = box.column(align=True)
            column.operator("dkr.track_from_mesh_blank", text="Track From Mesh",
                            icon="MESH_MONKEY")
            column.operator("dkr.track_from_mesh",
                            text="Track From Mesh + A Track's Textures",
                            icon="MESH_MONKEY")
        for mesh in new_track_ops.converted_meshes(context):
            box = info_box(layout, context,
                           '"%s" is not offered for conversion: it was already '
                           "turned into a track." % mesh.name)
            box.operator("dkr.make_convertible", icon="LOOP_BACK").object_name = mesh.name

        if not settings.geometry_path:
            box = info_box(layout, context,
                           "Load the track to place objects against and to "
                           "reshape. Without it there is nothing to aim at.")
            _dim_label(box, "levels/models/<world>/*.bin")
            return

        layout.separator()
        layout.label(text=bpy.path.basename(settings.geometry_path), icon="FILE")

        objects = geometry_ops.geometry_objects(context)
        for obj in objects[:1]:
            layout.label(
                text="%d vertices, %d faces"
                % (len(obj.data.vertices), len(obj.data.polygons)),
                icon="MESH_DATA",
            )
            # The game reserves a fixed arena for a level model, and the
            # heaviest retail track already sits at 68% of it, so an author
            # adding geometry needs to see the ceiling before an export rather
            # than meet it as an overflow at load.
            budget = geometry_ops.budget_of(obj)
            if budget is not None:
                fraction, headroom = budget
                layout.label(
                    text="Load budget %d%%, %d tris spare"
                    % (round(fraction * 100.0), headroom),
                    icon="ERROR" if fraction >= 0.9 else "INFO",
                )
            # The quieter ceiling: collision looks at ten segments at a time,
            # so segments stretched across the map hold slots everywhere and
            # the ground a racer stands on stops being considered. It produces
            # no diagnostic in game at all, which is why it is shown here.
            pressure = geometry_ops.collision_pressure(obj)
            if pressure is not None and pressure[0] > pressure[1]:
                info_box(layout, context,
                         "%d oversized segments. Collision sees %d at a time, "
                         "so these crowd it out and racers fall through the "
                         "floor somewhere else." % (pressure[0], pressure[2]),
                         icon="ERROR")

        if any(geometry_ops.WALL_GROUP in o.vertex_groups for o in objects):
            hidden = geometry_ops.walls_hidden(context)
            layout.operator(
                "dkr.toggle_walls",
                text="Show Invisible Walls" if hidden else "Hide Invisible Walls",
                icon="MOD_SOLIDIFY",
            )

        _draw_surface(layout, context, objects)

        column = layout.column(align=True)
        column.operator("dkr.edit_geometry", icon="EDITMODE_HLT")
        column.operator("dkr.check_geometry", icon="CHECKMARK")
        column.operator("dkr.resegment", icon="MOD_EXPLODE")
        column.operator("dkr.drop_to_surface", icon="SNAP_NORMAL")

        info_box(layout, context,
                 "Move vertices freely. The export reloads the shipped model and "
                 "applies only what you changed, so the rest is byte-identical. "
                 "Adding or deleting vertices is a later step, and an export "
                 "says so rather than dropping it.")


class DKR_PT_textures(DkrPanel, bpy.types.Panel):
    """Browse the ROM's 3D textures and put one on the selected faces.

    A separate panel rather than a corner of the geometry one, because it is a
    gallery: fourteen hundred textures is not something that fits beside a
    paragraph, and picking one is the step an author spends time on. Everything
    it needs is in the scene, so it keeps its state across a mode switch into
    Edit Mode - which is where the faces get selected.
    """

    bl_label = "Textures"
    bl_idname = "DKR_PT_textures"
    bl_parent_id = "DKR_PT_track"
    bl_options = {"DEFAULT_CLOSED"}

    @classmethod
    def poll(cls, context):
        return bool(geometry_ops.geometry_objects(context))

    def draw(self, context):
        from .. import textures as texture_catalogue
        from ..operators import textures as texture_ops

        layout = self.layout
        settings = context.scene.dkr

        _draw_own_textures(layout, context, settings, texture_ops)

        tree = prefs.resolve(context)
        entries = texture_catalogue.catalogue(tree)
        if not entries:
            box = info_box(layout, context,
                           "No extracted textures found. A track can also draw "
                           "with any of the ROM's 1401 textures, but the addon "
                           "has to be able to see them first.")
            _dim_label(box, "Set the path in Preferences > Add-ons")
            _draw_chosen_texture(layout, context, settings, texture_ops)
            return

        layout.separator()
        layout.label(text="The ROM's textures", icon="ASSET_MANAGER")
        row = layout.row(align=True)
        row.prop(settings, "texture_group", text="")
        row.prop(settings, "texture_query", text="", icon="VIEWZOOM")

        matches = texture_catalogue.search(
            entries, settings.texture_query, settings.texture_group
        )
        layout.label(text="%d of %d textures" % (len(matches), len(entries)))

        grid = layout.grid_flow(row_major=True, columns=6, align=True)
        for entry in matches[:texture_ops.PAGE]:
            grid.operator(
                "dkr.pick_texture", text="", icon_value=texture_ops.icon_for(entry)
            ).index = entry.index
        if len(matches) > texture_ops.PAGE:
            layout.label(
                text="...and %d more; search to narrow"
                % (len(matches) - texture_ops.PAGE),
                icon="INFO",
            )

        _draw_chosen_texture(layout, context, settings, texture_ops)


def _draw_own_textures(layout, context, settings, texture_ops):
    """The pictures the track ships itself, above the ones the ROM shipped.

    Above rather than below because they are the ones the author put there, and
    because a track can be textured entirely with them - on a machine with no
    extraction at all, where everything under this is an explanation of why the
    gallery is empty.
    """
    from ..operators import custom_textures

    own = custom_textures.entries(context)

    header = layout.row(align=True)
    header.label(text="This track's own artwork", icon="IMAGE_DATA")
    header.operator("dkr.add_custom_texture", text="", icon="ADD")
    if own:
        header.operator("dkr.remove_custom_texture", text="", icon="REMOVE")

    if not own:
        info_box(layout, context,
                 "Add an image and the track ships it: the package carries the "
                 "texture and the runtime adds it to the ROM's table. Colour tops "
                 "out at 64x32, so expect a heavy reduction.")
        return

    # The .blend holds the paths, not the PNGs, so a scene moved without the
    # folder beside it draws and exports nothing for these until they are back.
    lost = custom_textures.missing(context)
    if lost:
        box = info_box(
            layout, context,
            "%d of these images are missing from %s beside the .blend - was it "
            "moved without that folder? Rebuild them from the pictures they "
            "were made from, or put the folder back."
            % (len(lost), custom_textures.FOLDER),
            icon="ERROR", alert=True)
        box.operator("dkr.restore_custom_textures", icon="FILE_REFRESH")

    grid = layout.grid_flow(row_major=True, columns=6, align=True)
    for entry in own:
        grid.operator(
            "dkr.pick_texture", text="", icon_value=texture_ops.icon_for(entry)
        ).index = entry.index

    # The list is the only place the order is visible, and the order is the
    # texture's identity: the runtime hands out ids by position and the level
    # model names them by the same position. Numbered for that reason, and
    # clicking a row picks it, so Remove has something unambiguous to act on.
    column = layout.column(align=True)
    for position, entry in enumerate(own):
        chosen = int(settings.texture_id) == entry.index
        look = texture_ops.LOOK_WORDS.get(entry.transparency, "")
        column.operator(
            "dkr.pick_texture",
            text="%d. %s  %dx%d%s" % (position + 1, entry.name,
                                      entry.width, entry.height,
                                      ", " + look if entry.translucent
                                      or entry.transparency != "OPAQUE" else ""),
            icon="RADIOBUT_ON" if chosen else "RADIOBUT_OFF",
            emboss=chosen,
        ).index = entry.index


def _draw_chosen_texture(layout, context, settings, texture_ops):
    """The picked texture, what it will behave like, and how it gets mapped."""
    chosen = texture_ops.picked(context)
    if chosen is None:
        info_box(layout, context,
                 "Pick a texture above, then select faces in Edit Mode and apply "
                 "it. A custom track is not limited to the textures it was built "
                 "from - any of the ROM's will load.")
        return

    box = layout.box()
    box.template_icon(icon_value=texture_ops.icon_for(chosen), scale=5.0)
    box.label(text=chosen.name, icon="TEXTURE")
    box.label(text="%dx%d, %s%s" % (chosen.width, chosen.height, chosen.group,
                                    ", animated" if chosen.animated else ""))

    # What the game will do with the picture's alpha. For one of the track's
    # own it is the texture's to change; for one of the ROM's it was decided
    # when the ROM was made.
    row = box.row(align=True)
    row.label(text="Made %s" % texture_ops.LOOK_WORDS.get(chosen.transparency,
                                                          chosen.transparency),
              icon="IMAGE_ALPHA" if chosen.translucent
              or chosen.transparency != "OPAQUE" else "IMAGE_RGB")
    if getattr(chosen, "own", False):
        row.operator_menu_enum("dkr.set_texture_transparency", "look",
                               text="Change")

    box.prop(settings, "texture_surface")
    # Beside the transparency but not part of it: the look is what the game
    # does with the picture's alpha, the opacity fades the faces whatever the
    # texture is - the ROM's included. Apply carries both onto the faces.
    box.prop(settings, "texture_opacity", slider=True)
    if settings.texture_opacity < 1.0:
        _dim_label(box, "Every face drawing the texture fades together")
        _dim_label(box, "Faded faces lose their baked light and fog")
        try:
            surface = int(settings.texture_surface)
        except (TypeError, ValueError):
            surface = None
        if looks.fade_stays_in_first_pass(chosen.format, chosen.render_mode,
                                          getattr(chosen, "own", False),
                                          surface):
            warning = box.column(align=True)
            warning.alert = True
            warning.label(text="In game, faded faces over this texture",
                          icon="ERROR")
            warning.label(text="hide what is behind them. Fade one of")
            warning.label(text="your own textures, or calm water")
    box.prop(settings, "texture_transparency")
    box.prop(settings, "texture_mapping", text="")
    if settings.texture_mapping == "PROJECT":
        box.prop(settings, "texture_scale")

    column = box.column(align=True)
    column.operator("dkr.apply_texture", icon="TEXTURE")
    row = column.row(align=True)
    row.operator("dkr.select_by_texture", text="Select", icon="RESTRICT_SELECT_OFF")
    row.operator("dkr.clear_texture", text="Remove", icon="X")
    column.operator("dkr.sync_uvs", icon="UV")
    column.operator_menu_enum("dkr.set_face_transparency", "look",
                              text="Transparency Of Selected",
                              icon="IMAGE_ALPHA")

    obj = texture_ops.target(context)
    if obj is not None:
        added = len(geometry_ops.extra_textures(obj))
        if added:
            layout.label(
                text="%d texture(s) added to this track" % added, icon="PLUS"
            )


def _draw_surface(layout, context, objects):
    """What the active material's ground behaves like.

    Per material rather than per face, because that is where the game keeps it:
    the surface type is a byte on the texture table entry, so every face drawn
    with one entry behaves the same way and nothing finer is expressible. In
    Edit Mode the active slot follows the selection, so picking a face and
    reading this is how an author finds out what they are standing on.
    """
    obj = context.active_object
    if obj is None or obj not in objects:
        return
    material = obj.active_material
    if material is None or geometry_ops.PROP_TEXTURE_INDEX not in material:
        return

    box = layout.box()
    box.label(text="Surface", icon="MATERIAL")
    surface = material.get(geometry_ops.PROP_SURFACE)
    row = box.row(align=True)
    row.label(text="texture %d" % int(material[geometry_ops.PROP_TEXTURE_INDEX]))
    row.operator(
        "dkr.set_surface_type",
        text=geometry_ops.surface_name(surface) if surface is not None else "set",
        icon="DOWNARROW_HLT",
    )


class DKR_PT_water(DkrPanel, bpy.types.Panel):
    """Water the track holds, and how its waves move.

    Waves are a simulation the game runs over a grid of equal squares, not a
    material, so this is where an author asks for them: *Add Water* lays the
    squares and cuts the track to fit, and the settings below are the header
    bytes that shape the simulation.
    """

    bl_label = "Water"
    bl_idname = "DKR_PT_water"
    bl_parent_id = "DKR_PT_track"
    bl_options = {"DEFAULT_CLOSED"}

    @classmethod
    def poll(cls, context):
        return bool(geometry_ops.geometry_objects(context))

    def draw(self, context):
        layout = self.layout
        obj = geometry_ops.geometry_objects(context)[0]
        summary = geometry_ops.water_summary(obj)

        box = layout.box()
        wavy = int(summary.get("wavy", 0))
        calm = int(summary.get("calm", 0))
        if not wavy and not calm:
            box.label(text="No water yet", icon="MOD_OCEAN")
        if wavy:
            tile = summary.get("tile") or [0, 0]
            box.label(text="Waves: %d tile(s), %dx%d"
                      % (int(summary.get("tiles", 0)), tile[0], tile[1]),
                      icon="MOD_OCEAN")
        if calm:
            box.label(text="Calm water: %d face(s)" % calm, icon="MOD_FLUIDSIM")
        for problem in summary.get("problems", [])[:2]:
            info_box(layout, context, problem, icon="ERROR", alert=True)
        for note in summary.get("notes", [])[:2]:
            info_box(layout, context, note, icon="INFO")

        column = layout.column(align=True)
        column.operator("dkr.add_water", icon="ADD")
        row = column.row(align=True)
        row.operator("dkr.select_water", text="Select", icon="RESTRICT_SELECT_OFF")
        row.operator("dkr.remove_water", text="Remove", icon="X")

        if not wavy:
            info_box(layout, context,
                     "Select the lake bed in Edit Mode, put the 3D cursor at the "
                     "water line, and Add Water. Waves cut the whole track into "
                     "equal squares, which is how every retail wave track is "
                     "built.")
            return

        layout.separator()
        header = layout.row(align=True)
        header.label(text="How the waves move", icon="FORCE_HARMONIC")
        header.operator_menu_enum("dkr.wave_preset", "preset", text="Preset")
        waves = context.scene.dkr_water
        column = layout.column(align=True)
        for name in ("power", "subdivisions"):
            column.prop(waves, name)
        row = layout.row(align=True)
        row.prop(waves, "scroll_x")
        row.prop(waves, "scroll_y")
        row = layout.row(align=True)
        row.prop(waves, "scale_x")
        row.prop(waves, "scale_y")
        row = layout.row(align=True)
        row.prop(waves, "translucent")
        row.prop(waves, "double")
        layout.prop(waves, "view")

        settings = context.scene.dkr
        layout.prop(settings, "show_wave_details", icon="PREFERENCES")
        if settings.show_wave_details:
            column = layout.column(align=True)
            for name in ("height0", "step0", "height1", "step1", "seed",
                         "pattern", "shore", "crest"):
                column.prop(waves, name)
        _dim_label(layout, "Waves run in single player; split screen draws "
                           "the water flat.", icon="INFO")


def _draw_scroll(layout, context, obj):
    from .. import texture_scroll
    from ..operators import waterfall, textures as texture_ops
    try:
        texture, index = waterfall.description(context, obj)
        if texture:
            layout.template_icon(icon_value=texture_ops.icon_for(texture), scale=3)
        layout.label(text=texture.name if texture else "Texture entry %d" % index)
        _dim_label(layout, "Entry %d" % index)
    except texture_scroll.ScrollError as error:
        info_box(layout, context, str(error), icon="ERROR", alert=True)
    if texture_scroll.PROP_ENTRY not in obj:
        info_box(layout, context, "No texture reference. Select a face in Edit Mode and pick its texture.")
    layout.prop(obj.dkr_scroll, "speed")
    layout.prop(obj.dkr_scroll, "direction")
    if int(obj.get("unkA", 0)) or context.scene.dkr.show_raw:
        layout.prop(obj.dkr_scroll, "horizontal")
    layout.operator("dkr.pick_scroll_face", icon="EYEDROPPER").object_name = obj.name


class DKR_PT_waterfalls(DkrPanel, bpy.types.Panel):
    bl_label = "Waterfalls"
    bl_idname = "DKR_PT_waterfalls"
    bl_parent_id = "DKR_PT_water"

    def draw(self, context):
        from .. import texture_scroll
        from ..operators import waterfall
        layout = self.layout
        layout.operator("dkr.add_waterfall", icon="ADD")
        objects = waterfall.scroll_objects(context)
        if not objects:
            lines(layout, context, "Select the faces of the fall in Edit Mode, then Add Waterfall.", icon="INFO")
            return
        current = waterfall.chosen(context)
        for obj in objects:
            box = layout.box()
            box.label(text=obj.name, icon="FORCE_TEXTURE")
            try:
                texture, index = waterfall.description(context, obj)
                label = texture.name if texture else "Texture %d" % index
                box.label(text="%s · %.2f texels/s" % (label,
                    texture_scroll.texels_per_second(obj.get("unkB", 0))))
            except texture_scroll.ScrollError as error:
                lines(box, context, str(error), icon="ERROR", tight=True)
            if texture_scroll.PROP_ENTRY not in obj:
                box.label(text="No texture reference", icon="ERROR")
            row = box.row(align=True)
            row.operator("dkr.select_waterfall", text="Select Faces", icon="RESTRICT_SELECT_OFF").object_name = obj.name
            row.operator("dkr.remove_waterfall", text="Remove", icon="X").object_name = obj.name
            # The pick button also works for a broken link, when Select Faces
            # cannot resolve it. The author selects the intended face manually.
            if obj == current or len(objects) == 1:
                _draw_scroll(box, context, obj)
            else:
                box.operator("dkr.pick_scroll_face", icon="EYEDROPPER").object_name = obj.name


def _counts(context):
    counts = {}
    for obj in scene.iter_dkr_objects(context):
        object_id = str(obj.get(scene.PROP_ID, ""))
        counts[object_id] = counts.get(object_id, 0) + 1
    return counts


class DKR_PT_place(DkrPanel, bpy.types.Panel):
    bl_label = "Place"
    bl_idname = "DKR_PT_place"
    bl_parent_id = "DKR_PT_track"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr
        catalog = _catalog_or_none()
        if catalog is None:
            return
        key = _key(context)

        tree = prefs.resolve(context)
        if tree is None:
            box = info_box(layout, context,
                           "No decomp assets found. Objects will be plain "
                           "markers. Set the path in Preferences > Add-ons > "
                           "DKR Track Editor.", icon="ERROR")
            box.operator("dkr.refresh_artwork", icon="FILE_REFRESH")
        else:
            row = layout.row(align=True)
            row.label(text="Artwork: %s" % tree.label, icon="TEXTURE")
            row.operator("dkr.refresh_artwork", text="", icon="FILE_REFRESH")

        # The tab is the object map a placed object joins, and the list: what
        # you see is where it goes.
        tabs = layout.row(align=True)
        tabs.scale_y = 1.2
        tabs.prop(settings, "slot", expand=True)
        layout.prop(settings, "category")
        hidden = level_types.hidden_count(catalog, key, settings.slot)
        layout.prop(settings, "show_incompatible",
                    text="Show incompatible types%s"
                    % (" (%d hidden)" % hidden if hidden else ""))

        placing = DKR_OT_place_object.placing()
        if placing is None:
            _dim_label(layout, "Pick a type, then left-click on the track",
                       icon="RESTRICT_SELECT_OFF")
        else:
            info_box(layout, context, "Placing the pressed type. Left-click on "
                     "the track for each one. The header's magnet snaps it "
                     "and Ctrl flips that; Esc or right-click stops.",
                     icon="REC")
        layout.separator()

        entries = level_types.visible_types(
            catalog, key, settings.slot, settings.category,
            settings.show_incompatible,
        )
        featured = [e for e in entries
                    if e.ok and (e.object_type.featured or e.preset)][:10]
        if featured:
            box = layout.box()
            box.label(text="Common", icon="SOLO_ON")
            grid = box.grid_flow(row_major=True, columns=2, even_columns=True,
                                 align=True)
            for entry in featured:
                _place_button(grid, entry, placing)

        box = layout.box()
        box.label(text="All types (%d)" % len(entries))
        column = box.column(align=True)
        for entry in entries[:60]:
            row = column.row(align=True)
            _place_button(row, entry, placing)
            row.operator("dkr.select_by_type", text="",
                         icon="RESTRICT_SELECT_OFF").object_id = entry.object_type.object_id
        if len(entries) > 60:
            _dim_label(box, "...and %d more; narrow by category" % (len(entries) - 60))
        if not entries:
            _dim_label(box, "Nothing in this tab for this level type")


def _place_button(layout, entry, placing=None):
    if entry.preset is not None:
        icon = entry.preset.icon
    else:
        icon = "NONE" if entry.ok else "ERROR"
    pid = entry.preset.pid if entry.preset else ""
    # Pressed while its placing session runs: the viewport shows nothing of
    # which type the next click will drop.
    op = layout.operator("dkr.place_object", text=entry.label, icon=icon,
                         depress=placing == (entry.object_type.object_id, pid))
    op.object_id = entry.object_type.object_id
    op.preset = pid


class DKR_PT_minimap(DkrPanel, bpy.types.Panel):
    """The track's minimap. Road edges drawn means the track has one; no edges
    means it ships without - there is nothing else to choose"""

    bl_label = "Minimap"
    bl_idname = "DKR_PT_minimap"
    bl_parent_id = "DKR_PT_track"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        from ..operators import minimap as minimap_ops

        layout = self.layout
        settings = context.scene.dkr
        try:
            state = minimap_ops.track_state(context)
        except Exception as error:  # noqa: BLE001 - the panel must still draw
            info_box(layout, context, "The minimap could not be read: %s" % error,
                     icon="ERROR", alert=True)
            return

        if state.kind == minimap_ops.OWN:
            self._own(layout, context, settings, state, minimap_ops)
            return

        box = layout.box()
        if state.kind == minimap_ops.RETAIL:
            lines(box, context, "Using %s's own minimap" % state.label,
                  icon="CHECKMARK", tight=True)
            if state.retail is not None:
                icon = minimap_ops.retail_icon(state.retail,
                                               int(state.base.minimap_colour))
                if icon:
                    box.template_icon(icon_value=icon, scale=6.0)
            layout.operator("dkr.minimap_draw_edge", icon="GREASEPENCIL")
            lines(layout, context, "The track keeps its shape, so the retail "
                  "picture still shows its road. Drawing edges replaces it.",
                  dim=True)
            return
        if state.kind == minimap_ops.PENDING:
            lines(box, context, "Checking whether the track was reshaped...",
                  icon="TIME", tight=True)
        elif state.kind == minimap_ops.CHANGED:
            text = ("Shape changed - draw road edges, or the track ships "
                    "without a minimap")
            if state.error:
                text = "%s. %s" % (text, state.error)
            lines(box, context, text, icon="ERROR", tight=True)
        elif state.kind == minimap_ops.NO_TRACK:
            lines(box, context, "No track geometry to put a minimap on",
                  icon="INFO", tight=True)
        else:
            lines(box, context, "No road edges - the track ships without a "
                  "minimap", icon="GREASEPENCIL", tight=True)

        column = layout.column(align=True)
        column.operator("dkr.minimap_from_ai", icon="TRACKING")
        column.operator("dkr.minimap_draw_edge", icon="GREASEPENCIL")
        lines(layout, context, "Trace the outside border of the road, then the "
              "inside one. Each closed line is an edge; the road is what lies "
              "between.", icon="INFO", dim=True)

    def _own(self, layout, context, settings, state, minimap_ops):
        built = minimap_ops.build(context, state)
        box = layout.box()
        count = len(state.edges)
        box.label(text="%d edge%s" % (count, "" if count == 1 else "s"),
                  icon="GREASEPENCIL")
        for index, gap in state.open[:3]:
            box.label(text="Edge %d is open (gap %d units)" % (index + 1, gap),
                      icon="ERROR")
        if len(state.open) > 3:
            box.label(text="...and %d more" % (len(state.open) - 3), icon="BLANK1")
        if state.open:
            row = box.row()
            row.alignment = "RIGHT"
            row.operator("dkr.minimap_close_edges", text="Close")

        if built is None or built.error:
            info_box(layout, context, built.error if built else
                     "The picture could not be made", icon="ERROR", alert=True)
        else:
            picture = layout.box()
            icon = minimap_ops.preview_icon(built, built.placement.colour)
            if icon:
                picture.template_icon(icon_value=icon, scale=7.0)
            placement = built.placement
            _dim_label(picture, "%dx%d px, turned %d°" % (
                placement.width, placement.height, placement.rotation))
            if built.png_error:
                lines(picture, context, "Your PNG is not used: %s" % built.png_error,
                      icon="ERROR", tight=True)
            elif settings.minimap_png:
                _dim_label(picture, "Picture: %s" % bpy.path.basename(settings.minimap_png),
                           icon="IMAGE_DATA")

        row = layout.row(align=True)
        row.prop(settings, "show_minimap_overlay", toggle=True,
                 icon="HIDE_OFF" if settings.show_minimap_overlay else "HIDE_ON")
        row.operator("dkr.minimap_draw_edge", icon="GREASEPENCIL")
        layout.operator("dkr.minimap_clear", icon="TRASH")


class DKR_PT_minimap_appearance(DkrPanel, bpy.types.Panel):
    bl_label = "Appearance"
    bl_idname = "DKR_PT_minimap_appearance"
    bl_parent_id = "DKR_PT_minimap"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr
        column = layout.column(align=True)
        column.prop(settings, "minimap_size", slider=True)
        _labelled(layout, "Rotation").prop(settings, "minimap_rotation", text="")
        row = _labelled(layout, "Colour").row(align=True)
        row.prop(settings, "minimap_colour", text="")
        row.menu("DKR_MT_minimap_colours", text="", icon="DOWNARROW_HLT")
        layout.prop(settings, "minimap_soft", slider=True)
        layout.prop(settings, "minimap_flag")


class DKR_PT_minimap_numbers(DkrPanel, bpy.types.Panel):
    """The nine header numbers, all calculated. Shown with Show Raw Bytes"""

    bl_label = "Numbers"
    bl_idname = "DKR_PT_minimap_numbers"
    bl_parent_id = "DKR_PT_minimap"
    bl_options = {"DEFAULT_CLOSED"}

    @classmethod
    def poll(cls, context):
        return context.scene.dkr.show_raw

    def draw(self, context):
        from ..operators import minimap as minimap_ops

        layout = self.layout
        settings = context.scene.dkr
        built = minimap_ops.build(context)
        if built is None or built.placement is None:
            _dim_label(layout, "Draw the road's edges to calculate them")
        else:
            placement = built.placement
            column = layout.column(align=True)
            for label, value in (
                ("Scale X / Y", "%.3f / %.3f" % (placement.x_scale, placement.y_scale)),
                ("Rotation", "%d°" % placement.rotation),
                ("Offset Adv 1", "%d, %d" % (placement.offset_x, placement.offset_y)),
                ("Offset Adv 2", "%d, %d  (mirror)" % (placement.offset_x2,
                                                       placement.offset_y2)),
                ("Anchor", "%d, %d" % (placement.anchor_x, placement.anchor_y)),
                ("Colour", "%06x" % placement.colour),
            ):
                split = _labelled(column, label, factor=0.42)
                row = split.row()
                row.enabled = False
                row.label(text=value)
            _dim_label(layout, "All calculated from the edges")
        row = layout.row(align=True)
        row.operator("dkr.minimap_save_png", icon="EXPORT")
        row.operator("dkr.minimap_use_png", icon="IMPORT")
        if settings.minimap_png:
            layout.operator("dkr.minimap_forget_png", icon="LOOP_BACK")


# ---------------------------------------------------------------------------
# DKR Object and Start Grid Root
# ---------------------------------------------------------------------------

class DKR_PT_object(LevelPanel, bpy.types.Panel):
    bl_label = "DKR Object"
    bl_idname = "DKR_PT_object"
    bl_options = {"DEFAULT_CLOSED"}

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return (super().poll(context) and obj is not None
                and scene.PROP_ID in obj)

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr
        obj = context.active_object
        catalog = _catalog_or_none()
        if catalog is None:
            return

        object_id = str(obj[scene.PROP_ID])
        object_type = catalog.get(object_id)
        chosen = level_types.preset_for(object_id, obj)

        header = layout.box()
        header.label(text=chosen.label if chosen else
                     object_type.label if object_type else object_id,
                     icon="EMPTY_DATA")
        _dim_label(header, object_id)
        if object_type and object_type.struct:
            _dim_label(header, object_type.struct)

        if object_type is None:
            layout.label(text="Not in the catalogue; kept verbatim", icon="INFO")
            return

        key = _key(context)
        if not level_types.visible(object_type, key):
            info_box(layout, context,
                     "%s is not used in a %s level; validation will warn."
                     % (object_type.label, level_types.label(key)),
                     icon="ERROR", alert=True)

        row = layout.row(align=True)
        row.operator("dkr.select_by_type", icon="RESTRICT_SELECT_OFF")
        row.prop(settings, "show_raw", text="", icon="THREE_DOTS")

        # Which of the level's two maps this object goes back to. Not inferred:
        # retail puts the same types in both.
        _labelled(layout, "Object map").operator(
            "dkr.set_slot", text=scene.slot_of(obj).title(), icon="MOD_BUILD",
        )

        if scene.is_grid_root(obj.parent):
            info_box(layout, context,
                     "Part of %s. Move or rotate the root to move the whole grid."
                     % obj.parent.name)

        angle_field = object_type.angle_field
        if angle_field is not None:
            note = layout.box()
            note.label(text="Rotate in the viewport to set %s" % angle_field.name,
                       icon="DRIVER_ROTATIONAL_DIFFERENCE")

        from .. import texture_scroll
        is_scroll = object_id == texture_scroll.OBJECT_ID
        if is_scroll:
            _draw_scroll(layout, context, obj)
        column = layout.column()
        drawn = 3 if is_scroll else 0
        for field in object_type.fields:
            if is_scroll and field.name in ("textureIndex", "unkA", "unkB") and not settings.show_raw:
                continue
            if field.unused:
                continue
            if is_hidden_raw(field, settings.show_raw):
                continue
            if angle_field is not None and field is angle_field:
                continue
            _draw_field(column, obj, field, object_type)
            drawn += 1

        if not drawn:
            usable = [f for f in object_type.fields if not f.unused]
            if usable:
                # Every field this type has is a pad or an unidentified byte.
                lines(column, context,
                      "Every field of this type is a raw byte nobody has "
                      "identified. Position is all there is to author.",
                      icon="INFO")
            else:
                column.label(text="This type carries only a position")

        absent = obj.get(scene.PROP_ABSENT)
        if absent:
            layout.label(text="Omitted on import: %s" % absent, icon="INFO")


def is_hidden_raw(field, show_raw: bool) -> bool:
    """Whether a field is a byte to hide rather than something to author.

    A field carrying a label is no longer unidentified, whatever its name still
    looks like. A checkpoint's ``unkB``..``unk16`` are three groups of four -
    lateral offset, vertical offset and route flag, one slot per AI lane - and
    leaving those behind *Show Raw Bytes* buries the part of a checkpoint an
    author would most want to reach. The rule keeps itself up to date: labelling
    a field in the catalogue is what reveals it here, with no edit needed.
    """
    return bool(field.is_raw and field.label == field.name and not show_raw)


def _draw_field(layout, obj, field, object_type):
    """One row per field, with the widget the field's kind calls for.

    Drawn under ``field.label`` and stored under ``field.name``. The two are the
    same for most fields, and deliberately different where the decomp has
    identified what a byte does: the catalogue keeps the name, because it is the
    custom property key an existing ``.blend`` already holds and renaming it
    would drop the author's value in silence, and carries the meaning alongside.
    """
    if field.name not in obj:
        row = layout.row(align=True)
        row.label(text=field.label)
        row.operator("dkr.reset_field", text="Add", icon="ADD").field = field.name
        return

    row = layout.row(align=True)
    if field.kind == "enum":
        row.label(text=field.label)
        row.operator(
            "dkr.set_enum_field", text=str(obj[field.name]), icon="DOWNARROW_HLT"
        ).field = field.name
    else:
        try:
            row.prop(obj, '["%s"]' % field.name, text=field.label)
        except (RuntimeError, TypeError):
            row.label(text="%s: %s" % (field.label, obj[field.name]))
    row.operator("dkr.reset_field", text="", icon="LOOP_BACK").field = field.name


class DKR_PT_grid_root(LevelPanel, bpy.types.Panel):
    bl_label = "Start Grid Root"
    bl_idname = "DKR_PT_grid_root"

    @classmethod
    def poll(cls, context):
        return super().poll(context) and scene.is_grid_root(context.active_object)

    def draw(self, context):
        layout = self.layout
        root = context.active_object
        built = root.get(scene.PROP_GRID_KEY)
        key = _key(context)
        children = start_grid.children_of(root)

        box = layout.box()
        box.label(text=root.name, icon="EMPTY_ARROWS")
        _dim_label(box, "Built for %s · entrance %d" % (
            level_types.label(built), int(root.get(scene.PROP_GRID_ENTRANCE, 0))))
        _dim_label(box, "%d start positions, local +Y is forward" % len(children))

        if built != key and level_types.spawn_count(built) != level_types.spawn_count(key):
            info_box(layout, context,
                     "Built for %s: %d start positions, a %s uses %d."
                     % (level_types.label(built), level_types.spawn_count(built),
                        level_types.label(key), level_types.spawn_count(key)),
                     icon="ERROR", alert=True)

        lines(layout, context,
              "Not exported itself. Move and rotate it to move the whole grid; "
              "angleY of each start position follows its rotation.", icon="INFO")
        layout.separator()
        row = layout.row(align=True)
        row.operator("dkr.select_grid_children",
                     icon="RESTRICT_SELECT_OFF").root_name = root.name
        op = row.operator("dkr.generate_start_grid", text="Regenerate",
                          icon="FILE_REFRESH")
        op.root_name = root.name


# ---------------------------------------------------------------------------
# AI, Validate, Package
# ---------------------------------------------------------------------------

#: Split factor between a row's label and its widgets in the AI Racers grids.
_AI_LABEL = 0.46


def _ai_columns(layout, *titles):
    split = layout.split(factor=_AI_LABEL, align=True)
    split.label(text="")
    row = split.row(align=True)
    for title in titles:
        _dim_label(row, title)
    return split


def _ai_row(layout, label):
    split = layout.split(factor=_AI_LABEL, align=True)
    split.label(text=label)
    return split.row(align=True)


class DKR_PT_race_ai(LevelPanel, bpy.types.Panel):
    """What the computer racers drive, and how hard they race."""

    bl_label = "AI Racers"
    bl_idname = "DKR_PT_race_ai"
    bl_options = {"DEFAULT_CLOSED"}

    @classmethod
    def poll(cls, context):
        return super().poll(context) and level_types.needs_checkpoints(_key(context))

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr

        row = layout.row(align=True)
        row.prop(settings, "show_ai_lines", toggle=True,
                 icon="HIDE_OFF" if settings.show_ai_lines else "HIDE_ON")
        row.prop(settings, "ai_lines_on_top", text="", toggle=True, icon="XRAY")
        layout.row(align=True).prop(settings, "ai_line_vehicle", expand=True)

        try:
            route, _objects = race_ai_ops.read_route(context)
        except Exception:  # noqa: BLE001 - the panel must still draw
            info_box(layout, context, "The checkpoints could not be read.",
                     icon="ERROR", alert=True)
            return

        chosen = race_ai_ops.vehicle_set(context)
        vehicle = level_types.vehicle_name(settings.ai_line_vehicle).lower()
        if not route.main:
            counts = race_ai_ops.set_counts(context)
            text = ("No checkpoint is in set %d, the one %s racers load, so "
                    "their bots drive in circles and nobody's laps count."
                    % (chosen, vehicle))
            if counts:
                text += " This scene's checkpoints are in set %s." % ", ".join(
                    str(s) for s in sorted(counts))
            info_box(layout, context, text, icon="ERROR", alert=True)
        else:
            detour = (" · %d on the alternate route" % len(route.alternate_of)
                      if route.alternate_of else "")
            _dim_label(layout, "Set %d: %d checkpoints%s"
                       % (chosen, len(route.main), detour), icon="CHECKMARK")
            if len(route.main) < 3:
                info_box(layout, context, "With fewer than three checkpoints "
                         "the line folds back on itself.", icon="ERROR")

        if route.duplicates:
            info_box(layout, context,
                     "Index %s is on more than one checkpoint. The game prints "
                     "an error over the race, and which one the bots take is "
                     "down to spawn order." % ", ".join(
                         str(i if i < race_ai.ALTERNATE_OFFSET
                             else "%d (alternate)" % (i - race_ai.ALTERNATE_OFFSET))
                         for i in route.duplicates[:6]),
                     icon="ERROR", alert=True)
        if route.dropped:
            info_box(layout, context,
                     "%d checkpoint(s) past the first %d in this set are never "
                     "loaded: the game stops counting there."
                     % (route.dropped, race_ai.MAX_CHECKPOINTS),
                     icon="ERROR", alert=True)
        if route.unpaired:
            info_box(layout, context,
                     "%d alternate checkpoint(s) name an index no main "
                     "checkpoint has, so no racer ever takes them. Retail "
                     "ships ten of these." % len(route.unpaired))

        if settings.show_ai_lines:
            _dim_label(layout, "Lanes 1-4: %s" % ", ".join(race_ai_ops.LANE_NAMES))
        lines(layout, context,
              "Each bot drives one of the four lanes and changes lane to "
              "overtake. A lane is the game's own spline through the "
              "checkpoints, moved by that lane's offsets on each one; the faint "
              "lines are the alternate route.", dim=True)


class DKR_PT_race_ai_difficulty(DkrPanel, bpy.types.Panel):
    bl_label = "Difficulty"
    bl_idname = "DKR_PT_race_ai_difficulty"
    bl_parent_id = "DKR_PT_race_ai"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        ai = context.scene.dkr_ai
        lines(layout, context,
              "Behaviour level 0 (easiest) to 9 (the Ultimate AI cheat's): how "
              "fast the bots aim to go and how often they boost, attack and "
              "cheat. The game picks the row from where the save stands.",
              dim=True)
        column = layout.column(align=True)
        _ai_columns(column, "Adventure", "Adv. 2")
        for number, (_slot, label, _help) in enumerate(race_ai.AI_LEVEL_SLOTS):
            row = _ai_row(column, label)
            row.prop(ai, "adv1_%d" % number, text="")
            row.prop(ai, "adv2_%d" % number, text="")
        layout.operator("dkr.ai_copy_difficulty", icon="DUPLICATE")


class DKR_PT_race_ai_skill(DkrPanel, bpy.types.Panel):
    bl_label = "Start Skill"
    bl_idname = "DKR_PT_race_ai_skill"
    bl_parent_id = "DKR_PT_race_ai"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        ai = context.scene.dkr_ai
        lines(layout, context,
              "Only the start: Master always gets the start boost, Expert gets "
              "it when the player does, Hard goes on the beep, Medium and Easy "
              "are 4 and 8 frames late. Used in single-player races, boss races "
              "and Taj's challenges; with two or more players each bot draws "
              "Master to Hard instead.", dim=True)
        column = layout.column(align=True)
        _ai_columns(column, "Race", "Trophy")
        for index, character in enumerate(race_ai.CHARACTERS):
            row = _ai_row(column, character)
            row.prop(ai, "skill_%d" % index, text="")
            row.prop(ai, "trophy_%d" % index, text="")
        _dim_label(layout, "0 Master · 1 Expert · 2 Hard · 3 Medium · 4 Easy")


class DKR_PT_race_ai_sets(DkrPanel, bpy.types.Panel):
    bl_label = "Checkpoint Sets"
    bl_idname = "DKR_PT_race_ai_sets"
    bl_parent_id = "DKR_PT_race_ai"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr
        ai = context.scene.dkr_ai
        lines(layout, context,
              "Each vehicle's racers load only the checkpoints whose "
              "vehicleType is its set, which is how a track gives the plane a "
              "route of its own.", dim=True)
        counts = race_ai_ops.set_counts(context)
        allowed = level_types.scene_allowed_vehicles(settings)
        stranded = []
        column = layout.column(align=True)
        for index, (vehicle, label) in enumerate(level_types.PLAYER_VEHICLES):
            value = getattr(ai, "set_%d" % index)
            row = _ai_row(column, label)
            row.active = vehicle in allowed
            row.prop(ai, "set_%d" % index, text="")
            _dim_label(row, "%d checkpoints" % counts.get(value, 0))
            if vehicle in allowed and counts and not counts.get(value):
                stranded.append(label)
        if stranded:
            info_box(layout, context,
                     "%s racers load a set with no checkpoints: their bots "
                     "drive in circles and nobody's laps count."
                     % " and ".join(stranded), icon="ERROR", alert=True)


class DKR_PT_ai(LevelPanel, bpy.types.Panel):
    bl_label = "AI Node Graph"
    bl_idname = "DKR_PT_ai"
    bl_options = {"DEFAULT_CLOSED"}

    def draw_header_preset(self, context):
        self.layout.label(text="in development", icon="EXPERIMENTAL")

    def draw(self, context):
        layout = self.layout
        lines(layout, context,
              "Battle and banana arenas steer their bots along this graph. Race "
              "bots follow the checkpoints instead - see AI Racers. The arena "
              "tools stay off until the arena AI has been studied.",
              icon="INFO")
        column = layout.column(align=True)
        column.enabled = False
        column.operator("dkr.ai_from_curve", text="AI Line From Curve",
                        icon="CURVE_PATH")
        column.operator("dkr.ai_add_branch", text="Add Branch",
                        icon="CURVE_BEZCIRCLE")


class DKR_PT_validate(LevelPanel, bpy.types.Panel):
    bl_label = "Validate"
    bl_idname = "DKR_PT_validate"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr

        layout.operator("dkr.validate", icon="CHECKMARK")

        if not settings.has_validated:
            return
        if not len(settings.results):
            layout.label(text="No problems found", icon="CHECKMARK")
            return

        titles = {"error": ("Error", "CANCEL"), "warning": ("Warning", "ERROR"),
                  "info": ("Info", "INFO")}
        for group in ("error", "warning", "info"):
            entries = [r for r in settings.results if r.severity == group]
            if not entries:
                continue
            title, icon = titles[group]
            box = layout.box()
            box.alert = group == "error"
            box.label(text="%s (%d)" % (title, len(entries)), icon=icon)
            column = box.column(align=True)
            for entry in entries[:12]:
                lines(column, context, entry.message, tight=True)
                if entry.objects:
                    count = len(entry.objects.split(","))
                    column.operator(
                        "dkr.select_issue",
                        text="Select the %d object(s)" % count,
                        icon="RESTRICT_SELECT_OFF",
                    ).objects = entry.objects
                column.separator()
            if len(entries) > 12:
                column.label(text="...and %d more" % (len(entries) - 12))


class DKR_PT_export(LevelPanel, bpy.types.Panel):
    bl_label = "Package"
    bl_idname = "DKR_PT_export"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout
        settings = context.scene.dkr

        column = layout.column()
        column.prop(settings, "track_name")
        column.prop(settings, "track_id")
        column.prop(settings, "track_author")

        # The id keys a track for arming and sibling resolution, so two tracks
        # sharing one collide. Show what will actually be written, since a
        # stale id set once persists across exports and is otherwise invisible.
        from .. import dkrmap
        if settings.track_id:
            resolved = settings.track_id
            source = "set explicitly"
        else:
            resolved = dkrmap.normalise_id(settings.track_name) or "<from folder>"
            source = "from the name" if settings.track_name else "from the folder"
        row = layout.row()
        row.label(text="id: %s" % resolved, icon="COPY_ID")
        _dim_label(row, source)

        layout.separator()
        layout.operator("dkr.export_dkrmap", icon="PACKAGE")

        slots = scene.slot_counts(context)
        box = layout.box()
        box.label(text="Writes header and both maps:", icon="INFO")
        box.label(text="structure %d, collectables %d"
                  % (slots[scene.SLOT_STRUCTURE], slots[scene.SLOT_COLLECTABLES]))
        if not slots[scene.SLOT_COLLECTABLES]:
            lines(box, context, "An empty slot ships an empty map, not nothing: "
                  "zero in the header means map 0, not none.", dim=True,
                  tight=True)


class DKR_PT_header(DkrPanel, bpy.types.Panel):
    """The level header, for a track with no ancestor to inherit one from."""

    bl_label = "Level Header"
    bl_idname = "DKR_PT_header"
    bl_parent_id = "DKR_PT_export"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        from .. import level_header_template as template

        layout = self.layout
        settings = context.scene.dkr
        key = _key(context)
        catalog = _catalog_or_none()

        level, _header = level_type_ops.imported_level(context)
        if level is not None:
            info_box(layout, context,
                     "This track inherits its header from %s, so the fields "
                     "below are unused - except the ones set by Level Type, "
                     "the world, the music, the skybox and the AI Racers settings, which "
                     "the export lays over it." % level.label)

        layout.operator("dkr.header_defaults", icon="LOOP_BACK")

        outstanding = header_ops.unanswered(context)
        if outstanding:
            names = [header_ops.choice_for(p).label if header_ops.choice_for(p)
                     else p for p in outstanding]
            info_box(layout, context,
                     "%d field(s) unanswered (%s). Zero is a real world, so a "
                     "track that never answered would quietly become one."
                     % (len(outstanding), ", ".join(names)),
                     icon="CANCEL", alert=True)

        column = layout.column(align=True)
        for choice in template.CHOICES:
            if choice.pointer in header_ops.LEVEL_OWNED or \
                    choice.pointer in (header_ops.MUSIC, header_ops.SKYBOX):
                continue
            _draw_header_choice(column, context, choice, header_ops)
            if choice.pointer == "/world":
                row = column.row(align=True)
                row.label(text="Vehicle override")
                row.prop(settings, "vehicle_override", text="")

        layout.separator()
        _dim_label(layout, "Set by Level Type")
        locked = layout.column(align=True)
        race_type = level_types.race_type(key) if key else "not chosen"
        value = (catalog.raw.get("enumValues", {}).get("RaceType", {}).get(race_type)
                 if catalog else None)
        _locked(locked, "Race type", "%s (%s)" % (race_type, value)
                if value is not None else race_type)
        if settings.level_type == level_types.BOSS:
            _locked(locked, "Boss", settings.boss)
        if level_types.has_vehicles(key):
            allowed = level_types.scene_allowed_vehicles(settings)
            _locked(locked, "Vehicles",
                    " + ".join(level_types.vehicle_name(v) for v in allowed))
            _locked(locked, "Default vehicle", level_types.vehicle_name(
                level_types.default_vehicle(allowed, settings.default_vehicle)))
        if level_types.needs_checkpoints(key):
            _locked(locked, "Laps", str(settings.laps))

        layout.separator()
        _draw_music(layout, context, catalog)
        _draw_skybox(layout, context)


def _locked(layout, text, value):
    split = _labelled(layout, text, factor=0.33)
    row = split.row()
    row.enabled = False
    row.label(text=value, icon="LOCKED")


def _draw_music(layout, context, catalog):
    tracks = level_types.music_tracks(catalog)
    index = header_ops.music_index(context)
    name = (level_types.music_label(tracks[index]) if 0 <= index < len(tracks)
            else "Track %d" % index)
    box = layout.box()
    row = box.row()
    row.label(text="Music", icon="SOUND")
    _dim_label(row, "%s · %d / %d" % (name, index + 1, len(tracks) or 256))
    row = box.row(align=True)
    row.operator("dkr.step_music", text="", icon="PREV_KEYFRAME").step = -1
    row.operator("dkr.play_music", text="Play", icon="PLAY")
    row.operator("dkr.step_music", text="", icon="NEXT_KEYFRAME").step = 1
    lines(box, context, "The track showing when you export is the one the "
          "header gets. Listening is not available yet.", dim=True, tight=True)


def _draw_skybox(layout, context):
    """The chosen sky large, then every dome to pick from, as the game shows it.

    The thumbnails are panoramas of each dome seen from its centre, made a few
    at a time after the panel first opens; a tile shows an icon until its
    picture is ready.
    """
    tree = prefs.resolve(context)
    domes = skyboxes.catalogue(tree)
    current = skybox_ops.chosen(context)
    chosen = next((s for s in domes if s.asset_id == current), None)

    box = layout.box()
    row = box.row()
    row.label(text="Skybox", icon="WORLD")
    _dim_label(row, chosen.label if chosen else (current or "none"))

    if not domes:
        lines(box, context, "The skyboxes are read from the decomp assets. Set "
              "the path in Preferences > Add-ons to see and pick them here.",
              icon="INFO", dim=True, tight=True)
        choice = header_ops.choice_for(header_ops.SKYBOX)
        if choice is not None:
            _draw_header_choice(box.column(align=True), context, choice, header_ops)
        return

    if chosen is not None:
        icon = skybox_ops.icon_for(tree, chosen)
        if icon:
            box.template_icon(icon_value=icon, scale=6.0)
        if chosen.used_by:
            shown = ", ".join(chosen.used_by[:3])
            if len(chosen.used_by) > 3:
                shown += " and %d more" % (len(chosen.used_by) - 3)
            lines(box, context, "Used by %s" % shown, dim=True, tight=True)
        else:
            _dim_label(box, "No retail track uses it")
    else:
        lines(box, context, "No skybox: the game draws a plain background. "
              "Pick one below.", dim=True, tight=True)

    grid = box.grid_flow(row_major=True, columns=3, even_columns=True, align=False)
    for sky in domes:
        cell = grid.column(align=True)
        icon = skybox_ops.icon_for(tree, sky)
        if icon:
            cell.template_icon(icon_value=icon, scale=2.5)
        else:
            cell.label(text="", icon="WORLD")
        op = cell.operator("dkr.pick_skybox", text=sky.label,
                           depress=sky.asset_id == current)
        op.asset_id = sky.asset_id

    shown = skybox_ops.preview_object(context) is not None
    box.operator("dkr.show_skybox",
                 text="Hide From Viewport" if shown else "Show In Viewport",
                 icon="HIDE_ON" if shown else "HIDE_OFF")


def _draw_header_choice(layout, context, choice, header_ops):
    """One row per header field, with the widget its kind calls for.

    Everything shown here is read from the template's own descriptors - the
    label, the kind, the enum it draws from, the range - so a field added or
    changed on that side appears with no edit here. Hard-coding the pointers
    would have drifted the first time the template moved, with nothing to catch
    it.
    """
    key = header_ops.key_for(choice.pointer)
    answered = key in context.scene

    if choice.kind == "bitfield":
        box = layout.box()
        row = box.row(align=True)
        row.label(text=choice.label)
        row.operator(
            "dkr.set_header_choice", text="", icon="ADD"
        ).pointer = choice.pointer
        for member in (list(context.scene[key]) if answered else []):
            entry = box.row(align=True)
            entry.label(text=str(member), icon="DOT")
            drop = entry.operator("dkr.clear_header_choice", text="", icon="X")
            drop.pointer = choice.pointer
            drop.member = str(member)
        return

    row = layout.row(align=True)
    if choice.kind in ("enum", "asset"):
        row.label(text=choice.label)
        shown = str(context.scene[key]) if answered else "not set"
        row.operator(
            "dkr.set_header_choice", text=shown, icon="DOWNARROW_HLT"
        ).pointer = choice.pointer
    elif answered:
        try:
            row.prop(context.scene, '["%s"]' % key, text=choice.label)
        except (RuntimeError, TypeError):
            row.label(text="%s: %s" % (choice.label, context.scene[key]))
    else:
        row.label(text=choice.label)
        _dim_label(row, "default")

    if answered:
        row.operator(
            "dkr.clear_header_choice", text="", icon="X"
        ).pointer = choice.pointer


def _wrap(text, width):
    """Blender labels do not wrap, so break the message onto several."""
    return textwrap.wrap(text, width)


CLASSES = (
    DKR_MT_level_type,
    DKR_MT_challenge,
    DKR_MT_special,
    DKR_MT_default_vehicle,
    DKR_PT_level_type,
    DKR_PT_track,
    DKR_PT_geometry,
    DKR_PT_textures,
    DKR_PT_water,
    DKR_PT_waterfalls,
    DKR_PT_place,
    DKR_PT_minimap,
    DKR_PT_minimap_appearance,
    DKR_PT_minimap_numbers,
    DKR_PT_object,
    DKR_PT_grid_root,
    DKR_PT_race_ai,
    DKR_PT_race_ai_difficulty,
    DKR_PT_race_ai_skill,
    DKR_PT_race_ai_sets,
    DKR_PT_ai,
    DKR_PT_validate,
    DKR_PT_export,
    DKR_PT_header,
)
