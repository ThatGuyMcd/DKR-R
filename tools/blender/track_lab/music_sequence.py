"""Validate a native DKR music sequence before it reaches the game's player.

DKR plays music as *compact* sequences (libultra's ``ALCSeq``): a 68-byte
header of sixteen big-endian track offsets and a time division, then per-track
event streams. They are not MIDI files. Notes carry their own duration instead
of a note-off, a track may repeat earlier bytes through a ``0xFE`` back
reference, and loops are meta events whose counters the player rewrites in the
buffer as it plays. A renamed ``.mid`` fails here, by the bytes, not the
extension.

The player trusts the data completely. An unknown meta event leaves its event
type uninitialised, a zero-length back reference makes a byte counter wrap, a
loop that lands mid-event desynchronises a track for good, and a loop with no
ticks in it never lets the frame end. So this module reads a song the way
``cseq.c`` and ``csplayer.c`` will - the same byte reader, the same loop and
running-status rules, the same merge of the sixteen tracks in tick order - and
refuses anything the player would get wrong, with a message an author can act
on. Each rule cites the behaviour it guards; ``runtime-recomp`` carries a C++
twin of this validator and the two are held to the same fixtures.

Given the instrument bank (``music_bank``), it also follows each channel's
program and checks that every note reaches a sound, as the game's own
binary-search lookup would find it.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import struct
from typing import Dict, List, Optional, Tuple

from . import music_bank

HEADER_BYTES = 68
TRACKS = 16

#: The music buffer the retail game allocates at boot (largest even-rounded
#: song in US 1.0 and 1.1). The runtime copies a custom song into that same
#: buffer, so it must fit. Prefer ``music_bank.sequence_capacity`` of the
#: author's own ROM when it is at hand.
RETAIL_CAPACITY = 13032

#: The music player's voices (``sound_seqplayer_init(24, 120)``). A note that
#: finds none is dropped by ``__mapVoice``; notes in their release tail still
#: hold one, so a song should stay well below.
PLAYER_VOICES = 24
POLYPHONY_WARNING = 18

#: Work bounds. A song that is valid needs nowhere near them; a damaged one
#: could otherwise make validation spin.
MAX_EVENTS = 400000
MAX_EVENTS_PER_TICK = 4096
MAX_TICKS = 1 << 30

_META = 0xFF
_META_TEMPO = 0x51
_META_END = 0x2F
_META_LOOP_START = 0x2E
_META_LOOP_END = 0x2D
_BLOCK = 0xFE
_INFINITE = 0xFF

NOTE_OFF, NOTE_ON, POLY_PRESSURE, CONTROL, PROGRAM, CHANNEL_PRESSURE, PITCH_BEND = (
    0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0)

#: Controllers ``__CSPHandleMIDIMsg`` acts on; the player ignores the rest, so
#: a song that relies on one is not doing what its author hears elsewhere.
CONTROLLERS = {
    0x07: "volume",
    0x08: "channel fade",
    0x0A: "pan",
    0x10: "voice priority",
    0x40: "sustain",
    0x5B: "reverb send",
    0x5F: "reverb threshold",
    0x6A: "channel off",
    0x6C: "channel on",
}
_CHANNEL_SWITCHES = (0x6A, 0x6C)


class SequenceError(Exception):
    """A song the player would mishandle. ``code`` is stable and shared with
    the runtime's C++ validator; the message is for the author."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


class Note:
    __slots__ = ("tick", "channel", "key", "velocity", "duration", "track")

    def __init__(self, tick, channel, key, velocity, duration, track):
        self.tick = tick
        self.channel = channel
        self.key = key
        self.velocity = velocity
        self.duration = duration
        self.track = track


class Loop:
    """One loop-end event and where it sends its track."""

    __slots__ = ("track", "start_tick", "end_tick", "count", "infinite")

    def __init__(self, track, start_tick, end_tick, count, infinite):
        self.track = track
        self.start_tick = start_tick
        self.end_tick = end_tick
        self.count = count
        self.infinite = infinite

    @property
    def period(self) -> int:
        return self.end_tick - self.start_tick


