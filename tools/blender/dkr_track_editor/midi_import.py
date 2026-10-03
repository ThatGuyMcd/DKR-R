"""Convert a Standard MIDI File into a native DKR music sequence.

DKR's music player reads compact sequences (``ALCSeq``), not MIDI files: notes
carry a duration instead of a note-off, loops are meta events and the whole
song must fit the game's 13 KB music buffer. An author composes in any DAW and
exports a ``.mid``; this module turns it into those bytes, and
``music_sequence.validate`` then checks the result the way the game will read
it.

What carries over, and how:

* **Notes.** Note-on/note-off pairs become one note with a duration. A note-on
  with velocity zero is a note-off. When a key starts again on a channel while
  it is still held, the held note ends there (the usual synth behaviour); a
  note still held at the end of the song is ended at the end. A note shorter
  than a tick lasts one tick.
* **Tempo.** One tempo for the whole song: DKR's final-lap speed-up scales the
  tempo it read at the start. A song whose tempo changes plays at the tempo
  heard for most of it, with a warning (``single_tempo=False`` refuses it).
* **Instruments, automatically.** DKR's bank is not General MIDI, so General
  MIDI program numbers are translated, with no input from the author:

  - Channel 10 is a drum part. DKR's kits sit on the General MIDI drum map:
    program 116, its main kit, holds kick 36, snare 38, hats 42/44/46, toms
    41-50, crash 49, ride 51, china 52 and tambourine 54, and smaller kits hold
    the Latin pieces on their GM keys (bongos and congas in 88 and 47,
    timbales in 14 and 47, agogos in 63, cabasa and whistle in 18, guiros in
    44, claves and a second snare in 41). ``DRUM_PIECES`` sends each GM drum
    key to its piece; a key with no piece of its own goes to the closest one
    by GM name (maracas to the cabasa, wood blocks to the claves, cowbell to
    the agogo, splash and crash 2 to the crash), and the few with nothing
    close - vibraslap, cuicas, triangles - are dropped and counted. A channel
    holds one program at a time, so each kit the part needs plays on a free
    channel of its own, carrying the drum part's volume, pan and reverb.
  - Every other channel is placed by the family of its GM program (pianos,
    basses, strings, brass...). Each family names the DKR programs the retail
    songs themselves use in that role - the bass lines, the sustained chords,
    the melodies - measured over all 66 retail songs (``FAMILY_PROGRAMS``).
    Channels of one family take successive programs from its list, so two
    parts do not collapse into one timbre. A channel with no program change is
    a GM piano, as in any GM player.

  ``program_map`` (stored 0-127 numbers on both sides; DAWs often display
  1-128) overrides the choice for a GM program, and ``auto_programs=False``
  turns the translation off: programs then pass through unchanged and channel
  10 is an ordinary channel.
* **Controllers.** Volume (7), pan (10), sustain (64) and reverb send (91) pass
  through. Expression (11) is folded into volume: DKR's own per-channel fade
  (controller 8) is driven by the game - music zones, Taj, the character menu -
  so a song must not write it. Pitch bend passes through; its range is the DKR
  program's, not the file's RPN setting. Everything else is dropped and
  counted in the warnings.
* **Loop.** Marker or cue-point events named ``loopStart`` and ``loopEnd`` (any
  case, spaces or underscores allowed) set the loop; otherwise the whole song
  loops, its end rounded up to a bar. Every track loops over exactly the same
  span. At the loop start each channel's state - program, volume, pan, reverb,
  sustain and bend - is written again, so every repeat sounds like the first
  pass; the tempo stays outside the loop so it cannot undo the final-lap
  speed-up. Notes running past the loop end are cut there.
* **Ignored.** Text, names, time and key signatures, and SysEx messages (a GM or
  GS reset is common in DAW exports and means nothing to DKR's player).

Refused outright: format 2 files, SMPTE time division, and anything structurally
broken. Deliberately free of ``bpy``.
"""

from __future__ import annotations

import re
import struct
from typing import Dict, List, Optional, Tuple

from . import music_bank, music_sequence

DEFAULT_TEMPO = 500000  # microseconds per beat: 120 BPM, the SMF default
MAX_FILE_BYTES = 4 * 1024 * 1024

NOTE_OFF, NOTE_ON, POLY_PRESSURE, CONTROL, PROGRAM, CHANNEL_PRESSURE, PITCH_BEND = (
    0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0)
VOLUME, PAN, EXPRESSION, SUSTAIN, REVERB = 7, 10, 11, 64, 91
_PASSED_CONTROLLERS = (PAN, SUSTAIN, REVERB)
_LOOP_NAME = re.compile(r"^\s*loop[\s_-]*(start|end)\s*$", re.IGNORECASE)

