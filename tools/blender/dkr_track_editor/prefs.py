"""Where the addon looks for the extracted decomp assets.

Object artwork - the sprite for a coin, the mesh for a frog - lives in an
extracted decomp asset tree, not inside the addon. So the addon has to know
where that tree is before it can draw anything, and it has to know from the
moment it is enabled: pressing *Coin* must produce a coin straight away, not
only after an object map has been imported from somewhere inside the tree.

Three places are consulted, in order:

1. the scene, set when a map or a level model was imported from inside a tree
2. this addon preference, which the author sets once and which persists
3. a search of the usual locations: the folder the addon extracts a ROM into
   (:mod:`.rom_extract`), and a checkout of this repository

Most authors have no decomp at all, so the sidebar opens on *Game Assets*
(``ui/panels.py``) until one of those finds a tree: extracting from the ROM
writes the tree into :func:`extraction_root` and points the preference at it.
"""

from __future__ import annotations

import os
import time

import bpy
from bpy.props import BoolProperty, StringProperty

from . import assets

#: Cached between calls so placing a hundred objects does not re-scan the disk.
_resolved = {"key": None, "tree": None, "missed": 0.0}

#: How long "no tree anywhere" is believed before the disk is searched again.
#: Every panel's poll asks, so without this a missing tree re-runs the whole
#: search on each redraw; with it, a tree extracted outside Blender still turns
#: up within a couple of seconds.
MISS_SECONDS = 2.0


def package_name() -> str:
    """The identifier Blender registered this addon under."""
    return __package__ or "dkr_track_editor"


class DKR_AddonPreferences(bpy.types.AddonPreferences):
    bl_idname = package_name()

    asset_root: StringProperty(
        name="Decomp Assets",
        description=(
            "Extracted asset version directory, the one holding "
            "asset_objects.meta.json - the folder Extract from ROM writes, or a "
            "decomp's extern/dkr-decomp/assets/.vanilla/us.v77. Every object is "
            "drawn with its real sprite or model from here"
        ),
        default="",
        subtype="DIR_PATH",
        update=lambda self, context: invalidate(),
    )

    extraction_folder: StringProperty(
        name="Extraction Folder",
        description=(
            "Where Extract from ROM writes the assets. Leave empty for the "
            "addon's own folder; choose a shorter path if Windows refuses that "
            "one as too long"
        ),
        default="",
        subtype="DIR_PATH",
    )

    assets_declined: BoolProperty(
        name="Continue Without Assets",
        description=(
            "Use the addon without the game's assets: objects are drawn as "
            "markers and retail textures, skyboxes and Import Retail Track are "
            "unavailable"
        ),
        default=False,
    )

    def draw(self, context):
        layout = self.layout
        tree = resolve(context)

        box = layout.box()
        if tree is None:
            box.label(text="No game assets", icon="ERROR")
            box.label(text="Objects are drawn as plain markers.")
        else:
            box.label(text="Using %s" % tree.label, icon="CHECKMARK")
            box.label(text=tree.root)

        from .operators import rom_assets  # noqa: PLC0415 - registered later

        job = rom_assets.current_job()
        if job is not None and job.running:
            column = layout.column()
            column.progress(factor=job.fraction, type="BAR",
                            text="%s - %d%%" % (job.message, job.fraction * 100))
            column.operator("dkr.cancel_rom_extraction", icon="CANCEL")
        else:
            row = layout.row(align=True)
            row.operator("dkr.extract_rom_assets", icon="IMPORT",
                         text="Extract from ROM..." if tree is None
                         else "Extract Again from ROM...")
            row.operator("dkr.use_asset_folder", icon="FILE_FOLDER")
            if job is not None and job.error:
                error = layout.box()
                error.alert = True
                error.label(text=job.error, icon="ERROR")
        layout.prop(self, "asset_root")
        layout.prop(self, "extraction_folder")
        layout.label(text="Extracts into: %s" % extraction_root(create=False),
                     icon="INFO")


