"""Exercise the track-music controls inside Blender.

test_music.py covers the format and the package without Blender. This covers
what only Blender can: the operators register and run, the settings survive a
save and reopen with the file kept relative to the .blend, the panel draws, an
imported level drops a music file chosen for something else, and the export
step attaches the music with the header's song as its carrier - for a
recording and for a MIDI file converted to the game's own format.

    blender --background --python tools/blender/tests/test_blender_music.py
"""

from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile
import traceback

import bpy

_HERE = os.path.dirname(os.path.abspath(__file__))
for argument in sys.argv:
    if argument.endswith("test_blender_music.py"):
        _HERE = os.path.dirname(os.path.abspath(argument))
        break

sys.path.insert(0, os.path.abspath(os.path.join(_HERE, "..")))

import dkr_track_editor  # noqa: E402
from dkr_track_editor import dkrmap, music_sequence  # noqa: E402

sys.path.insert(0, _HERE)
from test_midi_import import MidiTrack, midi_file  # noqa: E402

FIXTURES = os.path.join(_HERE, "fixtures")
FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def fresh():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.dkr.level_type = "RACE"


def test_defaults():
    print("a scene starts on the game's music")
    fresh()
    settings = bpy.context.scene.dkr
    check(settings.music_source == "GAME", "source defaults to Game Music")
    check(settings.music_volume == 100 and settings.music_final_lap == "speedup",
          "volume 100% and Speed Up by default")


def test_choose_and_persist(root):
    print("choosing a file, then saving and reopening")
    fresh()
    blend = os.path.join(root, "scene", "track.blend")
    os.makedirs(os.path.dirname(blend))
    song_dir = os.path.join(root, "scene", "music")
    os.makedirs(song_dir)
    song = os.path.join(song_dir, "song.mp3")
    shutil.copyfile(os.path.join(FIXTURES, "sine_stereo_44k.mp3"), song)
    bpy.ops.wm.save_as_mainfile(filepath=blend)

    settings = bpy.context.scene.dkr
    settings.music_loop_end = 99.0   # from some longer song
    result = bpy.ops.dkr.choose_music(filepath=song)
    check(result == {"FINISHED"}, "dkr.choose_music runs")
    check(settings.music_source == "FILE", "choosing a file switches the source")
    check(settings.music_file.startswith("//"),
          "the path is kept relative to the .blend (%s)" % settings.music_file)
    check(settings.music_ok and "MP3" in settings.music_report,
          "the file is checked: %s" % settings.music_report)
    check(settings.music_loop_end == 0.0, "a loop end past this song is reset")

    settings.music_volume = 80
    settings.music_loop_start = 0.5
    settings.music_final_lap = "constant"
    bpy.ops.wm.save_mainfile()
    bpy.ops.wm.open_mainfile(filepath=blend)
    settings = bpy.context.scene.dkr
    check(settings.music_source == "FILE" and settings.music_volume == 80 and
          abs(settings.music_loop_start - 0.5) < 1e-6 and
          settings.music_final_lap == "constant",
          "the settings survive a save and reopen")
    check(bpy.ops.dkr.check_music() == {"FINISHED"} and settings.music_ok,
          "the relative path still resolves after reopening")

    text = os.path.join(root, "not-music.mp3")
    with open(text, "w") as handle:
        handle.write("hello " * 1000)
    try:
        result = bpy.ops.dkr.choose_music(filepath=text)
    except RuntimeError:
        result = {"CANCELLED"}
    check(result == {"CANCELLED"} and settings.music_ok,
          "a file that is not music is refused and the chosen one kept")

    settings.music_file = text
    check(not settings.music_ok and settings.music_report,
          "typing a bad path reports why: %s" % settings.music_report)

    check(bpy.ops.dkr.clear_music() == {"FINISHED"} and
          settings.music_source == "GAME", "dkr.clear_music goes back to game music")


def test_panel_draws():
    print("the header panel draws in both modes")
    from dkr_track_editor.ui import panels

    class Layout:
        """Enough of UILayout for a draw call to run end to end."""

        def __getattr__(self, name):
            if name in ("row", "column", "box", "split", "grid_flow",
                        "column_flow"):
                return lambda *a, **k: Layout()
            if name in ("operator",):
                return lambda *a, **k: type("Props", (), {})()
            return lambda *a, **k: None

        def __setattr__(self, name, value):
            pass

    context = bpy.context
    for source in ("GAME", "FILE", "MIDI"):
        context.scene.dkr.music_source = source
        try:
            panels._draw_music(Layout(), context, None)
            check(True, "%s draws" % source)
        except Exception:  # noqa: BLE001 - reported as a failure
            traceback.print_exc()
            check(False, "%s draws" % source)


