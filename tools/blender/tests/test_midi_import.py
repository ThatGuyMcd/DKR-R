"""Check the MIDI-to-DKR converter: MIDI in, native sequence out, notes intact.

Every MIDI file here is written by this suite, so the expected notes are known
exactly. Each conversion is read back through music_sequence.validate - the
same reading of the bytes the game's player does - and the decoded notes,
programs and controllers are compared with what the MIDI file said.

    python tools/blender/tests/test_midi_import.py
"""

from __future__ import annotations

import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from dkr_track_editor import midi_import, music_bank, music_sequence  # noqa: E402
from test_music_sequence import synthetic_bank  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def raises(error_type, call):
    try:
        call()
    except error_type as error:
        return str(error) or True
    return None


# ------------------------------------------------------------------ SMF writer

def var_len(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


class MidiTrack:
    """Events at absolute ticks; written sorted, as a DAW would."""

    def __init__(self):
        self.events = []

    def add(self, tick, data):
        self.events.append((tick, len(self.events), bytes(data)))
        return self

    def tempo(self, tick, bpm):
        micros = int(round(60000000 / bpm))
        return self.add(tick, [0xFF, 0x51, 3, micros >> 16, (micros >> 8) & 0xFF, micros & 0xFF])

    def marker(self, tick, text):
        raw = text.encode("latin-1")
        return self.add(tick, bytes([0xFF, 0x06]) + var_len(len(raw)) + raw)

    def time_signature(self, tick, numerator, denominator_power):
        return self.add(tick, [0xFF, 0x58, 4, numerator, denominator_power, 24, 8])

    def program(self, tick, channel, number):
        return self.add(tick, [0xC0 | channel, number])

    def control(self, tick, channel, controller, value):
        return self.add(tick, [0xB0 | channel, controller, value])

    def note(self, tick, channel, key, velocity, length, off_velocity_zero=False):
        self.add(tick, [0x90 | channel, key, velocity])
        if off_velocity_zero:
            return self.add(tick + length, [0x90 | channel, key, 0])
        return self.add(tick + length, [0x80 | channel, key, 64])

    def sysex(self, tick):
        return self.add(tick, [0xF0, 5, 0x7E, 0x7F, 0x09, 0x01, 0xF7])

    def chunk(self, end=None):
        body = bytearray()
        tick = 0
        for at, _, data in sorted(self.events):
            body += var_len(at - tick) + data
            tick = at
        body += var_len(max(0, (end or tick) - tick)) + b"\xFF\x2F\x00"
        return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def midi_file(*tracks, division=480, midi_format=None, end=None):
    midi_format = (0 if len(tracks) == 1 else 1) if midi_format is None else midi_format
    return (b"MThd" + struct.pack(">IHHH", 6, midi_format, len(tracks), division)
            + b"".join(t.chunk(end) for t in tracks))


def notes_of(conversion):
    return [(n.tick, n.channel, n.key, n.velocity, n.duration)
            for n in conversion.report.note_list]


# ------------------------------------------------------------------ tests

def test_basic():
    print("format 0, one channel, whole song loops")
    track = MidiTrack().tempo(0, 140).program(0, 0, 5)
    track.note(0, 0, 60, 100, 240).note(480, 0, 64, 90, 240).note(960, 0, 67, 80, 480)
    result = midi_import.convert(midi_file(track))
    check(abs(result.bpm - 140) < 0.01 and abs(result.report.initial_bpm - 140) < 0.01,
          "tempo carried (%.2f BPM)" % result.bpm)
    check(result.loop == (0, 1920), "loop is the whole song rounded to a 4/4 bar %s"
          % (result.loop,))
    first = [n for n in notes_of(result) if n[0] < 1920]
    check(first == [(0, 0, 60, 100, 240), (480, 0, 64, 90, 240), (960, 0, 67, 80, 480)],
          "notes, velocities and durations survive %s" % first)
    repeat = [n for n in notes_of(result) if n[0] >= 1920]
    check([(t - 1920,) + tuple(rest) for t, *rest in repeat] == first,
          "the repeat plays the same notes one loop later")
    check(result.report.programs == {0: [9]} and result.programs == {(0, 5): 9},
          "GM 5 (a piano) plays DKR's first chord program, 9 %s" % result.programs)
    check(result.report.division == 480, "time division kept")
    check(not result.warnings, "no warnings %s" % result.warnings)


def test_markers_and_tracks():
    print("format 1, conductor track, loop markers, two channels")
    conductor = MidiTrack().tempo(0, 120).time_signature(0, 3, 2)
    conductor.marker(480, "loopStart").marker(1920, "Loop End")
    bass = MidiTrack().program(0, 1, 33).control(0, 1, 7, 90).control(0, 1, 10, 30)
    lead = MidiTrack().program(0, 2, 80)
    for beat in range(4):
        bass.note(beat * 480, 1, 36, 110, 400)
        lead.note(beat * 480 + 240, 2, 72, 70, 120)
    lead.program(960, 2, 81)  # changes inside the loop: each repeat must start on 80 again
    result = midi_import.convert(midi_file(conductor, bass, lead))
    check(result.loop == (480, 1920), "loop from the markers %s" % (result.loop,))
    check(result.channels == [1, 2], "two channels, one track each")
    check(result.report.tracks == [0, 1] and result.report.channels == [1, 2],
          "tracks %s, channels %s" % (result.report.tracks, result.report.channels))
    loops = {(loop.start_tick, loop.end_tick) for loop in result.report.loops}
    check(loops == {(480, 1920)}, "every track loops over the same span %s" % loops)
    at_repeat = [m for m in result.report.messages if m[0] == 1920 and m[1] == 0xC2]
    check(at_repeat == [(1920, 0xC2, 23, 0)],
          "the loop start restates the lead's first program on the repeat %s" % at_repeat)
    check(result.programs == {(1, 33): 22, (2, 80): 23, (2, 81): 28},
          "bass -> 22, two synth leads -> 23 then 28 %s" % result.programs)
    volume = [m for m in result.report.messages if m[1] == 0xB1 and m[2] == 7]
    check(volume and all(m[3] == 90 for m in volume), "bass volume restated with its value")
    repeat = sorted(n for n in notes_of(result) if 1920 <= n[0] < 1920 + 1440)
    first = sorted(n for n in notes_of(result) if 480 <= n[0] < 1920)
    check([(t + 1440,) + tuple(r) for t, *r in first] == [tuple(n) for n in repeat],
          "the loop body repeats note for note")


def test_note_pairing():
    print("note pairing")
    track = MidiTrack().program(0, 0, 1)
    track.note(0, 0, 60, 100, 100, off_velocity_zero=True)  # velocity-0 note-off
    track.add(200, [0x90, 62, 100]).add(300, [0x90, 62, 90]).add(400, [0x80, 62, 0])
    track.add(500, [0x80, 70, 0])                            # off with nothing playing
    track.add(600, [0x90, 64, 100])                          # never released
    result = midi_import.convert(midi_file(track, end=960), looping=False)
    check(notes_of(result) == [(0, 0, 60, 100, 100), (200, 0, 62, 100, 100),
                               (300, 0, 62, 90, 100), (600, 0, 64, 100, 360)],
          "velocity-0 off, retrigger and held-to-end %s" % notes_of(result))
    text = " ".join(result.warnings)
    check("started again" in text and "no note playing" in text and "still held" in text,
          "each case is reported")
    check(result.report.ends and not result.report.loops, "looping=False plays once")

    # The next note-on written before the previous note's note-off at the same
    # tick, as some exporters do: the off still ends the earlier note.
    track = MidiTrack().program(0, 0, 1)
    track.add(0, [0x90, 65, 100]).add(100, [0x90, 65, 90]).add(100, [0x80, 65, 0])
    track.add(200, [0x80, 65, 0])
    result = midi_import.convert(midi_file(track, end=480), looping=False)
    check(notes_of(result) == [(0, 0, 65, 100, 100), (100, 0, 65, 90, 100)],
          "an off written after the next on still ends the earlier note %s" % notes_of(result))
    check(not result.warnings, "and nothing is reported %s" % result.warnings)

    # Two layers playing the same key at once, each with its own note-off.
    track = MidiTrack().program(0, 0, 1)
    track.add(0, [0x90, 60, 51]).add(0, [0x90, 60, 52])
    track.add(80, [0x80, 60, 0]).add(120, [0x80, 60, 0])
    result = midi_import.convert(midi_file(track, end=480), looping=False)
    check(notes_of(result) == [(0, 0, 60, 52, 120)],
          "a doubled note is one note to the last note-off %s" % notes_of(result))
    check(any("doubled" in w for w in result.warnings) and len(result.warnings) == 1,
          "reported once, with no orphan note-off %s" % result.warnings)


def test_controllers():
    print("controllers")
    track = MidiTrack().control(0, 0, 7, 100).control(0, 0, 11, 64).program(0, 0, 3)
    track.control(0, 0, 1, 50).control(0, 0, 91, 40).note(0, 0, 60, 100, 240)
    track.add(120, [0xE0, 0, 0x50])
    result = midi_import.convert(midi_file(track))
    first_pass = [m for m in result.report.messages if m[0] < result.loop[1]]
    volumes = [m[3] for m in first_pass if m[1] == 0xB0 and m[2] == 7]
    check(volumes and volumes[-1] == 50, "expression 64 folded into volume 100 -> %s" % volumes)
    after_program = first_pass.index((0, 0xC0, 9, 0))
    check(first_pass[after_program + 1] == (0, 0xB0, 7, 50),
          "volume is written again after the program change resets it")
    check(not any(m[2] == 8 for m in result.report.messages if m[1] == 0xB0),
          "never writes DKR's game-driven channel fade (controller 8)")
    check(any(m[1] == 0xE0 for m in first_pass), "pitch bend kept")
    check(any("controller 1" in w for w in result.warnings), "mod wheel dropped and reported")


def test_programs_and_channel_10():
    print("automatic instruments")
    drums = MidiTrack().program(0, 9, 0)
    for step, key in enumerate((36, 38, 42, 40, 57, 80)):
        drums.note(step * 120, 9, key, 100, 60)
    drums.note(0, 3, 60, 90, 120)                    # channel 4, no program: a GM piano
    result = midi_import.convert(midi_file(drums))
    first = [n for n in notes_of(result) if n[0] < result.loop[1]]
    check([n[2] for n in first if n[1] == 9] == [36, 38, 42, 49],
          "main-kit keys kept, crash 2 played on the crash, triangle dropped %s"
          % [n[2] for n in first if n[1] == 9])
    check([(n[1], n[2]) for n in first if n[1] not in (3, 9)] == [(0, 40)],
          "the electric snare plays kit 41's own, on a free channel")
    check(result.report.programs == {0: [41], 3: [9], 9: [116]},
          "kits 116 and 41 and a piano program %s" % result.report.programs)
    text = " ".join(result.warnings)
    check("Played 1 drum" in text and "Dropped 1 drum" in text, "drum changes reported")

    latin = MidiTrack().control(0, 9, 7, 90)
    for step, key in enumerate((36, 70, 60, 61, 75, 56)):  # kick, maracas, bongos, claves, cowbell
        latin.note(step * 120, 9, key, 100, 60)
    result = midi_import.convert(midi_file(latin))
    kits = {channel: programs for channel, programs in result.report.programs.items()}
    check(sorted(kits.values()) == [[18], [41], [47], [63], [88], [116]],
          "a Latin part spreads over six kits %s" % kits)
    volumes = {m[1] & 0x0F for m in result.report.messages if m[1] & 0xF0 == 0xB0 and m[2] == 7}
    check(volumes == set(kits), "every kit channel carries the drum part's volume")
    pianos = MidiTrack().program(0, 0, 0).program(0, 1, 1)
    pianos.note(0, 0, 60, 90, 120).note(0, 1, 64, 90, 120)
    result = midi_import.convert(midi_file(pianos))
    check(result.programs == {(0, 0): 9, (1, 1): 76},
          "two pianos get two different programs %s" % result.programs)

    print("manual programs")
    result = midi_import.convert(midi_file(drums), program_map={0: 12})
    check(result.report.programs[3] == [12] and result.report.programs[9] == [116],
          "an override wins, except on the drum channel %s" % result.report.programs)
    result = midi_import.convert(midi_file(drums), program_map={0: 12}, auto_programs=False)
    check(result.report.programs == {3: [12], 9: [12]}, "auto_programs=False passes through %s"
          % result.report.programs)
    check(any("Channel 10" in w for w in result.warnings), "channel 10 is then ordinary")
    check(any("Channel 4 plays before any program change" in w for w in result.warnings),
          "a channel without a program gets the mapped default")


def test_tables_against_retail_bank():
    """Every drum piece and family program must exist in the game's own bank
    (local extracted assets only; nothing is read into the repository)."""
    repo = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
    found = False
    for revision in ("us.v77", "us.v80"):
        control = os.path.join(repo, "extern", "dkr-decomp", "assets", ".vanilla", revision,
                               "audio", "unknown", "asset_audio_0.bin")
        if not os.path.isfile(control):
            continue
        found = True
        print("instrument tables against the %s bank (local assets)" % revision)
        with open(control, "rb") as handle:
            bank = music_bank.parse_bank(handle.read())
        silent = ["%d->%s" % (key, piece) for key, piece in sorted(midi_import.DRUM_PIECES.items())
                  if bank.program(piece[0]) is None
                  or any(bank.program(piece[0]).lookup(piece[1], v) is None for v in (1, 64, 127))]
        check(not silent, "every drum piece sounds at any velocity %s" % silent)
        programs = {p for family in midi_import.FAMILY_PROGRAMS.values() for p in family}
        missing = [p for p in sorted(programs) if bank.program(p) is None
                   or bank.program(p).lookup(60, 100) is None]
        check(not missing, "every family program sounds middle C %s" % missing)
    if not found:
        print("instrument tables: skipped (no extracted audio assets)")


def test_with_bank():
    print("against an instrument bank")
    bank = synthetic_bank()
    track = MidiTrack().program(0, 0, 1).note(0, 0, 40, 100, 120).note(240, 0, 80, 100, 120)
    result = midi_import.convert(midi_file(track), bank=bank, auto_programs=False)
    check(result.report.notes >= 2, "notes inside the program's regions pass")
    error = raises(music_sequence.SequenceError, lambda: midi_import.convert(
        midi_file(MidiTrack().note(0, 0, 60, 100, 120)), bank=bank, auto_programs=False))
    check(error and "program 0" in error, "unmapped GM default hits DKR's silent program 0")


def test_refusals():
    print("refusals")
    tempo_map = MidiTrack().tempo(0, 120).tempo(960, 150).program(0, 0, 1)
    tempo_map.note(0, 0, 60, 100, 100)
    error = raises(midi_import.MidiError, lambda: midi_import.convert(midi_file(tempo_map),
                                                                   single_tempo=False))
    check(error and "120.00" in error and "150.00" in error, "tempo map refused: %s" % error)
    flat = midi_import.convert(midi_file(tempo_map, end=3840), single_tempo=True)
    check(abs(flat.bpm - 150) < 0.01 and any("150.00 BPM" in w for w in flat.warnings),
          "single_tempo plays the tempo heard longest (%.2f)" % flat.bpm)
    late = MidiTrack().program(0, 0, 1).note(0, 0, 60, 100, 100).tempo(480, 90)
    check(raises(midi_import.MidiError, lambda: midi_import.convert(midi_file(late),
                                                                    single_tempo=False)),
          "a tempo set after the first note is refused")
    song = MidiTrack().program(0, 0, 1).note(0, 0, 60, 100, 100)
    check(raises(midi_import.MidiError, lambda: midi_import.convert(
        midi_file(song, midi_format=2))), "format 2 refused")
    smpte = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 0xE728) + song.chunk()
    check(raises(midi_import.MidiError, lambda: midi_import.convert(smpte)), "SMPTE refused")
    check(raises(midi_import.MidiError, lambda: midi_import.convert(b"RIFF....WAVEfmt ")),
          "not a MIDI file")
    check(raises(midi_import.MidiError, lambda: midi_import.convert(midi_file(song)[:-6])),
          "truncated file refused")
    check(raises(midi_import.MidiError, lambda: midi_import.convert(
        midi_file(MidiTrack().tempo(0, 120)))), "a file with no notes refused")
    # Pseudo-random notes: nothing repeats, so back references cannot save it.
    big = MidiTrack().program(0, 0, 1)
    state = 12345
    for i in range(3500):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        big.note(i * 10 + state % 7, 0, 30 + state % 60, 1 + (state >> 8) % 126,
                 1 + (state >> 16) % 300)
    error = raises(music_sequence.SequenceError, lambda: midi_import.convert(midi_file(big)))
    check(error and "13032" in error, "too large for the music buffer: %s" % error)
    check(raises(midi_import.MidiError, lambda: midi_import.convert(midi_file(song), loop=(500, 400))),
          "a loop that ends before it starts is refused")


