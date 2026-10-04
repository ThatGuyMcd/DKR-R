"""Author and inspect waterfalls; the runtime motion is an ordinary TexScroll."""

from __future__ import annotations

import json
from types import SimpleNamespace

import bpy
import bmesh
from bpy.props import BoolProperty, EnumProperty, FloatProperty, StringProperty

from .. import catalog, gltf_io, level_model, scene, texture_scroll as scroll, transparency, validate
from . import geometry, textures


def scroll_objects(context):
    return [o for o in scene.iter_dkr_objects(context) if o.get(scene.PROP_ID) == scroll.OBJECT_ID]


def read_reference(obj):
    try:
        return json.loads(obj[scroll.PROP_ENTRY])
    except (KeyError, ValueError, TypeError):
        raise scroll.ScrollError("invalid texture reference; pick the texture from an active face") from None


def bind(obj, table, index):
    obj[scroll.PROP_ENTRY] = json.dumps(scroll.reference(table, index))
    set_field(obj, "textureIndex", index)


def set_field(obj, name, value):
    obj[name] = value
    absent = scene._load_json(obj, scene.PROP_ABSENT, [])
    if name in absent:
        obj[scene.PROP_ABSENT] = json.dumps([f for f in absent if f != name])


def refresh_rebuilt(context, before, after):
    """Carry intentional surface edits into references when the base rebuilds.

    Rebuilds preserve table order. Only an already valid reference to the same
    image entry may follow its changed surface type; broken links stay broken.
    """
    for obj in scroll_objects(context):
        if scroll.PROP_ENTRY not in obj:
            continue
        try:
            index = resolved_index(obj, before)
        except scroll.ScrollError:
            continue
        if index < len(after) and all(before[index][k] == after[index][k]
                                     for k in scroll.SIGNATURE if k != "surface"):
            bind(obj, after, index)


def bind_imported(context, objects=None):
    meshes = geometry.geometry_objects(context)
    if not meshes or geometry.PROP_BASE_TEXTURES not in meshes[0]:
        return
    table = geometry.texture_table(meshes[0])
    for obj in scroll_objects(context) if objects is None else objects:
        if obj.get(scene.PROP_ID) != scroll.OBJECT_ID or scroll.PROP_ENTRY in obj:
            continue
        index = int(obj.get("textureIndex", -1))
        if 0 <= index < len(table):
            bind(obj, table, index)


def resolved_index(obj, table):
    if scroll.PROP_ENTRY in obj:
        try:
            return scroll.resolve_index(table, read_reference(obj))
        except scroll.ScrollError as error:
            raise scroll.ScrollError('TexScroll "%s" %s' % (obj.name, error)) from error
    index = int(obj.get("textureIndex", -1))
    scroll.reference(table, index)
    return index


def reserved_indices(context, table):
    found = set()
    for obj in scroll_objects(context):
        try:
            found.add(resolved_index(obj, table))
        except scroll.ScrollError:
            # Still protect the raw target of a broken reference.
            found.add(int(obj.get("textureIndex", -1)))
    return found


def resolve_export(context, empties, objects):
    meshes = geometry.geometry_objects(context)
    if not meshes:
        return  # Object-only remixes keep their raw bytes.
    table = geometry.texture_table(meshes[0])
    for empty, obj in zip(empties, objects):
        if obj.object_id == scroll.OBJECT_ID and scroll.PROP_ENTRY in empty:
            obj.fields["textureIndex"] = resolved_index(empty, table)


def face_records(obj, index):
    if obj.mode == "EDIT":
        obj.update_from_editmode()
    mesh = obj.data
    _a, entries = textures._attribute(mesh, geometry.ATTR_TEXTURE)
    _a, uv = textures._attribute(mesh, geometry.ATTR_UV, 2)
    _a, flags = textures._attribute(mesh, geometry.ATTR_TRI_FLAGS)
    if entries is None or uv is None or flags is None:
        raise scroll.ScrollError("track geometry lacks texture or UV attributes; import it again")
    return [{"number": p.index, "flags": flags[p.index],
             "uvs": [(uv[c * 2], uv[c * 2 + 1]) for c in p.loop_indices]}
            for p in mesh.polygons if entries[p.index] == index]


