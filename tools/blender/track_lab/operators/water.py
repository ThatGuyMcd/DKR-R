"""Put water in a track - calm, or with waves - and keep the wave grid honest.

:mod:`..water` has the rules; this is the part an author presses. *Add Water*
lays a flat quad per tile over the area asked for, at the height asked for,
skipping tiles where the ground stands above it, and flags each the way retail
flags its own water. Waves then need the whole track cut into the wave grid,
so the operator does that too and makes the result the track's base, as
*Re-segment Track* does - after it, the track is the author's own file.

Nothing here writes the header. The wave settings are header answers, drawn by
the Water panel through :class:`..props.DKR_WaterSettings`; a remix starts with
its base track's, and a track with no ancestor with the ones most of retail
carries, which are a real wave setting (Haunted Woods ships them).
"""

from __future__ import annotations

import traceback

import bpy
import bmesh
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty

from .. import prefs, scene, textures as texture_catalogue, water
from . import geometry

WAVES = "WAVES"
CALM = "CALM"

#: The rules' own error, which is what an author is shown.
WaterOpError = water.WaterError


def _retail_texture(context, asset_id):
    tree = prefs.resolve(context)
    for entry in texture_catalogue.catalogue(tree):
        if entry.asset_id == asset_id:
            return entry
    return None


def water_texture(context, kind, use_picked):
    """The texture new water draws, and a note if it is not the one asked for."""
    from . import textures as texture_ops  # noqa: PLC0415

    if use_picked:
        picked = texture_ops.picked(context)
        if picked is None:
            raise WaterOpError(
                "no texture is chosen in the Textures panel; pick one, or use "
                "retail's water")
        if kind == WAVES:
            problem = water.wave_texture_problem(picked.width, picked.height,
                                                 picked.format)
            if problem:
                raise WaterOpError(
                    "%s cannot draw waves: %s. Retail's water is 16x16 RGBA32"
                    % (picked.name, problem))
        return picked
    asset = water.DEFAULT_WAVY_TEXTURE if kind == WAVES else water.DEFAULT_CALM_TEXTURE
    entry = _retail_texture(context, asset)
    if entry is None:
        raise WaterOpError(
            "retail's water texture is not in the asset tree (%s); set the "
            "extracted assets in the addon preferences, or use a texture of "
            "your own" % asset)
    return entry


def selected_footprint(obj):
    """``(x1, z1, x2, z2)`` in map space of the selected faces, or ``None``."""
    mesh = obj.data
    matrix = obj.matrix_world
    xs, zs = [], []
    for polygon in mesh.polygons:
        if not polygon.select:
            continue
        for vertex in polygon.vertices:
            point = scene.to_map(matrix @ mesh.vertices[vertex].co)
            xs.append(point[0])
            zs.append(point[2])
    if not xs:
        return None
    return min(xs), min(zs), max(xs), max(zs)


def _kind_items():
    return [
        (WAVES, "Waves", "Water the game simulates: it heaves, and cars bob "
         "on it. Single player only - split screen shows it flat"),
        (CALM, "Calm", "Still water: a flat surface cars sink into, as on "
         "Ancient Lake. Works in every mode"),
    ]


