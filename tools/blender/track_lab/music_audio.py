"""Recognise the recorded music a track can ship instead of a retail song.

A track can bring its own music as an MP3 or WAV file. DKR itself cannot play
either: its music player runs sequences against the game's own instrument bank,
and a sequence buffer holds about 13 KB. So the runtime plays the file on the
host, mixed into the game's audio output, while the retail song the header
names - the *carrier* - keeps running silently underneath. The carrier is what
lets the game's own fades, pauses and final-lap speed-up keep driving the
music; see docs/CUSTOM_MUSIC_PLAN.md.

What this module decides is only whether a file is one the runtime will decode,
and how long it is, so loop points can be checked before anything is exported.
It reads the bytes, never the extension: a renamed file is refused here rather
than at the starting line.

Deliberately free of ``bpy``, so the tests run on a plain Python.
"""

from __future__ import annotations

import os
import struct
from typing import Optional

#: The codecs the runtime decodes, by the name the manifest records.
MP3 = "mp3"
WAV = "wav"
CODECS = (MP3, WAV)

#: The extension each codec is written into the package with.
EXTENSIONS = {MP3: ".mp3", WAV: ".wav"}

#: The runtime decodes the whole file up front, so the bounds are about memory:
#: fifteen minutes of 48 kHz stereo is about 170 MB of decoded samples.
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_SECONDS = 15 * 60
MIN_SECONDS = 1.0

#: The shortest loop the runtime accepts. A loop shorter than an audio block
#: would wrap several times per buffer, which is never what an author meant.
MIN_LOOP_SECONDS = 0.25

#: What the runtime resamples from. Anything outside is almost certainly not
#: music, and the decoders are only exercised inside it.
MIN_SAMPLE_RATE = 8000
MAX_SAMPLE_RATE = 192000

#: MP3 frames that must follow the first sync before it is believed. A JPEG in
#: an ID3 tag or a stray 0xFFE run in garbage can look like one frame header;
#: three consecutive frames that each land exactly on the next are not luck.
_MP3_CONFIRM_FRAMES = 3


class AudioError(Exception):
    pass


class AudioInfo:
    """What a music file holds, as the export and the panel need to know it."""

    __slots__ = ("codec", "sample_rate", "channels", "frames", "size")

    def __init__(self, codec: str, sample_rate: int, channels: int,
                 frames: int, size: int):
        self.codec = codec
        self.sample_rate = sample_rate
        self.channels = channels
        #: Sample frames (one per channel pair), not MP3 frames.
        self.frames = frames
        self.size = size

    @property
    def seconds(self) -> float:
        return self.frames / float(self.sample_rate) if self.sample_rate else 0.0

    def summary(self) -> str:
        minutes, seconds = divmod(self.seconds, 60.0)
        return "%s · %d:%04.1f · %.1f kHz %s" % (
            self.codec.upper(), int(minutes), seconds, self.sample_rate / 1000.0,
            "stereo" if self.channels == 2 else "mono")


# -- MP3 -----------------------------------------------------------------

# Kbit/s by [MPEG-1?][bitrate index], Layer III only.
_MP3_BITRATES = {
    True: (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0),
    False: (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0),
}
# Hz by version bits then sample-rate index.
_MP3_RATES = {
    3: (44100, 48000, 32000),   # MPEG-1
    2: (22050, 24000, 16000),   # MPEG-2
    0: (11025, 12000, 8000),    # MPEG-2.5
}


def _mp3_frame(data: bytes, offset: int):
    """``(length, sample_rate, channels, samples)`` for a Layer III frame
    header at ``offset``, or ``None`` when there is not one there."""
    if offset + 4 > len(data):
        return None
    b0, b1, b2, b3 = data[offset:offset + 4]
    if b0 != 0xFF or (b1 & 0xE0) != 0xE0:
        return None
    version = (b1 >> 3) & 0x03
    layer = (b1 >> 1) & 0x03
    if version == 1 or layer != 1:  # reserved version, or not Layer III
        return None
    bitrate_index = (b2 >> 4) & 0x0F
    rate_index = (b2 >> 2) & 0x03
    if bitrate_index in (0, 15) or rate_index == 3:
        # Free-format streams have no length in the header to walk by.
        return None
    mpeg1 = version == 3
    bitrate = _MP3_BITRATES[mpeg1][bitrate_index] * 1000
    sample_rate = _MP3_RATES[version][rate_index]
    padding = (b2 >> 1) & 0x01
    channels = 1 if ((b3 >> 6) & 0x03) == 3 else 2
    if mpeg1:
        length = 144 * bitrate // sample_rate + padding
        samples = 1152
    else:
        length = 72 * bitrate // sample_rate + padding
        samples = 576
    return length, sample_rate, channels, samples


