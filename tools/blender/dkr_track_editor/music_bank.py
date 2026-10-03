"""Read DKR's musical instrument bank, as its music player will use it.

A native DKR song is a sequence of note events against the game's own
instrument bank: audio record 0 (the ``B1`` control file) describes 128
programs, each a list of sounds keyed by note and velocity range, and record 1
holds their samples. A song composed for DKR is only right if every note it
plays lands on a sound of the program its channel has at that moment; the
player drops one that does not, silently.

This module reads the control file without relocating it and answers the one
question the sequence validator asks - which sound, if any, a note reaches -
the way ``__lookupSoundQuick`` does: a binary search over the program's sounds
in stored order. It also prints the per-program report the plan asks for.
Program numbers here are the stored 0-127 ones; a DAW usually displays them
as 1-128.

The bank is never redistributed: it is read from the author's own extracted
assets. Deliberately free of ``bpy``.
"""

from __future__ import annotations

import struct
from typing import List, Optional, Sequence

#: ``'B1'`` and revision 1, the only bank format DKR ships.
BANK_MAGIC = 0x4231
#: Bounds on what a bank may claim, so a damaged file cannot make the reader
#: walk an unbounded structure.
MAX_BANK_BYTES = 4 * 1024 * 1024
MAX_PROGRAMS = 128
MAX_SOUNDS = 1024

NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


class BankError(Exception):
    pass