def issues(context):
    meshes = geometry.geometry_objects(context)
    objects = scroll_objects(context)
    if not meshes or not objects:
        return []
    table = geometry.texture_table(meshes[0])
    result, grouped = [], {}
    for obj in objects:
        orders = [int(obj.get("dkr_order", -1))]
        try:
            index = resolved_index(obj, table)
        except scroll.ScrollError as error:
            result.append(validate.Issue(validate.ERROR, str(error), scroll.OBJECT_ID, objects=orders))
            continue
        grouped.setdefault(index, []).append(obj)
        if scroll.PROP_ENTRY not in obj:
            result.append(validate.Issue(validate.WARNING,
                '%s has no texture reference; all faces on entry %d will scroll. Pick From Active Face to link it.'
                % (obj.name, index), scroll.OBJECT_ID, objects=orders))
    for index, controllers in grouped.items():
        entry = table[index]
        texture = textures.texture_by_id(context, entry["id"])
        if texture is None:
            texture = SimpleNamespace(width=entry["w"], height=entry["h"])
        speeds = [(int(o.get("unkA", 0)), int(o.get("unkB", 0))) for o in controllers]
        orders = [int(o.get("dkr_order", -1)) for o in controllers]
        try:
            faces = [f for mesh in meshes for f in face_records(mesh, index)]
            problems = scroll.problems(faces, texture, speeds)
        except scroll.ScrollError as error:
            problems = [(validate.ERROR, str(error))]
        for severity, message in problems:
            result.append(validate.Issue(severity, "%s (entry %d): %s" %
                (controllers[0].name, index, message), scroll.OBJECT_ID, objects=orders))
    return result


def chosen(context, name=""):
    if name:
        obj = context.scene.objects.get(name)
    elif context.active_object is not None and context.active_object.get(scene.PROP_ID) == scroll.OBJECT_ID:
        obj = context.active_object
    else:
        obj = context.scene.objects.get(context.scene.dkr.waterfall_object)
    return obj if obj is not None and obj.get(scene.PROP_ID) == scroll.OBJECT_ID else None


def description(context, obj):
    mesh = textures.target(context)
    if mesh is None:
        return None, int(obj.get("textureIndex", -1))
    index = resolved_index(obj, geometry.texture_table(mesh))
    entry = geometry.texture_table(mesh)[index]
    return textures.texture_by_id(context, entry["id"]), index


def selected_mesh(context):
    obj = textures.target(context)
    if obj is None or context.active_object != obj:
        return None
    if obj.mode == "EDIT":
        edit = bmesh.from_edit_mesh(obj.data)
        return obj if any(f.select for f in edit.faces) else None
    return obj if any(p.select for p in obj.data.polygons) else None


def apply_flags(obj, faces, pass_through, double_sided):
    mesh = obj.data
    batch, values = textures._attribute(mesh, geometry.ATTR_FLAGS)
    tri, tri_values = textures._attribute(mesh, geometry.ATTR_TRI_FLAGS)
    for face in faces:
        value = geometry.to_unsigned32(values[face])
        # Waterfalls are ordinary rendered geometry, never invisible walls or
        # the special horizontal wave surface they might have been made from.
        value &= ~(level_model.RENDER_NO_COLLISION | level_model.RENDER_HIDDEN | transparency.RENDER_WATER)
        if pass_through:
            value |= level_model.RENDER_NO_COLLISION
        values[face] = geometry.to_signed32(value)
        tri_values[face] = (tri_values[face] & ~0xC0) | (0x40 if double_sided else 0)
    batch.data.foreach_set("value", values)
    tri.data.foreach_set("value", tri_values)