def preferences():
    """The addon's preferences, or ``None`` when it is not registered."""
    try:
        addon = bpy.context.preferences.addons.get(package_name())
        return addon.preferences if addon is not None else None
    except (AttributeError, KeyError):
        return None


def preference_root() -> str:
    """The configured path, or empty when the addon is not registered."""
    found = preferences()
    if found is None:
        return ""
    try:
        return bpy.path.abspath(found.asset_root or "")
    except AttributeError:
        return ""


def extraction_root(create: bool = True) -> str:
    """Where Extract from ROM writes its tree (one ``<version>`` folder each).

    The extension's own user directory when the addon is installed as an
    extension, which Blender keeps across updates of the addon; Blender's user
    data folder otherwise, as when the addon is run from a checkout. The
    *Extraction Folder* preference overrides both.
    """
    found = preferences()
    chosen = getattr(found, "extraction_folder", "") if found is not None else ""
    if chosen:
        path = bpy.path.abspath(chosen)
        if create:
            os.makedirs(path, exist_ok=True)
        return path
    try:
        return bpy.utils.extension_path_user(package_name(), path="assets", create=create)
    except Exception:  # noqa: BLE001 - not installed as an extension
        pass
    base = bpy.utils.user_resource("DATAFILES", path=package_name(), create=create)
    path = os.path.join(base, "assets")
    if create:
        os.makedirs(path, exist_ok=True)
    return path


def search_hints() -> list:
    """Places worth looking in when nothing has been configured."""
    hints = []

    # What Extract from ROM wrote, even if the preference was cleared since.
    try:
        hints.append(extraction_root(create=False))
    except Exception:  # noqa: BLE001 - Blender without a user folder
        pass

    # A checkout of this repository, when the addon runs from tools/blender.
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    hints.append(repo)

    # Next to the .blend the author is working in.
    if bpy.data.filepath:
        hints.append(os.path.dirname(bpy.data.filepath))

    return hints


def resolve(context=None):
    """The asset tree to draw objects with, or ``None``.

    Cached on what it was resolved from, so the answer is recomputed when the
    author changes the preference or imports from a different tree, and not on
    every one of a few hundred placements. A miss is cached briefly too
    (:data:`MISS_SECONDS`).
    """
    context = context or bpy.context
    scene_root = ""
    try:
        scene_root = bpy.path.abspath(context.scene.dkr.asset_root or "")
    except AttributeError:
        pass

    key = (scene_root, preference_root(), bpy.data.filepath)
    if _resolved["key"] == key:
        if _resolved["tree"] is not None:
            return _resolved["tree"]
        if time.monotonic() - _resolved["missed"] < MISS_SECONDS:
            return None

    tree = assets.AssetTree.discover(scene_root, preference_root(), *search_hints())
    _resolved["key"] = key
    _resolved["tree"] = tree
    _resolved["missed"] = time.monotonic() if tree is None else 0.0
    return tree


def setup_pending(context=None) -> bool:
    """Whether the sidebar should ask for the game's assets before anything else.

    True while there is no tree and the author has not chosen to go on without
    one, and while an extraction is running.
    """
    from .operators import rom_assets  # noqa: PLC0415 - avoids a cycle

    job = rom_assets.current_job()
    if job is not None and job.running:
        return True
    found = preferences()
    if found is not None and found.assets_declined:
        return False
    return resolve(context) is None


def invalidate():
    """Forget the cached answer; the configured path changed."""
    _resolved["key"] = None
    _resolved["tree"] = None
    _resolved["missed"] = 0.0
    from . import preview, skyboxes, textures

    preview.clear_cache()
    # The 3D texture catalogue is keyed by tree root, and the indices in it are
    # that extraction's, so pointing the addon at another one has to drop it.
    textures.clear_cache()
    skyboxes_cache = getattr(skyboxes, "_CATALOGUES", None)
    if isinstance(skyboxes_cache, dict):
        skyboxes_cache.clear()


CLASSES = (DKR_AddonPreferences,)
