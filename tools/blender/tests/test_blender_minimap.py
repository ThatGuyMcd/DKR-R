"""The Minimap panel's operators and the export, inside Blender.

``test_minimap.py`` proves the arithmetic. This proves the track: what an
imported retail track ships untouched, what a reshaped one ships, and that a
minimap started from the AI path reaches the package - picture, sprite, the
model's placeholder and numbers, and the header's "no minimap" bit - exactly
as the runtime reads them - and that its high-resolution copy reaches the
``-hd.zip`` beside it, named after the texture the package ships.

    blender --background --factory-startup --python tools/blender/tests/test_blender_minimap.py
"""

from __future__ import annotations

import json
import os
import shutil
import struct
import sys
import tempfile
import traceback
import zipfile

import bpy

_HERE = os.path.dirname(os.path.abspath(__file__))
for argument in sys.argv:
    if argument.endswith("test_blender_minimap.py"):
        _HERE = os.path.dirname(os.path.abspath(argument))
        break

sys.path.insert(0, os.path.abspath(os.path.join(_HERE, "..")))
sys.path.insert(0, _HERE)

import track_lab  # noqa: E402
from track_lab import (  # noqa: E402
    dkrmap, level_header, level_model, level_model_encoder, minimap, prefs,
    rice_pack, textures,
)

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def _ops():
    from track_lab.operators import minimap as minimap_ops
    return minimap_ops


def _lake():
    tree = prefs.resolve(bpy.context)
    if tree is None:
        return None
    return next((l for l in tree.levels() if l.label == "Ancient Lake"), None)


def _import_lake():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.dkr.level_type = "RACE"
    lake = _lake()
    if lake is None:
        return None
    bpy.ops.dkr.import_level(level=lake.name)
    return lake


def _export(target):
    try:
        result = bpy.ops.dkr.export_dkrmap(filepath=target, validate_first=False)
    except RuntimeError as error:
        print("  export raised: %s" % error)
        return None
    with open(os.path.join(target, "manifest.json"), "r", encoding="utf-8") as handle:
        return result, json.load(handle)


def _header_byte(target, offset):
    with open(os.path.join(target, "header.bin"), "rb") as handle:
        return handle.read()[offset]


def _geometry():
    from track_lab.operators import geometry as geometry_ops
    objects = geometry_ops.geometry_objects(bpy.context)
    return objects[0] if objects else None


# ---------------------------------------------------------------------------

def test_registration():
    print("registration")
    for name in ("minimap_draw_edge", "minimap_from_ai", "minimap_close_edges",
                 "minimap_clear", "minimap_save_png", "minimap_use_png",
                 "minimap_forget_png", "minimap_colour"):
        check(hasattr(bpy.ops.dkr, name), "operator dkr.%s exists" % name)
    for name in ("DKR_PT_minimap", "DKR_PT_minimap_appearance", "DKR_PT_minimap_numbers"):
        check(hasattr(bpy.types, name), "panel %s is registered" % name)
    check(not hasattr(bpy.types, "DKR_OT_minimap_fit"), "the placeholder is gone")


def test_empty_scene():
    print("a scene with no track")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.dkr.level_type = "RACE"
    ops = _ops()
    state = ops.track_state(bpy.context, found=ops.shape(bpy.context, wait=True))
    check(state.kind == ops.NO_TRACK, "nothing to put a minimap on (%s)" % state.kind)