class DKR_OT_add_water(bpy.types.Operator):
    """Lay water over the selected faces, or the whole track, at a height"""

    bl_idname = "dkr.add_water"
    bl_label = "Add Water"
    bl_options = {"REGISTER", "UNDO"}

    kind: EnumProperty(name="Kind", items=_kind_items(), default=WAVES)
    level: FloatProperty(
        name="Water Level",
        description="The height of the surface, in the viewport's units. "
                    "Starts at the 3D cursor",
        default=0.0,
    )
    area: EnumProperty(
        name="Cover",
        items=[
            ("SELECTED", "Selected Faces",
             "The rectangle the selected faces of the track span"),
            ("TRACK", "Whole Track", "Everything the track spans"),
        ],
        default="SELECTED",
    )
    tile: IntProperty(
        name="Tile Size",
        description=(
            "How large one square of water is, in map units. Waves are drawn a "
            "square at a time and the whole track is cut into squares of this "
            "size. 0 picks the smallest that keeps the track under the segment "
            "limit. Retail uses 828 to 2560"
        ),
        default=0, min=0, soft_max=6400,
    )
    skip_dry: BoolProperty(
        name="Skip Dry Ground",
        description="Leave out squares where the ground stands above the water "
                    "everywhere",
        default=True,
    )
    use_picked: BoolProperty(
        name="Use The Chosen Texture",
        description=(
            "Draw the water with the texture chosen in the Textures panel "
            "instead of retail's. Waves need a 16x16 or 32x32 RGBA texture"
        ),
        default=False,
    )

    @classmethod
    def poll(cls, context):
        return bool(geometry.geometry_objects(context))

    def invoke(self, context, event):
        self.level = float(context.scene.cursor.location.z)
        obj = geometry.geometry_objects(context)[0]
        if obj.mode == "EDIT":
            obj.update_from_editmode()
        if not any(polygon.select for polygon in obj.data.polygons):
            self.area = "TRACK"
        return context.window_manager.invoke_props_dialog(self, width=360)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "kind", expand=True)
        layout.prop(self, "level")
        layout.prop(self, "area")
        row = layout.row()
        row.prop(self, "tile")
        row.active = self.kind == WAVES or self.tile > 0
        layout.prop(self, "skip_dry")
        layout.prop(self, "use_picked")
        if self.kind == WAVES:
            box = layout.box()
            box.label(text="The whole track is cut into the", icon="INFO")
            box.label(text="wave grid and becomes its own")
            box.label(text="base file, as Re-segment does.")

    def execute(self, context):
        from . import geometry_export  # noqa: PLC0415

        objects = geometry.geometry_objects(context)
        if not objects:
            self.report({"ERROR"}, "no track geometry in the scene")
            return {"CANCELLED"}
        obj = objects[0]
        if not bpy.data.filepath:
            self.report({"ERROR"},
                        "save the .blend first: adding water rewrites the track "
                        "as its own model file beside it")
            return {"CANCELLED"}
        if obj.mode != "OBJECT":
            try:
                bpy.ops.object.mode_set(mode="OBJECT")
            except RuntimeError:
                pass

        rect = None
        if self.area == "SELECTED":
            rect = selected_footprint(obj)
            if rect is None:
                self.report({"ERROR"},
                            "no faces are selected; select the lake bed in Edit "
                            "Mode, or cover the whole track")
                return {"CANCELLED"}

        try:
            texture = water_texture(context, self.kind, self.use_picked)
            edit = geometry_export.build_edited_model(context)
            if edit is None:
                raise WaterOpError("no track geometry in the scene")
            model = edit.model
            if rect is None:
                bounds = model.bounds
                rect = (bounds[0], bounds[4], bounds[1], bounds[5])
            level = scene.to_map((0.0, 0.0, self.level))[1]
            from . import waterfall
            reserved = waterfall.reserved_indices(context, geometry.texture_table(obj))
            plan = water.lay_water(model, self.kind == WAVES, level, rect,
                                   texture, tile=self.tile,
                                   skip_dry=self.skip_dry, reserved=reserved)
            _rebuilt, target, _stats = geometry.replace_base(context, obj, model)
        except (WaterOpError, geometry_export.GeometryExportError) as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        except OSError as error:
            self.report({"ERROR"}, "could not write the track: %s" % error)
            return {"CANCELLED"}
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "could not add the water: %s" % error)
            return {"CANCELLED"}

        width, depth = plan.layout[0], plan.layout[1]
        skipped = []
        if plan.dry:
            skipped.append("%d dry" % plan.dry)
        if plan.held:
            skipped.append("%d already wet" % plan.held)
        if self.kind == WAVES:
            self.report(
                {"INFO"},
                "added %d square(s) of waves, %dx%d each%s, with %s; the track "
                "is cut into %d segments and is now its own base (%s). Waves "
                "run in single player"
                % (plan.tiles, width, depth,
                   " (skipped %s)" % ", ".join(skipped) if skipped else "",
                   texture.name, plan.segments, bpy.path.basename(target)))
        else:
            self.report(
                {"INFO"},
                "added %d square(s) of calm water, %dx%d each%s, with %s; the "
                "track has %d segments and is now its own base (%s)"
                % (plan.tiles, width, depth,
                   " (skipped %s)" % ", ".join(skipped) if skipped else "",
                   texture.name, plan.segments, bpy.path.basename(target)))
        return {"FINISHED"}


