"""Check the music paths: recognising recordings, packaging them and native
sequences.

A track can ship an MP3 or WAV in place of a retail song. What this suite pins
down is the part an author meets before the game: which files are accepted,
how long the addon thinks they are (it must agree with the runtime's decoder,
or a loop point checked here lands past the end there), and what the package
carries.

The two MP3s under fixtures/ are test tones generated for this suite with
ffmpeg and LAME; the WAVs are built here.

    python tools/blender/tests/test_music.py
"""

from __future__ import annotations

import json
import math
import os
import shutil
import struct
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import dkrmap, music_audio  # noqa: E402

FIXTURES = os.path.join(_HERE, "fixtures")
FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def refused(callable_, *args):
    """The message an AudioError/DkrMapError carried, or None if none was raised."""
    try:
        callable_(*args)
    except (music_audio.AudioError, dkrmap.DkrMapError) as error:
        return str(error)
    return None


def wav_bytes(seconds=1.5, rate=32000, channels=2, bits=16, tag=1,
              extensible=False):
    frames = int(seconds * rate)
    width = bits // 8
    block = channels * width
    samples = bytearray()
    for index in range(frames):
        value = math.sin(2 * math.pi * 440 * index / rate) * 0.5
        for _ in range(channels):
            if tag == 3:
                samples += struct.pack("<f" if bits == 32 else "<d", value)
            elif bits == 8:
                samples += struct.pack("<B", int(128 + value * 127))
            elif bits == 24:
                samples += int(value * 8388607).to_bytes(3, "little", signed=True)
            else:
                samples += struct.pack("<h" if bits == 16 else "<i",
                                       int(value * (2 ** (bits - 1) - 1)))
    if extensible:
        fmt = struct.pack("<HHIIHHHHIH14s", 0xFFFE, channels, rate, rate * block,
                          block, bits, 22, bits, 0, tag,
                          b"\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71")
    else:
        fmt = struct.pack("<HHIIHH", tag, channels, rate, rate * block, block, bits)
    chunks = (b"fmt " + struct.pack("<I", len(fmt)) + fmt
              + b"LIST" + struct.pack("<I", 5) + b"INFOx\x00"   # odd chunk, padded
              + b"data" + struct.pack("<I", len(samples)) + bytes(samples))
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def test_mp3_fixtures():
    print("MP3 files are measured the way the runtime decodes them")
    # dr_mp3 drops the Xing/Info frame and the LAME priming and padding, so the
    # decoder yields exactly the tone's length: 2.0 s at 44.1 kHz and 1.5 s at
    # 22.05 kHz. Counting raw frames instead would overstate both.
    stereo = music_audio.inspect_file(os.path.join(FIXTURES, "sine_stereo_44k.mp3"))
    check(stereo.codec == "mp3" and stereo.channels == 2 and stereo.sample_rate == 44100,
          "stereo 44.1 kHz MP3 recognised")
    check(stereo.frames == 88200, "gapless length 88200 frames (got %d)" % stereo.frames)

    mono = music_audio.inspect_file(os.path.join(FIXTURES, "sine_mono_22k_id3.mp3"))
    check(mono.channels == 1 and mono.sample_rate == 22050,
          "mono 22.05 kHz MPEG-2 MP3 behind an ID3v2 tag recognised")
    check(mono.frames == 33075, "gapless length 33075 frames (got %d)" % mono.frames)


def test_mp3_without_header_frame():
    print("an MP3 with no Xing/LAME header counts every frame")
    # MPEG-1 Layer III, 128 kbit/s, 44.1 kHz, joint stereo: 417-byte frames.
    header = bytes((0xFF, 0xFB, 0x90, 0x44))
    frame = header + bytes(417 - 4)
    data = frame * 60
    info = music_audio.inspect_bytes(data)
    check(info.frames == 60 * 1152, "60 frames of 1152 samples")
    check(music_audio.inspect_bytes(data + b"TAG" + bytes(125)).frames == 60 * 1152,
          "an ID3v1 trailer is not read as audio")
    check(refused(music_audio.inspect_bytes, bytes(2) + data[:417 * 2]) is not None,
          "two frames are too short to be music")