PRESET_ITEMS = [(asset, name, "Use the game's %s texture" % name) for asset, name in scroll.PRESETS]
PRESET_ITEMS += [("PICKED", "Use Selected Texture", "Use the texture chosen in the Textures panel, including your own images")]
DIRECTION_ITEMS = [("DOWN", "Down", "Flow down the fall"), ("UP", "Up", "Reverse the flow")]


class DKR_OT_add_waterfall(textures._FaceOperator, bpy.types.Operator):
    """Apply a scrolling texture to the selected faces and create its movement"""

    bl_idname = "dkr.add_waterfall"
    bl_label = "Add Waterfall"
    bl_options = {"REGISTER", "UNDO"}

    texture: EnumProperty(name="Texture", items=PRESET_ITEMS, default=scroll.PRESETS[0][0])
    speed: FloatProperty(name="Speed (texels/s)", default=scroll.texels_per_second(scroll.DEFAULT_SPEED),
                         min=0.0, max=scroll.texels_per_second(127), precision=2)
    direction: EnumProperty(name="Direction", items=DIRECTION_ITEMS, default="DOWN")
    repeats: FloatProperty(name="Repeats", description="Image repetitions over the full height of the selected faces",
                           default=1.0, min=0.01, soft_max=16)
    look: EnumProperty(name="Transparency", items=[
        ("BLEND", "Blended", "Use the texture's alpha for translucency; opaque textures stay opaque"),
        ("CUTOUT", "Cutout", "Cut holes in an otherwise solid surface")], default="BLEND")
    pass_through: BoolProperty(name="Pass-through", description="Vehicles can pass through the waterfall", default=True)
    double_sided: BoolProperty(name="Double-sided", default=True)
    appearance: BoolProperty(name="Appearance", default=False, options={"SKIP_SAVE"})

    @classmethod
    def poll(cls, context):
        if selected_mesh(context) is None:
            cls.poll_message_set("Select waterfall faces in Edit Mode")
            return False
        return True

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self, width=360)

    def picked_texture(self, context):
        if self.texture == "PICKED":
            result = textures.picked(context)
        else:
            from .water import _retail_texture
            result = _retail_texture(context, self.texture)
        if result is None:
            raise scroll.ScrollError("choose a texture in Textures, or configure the extracted game assets for presets")
        return result

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "texture")
        try:
            texture = self.picked_texture(context)
            layout.template_icon(icon_value=textures.icon_for(texture), scale=4)
            layout.label(text=texture.name)
            problem = scroll.axis_problem(texture, 1)
            if problem:
                layout.label(text=problem, icon="ERROR")
        except scroll.ScrollError as error:
            layout.label(text=str(error), icon="INFO")
        for prop in ("speed", "direction", "repeats"):
            layout.prop(self, prop)
        layout.prop(self, "appearance", icon="TRIA_DOWN" if self.appearance else "TRIA_RIGHT", emboss=False)
        if self.appearance:
            for prop in ("look", "pass_through", "double_sided"):
                layout.prop(self, prop)

    def execute(self, context):
        def work(obj, faces):
            try:
                texture = self.picked_texture(context)
                problem = scroll.axis_problem(texture, 1)
                if problem:
                    raise scroll.ScrollError(problem)
                for name in (geometry.ATTR_TEXTURE, geometry.ATTR_FLAGS, geometry.ATTR_TRI_FLAGS,
                             geometry.ATTR_UV, geometry.ATTR_OPAQUE):
                    if name not in obj.data.attributes:
                        raise scroll.ScrollError("track geometry lacks %s; import it again" % name)
                places = {f: [scene.to_map(obj.matrix_world @ obj.data.vertices[v].co)
                              for v in obj.data.polygons[f].vertices] for f in faces}
                across = scroll.horizontal_axis(places[faces[0]])
                points = [p for corners in places.values() for p in corners]
                us = [sum(a * b for a, b in zip(p, across)) for p in points]
                bounds = (min(us), max(us), min(p[1] for p in points), max(p[1] for p in points))
                mapped = {f: scroll.fall_mapping(p, texture, self.repeats, across, bounds)
                          for f, p in places.items()}
                velocity = scroll.speed_from_texels(self.speed) * (1 if self.direction == "DOWN" else -1)
                for axis, speed in ((0, 0), (1, velocity)):
                    problems = scroll.uv_problems(list(mapped.values()), texture, axis, speed)
                    if problems:
                        raise scroll.ScrollError(problems[0])
                object_catalog = catalog.load()
                object_type = object_catalog.get(scroll.OBJECT_ID)
                fields = {f.name: f.default for f in object_type.fields if not f.unused and f.default is not None}
                fields.update(textureIndex=0, unkA=0, unkB=velocity)
                center = [sum(p[i] for p in points) / len(points) for i in range(3)]
                # Stage the mesh on a copy. A refused operation must not leak a
                # texture entry, face flags, or a half-created controller.
                original = obj.data
                extras = geometry.extra_textures(obj)
                obj.data = original.copy()
                controller = None
                try:
                    apply_flags(obj, faces, self.pass_through, self.double_sided)
                    result = textures.apply_texture(obj, faces, texture, int(context.scene.dkr.texture_surface),
                        textures.KEEP, 1.0, self.look, dedicated=True, mapped_uvs=mapped)
                    fields["textureIndex"] = result.index
                    controller = scene.create_empty(context, gltf_io.MapObject(
                        scroll.OBJECT_ID, "Waterfall", center, fields), object_catalog,
                        scene.ensure_root(context), slot=scene.SLOT_STRUCTURE)
                    controller["dkr_order"] = max((int(o.get("dkr_order", -1)) for o in
                        scene.iter_dkr_objects(context)), default=-1) + 1
                    bind(controller, geometry.texture_table(obj), result.index)
                    controller["dkr_scroll_up"] = self.direction == "UP"
                    context.scene.dkr.waterfall_object = controller.name
                except Exception:
                    failed = obj.data
                    obj.data = original
                    bpy.data.meshes.remove(failed)
                    geometry.set_extra_textures(obj, extras)
                    if controller is not None:
                        bpy.data.objects.remove(controller, do_unlink=True)
                    raise
                if original.users == 0:
                    bpy.data.meshes.remove(original)
                if result.settled:
                    self.report({"WARNING"}, "%s uses %s; movement still applies" % (texture.name, result.look.lower()))
                self.report({"INFO"}, "%s: %d faces, %.2f texels/s %s, texture entry %d" %
                    (texture.name, len(faces), abs(scroll.texels_per_second(velocity)), self.direction.lower(), result.index))
                return {"FINISHED"}
            except scroll.ScrollError as error:
                raise textures.TextureError(str(error)) from error
        return self._run(context, work)