class Report:
    """What a valid song contains, for the export report and the panel."""

    def __init__(self):
        self.size = 0
        self.division = 0
        self.tracks: List[int] = []
        self.channels: List[int] = []
        self.programs: Dict[int, List[int]] = {}
        self.controllers: Dict[int, int] = {}
        self.notes = 0
        self.tempos: List[Tuple[int, int]] = []
        self.loops: List[Loop] = []
        self.length_ticks = 0
        self.ends = False
        self.max_polyphony = 0
        self.warnings: List[str] = []
        #: Every note played, in play order, through the first pass and (for
        #: a looping song) one repeat.
        self.note_list: List[Note] = []
        #: (tick, status, byte1, byte2) of every other channel message, likewise.
        self.messages: List[Tuple[int, int, int, int]] = []

    @property
    def initial_bpm(self) -> Optional[float]:
        if not self.tempos or self.tempos[0][0] != 0:
            return None
        return 60000000.0 / self.tempos[0][1]

    @property
    def loops_forever(self) -> bool:
        return any(loop.infinite for loop in self.loops)

    def summary(self) -> str:
        bpm = self.initial_bpm
        return "%d bytes · %d channel(s) · %d note(s) · %s · %s" % (
            self.size, len(self.channels), self.notes,
            "%.1f BPM" % bpm if bpm else "no initial tempo",
            "loops" if self.loops_forever else "plays once")


# --------------------------------------------------------------- byte reader

class _Reader:
    """``__getTrackByte`` and ``__readVarLen`` for one track, with the bounds
    the original does not check."""

    __slots__ = ("data", "track", "start", "position", "backup", "backup_left")

    def __init__(self, data, track, start):
        self.data = data
        self.track = track
        self.start = start
        self.position = start
        self.backup = 0
        self.backup_left = 0

    def fail(self, code, message):
        raise SequenceError(code, "Track %d %s" % (self.track + 1, message))

    def raw(self, position):
        if position >= len(self.data):
            self.fail("past-end", "runs past the end of the sequence.")
        return self.data[position]

    def byte(self) -> int:
        if self.backup_left:
            value = self.raw(self.backup)
            self.backup += 1
            self.backup_left -= 1
            return value
        code = self.position
        value = self.raw(self.position)
        self.position += 1
        if value != _BLOCK:
            return value
        following = self.raw(self.position)
        self.position += 1
        if following == _BLOCK:
            return _BLOCK  # FE FE is a literal FE.
        distance = (following << 8) | self.raw(self.position)
        length = self.raw(self.position + 1)
        self.position += 2
        source = self.position - (distance + 4)
        # cseq.c reads `length` bytes from `source` with no checks at all; a
        # zero length wraps curBULen to 0xFFFFFFFF.
        if length == 0:
            self.fail("backref-empty", "has a back reference of length zero.")
        if source < HEADER_BYTES or source + length > code:
            self.fail("backref-range", "has a back reference outside the bytes before it.")
        self.backup = source + 1
        self.backup_left = length - 1
        return self.raw(source)

    def var_len(self) -> int:
        value = self.byte()
        if value & 0x80:
            value &= 0x7F
            for _ in range(3):
                more = self.byte()
                value = (value << 7) | (more & 0x7F)
                if not more & 0x80:
                    return value
            self.fail("varlen", "has a variable-length number longer than four bytes.")
        return value


# --------------------------------------------------------------- events

_DELTA, _MIDI, _TEMPO, _END, _LOOP_START, _LOOP_END = range(6)


class _Event:
    __slots__ = ("kind", "status", "byte1", "byte2", "duration", "tempo",
                 "loop_count", "loop_current", "loop_target", "running")

    def __init__(self, kind):
        self.kind = kind
        self.status = self.byte1 = self.byte2 = self.duration = self.tempo = 0
        self.loop_count = self.loop_current = self.loop_target = 0
        self.running = False