def test_retail_flow(temporary):
    print("an imported retail track, untouched, reshaped, then its own")
    ops = _ops()
    lake = _import_lake()
    if lake is None:
        print("  skip: no decomp assets")
        return
    obj = _geometry()
    check(obj is not None, "Ancient Lake's geometry is in the scene")
    if obj is None:
        return

    state = ops.track_state(bpy.context, found=ops.shape(bpy.context, wait=True))
    check(state.kind == ops.RETAIL, "untouched, it keeps its own minimap (%s)" % state.kind)
    check(state.label == "Ancient Lake", "and says whose (%r)" % state.label)
    check(state.retail is not None and state.retail.name == "ancient_lake",
          "the retail picture is found (%r)" % (state.retail and state.retail.name))
    check(ops.shape(bpy.context) is not ops.PENDING,
          "the panel reuses the answer instead of asking again")

    target = os.path.join(temporary, "lake-minimap.dkrmap")
    bpy.context.scene.dkr.track_id = "lake-minimap"
    exported = _export(target)
    check(exported is not None and exported[0] == {"FINISHED"}, "the untouched remix exports")
    if exported is None:
        return
    sections = [entry["section"] for entry in exported[1]["adds"]]
    check("TEXTURES_2D" not in sections and "SPRITES" not in sections,
          "it ships no minimap of its own")
    check("LEVEL_MODELS" not in sections, "nor a model")
    check(not _header_byte(target, 0xBC) & 1, "and its header still shows the minimap")

    # Reshape: a vertex pulled past the edge of the track moves the bounds.
    widest = max(vertex.co.x for vertex in obj.data.vertices)
    obj.data.vertices[11].co.x = widest + 1500.0
    obj.data.update()
    state = ops.track_state(bpy.context, found=ops.shape(bpy.context, wait=True))
    check(state.kind == ops.CHANGED, "reshaped, it would show the wrong road (%s)" % state.kind)
    exported = _export(target)
    check(exported is not None and exported[0] == {"FINISHED"},
          "the reshaped track exports without edges - never blocked")
    if exported is None:
        return
    check(_header_byte(target, 0xBC) & 1, "and its header hides the minimap")

    # Start From AI Path: two closed strokes, the track's own minimap.
    result = bpy.ops.dkr.minimap_from_ai()
    check(result == {"FINISHED"}, "Start From AI Path runs")
    edges = ops.read_edges(bpy.context)
    check(len(edges) == 2 and all(edge.cyclic for edge in edges),
          "two closed edges (%d)" % len(edges))
    gp = ops.edge_object(bpy.context)
    check(gp is not None and gp.type == "GREASEPENCIL", "on the DKR Minimap object")

    # The bulk read and the stroke-by-stroke read agree.
    slow = []
    for layer, drawing in ops._drawings(gp):
        for stroke in drawing.strokes:
            slow.append(([tuple(p.position) for p in stroke.points], bool(stroke.cyclic)))
    fast = []
    for layer, drawing in ops._drawings(gp):
        fast += ops._stroke_points(drawing)
    check(len(slow) == len(fast) and all(
        a[1] == b[1] and len(a[0]) == len(b[0]) and all(
            max(abs(x - y) for x, y in zip(p, q)) < 1e-3 for p, q in zip(a[0], b[0]))
        for a, b in zip(slow, fast)), "the bulk stroke read matches the plain one")

    state = ops.track_state(bpy.context, found=ops.shape(bpy.context, wait=True))
    check(state.kind == ops.OWN and not state.open, "edges drawn: its own minimap")
    built = ops.build(bpy.context, state)
    check(built is not None and built.placement is not None and not built.error,
          "the picture is made (%s)" % (built and (built.error or built.placement)))
    if built is None or built.placement is None:
        return
    placement = built.placement
    check(len(built.markers) == 8, "the preview shows the player and 7 racers "
          "(%d markers)" % len(built.markers))
    icon = ops.preview_icon(built, placement.colour)
    check(icon >= 0, "the preview draws without error (icon %d)" % icon)

    exported = _export(target)
    check(exported is not None and exported[0] == {"FINISHED"}, "the track exports with it")
    if exported is None:
        return
    adds = exported[1]["adds"]
    files = {entry["section"]: entry["file"] for entry in adds}
    check(files.get("TEXTURES_2D") == "minimap/texture.bin"
          and files.get("SPRITES") == "minimap/sprite.bin",
          "the manifest claims the picture and the sprite")
    order = [entry["section"] for entry in adds]
    check(order.index("TEXTURES_2D") < order.index("SPRITES"),
          "the picture before the sprite that names it")
    check(not _header_byte(target, 0xBC) & 1, "the header shows the minimap again")

    with open(os.path.join(target, "model.bin"), "rb") as handle:
        payload = handle.read()
    at = level_model_encoder.STORED_PREFIX_AT + 0x20
    check(struct.unpack_from(">i", payload, at)[0] == minimap.CUSTOM_SPRITE_ID_BASE,
          "the model names the track's own sprite where the runtime rewrites it")
    shipped = level_model.parse(level_model.decompress(payload))
    back = minimap.placement_from_model(shipped)
    check((back.rotation, back.offset_x, back.offset_y, back.offset_x2)
          == (placement.rotation, placement.offset_x, placement.offset_y,
              placement.offset_x2), "and carries the fitted numbers")
    check(tuple(shipped.bounds) == tuple(state.bounds),
          "fitted to the bounds it ships with")

    with open(os.path.join(target, "minimap", "texture.bin"), "rb") as handle:
        texture = handle.read()
    check((texture[0], texture[1]) == (placement.width, placement.height)
          and texture[2] == 0x05 and len(texture) % 16 == 0,
          "the picture is a %dx%d IA8 texture" % (texture[0], texture[1]))
    with open(os.path.join(target, "minimap", "sprite.bin"), "rb") as handle:
        sprite = minimap.read_sprite_payload(handle.read())
    check(sprite["anchor"] == (placement.anchor_x, placement.anchor_y)
          and sprite["base"] == minimap.CUSTOM_TEXTURE_ID_BASE,
          "the sprite holds the anchor and the texture placeholder")
    with open(os.path.join(target, "HOW-TO-BUILD.md"), "r", encoding="utf-8") as handle:
        notes = handle.read()
    check("## Minimap" in notes, "the build notes explain it")
    check_hd_pack(target, texture, placement, exported[1])
    check("minimap drawn again" in notes, "and the HD copy")

    # Validate says so.
    bpy.ops.dkr.validate()
    lines = [r.message for r in bpy.context.scene.dkr.results]
    check(any(line == "Minimap: ok" for line in lines),
          "Validate has a minimap line (%s)" % [l for l in lines if "Minimap" in l])

    test_png(temporary, built)

    # An open edge is found, and Close closes it.
    for _layer, drawing in ops._drawings(gp):
        stroke = drawing.strokes[0]
        stroke.cyclic = False
        stroke.points[-1].position = (stroke.points[-1].position[0] + 3000.0,
                                      stroke.points[-1].position[1] + 3000.0,
                                      stroke.points[-1].position[2])
    state = ops.track_state(bpy.context)
    check(len(state.open) == 1, "an edge with its ends apart is open (%d)" % len(state.open))
    bpy.ops.dkr.minimap_close_edges()
    state = ops.track_state(bpy.context)
    check(not state.open, "Close closes it")

    # Clear: back to no minimap, and the files go.
    bpy.ops.dkr.minimap_clear("EXEC_DEFAULT")
    state = ops.track_state(bpy.context, found=ops.shape(bpy.context, wait=True))
    check(state.kind == ops.CHANGED, "cleared, the reshaped track has none (%s)" % state.kind)
    exported = _export(target)
    if exported is not None:
        sections = [entry["section"] for entry in exported[1]["adds"]]
        check("SPRITES" not in sections, "and the export stops claiming one")
        check(not os.path.exists(os.path.join(target, "minimap")),
              "and removes the stale files")
        check(_header_byte(target, 0xBC) & 1, "and hides it again")