def test_wav():
    print("WAV files")
    for bits in (8, 16, 24, 32):
        info = music_audio.inspect_bytes(wav_bytes(bits=bits))
        check(info.codec == "wav" and info.frames == 48000,
              "%d-bit PCM, 48000 frames (got %d)" % (bits, info.frames))
    check(music_audio.inspect_bytes(wav_bytes(tag=3, bits=32)).frames == 48000,
          "32-bit float")
    check(music_audio.inspect_bytes(wav_bytes(extensible=True, channels=1)).channels == 1,
          "WAVE_FORMAT_EXTENSIBLE mono")
    check("compressed" in (refused(music_audio.inspect_bytes, wav_bytes(tag=2)) or ""),
          "ADPCM is refused by name")
    check("channels" in (refused(music_audio.inspect_bytes, wav_bytes(channels=3)) or ""),
          "three channels are refused")
    check("at least" in (refused(music_audio.inspect_bytes, wav_bytes(seconds=0.5)) or ""),
          "half a second is refused as too short")


def test_not_audio():
    print("files that are not music")
    check(refused(music_audio.inspect_bytes, b"<html>not a song</html>" * 100) is not None,
          "text is refused")
    with open(os.path.join(FIXTURES, "sine_stereo_44k.mp3"), "rb") as handle:
        mp3 = handle.read()
    check(refused(music_audio.inspect_bytes, b"RIFF" + mp3[4:]) is not None,
          "an MP3 dressed as a WAV is refused by its bytes")
    check(refused(music_audio.inspect_file, os.path.join(FIXTURES, "missing.mp3")) is not None,
          "a missing file is refused")


def test_loop():
    print("loop points")
    info = music_audio.AudioInfo("wav", 32000, 2, 32000 * 10, 0)
    check(refused(music_audio.check_loop, info, 0.0, 0.0) is None, "whole file loops")
    check(refused(music_audio.check_loop, info, 2.5, 9.0) is None, "intro then loop")
    check(refused(music_audio.check_loop, info, 0.0, 10.5) is not None, "loop past the end")
    check(refused(music_audio.check_loop, info, 9.9, 0.0) is not None, "loop shorter than 0.25 s")
    check(refused(music_audio.check_loop, info, -1.0, 0.0) is not None, "negative loop start")


def test_package():
    print("the package carries the music and says so")
    root = tempfile.mkdtemp(prefix="dkr-music-")
    try:
        directory = os.path.join(root, "song-test.dkrmap")
        package = dkrmap.TrackPackage(directory, "song-test", "Song Test")
        package.payloads["LEVEL_HEADERS"] = _header_file(root)

        manifest = package.manifest()
        check(manifest["schemaVersion"] == 1 and "music" not in manifest,
              "without music the package stays schema 1")

        song = os.path.join(FIXTURES, "sine_stereo_44k.mp3")
        check("song under it" in (refused(package.set_music, song, 0) or ""),
              "a carrier of 0 (no music) is refused")
        check(refused(package.set_music, song, 12, 100, 0.0, 30.0) is not None,
              "a loop past the end of the file is refused")
        package.set_music(song, carrier=12, volume=250, loop_start=0.5,
                          loop_end=1.75, final_lap="constant")
        package.write()

        with open(os.path.join(directory, "manifest.json"), encoding="utf-8") as handle:
            written = json.load(handle)
        music = written.get("music", {})
        check(written["schemaVersion"] == 2, "with music the package is schema 2")
        check(music.get("file") == "music/main.mp3" and
              os.path.isfile(os.path.join(directory, "music", "main.mp3")),
              "the file is copied to music/main.mp3")
        check(music.get("carrierSequence") == 12, "the carrier is recorded")
        check(music.get("volume") == dkrmap.MAX_MUSIC_VOLUME, "volume is clamped to the maximum")
        check(music.get("loopStartFrame") == 22050 and music.get("loopEndFrame") == 77175,
              "loop points are written in sample frames")
        check(music.get("finalLap") == "constant", "final-lap behaviour is recorded")
        with open(song, "rb") as handle:
            import hashlib
            check(music.get("sha256") == hashlib.sha256(handle.read()).hexdigest(),
                  "the digest is the file's")
        check(music.get("frames") == 88200 and music.get("sampleRate") == 44100,
              "the decoded length is recorded")

        # Switching to a WAV replaces the MP3; switching back to the game's
        # music removes the copy but never the author's file.
        wav = os.path.join(root, "song.wav")
        with open(wav, "wb") as handle:
            handle.write(wav_bytes())
        package.set_music(wav, carrier=12)
        package.write()
        check(os.path.isfile(os.path.join(directory, "music", "main.wav")) and
              not os.path.isfile(os.path.join(directory, "music", "main.mp3")),
              "a new codec replaces the earlier copy")
        package.drop_music()
        package.write()
        with open(os.path.join(directory, "manifest.json"), encoding="utf-8") as handle:
            written = json.load(handle)
        check(written["schemaVersion"] == 1 and "music" not in written and
              not os.path.isdir(os.path.join(directory, "music")),
              "back to game music: schema 1 and no music folder")
        check(os.path.isfile(wav), "the author's own file is untouched")

        # The bytes shipped are the bytes validated.
        package.set_music(wav, carrier=12)
        with open(wav, "ab") as handle:
            handle.write(b"\0" * 64)
        check("changed" in (refused(package.write) or ""),
              "a file changed after validation is refused at write time")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def _fixture_sequence(name):
    """A sequence from the fixtures the Python and C++ validators share."""
    with open(os.path.join(FIXTURES, "music_sequences.txt"), encoding="utf-8") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) == 3 and parts[1] == name:
                return bytes.fromhex(parts[2])
    raise KeyError(name)


