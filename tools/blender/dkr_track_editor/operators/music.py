"""Choose the recorded music a track ships, and check it before export.

The format side - which files are music the runtime decodes, how long they are,
whether a loop fits - is :mod:`..music_audio`, tested without Blender. The
package side is :meth:`..dkrmap.TrackPackage.set_music`. This module is only
the Blender half: picking a file, keeping its path portable, and telling the
author what was found while there is still time to fix it.

A track with its own music still names a game song in its header. That song is
the *carrier*: it plays silently, and the runtime follows its volume and tempo,
which is how the game's fades, pause and final-lap speed-up reach a file DKR
cannot play itself. The carrier's channels are muted with it, so the objects
that switch music channels on and off as a racer passes them do nothing
audible; :func:`channel_objects` finds them so the panel and the export can say
so instead of leaving it to be discovered in a race.
"""

from __future__ import annotations

import os

import bpy
from bpy.props import StringProperty
from bpy_extras.io_utils import ImportHelper

from .. import music_audio

#: The object types that act on the music's channels: they fade or switch
#: individual channels of the playing sequence. A recorded file has none.
CHANNEL_OBJECT_IDS = frozenset((
    "ASSET_OBJECT_MIDIFADE",
    "ASSET_OBJECT_MIDIFADEPOINT",
    "ASSET_OBJECT_MIDICHSET",
))


def uses_file(settings) -> bool:
    return settings.music_source == "FILE"


def file_path(settings) -> str:
    """The chosen file as an absolute path, or ``""``."""
    return bpy.path.abspath(settings.music_file) if settings.music_file else ""


def refresh_report(settings) -> None:
    """Inspect the chosen file and remember what was found."""
    if not uses_file(settings):
        settings.music_report = ""
        settings.music_ok = False
        return
    try:
        info = music_audio.inspect_file(file_path(settings))
    except music_audio.AudioError as error:
        settings.music_report = str(error)
        settings.music_ok = False
        return
    settings.music_report = info.summary()
    settings.music_ok = True


def channel_objects(context) -> list:
    """Placed objects that switch or fade music channels."""
    from .. import scene  # noqa: PLC0415

    return [obj for obj in scene.iter_dkr_objects(context)
            if str(obj.get(scene.PROP_ID, "")) in CHANNEL_OBJECT_IDS]


class DKR_OT_choose_music(bpy.types.Operator, ImportHelper):
    """Choose an MP3 or WAV for this track to play instead of a game song"""

    bl_idname = "dkr.choose_music"
    bl_label = "Choose Music File"
    bl_options = {"REGISTER", "UNDO"}

    filter_glob: StringProperty(default="*.mp3;*.wav", options={"HIDDEN"})

    def execute(self, context):
        settings = context.scene.dkr
        path = self.filepath
        try:
            info = music_audio.inspect_file(path)
        except music_audio.AudioError as error:
            self.report({"ERROR"}, "%s: %s" % (os.path.basename(path), error))
            return {"CANCELLED"}
        # Relative once the .blend has a home, so the scene and its music can
        # move together; bpy.path.relpath only works for a saved file.
        if bpy.data.filepath:
            try:
                path = bpy.path.relpath(path)
            except ValueError:
                pass  # another drive on Windows: keep it absolute
        settings.music_file = path
        settings.music_source = "FILE"
        # Loop points from another song rarely fit this one.
        if settings.music_loop_end > info.seconds:
            settings.music_loop_end = 0.0
        if settings.music_loop_start >= (settings.music_loop_end or info.seconds):
            settings.music_loop_start = 0.0
        refresh_report(settings)
        self.report({"INFO"}, "music: %s" % info.summary())
        return {"FINISHED"}


class DKR_OT_check_music(bpy.types.Operator):
    """Read the music file again and check the loop against it"""

    bl_idname = "dkr.check_music"
    bl_label = "Check Music"

    def execute(self, context):
        settings = context.scene.dkr
        refresh_report(settings)
        if not settings.music_ok:
            self.report({"ERROR"}, settings.music_report or "no music file chosen")
            return {"CANCELLED"}
        try:
            info = music_audio.inspect_file(file_path(settings))
            music_audio.check_loop(info, settings.music_loop_start,
                                   settings.music_loop_end)
        except music_audio.AudioError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, "music: %s" % settings.music_report)
        return {"FINISHED"}


class DKR_OT_clear_music(bpy.types.Operator):
    """Go back to the game's own music. The file itself is not touched"""

    bl_idname = "dkr.clear_music"
    bl_label = "Use Game Music"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        settings = context.scene.dkr
        settings.music_source = "GAME"
        return {"FINISHED"}


CLASSES = (
    DKR_OT_choose_music,
    DKR_OT_check_music,
    DKR_OT_clear_music,
)