def _id3v2_length(data: bytes) -> int:
    if len(data) < 10 or data[:3] != b"ID3":
        return 0
    size = 0
    for byte in data[6:10]:
        if byte & 0x80:
            raise AudioError("the file's ID3 tag is damaged")
        size = (size << 7) | byte
    footer = 10 if data[5] & 0x10 else 0
    return 10 + size + footer


def inspect_mp3(data: bytes) -> AudioInfo:
    """Walk every frame of an MP3. Raises :class:`AudioError` when it is not
    one the runtime decodes."""
    start = _id3v2_length(data)
    end = len(data)
    if end - start >= 128 and data[end - 128:end - 125] == b"TAG":
        end -= 128  # ID3v1 trailer

    # The first frame: the first sync that the next few frames confirm.
    offset = start
    limit = min(end, start + 64 * 1024)
    first = None
    while offset < limit:
        frame = _mp3_frame(data, offset)
        if frame is not None:
            probe, ok = offset, True
            for _ in range(_MP3_CONFIRM_FRAMES):
                here = _mp3_frame(data, probe)
                if here is None or here[1:3] != frame[1:3]:
                    ok = False
                    break
                probe += here[0]
                if probe >= end:
                    break
            if ok:
                first = offset
                break
        offset += 1
    if first is None:
        raise AudioError("no MPEG audio Layer III frames were found; the file "
                         "is not an MP3 (or uses a free-format bitrate)")

    sample_rate, channels = _mp3_frame(data, first)[1:3]
    frames = 0
    offset = first
    trim = 0
    gapless = _mp3_gapless(data, first)
    if gapless is not None:
        # The first frame is an encoder's Xing/Info header, not audio, and the
        # LAME tag in it says how much priming and padding to drop. The runtime
        # decoder honours both, so the length - and every loop point checked
        # against it - is the length the player will actually hear.
        offset += _mp3_frame(data, first)[0]
        trim = gapless
    while offset < end:
        frame = _mp3_frame(data, offset)
        if frame is None:
            # Trailing tags (APE, Lyrics3) or padding after the last frame.
            break
        length, rate, chans, samples = frame
        if rate != sample_rate:
            raise AudioError("the MP3 changes sample rate part-way (%d Hz, then "
                             "%d Hz)" % (sample_rate, rate))
        if chans != channels:
            raise AudioError("the MP3 switches between mono and stereo")
        if offset + length > end:
            break  # a truncated last frame: the decoder drops it too
        frames += samples
        offset += length
    return AudioInfo(MP3, sample_rate, channels, max(frames - trim, 0), len(data))


def _mp3_gapless(data: bytes, offset: int) -> Optional[int]:
    """For a Xing/Info header frame at ``offset``: the sample frames the LAME
    tag says to trim (priming plus padding), 0 without a LAME tag. ``None``
    when the frame is ordinary audio.

    Mirrors dr_mp3's reading of the same bytes, which is what plays the file.
    """
    length = _mp3_frame(data, offset)[0]
    b1, b3 = data[offset + 1], data[offset + 3]
    mpeg1 = ((b1 >> 3) & 0x03) == 3
    mono = ((b3 >> 6) & 0x03) == 3
    side = (17 if mono else 32) if mpeg1 else (9 if mono else 17)
    tag = offset + 4 + (0 if b1 & 0x01 else 2) + side
    end = offset + length
    if tag + 8 > end or data[tag:tag + 4] not in (b"Xing", b"Info"):
        return None
    flags = data[tag + 7]
    cursor = tag + 8
    cursor += 4 if flags & 0x01 else 0
    cursor += 4 if flags & 0x02 else 0
    cursor += 100 if flags & 0x04 else 0
    cursor += 4 if flags & 0x08 else 0
    if cursor >= end or not data[cursor]:
        return 0
    cursor += 21
    if cursor + 14 >= end:
        return 0
    delay = ((data[cursor] << 4) | (data[cursor + 1] >> 4)) + 529
    padding = (((data[cursor + 1] & 0x0F) << 8) | data[cursor + 2]) - 529
    return delay + max(padding, 0)


# -- WAV -----------------------------------------------------------------

_WAVE_PCM = 1
_WAVE_FLOAT = 3
_WAVE_EXTENSIBLE = 0xFFFE