def test_sequence_package():
    print("the package carries a native sequence and says so")
    root = tempfile.mkdtemp(prefix="dkr-sequence-")
    try:
        directory = os.path.join(root, "tune-test.dkrmap")
        package = dkrmap.TrackPackage(directory, "tune-test", "Tune Test")
        package.payloads["LEVEL_HEADERS"] = _header_file(root)
        song = _fixture_sequence("loop-forever")
        midi = os.path.join(root, "tune.mid")
        with open(midi, "wb") as handle:
            handle.write(b"MThd")

        check("song under it" in (refused(package.set_sequence, song, 0, 120) or ""),
              "a carrier of 0 (no music) is refused")
        check("255 BPM" in (refused(package.set_sequence, song, 12, 300.0) or ""),
              "a tempo the final lap cannot scale is refused")
        check(refused(package.set_sequence, _fixture_sequence("no-end-of-track"), 12, 120)
              is not None, "bytes the player would misread are refused")
        package.set_sequence(song, carrier=12, tempo_bpm=127.97, volume=200,
                             source_path=midi)
        package.write()

        with open(os.path.join(directory, "manifest.json"), encoding="utf-8") as handle:
            written = json.load(handle)
        music = written.get("music", {})
        import hashlib
        check(written["schemaVersion"] == 2, "with a sequence the package is schema 2")
        check(music.get("format") == dkrmap.SEQUENCE_FORMAT and
              music.get("bank") == dkrmap.SEQUENCE_BANK, "the format and bank are named")
        path = os.path.join(directory, "music", "main.cseq")
        with open(path, "rb") as handle:
            check(handle.read() == song, "the bytes go to music/main.cseq unchanged")
        check(music.get("bytes") == len(song) and
              music.get("sha256") == hashlib.sha256(song).hexdigest(),
              "size and digest are the song's")
        check(music.get("tempoBpm") == 128, "the tempo is rounded to the game's whole BPM")
        check(music.get("volume") == dkrmap.MAX_SEQUENCE_VOLUME,
              "the volume is clamped to the game's 127")
        check(music.get("channelMask") == 0xFFFF and music.get("reverb") == 1,
              "every channel enabled, reverb on")
        check(os.path.isfile(os.path.join(directory, "source", dkrmap.SEQUENCE_SOURCE_FILE)),
              "the MIDI file is kept under source/")

        # A recording replaces the sequence, and the game's music removes both.
        package.set_music(os.path.join(FIXTURES, "sine_stereo_44k.mp3"), carrier=12)
        package.write()
        check(not os.path.isfile(path) and
              os.path.isfile(os.path.join(directory, "music", "main.mp3")) and
              not os.path.isfile(os.path.join(directory, "source", dkrmap.SEQUENCE_SOURCE_FILE)),
              "a recording replaces the sequence and its MIDI copy")
        package.set_sequence(song, carrier=12, tempo_bpm=120)
        package.drop_music()
        package.write()
        check(not os.path.isdir(os.path.join(directory, "music")),
              "back to game music: no music folder")
        check(os.path.isfile(midi), "the author's own MIDI file is untouched")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def _header_file(root):
    path = os.path.join(root, "header.bin")
    with open(path, "wb") as handle:
        handle.write(bytes(0xC8))
    return path


def main():
    test_mp3_fixtures()
    test_mp3_without_header_frame()
    test_wav()
    test_not_audio()
    test_loop()
    test_package()
    test_sequence_package()
    if FAILURES:
        print("\n%d failure(s)" % len(FAILURES))
        return 1
    print("\nall music checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