def _read_event(reader: _Reader, last_status: int) -> Tuple[_Event, int]:
    """``__alCSeqGetTrackEvent``: one event, and the running status after it."""
    status = reader.byte()
    if status == _META:
        kind = reader.byte()
        if kind == _META_TEMPO:
            event = _Event(_TEMPO)
            event.tempo = (reader.byte() << 16) | (reader.byte() << 8) | reader.byte()
            if event.tempo == 0:
                reader.fail("tempo-zero", "sets a tempo of zero microseconds per beat.")
            return event, 0
        if kind == _META_END:
            return _Event(_END), last_status
        if kind == _META_LOOP_START:
            reader.byte()
            reader.byte()
            return _Event(_LOOP_START), 0
        if kind == _META_LOOP_END:
            # The handler reads its six bytes straight from curLoc, past any
            # back reference in progress, and jumps relative to them.
            if reader.backup_left:
                reader.fail("loop-in-backref", "ends a loop inside a back reference.")
            at = reader.position
            event = _Event(_LOOP_END)
            event.loop_count = reader.raw(at)
            event.loop_current = reader.raw(at + 1)
            distance = struct.unpack_from(">I", reader.data, at + 2)[0] \
                if at + 6 <= len(reader.data) else reader.fail("past-end", "runs past the end of the sequence.")
            event.loop_target = at + 6 - distance
            reader.position = at + 6
            return event, 0
        # Release builds leave event->type uninitialised here.
        reader.fail("meta-unknown", "has meta event 0x%02X, which DKR's player does not understand." % kind)
    event = _Event(_MIDI)
    if status & 0x80:
        if status >= 0xF0:
            reader.fail("system-message", "has system message 0x%02X; compact sequences carry only channel "
                        "messages and meta events." % status)
        event.status = status
        event.byte1 = reader.byte()
        last_status = status
    else:
        if not last_status:
            reader.fail("running-status", "uses running status with no status before it (after a meta "
                        "event or at a loop start every event must carry its own).")
        event.status = last_status
        event.byte1 = status
        event.running = True
    kind = event.status & 0xF0
    if kind not in (PROGRAM, CHANNEL_PRESSURE):
        event.byte2 = reader.byte()
        if kind == NOTE_ON:
            event.duration = reader.var_len()
    if event.byte1 > 0x7F or event.byte2 > 0x7F:
        reader.fail("data-byte", "has a data byte above 0x7F in a 0x%02X message." % event.status)
    return event, last_status


class _TrackScan:
    """A track read once, front to back, as if every loop had finished: where
    each event starts and at which tick, which is what loop targets must hit."""

    def __init__(self, data, track, start):
        self.boundaries: Dict[int, Tuple[int, int]] = {}
        self.loops: List[Tuple[int, _Event]] = []
        reader = _Reader(data, track, start)
        tick = 0
        last_status = 0
        events = 0
        self.last_note_tick = None
        while True:
            if not reader.backup_left:
                self.boundaries[reader.position] = (tick, last_status)
            tick += reader.var_len()
            if tick > MAX_TICKS:
                reader.fail("too-long", "is longer than any song could be.")
            event, last_status = _read_event(reader, last_status)
            events += 1
            if events > MAX_EVENTS:
                reader.fail("too-many-events", "has more events than a song could hold.")
            if event.kind == _MIDI and event.status & 0xF0 == NOTE_ON:
                self.last_note_tick = tick
            if event.kind == _END:
                self.end_tick = tick
                break
            if event.kind == _LOOP_END:
                self.loops.append((tick, event))
                if event.loop_current == _INFINITE:
                    # Never passed: whatever follows is unreachable, and
                    # retail songs do leave stray bytes there.
                    self.end_tick = None
                    break
        for tick, event in self.loops:
            target = self.boundaries.get(event.loop_target)
            if target is None:
                reader.fail("loop-target", "loops back to a byte that is not the start of an event.")
            target_tick, _ = target
            if target_tick >= tick:
                # A loop over no ticks would replay forever within one frame.
                reader.fail("loop-direction", "loops forward or over no time; a loop must jump "
                            "back across at least one tick.")


# --------------------------------------------------------------- playback

class _Track:
    __slots__ = ("reader", "next_tick", "last_status", "done")


