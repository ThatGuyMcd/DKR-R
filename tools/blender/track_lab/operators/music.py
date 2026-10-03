"""Choose the music a track ships, and check it before export.

Two kinds: a recording (MP3/WAV) the runtime plays over a silent game song,
and a MIDI file converted into the game's own music format, which DKR then
plays on its own instruments in that song's place.

The format side - which files are music the runtime decodes, how long they are,
whether a loop fits, how a MIDI file becomes a native sequence - is
:mod:`..music_audio` and :mod:`..midi_import`, tested without Blender. The
package side is :meth:`..dkrmap.TrackPackage.set_music` and
:meth:`~..dkrmap.TrackPackage.set_sequence`. This module is only the Blender
half: picking a file, keeping its path portable, and telling the author what
was found while there is still time to fix it.

A MIDI file needs nothing else from the author: instruments, drums, tempo and
loop are all chosen by the converter. The conversion runs when the file is
chosen (for the report) and again at export (for the bytes), so a file saved
over in between is exported as it is now.

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

from .. import dkrmap, midi_import, music_audio, music_bank, music_sequence

#: The object types that act on the music's channels: they fade or switch
#: individual channels of the playing sequence. A recorded file has none.
CHANNEL_OBJECT_IDS = frozenset((
    "ASSET_OBJECT_MIDIFADE",
    "ASSET_OBJECT_MIDIFADEPOINT",
    "ASSET_OBJECT_MIDICHSET",
))


def uses_file(settings) -> bool:
    return settings.music_source == "FILE"


def uses_midi(settings) -> bool:
    return settings.music_source == "MIDI"


def file_path(settings) -> str:
    """The chosen file as an absolute path, or ``""``."""
    return bpy.path.abspath(settings.music_file) if settings.music_file else ""


def midi_path(settings) -> str:
    """The chosen MIDI file as an absolute path, or ``""``."""
    return bpy.path.abspath(settings.music_midi) if settings.music_midi else ""


def sequence_volume(settings) -> int:
    """DKR's own base volume for a converted song. 100% is 110, the level of
    most retail race songs; the game's ceiling is 127, so above about 115% the
    slider changes nothing."""
    level = dkrmap.DEFAULT_SEQUENCE_VOLUME * settings.music_volume / 100.0
    return max(0, min(dkrmap.MAX_SEQUENCE_VOLUME, int(round(level))))


_bank_cache = {"key": None, "bank": None}


def instrument_bank(context=None):
    """The game's instrument bank from the configured decomp assets, or
    ``None``. With it the conversion also checks every note reaches a sound;
    without it the song is still converted and checked against the buffer."""
    from .. import prefs  # noqa: PLC0415

    try:
        tree = prefs.resolve(context)
    except Exception:  # noqa: BLE001 - no assets is an ordinary state
        tree = None
    path = tree.music_bank_path() if tree is not None else None
    if not path:
        return None
    try:
        key = (path, os.path.getmtime(path))
    except OSError:
        return None
    if _bank_cache["key"] != key:
        try:
            with open(path, "rb") as handle:
                _bank_cache["bank"] = music_bank.parse_bank(handle.read())
        except (OSError, music_bank.BankError):
            _bank_cache["bank"] = None
        _bank_cache["key"] = key
    return _bank_cache["bank"]


def convert_midi(settings, context=None) -> "midi_import.Conversion":
    """Convert the chosen MIDI file. Raises MidiError or SequenceError with a
    message for the author."""
    path = midi_path(settings)
    if not path:
        raise midi_import.MidiError("Choose a MIDI file.")
    try:
        if os.path.getsize(path) > midi_import.MAX_FILE_BYTES:
            raise midi_import.MidiError("%s is over 4 MB; that is not a song DKR could hold."
                                        % os.path.basename(path))
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        raise midi_import.MidiError("Could not read %s: %s"
                                    % (os.path.basename(path), error.strerror or error))
    return midi_import.convert(data, bank=instrument_bank(context))


def describe(conversion) -> str:
    """One line for the panel: size against the buffer, channels, notes,
    tempo and loop."""
    report = conversion.report
    return "%d of %d bytes · %d channel(s) · %d note(s) · %.1f BPM · %s" % (
        len(conversion.data), music_sequence.RETAIL_CAPACITY, len(conversion.channels),
        report.notes if report else 0, conversion.bpm,
        "loops" if conversion.loop else "plays once")


def refresh_report(settings, context=None) -> None:
    """Inspect the chosen file and remember what was found."""
    settings.music_warnings = ""
    if uses_midi(settings):
        if not settings.music_midi:
            settings.music_report = ""
            settings.music_ok = False
            return
        try:
            conversion = convert_midi(settings, context)
        except (midi_import.MidiError, music_sequence.SequenceError) as error:
            settings.music_report = str(error)
            settings.music_ok = False
            return
        settings.music_report = describe(conversion)
        settings.music_warnings = "\n".join(conversion.warnings)
        settings.music_ok = True
        return
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
        refresh_report(settings, context)
        self.report({"INFO"}, "music: %s" % info.summary())
        return {"FINISHED"}


class DKR_OT_choose_midi(bpy.types.Operator, ImportHelper):
    """Choose a MIDI file for this track. It is converted to the game's own
    music format and played on DKR's instruments - nothing else to set"""

    bl_idname = "dkr.choose_midi"
    bl_label = "Choose MIDI File"
    bl_options = {"REGISTER", "UNDO"}

    filter_glob: StringProperty(default="*.mid;*.midi", options={"HIDDEN"})

    def execute(self, context):
        settings = context.scene.dkr
        path = self.filepath
        if bpy.data.filepath:
            try:
                path = bpy.path.relpath(path)
            except ValueError:
                pass  # another drive on Windows: keep it absolute
        settings.music_midi = path
        settings.music_source = "MIDI"
        refresh_report(settings, context)
        if not settings.music_ok:
            self.report({"ERROR"}, "%s: %s" % (os.path.basename(self.filepath),
                                               settings.music_report))
            return {"CANCELLED"}
        self.report({"INFO"}, "music: %s" % settings.music_report)
        return {"FINISHED"}


class DKR_OT_check_music(bpy.types.Operator):
    """Read the music file again and check it"""

    bl_idname = "dkr.check_music"
    bl_label = "Check Music"

    def execute(self, context):
        settings = context.scene.dkr
        refresh_report(settings, context)
        if uses_midi(settings):
            if not settings.music_ok:
                self.report({"ERROR"}, settings.music_report or "no MIDI file chosen")
                return {"CANCELLED"}
            self.report({"INFO"}, "music: %s" % settings.music_report)
            return {"FINISHED"}
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
    DKR_OT_choose_midi,
    DKR_OT_check_music,
    DKR_OT_clear_music,
)