def check_hd_pack(target, texture, placement, manifest):
    """The minimap's HD copy is in the pack, under the shipped texture's name."""
    pack = dkrmap.hd_pack_path(target)
    check(os.path.isfile(pack), "the HD pack is written beside it")
    if not os.path.isfile(pack):
        return
    identity = minimap.texture_identity(texture)
    name = rice_pack.entry_name(identity)
    with zipfile.ZipFile(pack) as archive:
        names = archive.namelist()
        check(name in names, "it holds the minimap as %s (%s)" % (name, names))
        if name in names:
            with tempfile.TemporaryDirectory() as scratch:
                path = archive.extract(name, scratch)
                size = textures.png_size(path)
            scale = minimap.HD_SCALE
            check(size == (placement.width * scale, placement.height * scale),
                  "at %d times the texture's size (%r)" % (scale, size))
    stamp = rice_pack.read_stamp(pack) or {}
    check(stamp.get("minimap", {}).get("identity") == identity,
          "the stamp names it (%r)" % stamp.get("minimap"))
    digest = manifest.get("hdTexturePack", {}).get("textureDigest")
    check(digest is not None and digest == stamp.get("textureDigest"),
          "and pairs with the manifest (%r, %r)" % (digest, stamp.get("textureDigest")))


def test_png(temporary, built):
    print("Save PNG and Use My PNG")
    ops = _ops()
    placement = built.placement
    path = os.path.join(temporary, "retouched.png")
    result = bpy.ops.dkr.minimap_save_png(filepath=path)
    check(result == {"FINISHED"} and os.path.isfile(path), "Save PNG writes the picture")
    width, height, rgba = textures.read_png(path)
    check((width, height) == (placement.width, placement.height)
          and bytes(rgba) == bytes(built.rgba), "texel for texel")
    rgba = bytearray(rgba)
    rgba[3] = 255
    rgba[0:3] = b"\x00\x00\x00"
    minimap.write_png(path, width, height, rgba)
    result = bpy.ops.dkr.minimap_use_png(filepath=path)
    check(result == {"FINISHED"}, "Use My PNG takes a picture of the same size")
    again = ops.build(bpy.context)
    check(again is not None and bytes(again.rgba[:4]) == b"\x00\x00\x00\xff",
          "and the minimap uses it")
    check(again is not None and again.hd_picture() is None,
          "your own PNG has no larger version to put in the HD pack")
    wrong = os.path.join(temporary, "wrong.png")
    minimap.write_png(wrong, width + 8, height, bytearray((width + 8) * height * 4))
    try:
        result = bpy.ops.dkr.minimap_use_png(filepath=wrong)
    except RuntimeError:
        result = {"CANCELLED"}
    check(result == {"CANCELLED"}, "a picture of another size is refused")
    check(bpy.context.scene.dkr.minimap_png == path, "and the good one is kept")
    bpy.ops.dkr.minimap_forget_png()
    check(not bpy.context.scene.dkr.minimap_png, "Use The Drawn Picture goes back")


