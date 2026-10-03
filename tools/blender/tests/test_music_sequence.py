"""Check the native-sequence validator against songs built here, byte by byte.

DKR's music player trusts its data, so the validator's job is to refuse what
it would mishandle. Each case below is a small synthetic song - original data,
nothing extracted from the game - that either must pass or must fail with one
specific code. The same cases are written to fixtures/music_sequences.txt,
which the runtime's C++ validator reads too, so the two cannot drift apart:

    python tools/blender/tests/test_music_sequence.py
    python tools/blender/tests/test_music_sequence.py --write-fixtures

When the decomp's extracted assets are present (extern/dkr-decomp/assets/
.vanilla/<rev>/audio), every retail song is also read: none may fail for a
structural reason. They stay local; nothing from them is written anywhere.
"""

from __future__ import annotations

import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import music_bank, music_sequence  # noqa: E402

FIXTURES = os.path.join(_HERE, "fixtures", "music_sequences.txt")
REPO = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


# ------------------------------------------------------------------ builder

def var_len(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


class Track:
    """One track's bytes, at a known place in the sequence, so loop ends and
    back references can be written as absolute targets."""

    def __init__(self, base):
        self.base = base
        self.data = bytearray()

    @property
    def here(self):
        return self.base + len(self.data)

    def raw(self, *values):
        self.data += bytes(values)
        return self

    def delta(self, ticks):
        self.data += var_len(ticks)
        return self

    def tempo(self, bpm, ticks=0):
        micros = int(round(60000000 / bpm))
        return self.delta(ticks).raw(0xFF, 0x51, micros >> 16, (micros >> 8) & 0xFF, micros & 0xFF)

    def program(self, channel, number, ticks=0):
        return self.delta(ticks).raw(0xC0 | channel, number)

    def control(self, channel, controller, value, ticks=0):
        return self.delta(ticks).raw(0xB0 | channel, controller, value)

    def note(self, channel, key, velocity, duration, ticks=0):
        self.delta(ticks).raw(0x90 | channel, key, velocity)
        self.data += var_len(duration)
        return self

    def loop_start(self, ticks=0):
        self.delta(ticks)
        mark = self.here  # where the loop end will send the track: its next delta
        self.raw(0xFF, 0x2E, 0x00, 0xFF)
        return mark + 4

    def loop_end(self, target, count=0xFF, current=0xFF, ticks=0):
        self.delta(ticks).raw(0xFF, 0x2D, count, current)
        distance = self.here + 4 - target
        self.data += struct.pack(">I", distance & 0xFFFFFFFF)
        return self

    def end(self, ticks=0):
        return self.delta(ticks).raw(0xFF, 0x2F)


def sequence(*builders, division=384):
    """Lay tracks out after the header. Each builder receives a Track already
    placed at its final offset."""
    tracks = []
    offset = music_sequence.HEADER_BYTES
    for build in builders:
        track = Track(offset)
        build(track)
        tracks.append(track)
        offset += len(track.data)
    header = [t.base for t in tracks] + [0] * (16 - len(tracks))
    return struct.pack(">16I", *header) + struct.pack(">I", division) + b"".join(
        bytes(t.data) for t in tracks)


def song(track):
    track.tempo(120).program(0, 1).note(0, 60, 100, 192).note(0, 64, 100, 192, ticks=192).end(192)


def looping(track):
    track.tempo(120).program(0, 1)
    start = track.loop_start()
    track.note(0, 60, 100, 192).note(0, 67, 100, 192, ticks=384)
    track.loop_end(start, ticks=384).end()


# ------------------------------------------------------------------ cases

def cases():
    """(code, name, bytes). ``ok`` means the song must pass."""
    out = []

    def add(code, name, data):
        out.append((code, name, bytes(data)))

    add("ok", "minimal", sequence(song))
    add("ok", "two-tracks", sequence(
        lambda t: t.tempo(140).end(),
        lambda t: t.program(3, 7).control(3, 7, 100).control(3, 10, 64)
                   .note(3, 48, 90, 96).note(3, 50, 90, 96, ticks=96).end(96)))
    add("ok", "loop-forever", sequence(looping))
    add("ok", "loop-twice", sequence(looping_twice))

    def backref(t):
        t.program(0, 1)
        first = t.here
        t.note(0, 60, 100, 96)                    # 00 90 3C 64 60
        t.note(0, 60, 100, 96, ticks=96)          # 60 90 3C 64 60
        # Repeat the five bytes of the second note through a back reference.
        source = first + 5
        code = t.here
        distance = code - source
        t.raw(0xFE, distance >> 8, distance & 0xFF, 5)
        t.end(96)
    add("ok", "back-reference", sequence(backref))

    def escaped(t):
        # A delta whose first byte is 0xFE, written FE FE: 0x3F00 ticks.
        t.program(0, 1).note(0, 60, 100, 96)
        t.raw(0xFE, 0xFE, 0x00).raw(0x90, 62, 100, 96).end()
    add("ok", "escaped-fe", sequence(escaped))
    add("ok", "running-status", sequence(
        lambda t: t.program(0, 1).note(0, 60, 100, 96).delta(96).raw(62, 100, 96).end(96)))

    add("too-short", "too-short", b"\x00" * 40)
    add("too-large", "too-large", sequence(
        lambda t: (t.program(0, 1), [t.note(0, 60, 100, 1, ticks=1) for _ in range(2700)],
                   t.end())))
    add("division", "division-zero", sequence(song, division=0))
    add("no-tracks", "no-tracks", struct.pack(">16I", *([0] * 16)) + struct.pack(">I", 384))
    add("track-offset", "track-offset", struct.pack(">16I", 9000, *([0] * 15))
        + struct.pack(">I", 384) + b"\x00\xff\x2f")
    add("past-end", "no-end-of-track", sequence(
        lambda t: t.program(0, 1).note(0, 60, 100, 96)))
    add("backref-empty", "backref-empty", sequence(
        lambda t: t.program(0, 1).note(0, 60, 100, 96).raw(0xFE, 0x00, 0x05, 0x00).end()))
    add("backref-range", "backref-before-header", sequence(
        lambda t: t.program(0, 1).raw(0xFE, 0x01, 0x00, 0x04).end()))
    add("backref-range", "backref-overlaps-itself", sequence(
        lambda t: t.program(0, 1).note(0, 60, 100, 96).raw(0xFE, 0x00, 0x02, 0x05).end()))
    add("varlen", "varlen-too-long", sequence(
        lambda t: t.program(0, 1).raw(0x81, 0x81, 0x81, 0x81, 0x01).raw(0xFF, 0x2F)))
    add("tempo-zero", "tempo-zero", sequence(
        lambda t: t.raw(0x00, 0xFF, 0x51, 0, 0, 0).program(0, 1).note(0, 60, 100, 96).end()))

    def loop_in_backref(t):
        t.program(0, 1).note(0, 60, 100, 96)
        source = t.here + 1
        t.delta(0).raw(0xFF, 0x2D, 0x00, 0x00, 0, 0, 0, 0)  # a finished loop end
        t.delta(0)
        distance = t.here - source
        # Re-read FF 2D 00 through a back reference; the handler would then
        # take its counters from the raw stream, not from the copy.
        t.raw(0xFE, distance >> 8, distance & 0xFF, 3).end()
    add("loop-in-backref", "loop-in-backref", sequence(loop_in_backref))
    add("meta-unknown", "meta-text", sequence(
        lambda t: t.raw(0x00, 0xFF, 0x01).program(0, 1).note(0, 60, 100, 96).end()))
    add("system-message", "sysex", sequence(
        lambda t: t.raw(0x00, 0xF0, 0x00).program(0, 1).note(0, 60, 100, 96).end()))
    add("running-status", "running-status-first", sequence(
        lambda t: t.raw(0x00, 0x3C, 0x64, 0x10).end()))
    add("running-status", "running-status-after-meta", sequence(
        lambda t: t.program(0, 1).note(0, 60, 100, 96).tempo(120).delta(0).raw(62, 100, 96).end()))
    add("data-byte", "data-byte", sequence(
        lambda t: t.program(0, 1).raw(0x00, 0x90, 0x80, 0x64, 0x10).end()))

    def loop_mid_event(t):
        t.program(0, 1)
        target = t.here + 2  # inside the note below
        t.note(0, 60, 100, 96)
        t.loop_end(target, ticks=96).end()
    add("loop-target", "loop-mid-event", sequence(loop_mid_event))

    def loop_no_time(t):
        t.program(0, 1).note(0, 60, 100, 96)
        start = t.loop_start(ticks=96)
        t.control(0, 7, 100)
        t.loop_end(start).end()
    add("loop-direction", "loop-over-no-time", sequence(loop_no_time))

    def zero_time(t):
        t.note(0, 60, 100, 96).program(0, 1)
        for _ in range(music_sequence.MAX_EVENTS_PER_TICK + 2):
            t.raw(0x00, 0x01)  # running-status program changes, all at tick 0
        t.end()
    add("zero-time", "events-without-time", sequence(zero_time))
    add("no-notes", "no-notes", sequence(lambda t: t.tempo(120).program(0, 1).end(384)))
    add("velocity-zero", "velocity-zero", sequence(
        lambda t: t.program(0, 1).note(0, 60, 0, 96).end()))
    add("duration-zero", "duration-zero", sequence(
        lambda t: t.program(0, 1).note(0, 60, 100, 0).end()))
    add("note-off", "note-off", sequence(
        lambda t: t.program(0, 1).note(0, 60, 100, 96).raw(0x00, 0x80, 60, 0).end()))
    add("channel-switch", "channel-switch-17", sequence(
        lambda t: t.program(0, 1).control(0, 0x6A, 16).note(0, 60, 100, 96).end()))
    return out


def looping_twice(track):
    # count 2 / current 2: jump back twice, then pass - three times through.
    track.program(0, 1)
    start = track.loop_start()
    track.note(0, 60, 90, 96)
    track.loop_end(start, count=2, current=2, ticks=96).end()


# ------------------------------------------------------------------ fixtures

def fixture_text():
    lines = ["# Native DKR sequences and the code each must produce (ok = valid).",
             "# Generated by tools/blender/tests/test_music_sequence.py --write-fixtures;",
             "# read by runtime-recomp/tests/custom_music_sequence_tests.cpp.",
             "# Format: <code> <name> <hex bytes>"]
    lines += ["%s %s %s" % (code, name, data.hex()) for code, name, data in cases()]
    return "\n".join(lines) + "\n"


def outcome(data, bank=None):
    try:
        music_sequence.validate(data, bank)
        return "ok"
    except music_sequence.SequenceError as error:
        return error.code


def test_cases():
    print("synthetic songs")
    for code, name, data in cases():
        got = outcome(data)
        check(got == code, "%s -> %s (expected %s)" % (name, got, code))


def test_fixture_file():
    print("shared fixture file")
    try:
        with open(FIXTURES, encoding="utf-8") as handle:
            current = handle.read()
    except OSError:
        current = ""
    check(current == fixture_text(),
          "fixtures/music_sequences.txt matches the cases (rerun with --write-fixtures)")


def test_report():
    print("report")
    report = music_sequence.validate(sequence(looping))
    check(report.loops_forever and report.loops[0].period == 768, "loop span is read")
    check(abs(report.initial_bpm - 120.0) < 0.01, "initial tempo is read")
    check(report.channels == [0] and report.programs == {0: [1]}, "channels and programs")
    check(report.notes == 4, "first pass and one repeat are followed (%d notes)" % report.notes)
    report = music_sequence.validate(sequence(
        lambda t: t.tempo(120).program(0, 1).control(0, 23, 1).note(0, 60, 100, 96).end()))
    check(any("23" in w for w in report.warnings), "an ignored controller is a warning")
    # The loop counter is rewritten in a copy, never in the caller's bytes.
    data = sequence(looping_twice)
    before = bytes(data)
    report = music_sequence.validate(data)
    check(data == before and report.notes == 3, "a twice-repeated loop plays three times")


# ------------------------------------------------------------------ bank

def synthetic_bank():
    """A two-program B1 file: program 0 silent like DKR's, program 1 two
    sounds splitting the keyboard at middle C."""
    control = bytearray(b"\x42\x31\x00\x01")
    control += struct.pack(">I", 8)                       # bank at 8
    bank = 8
    control += struct.pack(">hBBiI", 3, 0, 0, 22050, 0)    # 3 slots, no percussion
    control += b"\x00" * 12                                # instArray filled below
    envelope = len(control)
    control += struct.pack(">iiiBB", 0, 100000, 200000, 127, 100) + b"\x00\x00"
    wave = len(control)
    control += struct.pack(">IiBBxx", 0, 1000, 0, 0) + b"\x00" * 8

    def key_map(vmin, vmax, kmin, kmax, base):
        at = len(control)
        control.extend(struct.pack(">BBBBBb", vmin, vmax, kmin, kmax, base, 0) + b"\x00\x00")
        return at

    def sound(keys):
        at = len(control)
        control.extend(struct.pack(">IIIBBBx", envelope, keys, wave, 64, 127, 0))
        return at

    low = sound(key_map(0, 127, 0, 59, 48))
    high = sound(key_map(0, 127, 60, 127, 72))
    silent = sound(key_map(2, 0, 2, 0, 0))

    def instrument(sounds):
        at = len(control)
        control.extend(struct.pack(">BBBBBBBBBBBBhh", 127, 64, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   200, len(sounds)))
        for s in sounds:
            control.extend(struct.pack(">I", s))
        return at

    struct.pack_into(">I", control, bank + 12, instrument([silent]))
    struct.pack_into(">I", control, bank + 16, instrument([low, high]))
    # slot 2 stays null
    return music_bank.parse_bank(bytes(control))


def test_bank():
    print("instrument bank")
    bank = synthetic_bank()
    check(bank.default_program.number == 0 and bank.program(2) is None, "slots read")
    check(bank.program(1).lookup(40, 100) is bank.program(1).sounds[0], "low split")
    check(bank.program(1).lookup(80, 100) is bank.program(1).sounds[1], "high split")
    check(bank.program(0).lookup(60, 100) is None, "silent program reaches nothing")
    check("program   1: 2 sound(s)" in music_bank.report(bank), "report lists programs")
    check(outcome(sequence(song), bank) == "ok", "a song on program 1 passes")
    check(outcome(sequence(lambda t: t.note(0, 60, 100, 96).end()), bank) == "no-program",
          "a note before any program change is refused")
    check(outcome(sequence(lambda t: t.program(0, 2).note(0, 60, 100, 96).end()), bank)
          == "program-missing", "a null program slot is refused")
    check(outcome(sequence(lambda t: t.program(0, 0).note(0, 60, 100, 96).end()), bank)
          == "note-unmapped", "a note the program cannot sound is refused")


def test_retail():
    found = False
    for revision in ("us.v77", "us.v80"):
        root = os.path.join(REPO, "extern", "dkr-decomp", "assets", ".vanilla", revision,
                            "audio", "unknown")
        control = os.path.join(root, "asset_audio_0.bin")
        songs = os.path.join(root, "asset_audio_5.bin")
        if not (os.path.isfile(control) and os.path.isfile(songs)):
            continue
        found = True
        print("retail songs, %s (local assets)" % revision)
        with open(songs, "rb") as handle:
            sequence_bank = handle.read()
        with open(control, "rb") as handle:
            bank = music_bank.parse_bank(handle.read())
        check(music_bank.sequence_capacity(sequence_bank) == music_sequence.RETAIL_CAPACITY,
              "music buffer capacity is %d" % music_sequence.RETAIL_CAPACITY)
        check(sum(p is not None for p in bank.programs) == 128, "128 programs")
        structural = []
        for index, data in enumerate(music_bank.sequences(sequence_bank)):
            code = outcome(data)
            if code not in ("ok", "no-notes"):
                structural.append("%d:%s" % (index, code))
            # Retail songs do drop a few notes; that is the only bank finding allowed.
            with_bank = outcome(data, bank)
            if with_bank not in ("ok", "no-notes", "no-program", "note-unmapped"):
                structural.append("%d:%s with bank" % (index, with_bank))
        check(not structural, "every retail song reads cleanly %s" % structural)
    if not found:
        print("retail songs: skipped (no extracted audio assets)")


def main():
    if "--write-fixtures" in sys.argv:
        os.makedirs(os.path.dirname(FIXTURES), exist_ok=True)
        with open(FIXTURES, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(fixture_text())
        print("wrote %s" % FIXTURES)
    test_cases()
    test_fixture_file()
    test_report()
    test_bank()
    test_retail()
    if FAILURES:
        print("\n%d failure(s)" % len(FAILURES))
        return 1
    print("\nall passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