class DKR_OT_select_waterfall(bpy.types.Operator):
    """Select all faces moved by this TexScroll"""
    bl_idname = "dkr.select_waterfall"
    bl_label = "Select Faces"
    bl_options = {"REGISTER", "UNDO"}
    object_name: StringProperty(options={"HIDDEN"})

    def execute(self, context):
        controller = chosen(context, self.object_name)
        mesh = textures.target(context)
        if controller is None or mesh is None:
            self.report({"ERROR"}, "choose a waterfall with track geometry")
            return {"CANCELLED"}
        try:
            index = resolved_index(controller, geometry.texture_table(mesh))
            faces = {f["number"] for f in face_records(mesh, index)}
        except scroll.ScrollError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        if context.object and context.object.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        for obj in context.selected_objects:
            obj.select_set(False)
        mesh.hide_select = False
        mesh.hide_set(False)
        mesh.select_set(True)
        context.view_layer.objects.active = mesh
        context.scene.dkr.waterfall_object = controller.name
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_mode(type="FACE")
        bpy.ops.mesh.select_all(action="DESELECT")
        edit = bmesh.from_edit_mesh(mesh.data)
        edit.faces.ensure_lookup_table()
        for number in faces:
            edit.faces[number].select_set(True)
        if faces:
            edit.faces.active = edit.faces[min(faces)]
        bmesh.update_edit_mesh(mesh.data)
        self.report({"INFO"}, "%d waterfall faces selected" % len(faces))
        return {"FINISHED"}


