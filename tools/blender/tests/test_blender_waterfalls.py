"""Waterfalls from faces through Blender editing, rebuilds and package export."""

import json
import os
import sys
import tempfile
import traceback

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [os.path.dirname(HERE), HERE]
import track_lab
from track_lab import assets, catalog, gltf_io, level_model, scene, texture_scroll as scroll
from track_lab.operators import custom_textures, geometry, geometry_export, textures, waterfall
from track_lab.ui import panels
from test_blender_operators import fresh, _draw_panel, _write_alpha_image, _refused, _select_faces


def activate(obj):
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for other in bpy.context.selected_objects:
        other.select_set(False)
    obj.hide_select = False
    obj.hide_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def make_track(directory):
    fresh()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(directory, "waterfalls.blend"))
    mesh = bpy.data.meshes.new("fall sheets")
    mesh.from_pydata([(0, 0, 0), (256, 0, 0), (256, 0, 256), (0, 0, 256),
                      (512, 0, 0), (768, 0, 0), (768, 0, 256), (512, 0, 256),
                      (-256, -256, -64), (1024, -256, -64), (1024, 256, -64), (-256, 256, -64)],
                     [], [(0, 1, 2, 3), (4, 5, 6, 7), (8, 9, 10, 11)])
    obj = bpy.data.objects.new("Falls", mesh)
    bpy.context.scene.collection.objects.link(obj)
    activate(obj)
    assert bpy.ops.dkr.track_from_mesh_blank(keep_source=False) == {"FINISHED"}
    obj = geometry.geometry_objects(bpy.context)[0]
    activate(obj)
    return obj


