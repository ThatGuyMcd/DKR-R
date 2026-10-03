"""Getting the game's assets: extract them from the ROM, or point at a folder.

The extraction itself is :mod:`..rom_extract`, plain Python. This module runs
it on a worker thread so Blender stays responsive for the few seconds it takes,
and finishes the job from a :mod:`bpy.app.timers` callback rather than a modal
operator: a timer survives the author opening another file mid-extraction, and
it is the only place the result may touch ``bpy`` - the worker never does.
"""

from __future__ import annotations

import os
import threading
import time

import bpy
from bpy.props import StringProperty

from .. import prefs, rom_extract


class Job:
    """One extraction, shared between the worker, the timer and the panels."""

    def __init__(self, rom_path, label):
        self.rom_path = rom_path
        self.label = label
        self.fraction = 0.0
        self.message = "Starting"
        self.error = ""
        self.result = ""
        self.cancel = threading.Event()
        self.finished = threading.Event()
        self.started = time.monotonic()
        self.thread = None

    @property
    def running(self) -> bool:
        return self.thread is not None and not self.finished.is_set()

    def progress(self, fraction, message):
        self.fraction = max(0.0, min(1.0, float(fraction)))
        self.message = message

    def work(self, destination):
        try:
            self.result = rom_extract.extract(self.rom_path, destination,
                                              progress=self.progress,
                                              cancelled=self.cancel.is_set)
        except rom_extract.Cancelled:
            self.error = ""
        except rom_extract.RomError as error:
            self.error = str(error)
        except ImportError as error:
            self.error = "this Blender is missing numpy (%s)" % error
        except Exception as error:  # noqa: BLE001 - shown to the author, not lost
            self.error = "extraction failed: %s" % error
        finally:
            self.finished.set()


_job = {"current": None}


def current_job():
    """The running or most recent extraction, or ``None``."""
    return _job["current"]


def _redraw():
    wm = getattr(bpy.context, "window_manager", None)
    if wm is None:
        return
    for window in wm.windows:
        screen = window.screen
        if screen is None:
            continue
        for area in screen.areas:
            if area.type in {"VIEW_3D", "PREFERENCES"}:
                area.tag_redraw()


def _poll_job():
    """Timer: redraw the progress bar, and adopt the tree once it is done."""
    job = current_job()
    if job is None:
        return None
    _redraw()
    if not job.finished.is_set():
        return 0.2
    if job.result:
        found = prefs.preferences()
        if found is not None:
            found.asset_root = job.result  # invalidates via the update callback
            found.assets_declined = False
        prefs.invalidate()
        print("DKR: extracted %s into %s in %.1f s"
              % (job.label, job.result, time.monotonic() - job.started))
    elif job.error:
        print("DKR: extraction failed: %s" % job.error)
    _redraw()
    return None


def start(rom_path, label) -> Job:
    """Begin extracting ``rom_path`` on a worker thread."""
    job = Job(rom_path, label)
    destination = prefs.extraction_root(create=True)
    job.thread = threading.Thread(target=job.work, args=(destination,),
                                  name="dkr-rom-extract", daemon=True)
    _job["current"] = job
    job.thread.start()
    if not bpy.app.timers.is_registered(_poll_job):
        bpy.app.timers.register(_poll_job, first_interval=0.1, persistent=True)
    return job


# ---------------------------------------------------------------------------
# The ROMs DKR-R already knows about
# ---------------------------------------------------------------------------

_known = {"key": None, "found": []}


def known_roms():
    """``[(path, RomIdentity)]`` for the supported ROMs DKR-R has, cached.

    Reading a ROM to identify it costs a few tens of milliseconds, so this runs
    once per set of files, not on every redraw.
    """
    candidates = rom_extract.candidate_roms()
    key = []
    for path in candidates:
        try:
            stat = os.stat(path)
            key.append((path, stat.st_size, stat.st_mtime))
        except OSError:
            continue
    key = tuple(key)
    if key == _known["key"]:
        return _known["found"]

    found = []
    seen = set()
    for path, _size, _mtime in key:
        try:
            identity = rom_extract.identify(path)
        except rom_extract.RomError:
            continue
        if identity.supported and identity.sha1 not in seen:
            seen.add(identity.sha1)
            found.append((path, identity))
    _known["key"] = key
    _known["found"] = found
    return found


# ---------------------------------------------------------------------------
# Operators
# ---------------------------------------------------------------------------