def _water_faces(obj, which):
    mesh = obj.data
    values = [0] * len(mesh.polygons)
    attribute = mesh.attributes.get(geometry.ATTR_FLAGS)
    if attribute is None:
        return []
    attribute.data.foreach_get("value", values)
    found = []
    for index, value in enumerate(values):
        flags = geometry.to_unsigned32(value)
        if not water.is_water(flags):
            continue
        if which == WAVES and not water.is_wavy(flags):
            continue
        if which == CALM and water.is_wavy(flags):
            continue
        found.append(index)
    return found


def _which_items():
    return [("ALL", "All Water", "Every water face")] + _kind_items()


class DKR_OT_select_water(bpy.types.Operator):
    """Select the track's water faces, to move, retexture or delete them"""

    bl_idname = "dkr.select_water"
    bl_label = "Select Water"
    bl_options = {"REGISTER", "UNDO"}

    which: EnumProperty(name="Which", items=_which_items(), default="ALL")

    @classmethod
    def poll(cls, context):
        return bool(geometry.geometry_objects(context))

    def execute(self, context):
        obj = geometry.geometry_objects(context)[0]
        if obj.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        faces = set(_water_faces(obj, self.which))
        for polygon in obj.data.polygons:
            polygon.select = polygon.index in faces
        obj.data.update()
        obj.hide_select = False
        obj.hide_set(False)
        for other in context.selected_objects:
            other.select_set(False)
        obj.select_set(True)
        context.view_layer.objects.active = obj
        try:
            bpy.ops.object.mode_set(mode="EDIT")
            bpy.ops.mesh.select_mode(type="FACE")
        except RuntimeError:
            pass
        self.report({"INFO"}, "%d water face(s) selected" % len(faces))
        return {"FINISHED"}


class DKR_OT_remove_water(bpy.types.Operator):
    """Delete the track's water faces. The export switches their waves off"""

    bl_idname = "dkr.remove_water"
    bl_label = "Remove Water"
    bl_options = {"REGISTER", "UNDO"}

    which: EnumProperty(name="Which", items=_which_items(), default="ALL")

    @classmethod
    def poll(cls, context):
        return bool(geometry.geometry_objects(context))

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event)

    def execute(self, context):
        obj = geometry.geometry_objects(context)[0]
        if obj.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        doomed = set(_water_faces(obj, self.which))
        if not doomed:
            self.report({"WARNING"}, "the track has no such water")
            return {"CANCELLED"}
        mesh = obj.data
        edit = bmesh.new()
        try:
            edit.from_mesh(mesh)
            edit.faces.ensure_lookup_table()
            # FACES also takes the edges and vertices only these faces used, so
            # no vertex is left loose - a loose vertex has no segment to
            # belong to, and the export refuses one.
            bmesh.ops.delete(edit, geom=[edit.faces[i] for i in sorted(doomed)],
                             context="FACES")
            edit.to_mesh(mesh)
        finally:
            edit.free()
        mesh.update()
        self.report({"INFO"},
                    "removed %d water face(s); export or re-segment to see the "
                    "track without them" % len(doomed))
        return {"FINISHED"}


#: Static, so Blender holds the strings for as long as the operator exists.
PRESET_ITEMS = [(key, label, "%s. From %s" % (text, label))
                for key, label, _header, text in water.PRESETS]


class DKR_OT_wave_preset(bpy.types.Operator):
    """Set the waves to look like one of retail's"""

    bl_idname = "dkr.wave_preset"
    bl_label = "Wave Preset"
    bl_options = {"REGISTER", "UNDO"}

    preset: EnumProperty(name="Preset", items=PRESET_ITEMS)

    def execute(self, context):
        from . import header as header_ops  # noqa: PLC0415

        try:
            answers = water.preset_answers(self.preset)
        except water.WaterError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        for pointer, value in answers.items():
            context.scene[header_ops.key_for(pointer)] = value
        label = next((l for k, l, _h, _t in water.PRESETS if k == self.preset),
                     self.preset)
        self.report({"INFO"}, "the waves now move like %s's" % label)
        for area in (context.screen.areas if context.screen else []):
            area.tag_redraw()
        return {"FINISHED"}


CLASSES = (
    DKR_OT_add_water,
    DKR_OT_select_water,
    DKR_OT_remove_water,
    DKR_OT_wave_preset,
)