def note_name(key: int) -> str:
    """MIDI key number as a note name, middle C (60) being C4."""
    return "%s%d" % (NOTE_NAMES[key % 12], key // 12 - 1)


class KeyMap:
    __slots__ = ("velocity_min", "velocity_max", "key_min", "key_max",
                 "key_base", "detune")

    def __init__(self, velocity_min, velocity_max, key_min, key_max,
                 key_base, detune):
        self.velocity_min = velocity_min
        self.velocity_max = velocity_max
        self.key_min = key_min
        self.key_max = key_max
        self.key_base = key_base
        self.detune = detune


class Sound:
    __slots__ = ("key_map", "sample_offset", "sample_length", "pan", "volume",
                 "attack_us", "decay_us", "release_us")

    def __init__(self, key_map, sample_offset, sample_length, pan, volume,
                 attack_us, decay_us, release_us):
        self.key_map = key_map
        self.sample_offset = sample_offset
        self.sample_length = sample_length
        self.pan = pan
        self.volume = volume
        self.attack_us = attack_us
        self.decay_us = decay_us
        self.release_us = release_us


class Program:
    __slots__ = ("number", "volume", "pan", "priority", "bend_range",
                 "tremolo", "vibrato", "sounds")

    def __init__(self, number, volume, pan, priority, bend_range, tremolo,
                 vibrato, sounds):
        self.number = number
        self.volume = volume
        self.pan = pan
        self.priority = priority
        #: Pitch-bend range in cents.
        self.bend_range = bend_range
        self.tremolo = tremolo
        self.vibrato = vibrato
        self.sounds: List[Sound] = sounds

    def lookup(self, key: int, velocity: int) -> Optional[Sound]:
        """The sound ``__lookupSoundQuick`` reaches, or None if the note is
        dropped. Kept as the same binary search rather than a scan: with
        overlapping or unsorted regions the two differ, and the game's answer
        is the one that sounds."""
        low, high = 1, len(self.sounds)
        while high >= low:
            middle = (low + high) // 2
            key_map = self.sounds[middle - 1].key_map
            if (key_map.key_min <= key <= key_map.key_max
                    and key_map.velocity_min <= velocity <= key_map.velocity_max):
                return self.sounds[middle - 1]
            if key < key_map.key_min or (velocity < key_map.velocity_min
                                         and key <= key_map.key_max):
                high = middle - 1
            else:
                low = middle + 1
        return None

    def key_range(self):
        if not self.sounds:
            return None
        return (min(s.key_map.key_min for s in self.sounds),
                max(s.key_map.key_max for s in self.sounds))


class Bank:
    __slots__ = ("sample_rate", "programs", "percussion")

    def __init__(self, sample_rate: int, programs: List[Optional[Program]],
                 percussion: Optional[Program]):
        self.sample_rate = sample_rate
        #: Indexed by stored program number; None for a null slot.
        self.programs = programs
        self.percussion = percussion

    @property
    def default_program(self) -> Optional[Program]:
        """What every channel holds when a song starts (``__initFromBank``):
        the first non-null program."""
        return next((p for p in self.programs if p is not None), None)

    def program(self, number: int) -> Optional[Program]:
        return self.programs[number] if 0 <= number < len(self.programs) else None


def _u8(data, offset):
    _need(data, offset, 1)
    return data[offset]


def _u16(data, offset):
    _need(data, offset, 2)
    return struct.unpack_from(">H", data, offset)[0]


def _s16(data, offset):
    _need(data, offset, 2)
    return struct.unpack_from(">h", data, offset)[0]


def _u32(data, offset):
    _need(data, offset, 4)
    return struct.unpack_from(">I", data, offset)[0]


def _s32(data, offset):
    _need(data, offset, 4)
    return struct.unpack_from(">i", data, offset)[0]


def _need(data, offset, length):
    if offset < 0 or offset + length > len(data):
        raise BankError("The instrument bank points outside itself (offset 0x%X)." % offset)


def parse_bank(control: bytes) -> Bank:
    """Read a ``B1`` control file (audio record 0). Raises BankError for
    anything the music player could not use as-is."""
    if len(control) > MAX_BANK_BYTES:
        raise BankError("The instrument bank is larger than any DKR ships.")
    if len(control) < 8 or _u16(control, 0) != BANK_MAGIC or _u16(control, 2) != 1:
        raise BankError("Not a DKR instrument bank (expected a B1 file holding one bank).")
    bank = _u32(control, 4)
    count = _s16(control, bank)
    if count <= 0 or count > MAX_PROGRAMS:
        raise BankError("The instrument bank claims %d programs." % count)
    if _u8(control, bank + 2):
        raise BankError("The instrument bank is already relocated; read it from the extracted assets.")
    sample_rate = _s32(control, bank + 4)
    sounds = {}

    def read_sound(pointer):
        if pointer in sounds:
            return sounds[pointer]
        envelope, key_pointer, wave = (_u32(control, pointer + 4 * i) for i in range(3))
        if not envelope or not key_pointer or not wave:
            raise BankError("A sound in the instrument bank is incomplete.")
        key_map = KeyMap(_u8(control, key_pointer), _u8(control, key_pointer + 1),
                         _u8(control, key_pointer + 2), _u8(control, key_pointer + 3),
                         _u8(control, key_pointer + 4),
                         struct.unpack_from(">b", control, key_pointer + 5)[0])
        sound = Sound(key_map, _u32(control, wave), _s32(control, wave + 4),
                      _u8(control, pointer + 12), _u8(control, pointer + 13),
                      _s32(control, envelope), _s32(control, envelope + 4),
                      _s32(control, envelope + 8))
        sounds[pointer] = sound
        return sound

    def read_program(number, pointer):
        if not pointer:
            return None
        sound_count = _s16(control, pointer + 14)
        if sound_count < 0 or sound_count > MAX_SOUNDS:
            raise BankError("Program %d claims %d sounds." % (number, sound_count))
        return Program(
            number, _u8(control, pointer), _u8(control, pointer + 1),
            _u8(control, pointer + 2),
            _s16(control, pointer + 12),
            _u8(control, pointer + 4), _u8(control, pointer + 8),
            [read_sound(_u32(control, pointer + 16 + 4 * i)) for i in range(sound_count)])

    programs = [read_program(i, _u32(control, bank + 12 + 4 * i)) for i in range(count)]
    if not any(programs):
        raise BankError("The instrument bank has no programs.")
    return Bank(sample_rate, programs, read_program(-1, _u32(control, bank + 8)))


def report(bank: Bank) -> str:
    """One line per program, then its sounds: what a composer needs to pick
    programs and keep notes inside the regions that exist."""
    lines = ["DKR instrument bank: %d program slots, %d Hz. Program numbers are the "
             "stored 0-127 values (a DAW may show them as 1-128)."
             % (len(bank.programs), bank.sample_rate)]
    if bank.percussion is None:
        lines.append("No percussion program: channel 10 is an ordinary channel here.")
    for program in bank.programs:
        if program is None:
            continue
        span = program.key_range()
        lines.append("program %3d: %d sound(s), keys %s, volume %d, pan %d, bend %d cents%s"
                     % (program.number, len(program.sounds),
                        "%s-%s" % (note_name(span[0]), note_name(span[1])) if span else "none",
                        program.volume, program.pan, program.bend_range,
                        "" if not (program.tremolo or program.vibrato) else
                        ", tremolo %d vibrato %d" % (program.tremolo, program.vibrato)))
        for sound in program.sounds:
            key_map = sound.key_map
            lines.append("    keys %3d-%3d (%s-%s) vel %3d-%3d root %s%+d  sample @0x%X %d bytes"
                         % (key_map.key_min, key_map.key_max, note_name(key_map.key_min),
                            note_name(key_map.key_max), key_map.velocity_min,
                            key_map.velocity_max, note_name(key_map.key_base),
                            key_map.detune, sound.sample_offset, sound.sample_length))
    return "\n".join(lines)


def sequence_capacity(sequence_bank: bytes) -> int:
    """The music buffer size DKR allocates, from an ``S1`` sequence bank (audio
    record 5): the largest even-rounded sequence. A custom song must fit in
    it, because the runtime reuses the game's own buffer."""
    if len(sequence_bank) < 4 or _u16(sequence_bank, 0) != 0x5331:
        raise BankError("Not a DKR sequence bank (expected S1).")
    count = _u16(sequence_bank, 2)
    if not count or count > 256:
        raise BankError("The sequence bank claims %d songs." % count)
    return max((_u32(sequence_bank, 8 + 8 * i) + 1) & ~1 for i in range(count))


def sequences(sequence_bank: bytes) -> Sequence[bytes]:
    """Every song in an ``S1`` bank, in ID order."""
    sequence_capacity(sequence_bank)
    count = _u16(sequence_bank, 2)
    result = []
    for i in range(count):
        offset, length = _u32(sequence_bank, 4 + 8 * i), _u32(sequence_bank, 8 + 8 * i)
        _need(sequence_bank, offset, length)
        result.append(bytes(sequence_bank[offset:offset + length]))
    return result