DRUM_CHANNEL = 9
#: DKR's main drum kit: 25 sounds on keys 28-54, used by 38 of the 66 retail
#: songs, on the General MIDI drum layout. It takes channel 10 itself.
DRUM_PROGRAM = 116
_MAIN_KIT = {key: (DRUM_PROGRAM, key) for key in list(range(28, 37)) + [38, 39]
             + list(range(41, 55))}
#: General MIDI drum key -> (DKR program, key). The smaller kits were found by
#: their sounds sitting on exactly these GM keys; where several hold a piece,
#: the one more retail songs use is taken.
DRUM_PIECES = {**_MAIN_KIT, **{
    37: (116, 38), 40: (41, 40),                         # side stick, electric snare
    55: (116, 49), 57: (116, 49), 59: (116, 51),         # splash, crash 2, ride 2
    56: (63, 67),                                        # cowbell: high agogo
    60: (88, 60), 61: (47, 61), 62: (88, 62), 63: (47, 63), 64: (88, 64),
    65: (14, 65), 66: (47, 66), 67: (63, 67), 68: (47, 68),
    69: (18, 69), 70: (18, 69), 71: (18, 71), 72: (18, 71),  # cabasa, maracas, whistles
    73: (44, 73), 74: (44, 74),                          # guiros
    75: (41, 75), 76: (41, 75), 77: (41, 75),            # claves, wood blocks
}}

#: DKR programs by the role the retail songs give them, from their own usage:
#: the median key, how often they sound in chords, how long their notes are.
_BASS = [22, 92, 60]          # low (median key 40-48), almost never chords
_CHORDS = [9, 76, 17, 6]      # chords in 28-34% of notes, used by 11-24 songs
_PAD = [27, 99]               # notes held about four beats
_LEAD = [23, 28, 52, 24]      # single lines in the upper middle, 11-26 songs
#: General MIDI family (program // 8) to candidate DKR programs, best first.
FAMILY_PROGRAMS = {
    0: _CHORDS,                       # pianos
    1: [76, 9, 17],                   # chromatic percussion: celesta .. dulcimer
    2: [17, 9, 76],                   # organs
    3: [6, 76, 9],                    # guitars
    4: _BASS,                         # basses
    5: _PAD,                          # strings
    6: [99, 27],                      # ensembles and choirs
    7: [23, 28, 52],                  # brass
    8: [28, 23, 52],                  # reeds
    9: [52, 28, 23],                  # pipes
    10: _LEAD,                        # synth leads
    11: [99, 27],                     # synth pads
    12: [99, 27],                     # synth effects
    13: [76, 6, 9],                   # ethnic
    14: [76, 9, 17],                  # percussive
    15: [99, 27],                     # sound effects
}


class MidiError(Exception):
    pass


# --------------------------------------------------------------- SMF reading

class _MidiEvent:
    __slots__ = ("tick", "order", "status", "data", "meta", "text")

    def __init__(self, tick, order, status, data=b"", meta=None, text=None):
        self.tick = tick
        #: Position in the file, to keep same-tick events in their written order.
        self.order = order
        self.status = status
        self.data = data
        self.meta = meta
        self.text = text


class _File:
    def __init__(self):
        self.format = 0
        self.division = 0
        self.events: List[_MidiEvent] = []
        self.end_tick = 0
        self.sysex = 0


def _var_len(data, position, limit):
    value = 0
    for _ in range(4):
        if position >= limit:
            raise MidiError("A MIDI track ends in the middle of a number.")
        byte = data[position]
        position += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, position
    raise MidiError("A MIDI track holds a number longer than four bytes.")