def validate(data: bytes, bank: Optional[music_bank.Bank] = None,
             capacity: int = RETAIL_CAPACITY) -> Report:
    """Check a native sequence and describe it. Raises SequenceError with the
    first problem found."""
    data = bytes(data)
    report = Report()
    report.size = len(data)
    if len(data) < HEADER_BYTES:
        raise SequenceError("too-short", "Too short to be a DKR sequence (%d bytes)." % len(data))
    if len(data) > capacity:
        raise SequenceError("too-large", "The sequence is %d bytes; DKR's music buffer holds %d."
                            % (len(data), capacity))
    offsets = struct.unpack_from(">16I", data, 0)
    report.division = struct.unpack_from(">I", data, 64)[0]
    if not 0 < report.division <= 0x7FFF:
        raise SequenceError("division", "The time division (%d ticks per beat) is not usable."
                            % report.division)
    report.tracks = [i for i, offset in enumerate(offsets) if offset]
    if not report.tracks:
        raise SequenceError("no-tracks", "The sequence has no tracks.")
    for track in report.tracks:
        if not HEADER_BYTES <= offsets[track] < len(data):
            raise SequenceError("track-offset", "Track %d starts outside the sequence." % (track + 1))

    scans = {track: _TrackScan(data, track, offsets[track]) for track in report.tracks}
    for track, scan in scans.items():
        for tick, event in scan.loops:
            start_tick = scan.boundaries[event.loop_target][0]
            infinite = event.loop_current == _INFINITE
            report.loops.append(Loop(track, start_tick, tick, event.loop_count, infinite))
    infinite = [loop for loop in report.loops if loop.infinite]
    horizon = None
    if infinite:
        # Follow the first pass and one full repeat of the longest loop: that
        # crosses every loop seam at least once. A song that ends is followed
        # to its end.
        horizon = max(loop.end_tick + loop.period for loop in infinite)
        shapes = sorted({(loop.start_tick, loop.end_tick) for loop in infinite})
        if len(shapes) > 1:
            report.warnings.append(
                "The tracks loop over different spans (%s ticks). Retail songs do this, but "
                "tracks whose loops differ in length drift apart on every repeat."
                % ", ".join("%d-%d" % shape for shape in shapes))
        start = min(shape[0] for shape in shapes)
        late = [track for track, scan in scans.items()
                if scan.end_tick is not None and scan.last_note_tick is not None
                and scan.last_note_tick >= start]
        if late:
            report.warnings.append(
                "Track(s) %s play past the loop start but do not loop; they fall silent "
                "after the first pass." % ", ".join(str(t + 1) for t in late))

    _play(data, offsets, report, bank, horizon)
    return report


def _play(data, offsets, report, bank, horizon):
    """``alCSeqNextEvent`` over a private copy of the buffer (loop counters are
    written back into it), up to the end of the song or, for a looping song,
    through the first pass and one full repeat."""
    buffer = bytearray(data)
    tracks = {}
    for track in report.tracks:
        state = _Track()
        state.reader = _Reader(buffer, track, offsets[track])
        state.next_tick = state.reader.var_len()
        state.last_status = 0
        state.done = False
        tracks[track] = state

    channels = set()
    programs: Dict[int, set] = {}
    default = bank.default_program if bank else None
    channel_program: List[Optional[music_bank.Program]] = [default] * 16
    channel_set = [False] * 16
    sounding: List[Tuple[int, int]] = []  # (end tick, channel) of notes playing
    events = 0
    tick = 0
    same_tick = 0
    while True:
        live = [t for t in report.tracks if not tracks[t].done]
        if not live:
            report.ends = True
            break
        track = min(live, key=lambda t: (tracks[t].next_tick, t))
        state = tracks[track]
        if horizon is not None and state.next_tick >= horizon:
            break
        same_tick = same_tick + 1 if state.next_tick == tick else 0
        tick = state.next_tick
        if same_tick > MAX_EVENTS_PER_TICK:
            raise SequenceError("zero-time", "Track %d produces events forever without time passing."
                                % (track + 1))
        events += 1
        if events > MAX_EVENTS:
            raise SequenceError("too-many-events", "The song has more events than the validator will follow.")
        reader = state.reader
        event, state.last_status = _read_event(reader, state.last_status)
        report.length_ticks = max(report.length_ticks, tick)
        if event.kind == _END:
            state.done = True
            continue
        if event.kind == _TEMPO:
            report.tempos.append((tick, event.tempo))
        elif event.kind == _LOOP_END:
            at = reader.position - 6
            if event.loop_current == 0:
                buffer[at + 1] = event.loop_count
            else:
                if event.loop_current != _INFINITE:
                    buffer[at + 1] = event.loop_current - 1
                reader.position = event.loop_target
                reader.backup_left = 0
        elif event.kind == _MIDI:
            _midi(event, tick, track, report, bank, channels, programs,
                  channel_program, channel_set, sounding)
        state.next_tick = tick + reader.var_len()

    report.channels = sorted(channels)
    report.programs = {channel: sorted(numbers) for channel, numbers in programs.items()}
    if not report.notes:
        raise SequenceError("no-notes", "The sequence plays no notes.")
    if report.tempos and report.tempos[0][0] != 0:
        report.warnings.append("The first tempo event is not at the start; DKR plays the "
                               "opening at its default tempo.")
    if len({tempo for _, tempo in report.tempos}) > 1:
        report.warnings.append("The tempo changes during the song. DKR's final-lap speed-up "
                               "scales the tempo it last read, so a later change undoes it.")
    ignored = sorted(c for c in report.controllers if c not in CONTROLLERS)
    if ignored:
        report.warnings.append("Controller(s) %s have no effect in DKR's music player."
                               % ", ".join(str(c) for c in ignored))
    if report.max_polyphony > PLAYER_VOICES:
        report.warnings.append("Up to %d notes sound at once; DKR's music player has %d voices "
                               "and drops the notes that find none." % (report.max_polyphony,
                                                                       PLAYER_VOICES))
    elif report.max_polyphony > POLYPHONY_WARNING:
        report.warnings.append("Up to %d notes sound at once. Release tails also hold voices, "
                               "so some may be dropped in game." % report.max_polyphony)