def test_export_attaches(root):
    print("the export step attaches the music with the header's song")
    fresh()
    from dkr_track_editor.operators import pack

    settings = bpy.context.scene.dkr
    settings.music_source = "FILE"
    settings.music_file = os.path.join(FIXTURES, "sine_mono_22k_id3.mp3")

    class Operator:
        def __init__(self):
            self.reports = []

        def report(self, kind, message):
            self.reports.append((kind, message))

    directory = os.path.join(root, "attached.dkrmap")
    package = dkrmap.TrackPackage(directory, "attached", "Attached")
    pack._attach_music(Operator(), bpy.context, package, {"music": 7})
    check(package.music is not None and package.music[1]["carrierSequence"] == 7,
          "the carrier is the header's /music")

    try:
        pack._attach_music(Operator(), bpy.context, package, {"music": 0})
        check(False, "a header with no music is refused")
    except dkrmap.DkrMapError as error:
        check("song under it" in str(error), "a header with no music is refused")

    # A MidiFade in the scene is reported, not removed.
    empty = bpy.data.objects.new("fade", None)
    bpy.context.scene.collection.objects.link(empty)
    empty["dkr_id"] = "ASSET_OBJECT_MIDIFADE"
    operator = Operator()
    pack._attach_music(operator, bpy.context, package, {"music": 7})
    check(any("no effect" in message for _kind, message in operator.reports),
          "a music-channel object is warned about")
    check(bpy.data.objects.get("fade") is not None, "and left in the scene")

    settings.music_source = "GAME"
    pack._attach_music(Operator(), bpy.context, package, {"music": 7})
    package.payloads["LEVEL_HEADERS"] = _header(root)
    package.write()
    with open(os.path.join(directory, "manifest.json"), encoding="utf-8") as handle:
        check("music" not in json.load(handle), "game music writes no descriptor")


def _write_midi(path, bpm=126):
    """A short GM song: piano chords, a bass line and a drum beat."""
    track = MidiTrack().tempo(0, bpm).program(0, 0, 0).program(0, 1, 33)
    for bar in range(4):
        start = bar * 1920
        for key in (60, 64, 67):
            track.note(start, 0, key, 90, 1800)
        track.note(start, 1, 36, 100, 900).note(start + 960, 1, 43, 100, 900)
        for beat in range(4):
            track.note(start + beat * 480, 9, 36 if beat % 2 == 0 else 38, 110, 120)
            track.note(start + beat * 480, 9, 42, 80, 120)
    with open(path, "wb") as handle:
        handle.write(midi_file(track))


def test_midi_choose_and_persist(root):
    print("choosing a MIDI file, then saving and reopening")
    fresh()
    blend = os.path.join(root, "midi-scene", "track.blend")
    os.makedirs(os.path.join(root, "midi-scene", "music"))
    song = os.path.join(root, "midi-scene", "music", "song.mid")
    _write_midi(song)
    bpy.ops.wm.save_as_mainfile(filepath=blend)

    settings = bpy.context.scene.dkr
    result = bpy.ops.dkr.choose_midi(filepath=song)
    check(result == {"FINISHED"}, "dkr.choose_midi runs")
    check(settings.music_source == "MIDI", "choosing a MIDI file switches the source")
    check(settings.music_midi.startswith("//"),
          "the path is kept relative to the .blend (%s)" % settings.music_midi)
    check(settings.music_ok and "126.0 BPM" in settings.music_report and
          "of %d bytes" % music_sequence.RETAIL_CAPACITY in settings.music_report,
          "the file is converted and described: %s" % settings.music_report)

    settings.music_volume = 90
    bpy.ops.wm.save_mainfile()
    bpy.ops.wm.open_mainfile(filepath=blend)
    settings = bpy.context.scene.dkr
    check(settings.music_source == "MIDI" and settings.music_volume == 90 and
          settings.music_midi.startswith("//"), "the settings survive a save and reopen")
    check(bpy.ops.dkr.check_music() == {"FINISHED"} and settings.music_ok,
          "the relative path still resolves after reopening")

    text = os.path.join(root, "not-midi.mid")
    with open(text, "w") as handle:
        handle.write("hello " * 1000)
    try:
        result = bpy.ops.dkr.choose_midi(filepath=text)
    except RuntimeError:
        result = {"CANCELLED"}
    check(result == {"CANCELLED"} and not settings.music_ok and settings.music_report,
          "a file that is not MIDI is reported: %s" % settings.music_report)
    check(bpy.ops.dkr.clear_music() == {"FINISHED"} and
          settings.music_source == "GAME", "dkr.clear_music goes back to game music")