def parse_midi(data: bytes) -> _File:
    """Read the parts of a Standard MIDI File the converter uses."""
    if len(data) > MAX_FILE_BYTES:
        raise MidiError("The MIDI file is larger than any song for DKR could be.")
    if data[:4] != b"MThd" or len(data) < 14:
        raise MidiError("Not a Standard MIDI File (no MThd header).")
    length, midi_format, track_count, division = struct.unpack_from(">IHHH", data, 4)
    if length < 6:
        raise MidiError("The MIDI header is too short.")
    if midi_format == 2:
        raise MidiError("Format 2 MIDI files hold separate songs; export the song as format 0 "
                        "or 1.")
    if midi_format > 2:
        raise MidiError("Unknown MIDI file format %d." % midi_format)
    if division & 0x8000:
        raise MidiError("The file counts time in SMPTE frames; export it with musical time "
                        "(ticks per beat).")
    if not division:
        raise MidiError("The file has zero ticks per beat.")
    result = _File()
    result.format = midi_format
    result.division = division
    position = 8 + length
    order = 0
    for _ in range(track_count):
        while True:
            if position + 8 > len(data):
                raise MidiError("The file ends before all %d tracks." % track_count)
            chunk, size = data[position:position + 4], struct.unpack_from(">I", data,
                                                                           position + 4)[0]
            position += 8
            if position + size > len(data):
                raise MidiError("A MIDI track runs past the end of the file.")
            if chunk == b"MTrk":
                break
            position += size  # an unknown chunk: skipped, as the spec says
        end = position + size
        tick = 0
        running = 0
        while position < end:
            delta, position = _var_len(data, position, end)
            tick += delta
            if position >= end:
                raise MidiError("A MIDI track ends in the middle of an event.")
            status = data[position]
            if status == 0xFF:
                if position + 2 > end:
                    raise MidiError("A MIDI track ends in the middle of a meta event.")
                meta = data[position + 1]
                size_meta, position = _var_len(data, position + 2, end)
                payload = data[position:position + size_meta]
                position += size_meta
                if position > end:
                    raise MidiError("A meta event runs past its track.")
                running = 0
                order += 1
                if meta == 0x2F:
                    break
                text = payload.decode("latin-1") if meta in (0x06, 0x07) else None
                result.events.append(_MidiEvent(tick, order, 0xFF, bytes(payload), meta, text))
                continue
            if status in (0xF0, 0xF7):
                size_sysex, position = _var_len(data, position + 1, end)
                position += size_sysex
                running = 0
                result.sysex += 1
                continue
            if status & 0x80:
                if status >= 0xF0:
                    raise MidiError("Unexpected system message 0x%02X in a MIDI track." % status)
                running = status
                position += 1
            elif not running:
                raise MidiError("A MIDI track uses running status before any status byte.")
            count = 1 if running & 0xF0 in (PROGRAM, CHANNEL_PRESSURE) else 2
            payload = data[position:position + count]
            if len(payload) < count or position + count > end:
                raise MidiError("A MIDI track ends in the middle of an event.")
            if any(b & 0x80 for b in payload):
                raise MidiError("A MIDI event has a data byte above 0x7F.")
            position += count
            order += 1
            result.events.append(_MidiEvent(tick, order, running, bytes(payload)))
        result.end_tick = max(result.end_tick, tick)
        position = end
    result.events.sort(key=lambda event: (event.tick, event.order))
    return result


# --------------------------------------------------------------- conversion

class Conversion:
    """The native song and what the author should know about it."""

    def __init__(self):
        self.data = b""
        self.report: Optional[music_sequence.Report] = None
        self.warnings: List[str] = []
        self.division = 0
        self.tempo = DEFAULT_TEMPO
        self.loop: Optional[Tuple[int, int]] = None
        self.channels: List[int] = []
        #: (channel, GM program) -> DKR program, for every program the song uses.
        self.programs: Dict[Tuple[int, int], int] = {}

    @property
    def bpm(self) -> float:
        return 60000000.0 / self.tempo


class _Note:
    __slots__ = ("tick", "order", "channel", "key", "velocity", "end", "copies", "kit")

    def __init__(self, tick, order, channel, key, velocity):
        self.tick = tick
        self.order = order
        self.channel = channel
        self.key = key
        self.velocity = velocity
        self.end = None
        #: Note-ons of this key at this same tick, each with its own note-off.
        self.copies = 1
        #: The DKR kit a drum note plays on.
        self.kit = None


class _Out:
    """One native event before encoding: (tick, rank, order) sorts a track."""

    __slots__ = ("tick", "rank", "order", "kind", "status", "data", "duration")

    def __init__(self, tick, rank, order, kind, status=0, data=b"", duration=0):
        self.tick = tick
        self.rank = rank
        self.order = order
        self.kind = kind
        self.status = status
        self.data = data
        self.duration = duration


# Ranks within one tick: the tempo, then the loop end (it closes the previous
# pass before anything at that tick), then the loop start, then the restated
# channel state, then the song's own events.
_RANK_TEMPO, _RANK_LOOP_END, _RANK_LOOP_START, _RANK_STATE, _RANK_EVENT = range(5)