def test_sysex_and_running_status():
    print("tolerated input")
    track = MidiTrack().sysex(0).program(0, 0, 1)
    # Running status in the source file: three note-ons sharing one status.
    track.add(0, [0x90, 60, 100]).add(0, [0x90, 64, 100]).add(0, [0x90, 67, 100])
    track.add(480, [0x80, 60, 0]).add(480, [0x80, 64, 0]).add(480, [0x80, 67, 0])
    data = bytearray(midi_file(track))
    # Strip the repeated 0x90 status bytes to make the file use running status.
    data = bytes(data).replace(bytes([0x00, 0x90, 64, 100]), bytes([0x00, 64, 100]))
    data = data.replace(bytes([0x00, 0x90, 67, 100]), bytes([0x00, 67, 100]))
    data = data[:18] + struct.pack(">I", len(data) - 22) + data[22:]
    result = midi_import.convert(data)
    check(len([n for n in notes_of(result) if n[0] == 0]) == 3, "source running status read")
    check(any("SysEx" in w for w in result.warnings), "SysEx ignored and reported")


def test_compression():
    print("back references and escaping")
    track = MidiTrack().tempo(0, 128).program(0, 0, 1).program(0, 1, 2)
    for bar in range(32):  # a riff repeated, as songs are
        for step, key in enumerate((60, 63, 67, 70, 72, 70, 67, 63)):
            track.note(bar * 1920 + step * 240, 0, key, 100 - step, 200)
        track.note(bar * 1920, 1, 36, 110, 900).note(bar * 1920 + 960, 1, 43, 110, 900)
    data = midi_file(track)
    packed = midi_import.convert(data)
    source = midi_import.parse_midi(data)
    plain = midi_import.Conversion()
    # The same song without compression, decoded the same way.
    plain_bytes = midi_import._encode(source.division, packed.tempo, _tracks_of(data),
                                      packed.loop[1], compress=False)
    unpacked = music_sequence.validate(plain_bytes)
    check(len(packed.data) * 3 < len(plain_bytes),
          "a repeated riff packs %d -> %d bytes" % (len(plain_bytes), len(packed.data)))
    check([(n.tick, n.channel, n.key, n.velocity, n.duration) for n in unpacked.note_list]
          == notes_of(packed) and unpacked.messages == packed.report.messages,
          "packed and unpacked songs decode to the same events")
    del plain
    # 16128 ticks encodes as FE 00: the FE must be doubled or the player
    # would take it for a back reference.
    long_note = MidiTrack().program(0, 0, 1).note(0, 0, 60, 100, 16128)
    result = midi_import.convert(midi_file(long_note, end=16128), looping=False)
    check(notes_of(result) == [(0, 0, 60, 100, 16128)], "a duration starting with 0xFE survives")


def _tracks_of(data):
    """Re-run the conversion up to encoding, to encode it a second way."""
    captured = {}
    original = midi_import._encode

    def capture(division, tempo, tracks, song_end, compress=True):
        captured["tracks"] = [list(events) for events in tracks]
        return original(division, tempo, tracks, song_end, compress)
    midi_import._encode = capture
    try:
        midi_import.convert(data)
    finally:
        midi_import._encode = original
    return captured["tracks"]


def main():
    test_basic()
    test_compression()
    test_markers_and_tracks()
    test_note_pairing()
    test_controllers()
    test_programs_and_channel_10()
    test_tables_against_retail_bank()
    test_with_bank()
    test_refusals()
    test_sysex_and_running_status()
    if FAILURES:
        print("\n%d failure(s)" % len(FAILURES))
        return 1
    print("\nall passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