def run(directory):
    obj = make_track(directory)
    first = [p.index for p in obj.data.polygons if p.center.z > -1 and p.center.x < 300]
    second = [p.index for p in obj.data.polygons if p.center.z > -1 and p.center.x > 400]
    assert len(first) == 2 and len(second) == 2
    _select_faces(obj.data, first)
    for f in first:
        obj.data.attributes[geometry.ATTR_TRI_FLAGS].data[f].value |= 0x80
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_mode(type="FACE")
    bpy.ops.mesh.select_all(action="DESELECT")
    import bmesh
    edit_mesh = bmesh.from_edit_mesh(obj.data)
    edit_mesh.faces.ensure_lookup_table()
    for f in first:
        edit_mesh.faces[f].select_set(True)
    bmesh.update_edit_mesh(obj.data)
    assert bpy.ops.dkr.add_waterfall(repeats=3) == {"FINISHED"}
    assert obj.mode == "EDIT"
    bpy.ops.object.mode_set(mode="OBJECT")
    controller = waterfall.scroll_objects(bpy.context)[0]
    index = controller["textureIndex"]
    table = geometry.texture_table(obj)
    assert scroll.resolve_index(table, waterfall.read_reference(controller)) == index
    assert controller["unkB"] == 95 and controller["unkA"] == 0
    assert scene.slot_of(controller) == "structure"
    for f in first:
        assert obj.data.attributes[geometry.ATTR_TRI_FLAGS].data[f].value & 0xC0 == 0x40
        assert obj.data.attributes[geometry.ATTR_FLAGS].data[f].value & level_model.RENDER_NO_COLLISION
    assert not [i for i in waterfall.issues(bpy.context) if i.severity == "error"]
    assert ("operator", "dkr.add_waterfall") in _draw_panel(panels.DKR_PT_waterfalls)
    activate(controller)
    assert ("prop", "speed") in _draw_panel(panels.DKR_PT_object)
    controller.dkr_scroll.speed = 30.0
    controller.dkr_scroll.direction = "UP"
    assert controller["unkB"] == -64
    controller.dkr_scroll.direction = "DOWN"
    controller.dkr_scroll.speed = 0
    controller.dkr_scroll.direction = "UP"
    controller.dkr_scroll.speed = 30
    assert controller["unkB"] == -64
    controller.dkr_scroll.direction = "DOWN"

    activate(obj)
    texture = textures.texture_by_id(bpy.context, table[index]["id"])
    _select_faces(obj.data, second)
    settings = bpy.context.scene.dkr
    settings.texture_id = texture.index
    settings.texture_mapping = "PROJECT"
    assert bpy.ops.dkr.apply_texture() == {"FINISHED"}
    ordinary = obj.data.attributes[geometry.ATTR_TEXTURE].data[second[0]].value
    assert ordinary != index
    before = (json.dumps(geometry.texture_table(obj)), len(waterfall.scroll_objects(bpy.context)))
    assert _refused(lambda: bpy.ops.dkr.add_waterfall(repeats=1000))
    assert before == (json.dumps(geometry.texture_table(obj)), len(waterfall.scroll_objects(bpy.context)))

    assert bpy.ops.dkr.add_waterfall(texture="PICKED", speed=15, direction="UP",
                                    pass_through=False, double_sided=False, look="CUTOUT") == {"FINISHED"}
    second_controller = waterfall.scroll_objects(bpy.context)[-1]
    assert second_controller["textureIndex"] not in (ordinary, index)
    for f in second:
        assert not obj.data.attributes[geometry.ATTR_TRI_FLAGS].data[f].value & 0xC0
        assert not obj.data.attributes[geometry.ATTR_FLAGS].data[f].value & level_model.RENDER_NO_COLLISION
    # Exported geometry and object fields must name the same entry.
    edit = geometry_export.build_edited_model(bpy.context)
    assert edit.model.textures[index].texture_id == texture.index
    exported = scene.export_object_map(bpy.context).by_id(scroll.OBJECT_ID)
    assert {o.fields["textureIndex"] for o in exported} == {index, second_controller["textureIndex"]}
    for material in obj.data.materials:
        if material.get(geometry.PROP_TEXTURE_INDEX) in (index, second_controller["textureIndex"]):
            material[geometry.PROP_SURFACE] = 1
    assert bpy.ops.dkr.resegment() == {"FINISHED"}
    obj = geometry.geometry_objects(bpy.context)[0]
    assert waterfall.resolved_index(controller, geometry.texture_table(obj)) == index
    assert waterfall.read_reference(controller)["surface"] == 1
    assert index in waterfall.reserved_indices(bpy.context, geometry.texture_table(obj))
    # Add Water rebuilds the base table too.
    assert bpy.ops.dkr.add_water(kind="CALM", area="TRACK", level=-32, skip_dry=False) == {"FINISHED"}
    obj = geometry.geometry_objects(bpy.context)[0]
    assert waterfall.resolved_index(controller, geometry.texture_table(obj)) == index
    assert bpy.ops.dkr.select_waterfall(object_name=controller.name) == {"FINISHED"}
    assert bpy.context.mode == "EDIT_MESH"
    bpy.ops.object.mode_set(mode="OBJECT")
    assert {p.index for p in obj.data.polygons if p.select} == {
        f["number"] for f in waterfall.face_records(obj, index)}

    # A moved, unique reference resolves at export without rewriting raw bytes.
    saved = controller[scroll.PROP_ENTRY]
    ref = json.loads(saved)
    ref["index"] = 254
    controller[scroll.PROP_ENTRY] = json.dumps(ref)
    # There are identical waterfall entries, so an ambiguous rebind must refuse.
    try:
        scene.export_object_map(bpy.context)
        raise AssertionError("ambiguous reference was exported")
    except scroll.ScrollError:
        pass
    controller[scroll.PROP_ENTRY] = saved
    ref["id"] = -1234
    controller[scroll.PROP_ENTRY] = json.dumps(ref)
    assert any(i.severity == "error" and "no longer has" in i.message for i in waterfall.issues(bpy.context))
    try:
        scene.export_object_map(bpy.context)
        raise AssertionError("missing reference was exported")
    except scroll.ScrollError:
        pass
    controller[scroll.PROP_ENTRY] = saved

    # A custom PNG uses the identical operator and exports its payload.
    path = _write_alpha_image(directory, "own_fall", 32, 32, "soft")
    assert bpy.ops.dkr.add_custom_texture(filepath=path, texture_format="0", size="", transparency="BLEND") == {"FINISHED"}
    unused = custom_textures.entries(bpy.context)[-1].index
    assert bpy.ops.dkr.add_custom_texture(filepath=path, texture_format="0", size="", transparency="BLEND") == {"FINISHED"}
    own = custom_textures.entries(bpy.context)[-1]
    settings.texture_id = own.index
    assert bpy.ops.dkr.add_waterfall(texture="PICKED") == {"FINISHED"}
    own_controller = waterfall.chosen(bpy.context)
    own_index = own_controller["textureIndex"]
    settings.texture_id = unused
    assert bpy.ops.dkr.remove_custom_texture() == {"FINISHED"}
    assert waterfall.read_reference(own_controller)["id"] == own.index - 1
    assert waterfall.resolved_index(own_controller, geometry.texture_table(obj)) == own_index
    ref = waterfall.read_reference(own_controller)
    ref["index"] = 254
    own_controller[scroll.PROP_ENTRY] = json.dumps(ref)
    assert scene.export_object_map(bpy.context).by_id(scroll.OBJECT_ID)[-1].fields["textureIndex"] == own_index
    output = os.path.join(directory, "waterfalls.dkrmap")
    assert bpy.ops.dkr.export_dkrmap(filepath=output, validate_first=False) == {"FINISHED"}
    assert os.path.isfile(os.path.join(output, "model.bin"))
    assert bpy.ops.dkr.select_waterfall(object_name=own_controller.name) == {"FINISHED"}
    import bmesh
    edit_mesh = bmesh.from_edit_mesh(obj.data)
    edit_mesh.faces.active = next(f for f in edit_mesh.faces if f.select)
    assert bpy.ops.dkr.pick_scroll_face(object_name=own_controller.name) == {"FINISHED"}
    assert waterfall.read_reference(own_controller)["index"] == own_index
    bpy.ops.object.mode_set(mode="OBJECT")
    face_count = len(obj.data.polygons)
    assert bpy.ops.dkr.remove_waterfall(object_name=own_controller.name) == {"FINISHED"}
    assert len(obj.data.polygons) == face_count
    assert own_index not in waterfall.reserved_indices(bpy.context, geometry.texture_table(obj))
    print("PASS: waterfall creation, isolation, collision, UI, references, rebuilds, custom texture, package and removal")