def convert(data: bytes, program_map: Optional[Dict[int, int]] = None,
            loop: Optional[Tuple[int, int]] = None, looping: bool = True,
            single_tempo: bool = True, auto_programs: bool = True,
            bank: Optional[music_bank.Bank] = None,
            capacity: int = music_sequence.RETAIL_CAPACITY) -> Conversion:
    """Convert MIDI bytes. ``loop`` is (start, end) in the file's ticks and
    overrides any loop markers; ``looping=False`` makes a song that plays once.
    ``single_tempo`` plays a song with tempo changes at the tempo heard for
    most of it instead of refusing it; the notes keep their places in beats.
    Raises MidiError for a file that cannot be converted and
    music_sequence.SequenceError for a result DKR could not play."""
    source = parse_midi(bytes(data))
    result = Conversion()
    result.division = source.division
    if source.division > 0x7FFF:
        raise MidiError("The file's time division is too fine for DKR.")
    programs = _ProgramChooser(dict(program_map or {}), auto_programs)
    warn = _Warnings()
    if source.sysex:
        warn.add("Ignored %d SysEx message(s) (a GM or GS reset means nothing to DKR)."
                 % source.sysex)

    # -- tempo and loop markers
    tempos = [(e.tick, struct.unpack(">I", b"\x00" + e.data[:3])[0])
              for e in source.events if e.meta == 0x51 and len(e.data) >= 3]
    markers = {}
    for event in source.events:
        if event.text is not None:
            found = _LOOP_NAME.match(event.text)
            if found:
                markers.setdefault(found.group(1).lower(), event.tick)
    values = sorted({value for _, value in tempos})
    first_note = min((e.tick for e in source.events
                      if e.status & 0xF0 == NOTE_ON and e.data[1]), default=None)
    if first_note is None:
        raise MidiError("The MIDI file plays no notes.")
    if len(values) > 1:
        bpms = sorted(60000000.0 / v for v in values)
        if not single_tempo:
            raise MidiError("The song changes tempo %d time(s), between %.2f and %.2f BPM. DKR "
                            "plays one tempo per song, which its final-lap speed-up then "
                            "scales: set a single tempo, or convert with one tempo for the "
                            "whole song." % (len(tempos) - 1, bpms[0], bpms[-1]))
        result.tempo = _dominant_tempo(tempos, source.end_tick)
        warn.add("The song changes tempo between %.2f and %.2f BPM; it plays at %.2f BPM, the "
                 "tempo heard for most of it." % (bpms[0], bpms[-1], 60000000.0 / result.tempo))
    elif tempos:
        if tempos[0][0] > first_note and values[0] != DEFAULT_TEMPO and not single_tempo:
            raise MidiError("The tempo is set only after the first note, so the opening "
                            "plays at 120 BPM; move the tempo to the start.")
        result.tempo = values[0]

    # -- notes and channel events
    notes: List[_Note] = []
    held: Dict[Tuple[int, int], _Note] = {}
    channel_events: List[_MidiEvent] = []
    # At one tick, note-offs first: a file may write the next note-on before
    # the previous note's note-off, and the off belongs to the earlier note.
    for event in sorted(source.events, key=lambda e: (e.tick, not _is_note_off(e), e.order)):
        if event.meta is not None:
            continue
        kind = event.status & 0xF0
        channel = event.status & 0x0F
        if kind == NOTE_ON and event.data[1]:
            previous = held.get((channel, event.data[0]))
            if previous is not None and previous.tick == event.tick:
                # The same key twice at once (layered parts): one note, as
                # loud as the louder, held until its last note-off.
                previous.copies += 1
                previous.velocity = max(previous.velocity, event.data[1])
                warn.count("doubled", "Merged %d doubled note(s) - the same key started twice "
                                      "at once on one channel.")
                continue
            if previous is not None:
                previous.end = event.tick
                warn.count("retriggered", "%d note(s) started again while still held; each "
                                          "earlier one ends where the next begins.")
            note = _Note(event.tick, event.order, channel, event.data[0], event.data[1])
            if auto_programs and channel == DRUM_CHANNEL:
                piece = DRUM_PIECES.get(event.data[0])
                if piece is None:
                    warn.count("drum-dropped", "Dropped %d drum note(s) on pieces DKR has "
                                               "nothing close to (vibraslap, cuica, triangle).")
                    note.velocity = 0  # removed below, after pairing
                else:
                    note.kit, note.key = piece
                    if note.key != event.data[0]:
                        warn.count("drum-moved", "Played %d drum note(s) on the closest piece "
                                                 "DKR has.")
            notes.append(note)
            held[(channel, event.data[0])] = note
        elif kind in (NOTE_ON, NOTE_OFF):
            note = held.get((channel, event.data[0]))
            if note is None:
                warn.count("orphan", "Ignored %d note-off(s) with no note playing.")
            else:
                note.end = event.tick
                note.copies -= 1
                if not note.copies:
                    del held[(channel, event.data[0])]
        else:
            channel_events.append(event)

    # -- loop span
    song_end = max([source.end_tick] + [n.end for n in notes if n.end is not None]
                   + [n.tick + 1 for n in notes])
    if looping:
        if loop is not None:
            start, end = loop
        elif "start" in markers or "end" in markers:
            start, end = markers.get("start", 0), markers.get("end", _round_to_bar(
                source, song_end))
        else:
            start, end = 0, _round_to_bar(source, song_end)
        if not 0 <= start < end:
            raise MidiError("The loop must start before it ends (start tick %d, end tick %d)."
                            % (start, end))
        result.loop = (start, end)
        song_end = end
    for note in held.values():
        note.end = song_end
        warn.count("unended", "%d note(s) were still held at the end; they end there.")
    notes = [note for note in notes if note.velocity]
    kept = []
    for note in notes:
        if note.tick >= song_end:
            warn.count("after", "Dropped %d note(s) after the loop end; they would never play.")
            continue
        if note.end > song_end:
            note.end = song_end
            warn.count("cut", "Cut %d note(s) at the loop end.")
        if note.end <= note.tick:
            note.end = note.tick + 1
            warn.count("short", "Lengthened %d note(s) shorter than a tick to one tick.")
        if result.loop and note.tick < result.loop[0] < note.end:
            warn.count("across", "%d note(s) sound across the loop start; they are heard on "
                                 "the first pass only.")
        kept.append(note)
    notes = kept
    if auto_programs:
        notes, channel_events = _split_drums(notes, channel_events, programs, warn)
    channels = sorted({note.channel for note in notes})
    result.channels = channels
    if DRUM_CHANNEL in channels and not auto_programs:
        warn.add("Channel 10 is an ordinary channel here; its notes play the program it "
                 "selects, not a drum kit.")

    # -- per-channel output events
    tracks: Dict[int, List[_Out]] = {channel: [] for channel in channels}
    state = {channel: _ChannelState() for channel in channels}
    loop_state: Dict[int, _ChannelState] = {}
    order = 0
    for event in channel_events:
        channel = event.status & 0x0F
        if channel not in tracks or event.tick >= song_end:
            continue
        if result.loop and event.tick >= result.loop[0] and channel not in loop_state:
            loop_state[channel] = state[channel].copy()
        kind = event.status & 0xF0
        out = None
        current = state[channel]
        if kind == PROGRAM:
            target = programs.target(channel, event.data[0])
            current.program = target
            out = _Out(event.tick, _RANK_EVENT, event.order, "midi", PROGRAM | channel,
                       bytes([target]))
            # DKR's program change resets the channel's volume and pan to the
            # instrument's own (__setInstChanState); General MIDI keeps them.
            for again in current.after_program(channel, event.tick, event.order):
                tracks[channel].append(again)
        elif kind == CONTROL:
            controller, value = event.data
            if controller in (VOLUME, EXPRESSION):
                if controller == VOLUME:
                    current.volume = value
                else:
                    current.expression = value
                    warn.count("expression", "Folded %d expression change(s) into volume.")
                out = _Out(event.tick, _RANK_EVENT, event.order, "midi", CONTROL | channel,
                           bytes([VOLUME, current.effective_volume()]))
            elif controller in _PASSED_CONTROLLERS:
                current.controllers[controller] = value
                out = _Out(event.tick, _RANK_EVENT, event.order, "midi", CONTROL | channel,
                           bytes([controller, value]))
            elif controller in (100, 101, 6, 38):
                warn.count("rpn", "Ignored %d RPN/NRPN message(s); pitch-bend range is the "
                                  "DKR program's own.")
            elif controller in (120, 121, 123):
                pass  # resets and all-notes-off: durations already end every note
            else:
                warn.count("cc%d" % controller, "Dropped %%d change(s) of controller %d, which "
                                                "DKR's player does not use." % controller)
        elif kind == PITCH_BEND:
            current.bend = event.data
            out = _Out(event.tick, _RANK_EVENT, event.order, "midi", PITCH_BEND | channel,
                       bytes(event.data))
        else:
            warn.count("pressure", "Dropped %d aftertouch message(s); DKR's player would "
                                   "turn them into volume changes.")
        if out is not None:
            tracks[channel].append(out)
    if result.loop:
        for channel in channels:
            loop_state.setdefault(channel, state[channel].copy())

    # A channel playing before any program change would use DKR's silent
    # default program.
    for channel in channels:
        first = min(n.tick for n in notes if n.channel == channel)
        selected = [o for o in tracks[channel] if o.status & 0xF0 == PROGRAM
                    and o.tick <= first]
        if not selected:
            target = programs.target(channel, 0)
            if result.loop and loop_state[channel].program is None:
                loop_state[channel].program = target
            if not (result.loop and result.loop[0] == 0):  # else the loop start restates it
                tracks[channel].append(_Out(0, _RANK_STATE, -1, "midi", PROGRAM | channel,
                                            bytes([target])))
            if not auto_programs:
                warn.add("Channel %d plays before any program change; it uses program %d."
                         % (channel + 1, target))
    result.programs = dict(programs.chosen)

    for note in notes:
        tracks[note.channel].append(_Out(note.tick, _RANK_EVENT, note.order, "note",
                                         NOTE_ON | note.channel,
                                         bytes([note.key, note.velocity]),
                                         note.end - note.tick))
    first_track = channels[0]
    tracks[first_track].append(_Out(0, _RANK_TEMPO, 0, "tempo"))
    if result.loop:
        start, end = result.loop
        for channel in channels:
            tracks[channel].append(_Out(start, _RANK_LOOP_START, 0, "loop-start"))
            tracks[channel].append(_Out(end, _RANK_LOOP_END, 0, "loop-end"))
            for out in loop_state[channel].restate(channel, start):
                tracks[channel].append(out)

    result.data = _encode(source.division, result.tempo, [tracks[c] for c in channels],
                          song_end)
    result.warnings = warn.lines()
    result.report = music_sequence.validate(result.data, bank, capacity)
    return result