class DKR_OT_remove_waterfall(bpy.types.Operator):
    """Remove the movement; keep the faces, texture and collision settings"""
    bl_idname = "dkr.remove_waterfall"
    bl_label = "Remove Waterfall"
    bl_options = {"REGISTER", "UNDO"}
    object_name: StringProperty(options={"HIDDEN"})

    def execute(self, context):
        obj = chosen(context, self.object_name)
        if obj is None:
            self.report({"ERROR"}, "choose a waterfall first")
            return {"CANCELLED"}
        bpy.data.objects.remove(obj, do_unlink=True)
        context.scene.dkr.waterfall_object = ""
        self.report({"INFO"}, "waterfall movement removed; faces and texture kept")
        return {"FINISHED"}


class DKR_OT_pick_scroll_face(bpy.types.Operator):
    """Link this TexScroll to the active face's texture; every face on that entry will move"""
    bl_idname = "dkr.pick_scroll_face"
    bl_label = "Pick From Active Face"
    bl_options = {"REGISTER", "UNDO"}
    object_name: StringProperty(options={"HIDDEN"})

    @classmethod
    def poll(cls, context):
        return selected_mesh(context) is not None and context.mode == "EDIT_MESH"

    def execute(self, context):
        controller = chosen(context, self.object_name)
        obj = context.active_object
        if controller is None:
            self.report({"ERROR"}, "choose the TexScroll in Waterfalls first")
            return {"CANCELLED"}
        mesh = bmesh.from_edit_mesh(obj.data)
        face = mesh.faces.active
        layer = mesh.faces.layers.int.get(geometry.ATTR_TEXTURE)
        if face is None or layer is None:
            self.report({"ERROR"}, "make a textured face active first")
            return {"CANCELLED"}
        try:
            bind(controller, geometry.texture_table(obj), int(face[layer]))
        except scroll.ScrollError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        context.scene.dkr.waterfall_object = controller.name
        return {"FINISHED"}


def _speed_get(self):
    return abs(scroll.texels_per_second(self.id_data.get("unkB", 0)))


def _speed_set(self, value):
    obj = self.id_data
    up = bool(_direction_get(self))
    obj["dkr_scroll_up"] = up
    set_field(obj, "unkB", scroll.speed_from_texels(-value if up else value))


def _direction_get(self):
    speed = self.id_data.get("unkB", 0)
    return int(speed < 0 or (speed == 0 and self.id_data.get("dkr_scroll_up", False)))


def _direction_set(self, value):
    obj = self.id_data
    obj["dkr_scroll_up"] = value == 1
    maximum = 128 if value == 1 else 127
    set_field(obj, "unkB", min(maximum, abs(int(obj.get("unkB", 0)))) * (-1 if value == 1 else 1))


def _u_get(self):
    return scroll.texels_per_second(self.id_data.get("unkA", 0))


def _u_set(self, value):
    set_field(self.id_data, "unkA", scroll.speed_from_texels(value))


class DKR_ScrollSettings(bpy.types.PropertyGroup):
    speed: FloatProperty(name="Speed (texels/s)", get=_speed_get, set=_speed_set,
                         min=0, max=scroll.texels_per_second(127), precision=2)
    direction: EnumProperty(name="Direction", items=DIRECTION_ITEMS, get=_direction_get, set=_direction_set)
    horizontal: FloatProperty(name="U (texels/s)", get=_u_get, set=_u_set,
                              min=scroll.texels_per_second(-128), max=scroll.texels_per_second(127), precision=2)


CLASSES = (DKR_ScrollSettings, DKR_OT_add_waterfall, DKR_OT_select_waterfall,
           DKR_OT_remove_waterfall, DKR_OT_pick_scroll_face)