class DKR_OT_extract_rom_assets(bpy.types.Operator):
    """Extract the objects, textures and tracks the addon draws from your
    Diddy Kong Racing ROM (USA 1.0 or 1.1)"""

    bl_idname = "dkr.extract_rom_assets"
    bl_label = "Extract from ROM"
    bl_options = {"REGISTER"}

    filepath: StringProperty(name="ROM", subtype="FILE_PATH", default="")
    filter_glob: StringProperty(default="*.z64;*.n64;*.v64", options={"HIDDEN"})

    @classmethod
    def poll(cls, context):
        job = current_job()
        return job is None or not job.running

    def invoke(self, context, event):
        if self.filepath:
            return self.execute(context)
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        path = bpy.path.abspath(self.filepath)
        try:
            identity = rom_extract.identify(path)
        except rom_extract.RomError as error:
            self._fail(str(error))
            return {"CANCELLED"}
        if not identity.supported:
            self._fail("%s is not supported: DKR-R runs the USA ROM (1.0 or 1.1) "
                       "only" % identity.name)
            return {"CANCELLED"}
        start(path, identity.name)
        self.report({"INFO"}, "Extracting %s..." % identity.name)
        return {"FINISHED"}

    def _fail(self, message):
        # Kept on a finished job so the panel can show it after the popup goes.
        job = Job(self.filepath, os.path.basename(self.filepath))
        job.error = message
        job.finished.set()
        _job["current"] = job
        self.report({"ERROR"}, message)


class DKR_OT_cancel_rom_extraction(bpy.types.Operator):
    """Stop the extraction. Nothing is kept from a stopped run"""

    bl_idname = "dkr.cancel_rom_extraction"
    bl_label = "Cancel"
    bl_options = {"REGISTER"}

    @classmethod
    def poll(cls, context):
        job = current_job()
        return job is not None and job.running

    def execute(self, context):
        current_job().cancel.set()
        return {"FINISHED"}


class DKR_OT_use_asset_folder(bpy.types.Operator):
    """Use assets you already extracted - with this addon, or with the decomp's
    extract.sh (pick assets/.vanilla/us.v77 or any folder above it)"""

    bl_idname = "dkr.use_asset_folder"
    bl_label = "Use Folder..."
    bl_options = {"REGISTER"}

    directory: StringProperty(name="Folder", subtype="DIR_PATH", default="")

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        from .. import assets  # noqa: PLC0415

        folder = bpy.path.abspath(self.directory)
        tree = assets.AssetTree.discover(folder)
        if tree is None:
            self.report({"ERROR"}, "No extracted assets in %s (looking for "
                        "asset_objects.meta.json)" % folder)
            return {"CANCELLED"}
        found = prefs.preferences()
        if found is not None:
            found.asset_root = tree.root
            found.assets_declined = False
        prefs.invalidate()
        self.report({"INFO"}, "Using assets from %s" % tree.root)
        return {"FINISHED"}


class DKR_OT_decline_assets(bpy.types.Operator):
    """Go on without the game's assets. Objects are drawn as markers, and
    retail textures, skyboxes and Import Retail Track are unavailable until
    you set them up"""

    bl_idname = "dkr.decline_assets"
    bl_label = "Continue Without Assets"
    bl_options = {"REGISTER"}

    def execute(self, context):
        found = prefs.preferences()
        if found is not None:
            found.assets_declined = True
        return {"FINISHED"}


class DKR_OT_setup_assets(bpy.types.Operator):
    """Show the asset setup again"""

    bl_idname = "dkr.setup_assets"
    bl_label = "Set Up Assets"
    bl_options = {"REGISTER"}

    def execute(self, context):
        found = prefs.preferences()
        if found is not None:
            found.assets_declined = False
        return {"FINISHED"}


def teardown():
    """Stop a running extraction and its timer when the addon is disabled.

    The worker owns only its staging folder, which it removes on cancel."""
    job = current_job()
    if job is not None and job.running:
        job.cancel.set()
        job.thread.join(timeout=5.0)
    if bpy.app.timers.is_registered(_poll_job):
        bpy.app.timers.unregister(_poll_job)
    _job["current"] = None


CLASSES = (
    DKR_OT_extract_rom_assets,
    DKR_OT_cancel_rom_extraction,
    DKR_OT_use_asset_folder,
    DKR_OT_decline_assets,
    DKR_OT_setup_assets,
)