def program_suggestions(data: bytes, bank: music_bank.Bank,
                        limit: int = 8) -> Dict[int, List[Tuple[int, float]]]:
    """For each program the MIDI file plays, the DKR programs that can sound
    its notes: (DKR program, share of the notes it reaches), best first.

    DKR's bank has no instrument names, and its program numbers are not
    General MIDI's, so this cannot say which DKR program *sounds* like a
    piano. What it can say for certain is which ones would drop notes: a
    program whose regions miss the part's keys or velocities is silent there.
    """
    source = parse_midi(bytes(data))
    current = [0] * 16
    played: Dict[int, List[Tuple[int, int]]] = {}
    for event in source.events:
        if event.meta is not None:
            continue
        kind = event.status & 0xF0
        if kind == PROGRAM:
            current[event.status & 0x0F] = event.data[0]
        elif kind == NOTE_ON and event.data[1]:
            played.setdefault(current[event.status & 0x0F], []).append(
                (event.data[0], event.data[1]))
    suggestions = {}
    for program, notes in sorted(played.items()):
        ranked = []
        for candidate in bank.programs:
            if candidate is None or not candidate.sounds:
                continue
            reached = sum(1 for key, velocity in notes
                          if candidate.lookup(key, velocity) is not None)
            if reached:
                ranked.append((candidate.number, reached / float(len(notes))))
        ranked.sort(key=lambda item: (-item[1], item[0]))
        suggestions[program] = ranked[:limit]
    return suggestions