def test_midi_export(root):
    print("the export step converts the MIDI file and enables every channel")
    fresh()
    from dkr_track_editor.operators import pack

    song = os.path.join(root, "export.mid")
    _write_midi(song, bpm=140)
    settings = bpy.context.scene.dkr
    settings.music_source = "MIDI"
    settings.music_midi = song

    class Operator:
        def report(self, kind, message):
            pass

    directory = os.path.join(root, "midi.dkrmap")
    package = dkrmap.TrackPackage(directory, "midi", "Midi")
    header = {"music": 7, "instruments": 0x000F}
    pack._attach_music(Operator(), bpy.context, package, header)
    descriptor = package.music[1] if package.music else {}
    check(descriptor.get("format") == dkrmap.SEQUENCE_FORMAT and
          descriptor.get("carrierSequence") == 7, "a sequence descriptor over the header's song")
    check(descriptor.get("tempoBpm") == 140 and descriptor.get("volume") == 110 and
          descriptor.get("channelMask") == 0xFFFF, "tempo, volume and channel mask")
    check(header["instruments"] == 0xFFFF, "the header enables every channel")

    package.payloads["LEVEL_HEADERS"] = _header(root)
    package.write()
    cseq = os.path.join(directory, "music", "main.cseq")
    check(os.path.isfile(cseq), "music/main.cseq is written")
    with open(cseq, "rb") as handle:
        data = handle.read()
    check(music_sequence.validate(data).notes > 0, "and it is a valid sequence")
    check(os.path.isfile(os.path.join(directory, "source", "music.mid")),
          "the MIDI file is kept under source/")
    with open(os.path.join(directory, "manifest.json"), encoding="utf-8") as handle:
        manifest = json.load(handle)
    check(manifest["schemaVersion"] == 2 and manifest["music"]["bytes"] == len(data),
          "the manifest describes it")

    try:
        pack._attach_music(Operator(), bpy.context, package, {"music": 0})
        check(False, "a header with no music is refused")
    except dkrmap.DkrMapError as error:
        check("song under it" in str(error), "a header with no music is refused")

    settings.music_midi = os.path.join(root, "missing.mid")
    try:
        pack._attach_music(Operator(), bpy.context, package, {"music": 7})
        check(False, "a missing MIDI file stops the export")
    except dkrmap.DkrMapError as error:
        check("MIDI" in str(error), "a missing MIDI file stops the export: %s" % error)

    settings.music_source = "GAME"
    pack._attach_music(Operator(), bpy.context, package, {"music": 7})
    package.write()
    check(not os.path.exists(cseq) and
          not os.path.exists(os.path.join(directory, "source", "music.mid")),
          "going back to game music removes the generated song")


def _header(root):
    path = os.path.join(root, "header.bin")
    with open(path, "wb") as handle:
        handle.write(bytes(0xC8))
    return path


def main():
    dkr_track_editor.register()
    root = tempfile.mkdtemp(prefix="dkr-blender-music-")
    try:
        test_defaults()
        test_choose_and_persist(root)
        test_panel_draws()
        test_export_attaches(root)
        test_midi_choose_and_persist(root)
        test_midi_export(root)
    except Exception:  # noqa: BLE001 - a crash is a failure, not a skip
        traceback.print_exc()
        FAILURES.append("uncaught exception")
    finally:
        shutil.rmtree(root, ignore_errors=True)
        dkr_track_editor.unregister()
    if FAILURES:
        print("\n%d failure(s)" % len(FAILURES))
        return 1
    print("\nall Blender music checks passed")
    return 0


if __name__ == "__main__":
    code = main()
    if bpy.app.background:
        sys.exit(code)
