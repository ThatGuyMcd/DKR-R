"""Pick the sky a track is drawn under, and see it before exporting.

The gallery's pictures come from :func:`skyboxes.panorama`; this module is the
Blender end of it. The thumbnails live in a preview collection and are made a
few at a time from a timer, so the panel draws at once and fills in rather than
stalling the first time it opens. The viewport preview is the dome itself,
placed around the track, marked so that nothing ever mistakes it for geometry
or exports it.
"""

from __future__ import annotations

import traceback

import bpy
from bpy.props import StringProperty
from mathutils import Vector

from .. import catalog as catalog_module, object_model, preview, prefs, scene, skyboxes
from . import geometry as geometry_ops
from . import header as header_ops

PREVIEW_NAME = "DKR Skybox Preview"
PROP_SKY = "dkr_skybox_preview"
COLLECTION = "DKR Sky"
THUMBNAIL = (96, 48)

_state = {"collection": None, "pending": [], "failed": set(), "tree": None,
          "sampler": None}


def chosen(context) -> str:
    """The skybox the header will name, or ``""`` for none."""
    value = context.scene.get(header_ops.key_for(header_ops.SKYBOX))
    return str(value) if value else ""


# ---------------------------------------------------------------------------
# Thumbnails
# ---------------------------------------------------------------------------

def _collection():
    if _state["collection"] is None:
        import bpy.utils.previews  # noqa: PLC0415 - optional, and only on demand

        _state["collection"] = bpy.utils.previews.new()
    return _state["collection"]


def _key(skybox) -> str:
    return "dkr_sky_%s" % skybox.asset_id


def icon_for(tree, skybox) -> int:
    """A thumbnail id, or 0 while it is still being made.

    Never raises: a gallery that cannot draw is worse than one with a blank
    tile.
    """
    try:
        collection = _collection()
        key = _key(skybox)
        if key in collection:
            return collection[key].icon_id
        if skybox.asset_id in _state["failed"]:
            return 0
        if tree is not _state["tree"]:
            _state["tree"], _state["sampler"] = tree, None
        if skybox.asset_id not in _state["pending"]:
            _state["pending"].append(skybox.asset_id)
        if not bpy.app.timers.is_registered(_build_next):
            bpy.app.timers.register(_build_next, first_interval=0.02)
    except Exception:  # noqa: BLE001 - appearance only
        traceback.print_exc()
    return 0


def build_icon(tree, skybox) -> int:
    """Make one thumbnail now; the panel goes through the timer instead."""
    collection = _collection()
    key = _key(skybox)
    if key in collection:
        return collection[key].icon_id
    if _state["sampler"] is None or _state["tree"] is not tree:
        _state["tree"], _state["sampler"] = tree, skyboxes.PngSampler(tree)
    pixels = skyboxes.panorama(object_model.load(skybox.model_path),
                               _state["sampler"], *THUMBNAIL)
    item = collection.new(key)
    item.image_size = THUMBNAIL
    item.image_pixels_float = pixels
    return item.icon_id


def _build_next():
    pending = _state["pending"]
    if not pending:
        return None
    asset_id = pending.pop(0)
    tree = _state["tree"]
    skybox = skyboxes.find(tree, asset_id) if tree is not None else None
    if skybox is not None:
        try:
            build_icon(tree, skybox)
        except Exception:  # noqa: BLE001 - one bad dome must not stop the rest
            traceback.print_exc()
            _state["failed"].add(asset_id)
    try:
        for window in bpy.context.window_manager.windows:
            for area in window.screen.areas:
                if area.type == "VIEW_3D":
                    area.tag_redraw()
    except Exception:  # noqa: BLE001 - redrawing is a nicety
        pass
    return 0.01 if pending else None


def teardown() -> None:
    """Release the thumbnails. Called from the addon's ``unregister``."""
    if bpy.app.timers.is_registered(_build_next):
        bpy.app.timers.unregister(_build_next)
    collection = _state["collection"]
    _state.update(collection=None, pending=[], failed=set(), tree=None, sampler=None)
    if collection is None:
        return
    try:
        import bpy.utils.previews  # noqa: PLC0415

        bpy.utils.previews.remove(collection)
    except Exception:  # noqa: BLE001 - shutting down anyway
        pass


# ---------------------------------------------------------------------------
# The viewport preview
# ---------------------------------------------------------------------------

def preview_object(context):
    for obj in context.scene.objects:
        if PROP_SKY in obj:
            return obj
    return None