class _ProgramChooser:
    """Which DKR program each (channel, GM program) plays, decided once."""

    def __init__(self, overrides, automatic):
        self.overrides = overrides
        self.automatic = automatic
        #: (channel, GM program) -> DKR program, for the conversion report.
        self.chosen: Dict[Tuple[int, int], int] = {}
        self._kits: Dict[int, int] = {}
        self._taken: set = set()

    def kit(self, channel, program):
        """A drum channel plays its kit whatever programs the file selects."""
        self._kits[channel] = program
        self._taken.add(program)

    def target(self, channel, program):
        key = (channel, program)
        if key in self.chosen:
            return self.chosen[key]
        if channel in self._kits:
            target = self._kits[channel]
        elif program in self.overrides:
            target = self.overrides[program]
        elif self.automatic:
            # The family's best program no other part has taken yet, so two
            # parts keep two timbres; once all are taken, the best again.
            candidates = FAMILY_PROGRAMS[program // 8]
            target = next((c for c in candidates if c not in self._taken), candidates[0])
        else:
            target = program
        self._taken.add(target)
        self.chosen[key] = target
        return target


def _split_drums(notes, channel_events, programs, warn):
    """Give each DKR kit the drum part uses a channel of its own: the main kit
    keeps channel 10, the others take channels the song leaves free and copy
    the drum part's controllers."""
    kits = []
    for note in notes:
        if note.kit is not None and note.kit not in kits:
            kits.append(note.kit)
    if not kits:
        return notes, channel_events
    kits.sort(key=lambda kit: kit != DRUM_PROGRAM)
    busy = {n.channel for n in notes if n.kit is None} | {DRUM_CHANNEL}
    free = [c for c in range(16) if c not in busy]
    channel_of = {kits[0]: DRUM_CHANNEL}
    for kit in kits[1:]:
        if free:
            channel_of[kit] = free.pop(0)
    kept = []
    for note in notes:
        if note.kit is None:
            kept.append(note)
        elif note.kit in channel_of:
            note.channel = channel_of[note.kit]
            kept.append(note)
        else:
            warn.count("no-channel", "Dropped %d drum note(s): every channel is taken, so "
                                     "their kit has none to play on.")
    for kit, channel in channel_of.items():
        programs.kit(channel, kit)
    copies = []
    for event in channel_events:
        if event.status & 0x0F != DRUM_CHANNEL:
            copies.append(event)
        elif event.status & 0xF0 != PROGRAM:  # the kits are fixed
            for channel in sorted(channel_of.values()):
                copies.append(_MidiEvent(event.tick, event.order,
                                         (event.status & 0xF0) | channel, event.data))
    return kept, copies


class _ChannelState:
    __slots__ = ("program", "volume", "expression", "controllers", "bend")

    def __init__(self):
        self.program = None
        self.volume = None
        self.expression = 127
        self.controllers: Dict[int, int] = {}
        self.bend = None

    def copy(self):
        other = _ChannelState()
        other.program = self.program
        other.volume = self.volume
        other.expression = self.expression
        other.controllers = dict(self.controllers)
        other.bend = self.bend
        return other

    def effective_volume(self) -> int:
        volume = 100 if self.volume is None else self.volume
        return (volume * self.expression + 63) // 127

    def after_program(self, channel, tick, order):
        out = []
        if self.volume is not None or self.expression != 127:
            out.append(_Out(tick, _RANK_EVENT, order + 0.25, "midi", CONTROL | channel,
                            bytes([VOLUME, self.effective_volume()])))
        if PAN in self.controllers:
            out.append(_Out(tick, _RANK_EVENT, order + 0.5, "midi", CONTROL | channel,
                            bytes([PAN, self.controllers[PAN]])))
        return out

    def restate(self, channel, tick):
        """The channel's state written again at the loop start."""
        out = []
        if self.program is not None:
            out.append(_Out(tick, _RANK_STATE, 0, "midi", PROGRAM | channel,
                            bytes([self.program])))
        if self.volume is not None or self.expression != 127:
            out.append(_Out(tick, _RANK_STATE, 1, "midi", CONTROL | channel,
                            bytes([VOLUME, self.effective_volume()])))
        for index, (controller, value) in enumerate(sorted(self.controllers.items())):
            out.append(_Out(tick, _RANK_STATE, 2 + index, "midi", CONTROL | channel,
                            bytes([controller, value])))
        if self.bend is not None:
            out.append(_Out(tick, _RANK_STATE, 99, "midi", PITCH_BEND | channel,
                            bytes(self.bend)))
        return out


class _Warnings:
    def __init__(self):
        self._lines: List[str] = []
        self._counts: Dict[str, List] = {}

    def add(self, line):
        self._lines.append(line)

    def count(self, key, template):
        entry = self._counts.setdefault(key, [template, 0])
        entry[1] += 1

    def lines(self):
        return self._lines + [template % number for template, number in self._counts.values()]


def _is_note_off(event):
    kind = event.status & 0xF0
    return event.meta is None and (kind == NOTE_OFF or (kind == NOTE_ON and not event.data[1]))


def _dominant_tempo(tempos, end_tick):
    """The tempo in force over the most ticks (the SMF default before the
    first tempo event counts too)."""
    spans: Dict[int, int] = {}
    current, since = DEFAULT_TEMPO, 0
    for tick, value in sorted(tempos):
        spans[current] = spans.get(current, 0) + tick - since
        current, since = value, tick
    spans[current] = spans.get(current, 0) + max(0, end_tick - since)
    return max(spans.items(), key=lambda item: (item[1], -item[0]))[0]


def _round_to_bar(source, tick):
    numerator, denominator = 4, 4
    for event in source.events:
        if event.meta == 0x58 and len(event.data) >= 2:
            numerator, denominator = event.data[0], 1 << event.data[1]
            break
    bar = max(1, source.division * 4 * numerator // denominator)
    return -(-tick // bar) * bar


# --------------------------------------------------------------- encoding

def _var(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


#: A back reference costs four bytes (FE, distance high, low, length). The
#: player copies its source bytes as they are, without expanding references
#: inside them, so only literal runs can be copied: a reference taken too
#: eagerly breaks up the run a later, longer one would have copied. Encoding is
#: tried with these minimum lengths and the smallest result kept.
_MIN_MATCHES = (6, 8, 10, 12)
_MAX_MATCH = 255
_MAX_DISTANCE = 0xFFFF
_CANDIDATES = 256


class _Writer:
    """The sequence as the player reads it, with back references.

    Bytes the player reads through ``__getTrackByte`` go through ``stream``:
    each run is either a back reference to identical earlier bytes of the
    sequence, or literal, with a literal 0xFE doubled (FE FE) so it is not
    taken for the start of a reference. The loop-end payload is read straight
    from the buffer, so it goes through ``raw``: never escaped, never
    referenced, never used as a source.
    """

    def __init__(self, min_match):
        self.out = bytearray(music_sequence.HEADER_BYTES)
        #: 0 writes everything literally.
        self.min_match = min_match
        self.index: Dict[bytes, List[int]] = {}
        self.indexed = music_sequence.HEADER_BYTES
        self.protected = set()

    def _index_to_end(self):
        while self.indexed + 4 <= len(self.out):
            key = bytes(self.out[self.indexed:self.indexed + 4])
            if not any(p in self.protected for p in range(self.indexed, self.indexed + 4)):
                positions = self.index.setdefault(key, [])
                positions.append(self.indexed)
                if len(positions) > _CANDIDATES:
                    del positions[0]
            self.indexed += 1

    def _match(self, plain, at):
        if not self.min_match or at + self.min_match > len(plain):
            return 0, 0
        code = len(self.out)
        best_length, best_source = 0, 0
        for source in reversed(self.index.get(bytes(plain[at:at + 4]), ())):
            distance = code - source
            if distance > _MAX_DISTANCE or distance >> 8 == 0xFE:
                continue
            length = 0
            while (length < _MAX_MATCH and at + length < len(plain)
                   and source + length < code and source + length not in self.protected
                   and self.out[source + length] == plain[at + length]):
                length += 1
            if length > best_length:
                best_length, best_source = length, source
                if length == _MAX_MATCH:
                    break
        return (best_length, best_source) if best_length >= self.min_match else (0, 0)

    def stream(self, plain):
        at = 0
        while at < len(plain):
            self._index_to_end()
            length, source = self._match(plain, at)
            if length and self._match(plain, at + 1)[0] > length + 1:
                length = 0  # lazy: one literal byte buys a longer reference
            if length:
                distance = len(self.out) - source
                self.out += bytes([0xFE, distance >> 8, distance & 0xFF, length])
                at += length
            else:
                value = plain[at]
                self.out += bytes([0xFE, 0xFE]) if value == 0xFE else bytes([value])
                at += 1

    def raw(self, data):
        start = len(self.out)
        self.out += data
        self.protected.update(range(start, len(self.out)))


def _encode(division, tempo, tracks, song_end, compress=True):
    """Lay the tracks out as a compact sequence: header, then each track's
    events with running status where the player allows it (never across a
    meta event, which resets it), compressed with back references."""
    for events in tracks:
        events.sort(key=lambda e: (e.tick, e.rank, e.order))
    if not compress:
        return _encode_with(division, tempo, tracks, song_end, 0)
    return min((_encode_with(division, tempo, tracks, song_end, minimum)
                for minimum in _MIN_MATCHES), key=len)


def _encode_with(division, tempo, tracks, song_end, min_match):
    writer = _Writer(min_match)
    offsets = []
    for events in tracks:
        offsets.append(len(writer.out))
        plain = bytearray()
        tick = 0
        running = 0
        loop_target = None
        for event in events:
            plain += _var(event.tick - tick)
            tick = event.tick
            if event.kind == "tempo":
                plain += bytes([0xFF, 0x51, tempo >> 16, (tempo >> 8) & 0xFF, tempo & 0xFF])
                running = 0
            elif event.kind == "loop-start":
                plain += bytes([0xFF, 0x2E, 0x00, 0xFF])
                running = 0
                # The loop target must be a plain byte boundary: nothing may
                # reference across it.
                writer.stream(plain)
                plain.clear()
                loop_target = len(writer.out)
            elif event.kind == "loop-end":
                plain += bytes([0xFF, 0x2D])
                writer.stream(plain)
                plain.clear()
                # Counters and distance are read from the buffer itself; the
                # player jumps back from the end of these six bytes.
                end = len(writer.out) + 6
                writer.raw(bytes([0xFF, 0xFF]) + struct.pack(">I", end - loop_target))
                running = 0
            else:
                if event.status != running:
                    plain.append(event.status)
                    running = event.status
                plain += event.data
                if event.kind == "note":
                    plain += _var(event.duration)
        plain += _var(max(0, song_end - tick)) + bytes([0xFF, 0x2F])
        writer.stream(plain)
    header = offsets + [0] * (16 - len(offsets))
    writer.out[:music_sequence.HEADER_BYTES] = struct.pack(">16I", *header) + struct.pack(
        ">I", division)
    return bytes(writer.out)