def retail():
    fresh()
    from track_lab import prefs
    bpy.context.scene.dkr.asset_root = os.path.abspath(os.path.join(HERE, "../../../extern/dkr-decomp/assets/.vanilla/us.v77"))
    prefs.invalidate()
    tree = prefs.resolve(bpy.context)
    if tree is None:
        raise AssertionError("retail assets needed for waterfall tests")
    level = tree.level("WhaleBay")
    assert bpy.ops.dkr.import_level(level=level.name, with_geometry=True, with_collectables=True) == {"FINISHED"}
    controllers = waterfall.scroll_objects(bpy.context)
    assert controllers and all(scroll.PROP_ENTRY in obj for obj in controllers)
    for slot, path in (("structure", level.objects_path), ("collectables", level.collectables_path)):
        if path:
            assert gltf_io.dumps(scene.export_object_map(bpy.context, slot=slot)) == gltf_io.dumps(gltf_io.load(path))
    assert not [i for i in waterfall.issues(bpy.context) if i.severity == "error"]
    print("PASS: imported retail waterfall references and unchanged object maps")


def main():
    track_lab.register()
    try:
        with tempfile.TemporaryDirectory(prefix="dkr-waterfalls-") as directory:
            try:
                run(directory)
            finally:
                fresh()
        retail()
    finally:
        track_lab.unregister()


if __name__ == "__main__":
    try:
        main()
    except BaseException:
        traceback.print_exc()
        sys.exit(1)