def _midi(event, tick, track, report, bank, channels, programs, channel_program,
          channel_set, sounding):
    kind = event.status & 0xF0
    channel = event.status & 0x0F
    where = "Track %d, tick %d" % (track + 1, tick)
    if kind != NOTE_ON:
        report.messages.append((tick, event.status, event.byte1, event.byte2))
    if kind == NOTE_ON:
        if event.byte2 == 0:
            raise SequenceError("velocity-zero", "%s: a note-on with velocity zero; compact sequences give "
                                "every note a duration instead." % where)
        if event.duration == 0:
            raise SequenceError("duration-zero", "%s: a note with no duration never ends." % where)
        channels.add(channel)
        report.notes += 1
        report.note_list.append(Note(tick, channel, event.byte1, event.byte2, event.duration,
                                     track))
        sounding[:] = [entry for entry in sounding if entry[0] > tick]
        sounding.append((tick + event.duration, channel))
        report.max_polyphony = max(report.max_polyphony, len(sounding))
        if bank is not None:
            program = channel_program[channel]
            if not channel_set[channel]:
                raise SequenceError("no-program", "%s: channel %d plays a note before any program change; "
                                    "every channel starts on program %s, which is silent."
                                    % (where, channel + 1,
                                       program.number if program else "none"))
            if program.lookup(event.byte1, event.byte2) is None:
                raise SequenceError("note-unmapped", "%s: program %d has no sound for %s (key %d) at velocity "
                                    "%d; the game would drop the note."
                                    % (where, program.number, music_bank.note_name(event.byte1),
                                       event.byte1, event.byte2))
    elif kind == NOTE_OFF:
        raise SequenceError("note-off", "%s: an explicit note-off; compact sequences give every note a "
                            "duration instead." % where)
    elif kind == PROGRAM:
        programs.setdefault(channel, set()).add(event.byte1)
        if bank is not None:
            program = bank.program(event.byte1)
            if program is None:
                # __setInstChanState dereferences the slot: a null one crashes.
                raise SequenceError("program-missing", "%s: program %d does not exist in DKR's instrument bank."
                                    % (where, event.byte1))
            channel_program[channel] = program
            channel_set[channel] = True
    elif kind == CONTROL:
        if event.byte1 in _CHANNEL_SWITCHES and event.byte2 > 15:
            raise SequenceError("channel-switch", "%s: a channel switch names channel %d."
                                % (where, event.byte2 + 1))
        report.controllers[event.byte1] = report.controllers.get(event.byte1, 0) + 1