def test_draw_edge():
    print("Draw Edge without a viewport")
    ops = _ops()
    lake = _import_lake()
    if lake is None:
        print("  skip: no decomp assets")
        return
    result = bpy.ops.dkr.minimap_draw_edge()
    check(result == {"FINISHED"}, "Draw Edge runs")
    gp = ops.edge_object(bpy.context)
    check(gp is not None and bpy.context.view_layer.objects.active == gp,
          "the DKR Minimap object is made and active")
    check(gp is not None and gp.mode == "PAINT_GREASE_PENCIL", "in draw mode")
    tools = bpy.context.scene.tool_settings
    check(tools.gpencil_sculpt.lock_axis == "AXIS_Z"
          and tools.gpencil_stroke_placement_view3d == "ORIGIN",
          "drawing on the flat top plane")
    bounds = ops.track_state(bpy.context).bounds
    check(gp is not None and abs(gp.location.z - ops.plane_height(bounds)) < 1e-3,
          "above the whole track")
    bpy.ops.object.mode_set(mode="OBJECT")


class _Layout:
    """Stands in for a UILayout: every call answers another, and it records
    the labels and operators drawn so the panel's words can be checked."""

    def __init__(self, seen=None):
        self.__dict__["seen"] = seen if seen is not None else []

    def __getattr__(self, name):
        def call(*args, **kwargs):
            text = kwargs.get("text")
            if name in ("label", "operator", "prop", "menu") and text:
                self.seen.append(text)
            if name == "operator" and args:
                self.seen.append(args[0])
            return _Layout(self.seen)
        return call

    def __setattr__(self, name, value):
        pass