def inspect_wav(data: bytes) -> AudioInfo:
    """Read a RIFF WAVE header. Raises :class:`AudioError` for one the runtime
    does not decode."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        if data[:4] in (b"RF64", b"BW64"):
            raise AudioError("64-bit WAV (RF64) is not supported; export a "
                             "standard WAV under 4 GB")
        raise AudioError("not a RIFF WAVE file")
    offset = 12
    fmt = None
    data_size = None
    while offset + 8 <= len(data):
        chunk, size = data[offset:offset + 4], struct.unpack_from("<I", data, offset + 4)[0]
        body = offset + 8
        if chunk == b"fmt ":
            if size < 16 or body + size > len(data):
                raise AudioError("the WAV format chunk is truncated")
            fmt = struct.unpack_from("<HHIIHH", data, body)
            tag = fmt[0]
            if tag == _WAVE_EXTENSIBLE:
                if size < 40:
                    raise AudioError("the WAV extensible format chunk is truncated")
                tag = struct.unpack_from("<H", data, body + 24)[0]
            fmt = (tag,) + fmt[1:]
        elif chunk == b"data":
            # Some writers leave the size at 0 or past the end while streaming;
            # the decoders read to the end of the file in that case.
            available = len(data) - body
            data_size = size if 0 < size <= available else available
            break
        offset = body + size + (size & 1)
    if fmt is None:
        raise AudioError("the WAV has no format chunk")
    if data_size is None:
        raise AudioError("the WAV has no audio data")

    tag, channels, sample_rate, _rate, block_align, bits = fmt
    if tag == _WAVE_PCM:
        if bits not in (8, 16, 24, 32):
            raise AudioError("%d-bit PCM WAV is not supported; use 16 or 24-bit"
                             % bits)
    elif tag == _WAVE_FLOAT:
        if bits not in (32, 64):
            raise AudioError("%d-bit float WAV is not supported" % bits)
    else:
        raise AudioError("compressed WAV (format 0x%04X) is not supported; use "
                         "uncompressed PCM" % tag)
    if channels not in (1, 2):
        raise AudioError("the WAV has %d channels; use mono or stereo" % channels)
    if block_align != channels * bits // 8 or block_align == 0:
        raise AudioError("the WAV's block alignment does not match its format")
    return AudioInfo(WAV, sample_rate, channels, data_size // block_align, len(data))


# -- either --------------------------------------------------------------


def detect(data: bytes) -> Optional[str]:
    """The codec ``data`` looks like, from its leading bytes, or ``None``."""
    if data[:4] in (b"RIFF", b"RF64", b"BW64"):
        return WAV
    if data[:3] == b"ID3" or (len(data) >= 2 and data[0] == 0xFF and (data[1] & 0xE0) == 0xE0):
        return MP3
    return None


def inspect_bytes(data: bytes) -> AudioInfo:
    """Recognise and bound a music file's contents."""
    if len(data) > MAX_FILE_BYTES:
        raise AudioError("the file is %.1f MB; the limit is %d MB"
                         % (len(data) / 1048576.0, MAX_FILE_BYTES // 1048576))
    codec = detect(data)
    if codec == WAV:
        info = inspect_wav(data)
    elif codec == MP3:
        info = inspect_mp3(data)
    else:
        raise AudioError("not an MP3 or WAV file")
    if not MIN_SAMPLE_RATE <= info.sample_rate <= MAX_SAMPLE_RATE:
        raise AudioError("a sample rate of %d Hz is not supported" % info.sample_rate)
    if info.seconds < MIN_SECONDS:
        raise AudioError("the music is %.2f s long; it must be at least %.0f s"
                         % (info.seconds, MIN_SECONDS))
    if info.seconds > MAX_SECONDS:
        raise AudioError("the music is %.0f s long; the limit is %d minutes"
                         % (info.seconds, MAX_SECONDS // 60))
    return info


def inspect_file(path: str) -> AudioInfo:
    if not path or not os.path.isfile(path):
        raise AudioError("music file not found: %s" % (path or "(none chosen)"))
    if os.path.getsize(path) > MAX_FILE_BYTES:
        raise AudioError("the file is %.1f MB; the limit is %d MB"
                         % (os.path.getsize(path) / 1048576.0, MAX_FILE_BYTES // 1048576))
    with open(path, "rb") as handle:
        return inspect_bytes(handle.read())


def check_loop(info: AudioInfo, loop_start: float, loop_end: float) -> None:
    """Raise :class:`AudioError` unless the loop fits the music.

    ``loop_end`` of 0 means the end of the file. The song plays from the start,
    reaches ``loop_end`` and jumps back to ``loop_start``, so an intro can play
    once before the loop.
    """
    if loop_start < 0 or loop_end < 0:
        raise AudioError("loop points cannot be negative")
    end = loop_end if loop_end > 0 else info.seconds
    if end > info.seconds + 1e-3:
        raise AudioError("the loop ends at %.2f s but the music is only %.2f s"
                         % (end, info.seconds))
    if end - loop_start < MIN_LOOP_SECONDS:
        raise AudioError("the loop must be at least %.2f s long (it is %.2f s)"
                         % (MIN_LOOP_SECONDS, end - loop_start))