def _track_extent(context):
    """The centre of the track and how far it reaches from there."""
    points = []
    for obj in geometry_ops.geometry_objects(context):
        points += [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    for obj in scene.iter_dkr_objects(context):
        points.append(obj.matrix_world.translation.copy())
    if not points:
        return Vector((0.0, 0.0, 0.0)), 0.0
    low = Vector([min(p[axis] for p in points) for axis in range(3)])
    high = Vector([max(p[axis] for p in points) for axis in range(3)])
    centre = (low + high) / 2.0
    return centre, max((p - centre).length for p in points)


def _sky_collection(context):
    for child in context.scene.collection.children:
        if child.name == COLLECTION:
            return child
    collection = bpy.data.collections.new(COLLECTION)
    context.scene.collection.children.link(collection)
    return collection


def _widen_clip(context, distance):
    if context.screen is None:
        return
    for area in context.screen.areas:
        if area.type != "VIEW_3D":
            continue
        for space in area.spaces:
            if space.type == "VIEW_3D":
                space.clip_end = max(space.clip_end, distance)


def hide(context) -> bool:
    obj = preview_object(context)
    if obj is None:
        return False
    bpy.data.objects.remove(obj, do_unlink=True)
    return True


def show(context, asset_id):
    """Put the dome around the track; returns the object, or ``None``.

    In game the dome sits on the camera, so it is never nearer than it is at
    the start. Here there is no camera to follow, so it is centred on the track
    and grown until the whole track fits inside it - the nearest a viewport
    gets to seeing it the way a racer does.
    """
    hide(context)
    tree = prefs.resolve(context)
    if tree is None or not asset_id:
        return None
    try:
        catalog = catalog_module.load()
    except Exception:  # noqa: BLE001 - only picks a variant, which a dome lacks
        catalog = None
    mesh, _kind = preview.mesh_for(asset_id, {}, tree, catalog)
    if mesh is None:
        return None

    centre, reach = _track_extent(context)
    radius = max((vertex.co.length for vertex in mesh.vertices), default=0.0)
    factor = max(1.0, 1.15 * reach / radius) if radius else 1.0

    obj = bpy.data.objects.new(PREVIEW_NAME, mesh)
    # Marked as a preview, which is what keeps the export's "a mesh of your
    # own" check and Track From Mesh from ever offering it as geometry.
    obj[preview.PROP_PREVIEW] = "skybox"
    obj[PROP_SKY] = asset_id
    obj.location = centre
    obj.scale = (factor, factor, factor)
    obj.hide_select = True
    _sky_collection(context).objects.link(obj)
    _widen_clip(context, radius * factor * 2.5)
    return obj


def update_preview(context) -> None:
    """Keep a shown preview in step with the chosen skybox."""
    if preview_object(context) is None:
        return
    current = chosen(context)
    if current:
        show(context, current)
    else:
        hide(context)


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

class DKR_OT_pick_skybox(bpy.types.Operator):
    """Draw the track under this sky"""

    bl_idname = "dkr.pick_skybox"
    bl_label = "Skybox"
    bl_options = {"REGISTER", "UNDO", "INTERNAL"}

    asset_id: StringProperty(options={"HIDDEN"})

    @classmethod
    def description(cls, context, properties):
        found = skyboxes.find(prefs.resolve(context), properties.asset_id)
        text = found.description if found else (properties.asset_id or cls.__doc__)
        if properties.asset_id and properties.asset_id == chosen(context):
            text += (". Chosen - click again for none, and the game draws a plain "
                     "background instead")
        return text

    def execute(self, context):
        if not self.asset_id:
            return {"CANCELLED"}
        key = header_ops.key_for(header_ops.SKYBOX)
        if chosen(context) == self.asset_id:
            del context.scene[key]
            message = "no skybox: the game draws a plain background instead"
        else:
            context.scene[key] = self.asset_id
            found = skyboxes.find(prefs.resolve(context), self.asset_id)
            message = "skybox: %s" % (found.label if found else self.asset_id)
        update_preview(context)
        header_ops._redraw(context)
        self.report({"INFO"}, message)
        return {"FINISHED"}


class DKR_OT_show_skybox(bpy.types.Operator):
    """Show the chosen sky around the track in the viewport. A preview only: it
    is never exported, and in game the dome is centred on the camera, so it
    never gets any closer"""

    bl_idname = "dkr.show_skybox"
    bl_label = "Show Skybox"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        if preview_object(context) is not None:
            return True
        if not chosen(context):
            cls.poll_message_set("Pick a skybox first")
            return False
        if prefs.resolve(context) is None:
            cls.poll_message_set("The skyboxes are read from the decomp assets; "
                                 "set the path in the addon preferences")
            return False
        return True

    def execute(self, context):
        if hide(context):
            self.report({"INFO"}, "skybox preview hidden")
            return {"FINISHED"}
        obj = show(context, chosen(context))
        if obj is None:
            self.report({"ERROR"}, "could not build %s" % chosen(context))
            return {"CANCELLED"}
        self.report({"INFO"}, "showing %s around the track; Material Preview "
                    "draws its textures" % chosen(context))
        return {"FINISHED"}


CLASSES = (
    DKR_OT_pick_skybox,
    DKR_OT_show_skybox,
)