def _draw_panels():
    from track_lab.ui import panels
    seen = []
    context = bpy.context
    for panel in (panels.DKR_PT_minimap, panels.DKR_PT_minimap_appearance,
                  panels.DKR_PT_minimap_numbers):
        # The panel's own helpers, bound to a stand-in that has a layout.
        methods = {name: value for name, value in vars(panel).items()
                   if callable(value) and not name.startswith("__")}
        fake = type("Fake", (), dict(methods, layout=_Layout(seen)))()
        panel.draw(fake, context)
    return seen


def test_panels_draw():
    print("the panels draw in every state")
    ops = _ops()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.dkr.level_type = "RACE"
    bpy.context.scene.dkr.show_raw = True
    seen = _draw_panels()
    check("dkr.minimap_draw_edge" in seen, "with no track, Draw Edge is offered")

    lake = _import_lake()
    if lake is None:
        print("  skip: no decomp assets")
        return
    bpy.context.scene.dkr.show_raw = True
    ops.shape(bpy.context, wait=True)
    seen = _draw_panels()
    check("dkr.minimap_draw_edge" in seen and "dkr.minimap_from_ai" not in seen,
          "a retail track offers Draw Edge to replace its map")

    obj = _geometry()
    widest = max(vertex.co.x for vertex in obj.data.vertices)
    obj.data.vertices[11].co.x = widest + 1500.0
    obj.data.update()
    ops.shape(bpy.context, wait=True)
    seen = _draw_panels()
    check("dkr.minimap_from_ai" in seen, "a reshaped one offers Start From AI Path")

    bpy.ops.dkr.minimap_from_ai()
    seen = _draw_panels()
    check("2 edges" in seen, "with edges it counts them (%s)" % [s for s in seen if "edge" in s])
    check("dkr.minimap_clear" in seen and "dkr.minimap_save_png" in seen,
          "and offers Clear and, under Numbers, Save PNG")
    check(any(s.startswith("Offset Adv 2") for s in seen),
          "the Numbers sub-panel lists the mirrored offsets")


def test_overlay_corners():
    print("Show on Track lays the picture over the ground it covers")
    ops = _ops()
    placement = minimap.Placement((-5000, 7000, 0, 10, -3000, 9000), 245, 0.87,
                                  0.87, 40, -60, width=56, height=60)
    corners = ops.overlay_corners(placement, 123.0)
    check(len(corners) == 4 and all(abs(c[2] - 123.0) < 1e-9 for c in corners),
          "four corners at the given height")
    x, z = minimap.world_of(placement, placement.width / 2.0, placement.height / 2.0)
    centre = [sum(c[i] for c in corners) / 4.0 for i in range(2)]
    # mathutils stores single precision, and these are ten-thousands.
    check(abs(centre[0] - x) < 0.05 and abs(centre[1] - (-z)) < 0.05,
          "the quad's centre is the picture's centre on the ground")


def main():
    track_lab.register()
    temporary = tempfile.mkdtemp(prefix="dkr-minimap-")
    try:
        test_registration()
        test_empty_scene()
        test_retail_flow(temporary)
        test_draw_edge()
        test_panels_draw()
        test_overlay_corners()
    finally:
        shutil.rmtree(temporary, ignore_errors=True)
        track_lab.unregister()
    print()
    if FAILURES:
        print("FAIL: %d check(s)" % len(FAILURES))
        for line in FAILURES:
            print("  " + line)
        return 1
    print("PASS: all minimap operator checks")
    return 0


if __name__ == "__main__":
    try:
        _code = main()
    except BaseException:  # noqa: BLE001 - a crash must not read as a pass
        traceback.print_exc()
        _code = 1
    sys.exit(_code)
