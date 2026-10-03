"""Write the ``.dkrmap`` container DKR-R loads custom tracks from.

The layout and the rules are in docs/CUSTOM_TRACKS.md. A track is a directory
named ``*.dkrmap`` holding a manifest and one binary payload per asset-table
section, and DKR-R serves those bytes without ever parsing a level format.

**What this module produces.** The ``LEVEL_OBJECT_MAPS`` payload is written
here, by :mod:`object_map_encoder`, without the decomp's ``dkr_assets_tool`` -
that tool builds a whole ``assets.bin`` and ships as a Linux binary, so
depending on it would have put a C++ toolchain between an author and their
track. The encoder is checked against the retail bytes: it reproduces all 136
shipped object maps exactly.

``TEXTURES_3D`` is written here too, and it is the one section a track adds
**many** payloads to. Their order is their identity - the runtime numbers them
by position and the level model names them by the same position - so they are
written in ordinal order and never sorted.

The glTF sources go in beside the payloads, both as the input the asset tool
would take and as something an author can read.

A manifest never claims a payload that is not there: bytes promised and missing
fail at load time with a far more confusing error than the one reported here.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
from typing import Dict, List, Optional

from . import (gltf_io, level_header, level_model_encoder,
               minimap as minimap_module, music_audio, music_sequence,
               object_map_encoder, rice_pack, textures as texture_module)
from .gltf_io import ObjectMap

MANIFEST_NAME = "manifest.json"
SCHEMA_VERSION = 1
#: A package that carries its own music. Older runtimes refuse any schema they
#: do not know, which is the point: a track that silently played the carrier
#: song instead of its own music would look like a working export.
MUSIC_SCHEMA_VERSION = 2
SUFFIX = ".dkrmap"

#: Sections a track can add, and the payload file each conventionally uses.
SECTIONS = {
    "LEVEL_HEADERS": "header.bin",
    "LEVEL_NAMES": "name.bin",
    "LEVEL_MODELS": "model.bin",
}

#: A level has **two** object maps, and the runtime needs to know which is
#: which: it patches header 0xBA from the ``structure`` slot and 0x36 from
#: ``collectables``. A ``LEVEL_OBJECT_MAPS`` entry without a slot is refused
#: rather than guessed, so the two are named here.
OBJECT_MAP_SECTION = "LEVEL_OBJECT_MAPS"
OBJECT_MAP_SLOTS = {
    "structure": "objects_structure.bin",
    "collectables": "objects_collectables.bin",
}

#: The section a track's own artwork goes in. Unlike the four above it takes
#: **many** payloads rather than one, and their order in ``adds`` is what
#: decides which texture is which: the runtime assigns ids by position, and the
#: level model refers to them by the same position through
#: :data:`..textures.CUSTOM_ID_BASE`. Reordering these entries silently
#: repaints the track.
TEXTURE_SECTION = "TEXTURES_3D"

#: ``textures/0.bin``, ``textures/1.bin`` ... - a subdirectory because a track
#: with a dozen of its own images should not bury its four payloads.
TEXTURE_DIR = "textures"

#: The track's own minimap: its picture in ``ASSET_TEXTURES_2D`` and the sprite
#: that wraps it in ``ASSET_SPRITES``. Both tables are published once, at boot,
#: exactly as ``TEXTURES_3D`` is, and both entries are named by placeholder -
#: the model's ``minimapSpriteIndex`` and the sprite's ``baseTextureId`` - that
#: the runtime rewrites, because the real indices depend on the player's ROM.
#: See :mod:`.minimap`.
MINIMAP_TEXTURE_SECTION = "TEXTURES_2D"
MINIMAP_SPRITE_SECTION = "SPRITES"
MINIMAP_DIR = "minimap"
MINIMAP_FILES = {
    MINIMAP_TEXTURE_SECTION: MINIMAP_DIR + "/texture.bin",
    MINIMAP_SPRITE_SECTION: MINIMAP_DIR + "/sprite.bin",
}

#: The track's own recorded music. It is not an asset-table section: DKR never
#: sees these bytes. The runtime decodes the file on the host and mixes it into
#: the game's audio while the retail song the header names (the *carrier*)
#: plays silently, so the game's own fades and final-lap speed-up still drive
#: it. One file, named ``main`` plus the codec's extension.
MUSIC_DIR = "music"
MUSIC_STEM = "main"
MUSIC_FORMAT = "audio-stream-v1"

#: What the music does when the game speeds its song up on the final lap.
FINAL_LAP_SPEEDUP = "speedup"     # play faster, pitch rising, as retail does
FINAL_LAP_CONSTANT = "constant"   # keep the recorded speed
FINAL_LAP_MODES = (FINAL_LAP_SPEEDUP, FINAL_LAP_CONSTANT)

#: Percent of the file's own level. Above 100 boosts a quiet master; the
#: runtime clamps the mixed sample, so the ceiling only bounds how hard.
MAX_MUSIC_VOLUME = 200

#: The other kind of music: a native DKR sequence (``ALCSeq``), normally
#: converted from a MIDI file by :mod:`.midi_import`. The game plays it itself,
#: through its own instruments: the runtime copies it into the music buffer
#: when the carrier starts, so fades, the final-lap speed-up and the
#: MidiFade/MidiChSet objects all act on it. ``dkr-stock-v1`` is the program
#: and drum-key layout of the retail bank, the same in US 1.0 and 1.1.
SEQUENCE_FORMAT = "dkr-alcseq-v1"
SEQUENCE_BANK = "dkr-stock-v1"
SEQUENCE_EXTENSION = ".cseq"
#: The MIDI file a sequence was converted from, kept under ``source/`` for the
#: author. The runtime never reads it.
SEQUENCE_SOURCE_FILE = "music.mid"
#: DKR's own base volume for a song (gSeqSoundTable): most retail race songs
#: use 110. Over 127, base times the options slider overflows the player.
DEFAULT_SEQUENCE_VOLUME = 110
MAX_SEQUENCE_VOLUME = 127
#: The header's /instruments for a track with its own sequence: every channel
#: the song uses must start enabled, whatever an inherited header said.
SEQUENCE_CHANNEL_MASK = 0xFFFF

#: Where the addon leaves asset-tool input inside the track directory.
SOURCE_DIR = "source"

_ID_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")


class DkrMapError(Exception):
    pass


def normalise_id(text: str) -> str:
    """Turn a track title into the lowercase hyphenated id the manifest wants."""
    slug = re.sub(r"[^a-z0-9]+", "-", (text or "").lower()).strip("-")
    return slug or "untitled-track"


def validate_id(track_id: str) -> None:
    if not _ID_RE.match(track_id):
        raise DkrMapError(
            "track id %r must be lowercase words joined by single hyphens"
            % track_id
        )


class TrackPackage:
    """One ``.dkrmap`` directory being assembled."""

    def __init__(self, directory: str, track_id: str, name: str, author: str = "",
                 revision: str = ""):
        validate_id(track_id)
        self.directory = directory
        self.track_id = track_id
        self.name = name or track_id
        self.author = author
        #: Which extracted revision the payloads were built from, e.g.
        #: ``us.v80``. The encoding and the asset indices are portable between
        #: revisions, but a header's *content* is not: seven of its unknown
        #: fields differ in all 65 retail levels. Recording it lets the runtime
        #: warn when a track is loaded against a different ROM.
        self.revision = revision
        #: section -> absolute path of a compiled payload the author supplied.
        self.payloads: Dict[str, str] = {}
        #: slot -> absolute path, for the two object maps.
        self.object_maps: Dict[str, str] = {}
        #: The track's own textures, **in ordinal order**, as absolute paths.
        #: A list rather than a mapping because position is the identity here:
        #: see :data:`TEXTURE_SECTION`.
        self.texture_payloads: List[str] = []
        #: ``{"file", "textureDigest", "textures"}`` once the high-resolution
        #: texture pack has been written beside the package, else ``None``.
        #: The pack is not part of the package - see :mod:`.rice_pack` - but
        #: the manifest names it, so the two can be matched up later.
        self.hd_pack: Optional[Dict[str, object]] = None
        #: section -> absolute path, for the minimap's picture and sprite.
        self.minimap: Dict[str, str] = {}
        #: ``(source path, manifest descriptor)`` for the track's own music,
        #: set by :meth:`set_music` or :meth:`set_sequence`, else ``None``.
        self.music: Optional[tuple] = None
        #: A native sequence's bytes (set_sequence), written as they are.
        self.sequence: Optional[bytes] = None
        self.notes: List[str] = []

    # -- sources ---------------------------------------------------------

    def write_object_map(self, object_map: ObjectMap, stem: str = "objects") -> str:
        """Write one map's glTF pair into ``source/``, named for its slot."""
        source = os.path.join(self.directory, SOURCE_DIR)
        os.makedirs(source, exist_ok=True)
        gltf_name = stem + ".gltf"
        gltf_path = os.path.join(source, gltf_name)
        gltf_io.save(object_map, gltf_path)
        gltf_io.save_sidecar(os.path.join(source, stem + ".json"), gltf_name)
        return gltf_path

    def encode_object_map(self, slot: str, object_map: ObjectMap, catalog,
                          translation_table, asset_index=None) -> bytes:
        """Compile one of the two object maps and attach it under its slot.

        This is what makes a package loadable rather than merely well formed.
        The bytes are the same ones the asset tool would produce; see
        ``tests/test_encoder.py``, which requires exactly that for every retail
        map.
        """
        if slot not in OBJECT_MAP_SLOTS:
            raise DkrMapError(
                "%r is not an object-map slot; expected %s"
                % (slot, " or ".join(sorted(OBJECT_MAP_SLOTS)))
            )
        payload = object_map_encoder.pack(
            object_map, catalog, translation_table, asset_index
        )
        os.makedirs(self.directory, exist_ok=True)
        path = os.path.join(self.directory, OBJECT_MAP_SLOTS[slot])
        with open(path, "wb") as handle:
            handle.write(payload)
        self.object_maps[slot] = path
        return payload

    def encode_header(self, document: Dict, enum_values,
                      asset_index=None) -> bytes:
        """Compile the level header and attach it.

        ``document`` is an extracted level header, normally the base track's:
        a Phase 1 remix keeps its geometry, world and race type and changes only
        what the author edits. The two runtime-owned offsets are left at zero.
        """
        try:
            payload = level_header.encode(document, enum_values, asset_index)
        except level_header.HeaderError as error:
            raise DkrMapError("could not compile the level header: %s" % error) from error
        os.makedirs(self.directory, exist_ok=True)
        path = os.path.join(self.directory, SECTIONS["LEVEL_HEADERS"])
        with open(path, "wb") as handle:
            handle.write(payload)
        self.payloads["LEVEL_HEADERS"] = path
        return payload

    def encode_level_model(self, model) -> bytes:
        """Compile edited track geometry and attach it as ``LEVEL_MODELS``.

        Only worth calling when the author actually changed the geometry. A
        track that reworks objects over shipped geometry should ship no model
        payload at all: the header's ``geometry`` field then keeps pointing at
        the base track's model, and the package stays small and stays correct.
        Writing an unchanged copy would work, but it makes every remix carry a
        hundred kilobytes that say nothing.

        The bytes are what the game loads, not what the asset tool takes as
        input - see :mod:`level_model_encoder`, held to byte equality against
        every extracted retail model.

        A BSP the game cannot walk is rebuilt here, on the way out, whatever
        made it - a model re-segmented by an earlier version of the addon
        carries one that draws segment 255 and crashes. Rebuilding keeps the
        segment order and the node count, so the layout does not move, and a
        retail tree, which always walks, is never touched.
        """
        from . import level_model_layout  # noqa: PLC0415

        problems = level_model_layout.bsp_problems(model)
        if problems:
            model.bsp = level_model_layout.build_bsp(model.bounding_boxes)
            self.notes.append(
                "rebuilt the segment BSP: the game could not walk the old one "
                "(%s)" % "; ".join(problems))
        try:
            payload = level_model_encoder.pack(model)
        except level_model_encoder.LevelModelEncodeError as error:
            raise DkrMapError("could not compile the track geometry: %s" % error)
        os.makedirs(self.directory, exist_ok=True)
        path = os.path.join(self.directory, SECTIONS["LEVEL_MODELS"])
        with open(path, "wb") as handle:
            handle.write(payload)
        self.payloads["LEVEL_MODELS"] = path
        return payload

    def encode_textures(self, entries) -> List[bytes]:
        """Compile the track's own artwork and attach it, in ordinal order.

        ``entries`` are :class:`..textures.CustomTexture` in the order the model
        refers to them, which is the order this writes them and the order the
        manifest lists them. Nothing here reconciles the two: the caller has
        already written ``CUSTOM_ID_BASE + n`` into the model's texture table
        for the entry at position ``n``, and if that ever disagreed with this
        list the track would draw the wrong pictures rather than fail. So the
        ordinals are checked against their positions instead of trusted.
        """
        payloads = []
        folder = os.path.join(self.directory, TEXTURE_DIR)
        os.makedirs(folder, exist_ok=True)
        self.texture_payloads = []
        for position, entry in enumerate(entries):
            if int(getattr(entry, "ordinal", position)) != position:
                raise DkrMapError(
                    "texture %r says it is number %d but is being written %s, "
                    "and the runtime assigns ids by position - the track would "
                    "draw the wrong picture rather than fail"
                    % (getattr(entry, "name", "?"), entry.ordinal, position)
                )
            try:
                payload = entry.encode()
            except texture_module.TextureEncodeError as error:
                raise DkrMapError(
                    "could not compile the texture %r: %s"
                    % (getattr(entry, "name", "?"), error)
                )
            path = os.path.join(folder, "%d.bin" % position)
            with open(path, "wb") as handle:
                handle.write(payload)
            self.texture_payloads.append(path)
            payloads.append(payload)

        # A texture the author has since removed leaves its payload behind, and
        # while the manifest no longer names it, a stale numbered file next to
        # the live ones invites exactly the misreading this numbering cannot
        # survive. Clear the tail rather than leave it.
        position = len(payloads)
        while True:
            stale = os.path.join(folder, "%d.bin" % position)
            if not os.path.isfile(stale):
                break
            os.remove(stale)
            position += 1
        return payloads

    def encode_minimap(self, texture: bytes, sprite: bytes) -> None:
        """Attach the track's own minimap: its picture and the sprite around it.

        The picture is one texture, so it is the track's 2D texture 0, and the
        sprite's placeholder names exactly that. One of each, always together:
        a sprite without its texture would name a texture that is not there.
        """
        folder = os.path.join(self.directory, MINIMAP_DIR)
        os.makedirs(folder, exist_ok=True)
        for section, payload in ((MINIMAP_TEXTURE_SECTION, texture),
                                 (MINIMAP_SPRITE_SECTION, sprite)):
            path = os.path.join(self.directory, *MINIMAP_FILES[section].split("/"))
            with open(path, "wb") as handle:
                handle.write(payload)
            self.minimap[section] = path

    def drop_minimap(self) -> None:
        """Remove a minimap an earlier export left, for a track that has none.

        The manifest only claims what this export attached, so a stale pair
        would never load - but it would sit there looking as if it did.
        """
        self.minimap = {}
        for relative in MINIMAP_FILES.values():
            stale = os.path.join(self.directory, *relative.split("/"))
            if os.path.isfile(stale):
                os.remove(stale)
        folder = os.path.join(self.directory, MINIMAP_DIR)
        if os.path.isdir(folder) and not os.listdir(folder):
            os.rmdir(folder)

    def set_music(self, source_path: str, carrier: int, volume: int = 100,
                  loop_start: float = 0.0, loop_end: float = 0.0,
                  final_lap: str = FINAL_LAP_SPEEDUP) -> "music_audio.AudioInfo":
        """Ship ``source_path`` as the track's music.

        Everything is checked here, before anything is written: the file's
        bytes, the loop against its real length, and the carrier. ``carrier``
        is the header's ``/music`` - the retail song that plays silently so the
        game keeps driving fades and tempo - and must not be 0, which is "no
        music": with nothing playing there is nothing to follow.

        The file itself is copied by :meth:`write`, last but the manifest.
        """
        try:
            info = music_audio.inspect_file(source_path)
            music_audio.check_loop(info, loop_start, loop_end)
        except music_audio.AudioError as error:
            raise DkrMapError("the track's music cannot be used: %s" % error) from error
        carrier = self._check_carrier(
            carrier, "it stays silent and only drives the fades and the final-lap speed-up")
        if final_lap not in FINAL_LAP_MODES:
            raise DkrMapError("unknown final-lap behaviour %r" % final_lap)
        volume = max(0, min(int(volume), MAX_MUSIC_VOLUME))
        start_frame = int(round(loop_start * info.sample_rate))
        end_frame = int(round(loop_end * info.sample_rate)) if loop_end > 0 else 0
        end_frame = min(end_frame, info.frames)
        with open(source_path, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        descriptor = {
            "format": MUSIC_FORMAT,
            "codec": info.codec,
            "file": "%s/%s%s" % (MUSIC_DIR, MUSIC_STEM, music_audio.EXTENSIONS[info.codec]),
            "sha256": digest,
            "bytes": info.size,
            "sampleRate": info.sample_rate,
            "channels": info.channels,
            "frames": info.frames,
            "carrierSequence": carrier,
            "volume": volume,
            "loopStartFrame": start_frame,
            "loopEndFrame": end_frame,
            "finalLap": final_lap,
        }
        self.music = (os.path.abspath(source_path), descriptor)
        self.sequence = None
        self.notes.append("music: %s, loop %.2f s to %s" % (
            info.summary(), loop_start,
            "%.2f s" % loop_end if loop_end > 0 else "the end"))
        return info

    def set_sequence(self, data: bytes, carrier: int, tempo_bpm: float,
                     volume: int = DEFAULT_SEQUENCE_VOLUME, reverb: int = 1,
                     source_path: Optional[str] = None) -> "music_sequence.Report":
        """Ship ``data``, a native DKR sequence, as the track's music.

        The game plays it in place of ``carrier``, the header's ``/music``: it
        is copied into the music buffer whenever that song starts, so the song
        starts, loops, fades and speeds up on the final lap exactly as a
        retail one does. ``tempo_bpm`` is the song's own tempo, which the game
        must also be told - it scales that number on the final lap - so it has
        to round into 1-255. The header's /instruments must enable every
        channel (:data:`SEQUENCE_CHANNEL_MASK`); the runtime refuses a package
        whose header says otherwise. ``source_path``, the MIDI file it came
        from, is kept under ``source/`` for the author.

        Checked here against the music buffer, before anything is written.
        """
        try:
            report = music_sequence.validate(bytes(data))
        except music_sequence.SequenceError as error:
            raise DkrMapError("the track's music cannot be used: %s" % error) from error
        carrier = self._check_carrier(
            carrier, "your song plays in its place and keeps its fades and final-lap speed-up")
        tempo = int(round(float(tempo_bpm)))
        if not 1 <= tempo <= 255:
            raise DkrMapError(
                "the song plays at %.1f BPM; the game keeps a song's tempo in one "
                "byte and its final-lap speed-up only works up to 255 BPM. Halve "
                "the tempo (and double the note lengths) in your MIDI file"
                % float(tempo_bpm))
        volume = max(0, min(int(volume), MAX_SEQUENCE_VOLUME))
        reverb = 1 if reverb else 0
        descriptor = {
            "format": SEQUENCE_FORMAT,
            "bank": SEQUENCE_BANK,
            "file": "%s/%s%s" % (MUSIC_DIR, MUSIC_STEM, SEQUENCE_EXTENSION),
            "sha256": hashlib.sha256(bytes(data)).hexdigest(),
            "bytes": len(data),
            "carrierSequence": carrier,
            "tempoBpm": tempo,
            "volume": volume,
            "reverb": reverb,
            "channelMask": SEQUENCE_CHANNEL_MASK,
        }
        self.music = (os.path.abspath(source_path) if source_path else None, descriptor)
        self.sequence = bytes(data)
        self.notes.append("music: %s" % report.summary())
        return report

    def _check_carrier(self, carrier, role: str) -> int:
        try:
            carrier = int(carrier)
        except (TypeError, ValueError):
            carrier = 0
        if not 0 < carrier < 256:
            raise DkrMapError(
                "the track's music needs a game song under it (the header's "
                "music is \"none\"). Pick any race song in Music; %s" % role
            )
        return carrier

    def drop_music(self) -> None:
        """Forget the track's music and remove what an earlier export wrote.

        Only the generated copies under ``music/`` and ``source/`` go; the
        author's own file, wherever it lives, is never touched.
        """
        self.music = None
        self.sequence = None
        self._remove_music_copies(keep=None)

    def _remove_music_copies(self, keep: Optional[str]) -> None:
        source = os.path.join(self.directory, SOURCE_DIR, SEQUENCE_SOURCE_FILE)
        if self.sequence is None and os.path.isfile(source):
            os.remove(source)
        folder = os.path.join(self.directory, MUSIC_DIR)
        if not os.path.isdir(folder):
            return
        for extension in list(music_audio.EXTENSIONS.values()) + [SEQUENCE_EXTENSION]:
            name = MUSIC_STEM + extension
            path = os.path.join(folder, name)
            if name != keep and os.path.isfile(path):
                os.remove(path)
        if not os.listdir(folder):
            os.rmdir(folder)

    def add_payload(self, section: str, path: str) -> None:
        """Attach a compiled section payload produced by the asset tool."""
        if section not in SECTIONS:
            raise DkrMapError(
                "%r is not an asset-table section; expected one of %s"
                % (section, ", ".join(sorted(SECTIONS)))
            )
        if not os.path.isfile(path):
            raise DkrMapError("payload for %s not found: %s" % (section, path))
        self.payloads[section] = path

    # -- output ----------------------------------------------------------

    def manifest(self) -> Dict[str, object]:
        """The manifest, listing only payloads that actually exist."""
        adds = [
            {"section": section, "file": SECTIONS[section]}
            for section in SECTIONS
            if section in self.payloads
        ]
        # Every object-map entry carries its slot; the runtime refuses one
        # without, rather than guessing which header field to patch.
        adds += [
            {
                "section": OBJECT_MAP_SECTION,
                "slot": slot,
                "file": OBJECT_MAP_SLOTS[slot],
            }
            for slot in OBJECT_MAP_SLOTS
            if slot in self.object_maps
        ]
        # Position is the identity for these, so they are listed in the order
        # they were written and never sorted.
        adds += [
            {
                "section": TEXTURE_SECTION,
                "file": "%s/%d.bin" % (TEXTURE_DIR, position),
            }
            for position in range(len(self.texture_payloads))
        ]
        # The picture before the sprite, which names it by the picture's
        # position among this track's 2D textures.
        adds += [
            {"section": section, "file": MINIMAP_FILES[section]}
            for section in (MINIMAP_TEXTURE_SECTION, MINIMAP_SPRITE_SECTION)
            if section in self.minimap
        ]
        manifest = {
            "schemaVersion": MUSIC_SCHEMA_VERSION if self.music else SCHEMA_VERSION,
            "id": self.track_id,
            "name": self.name,
            "adds": adds,
        }
        if self.author:
            manifest["author"] = self.author
        if self.revision:
            manifest["builtFrom"] = self.revision
        if self.music:
            manifest["music"] = dict(self.music[1])
        if self.hd_pack:
            # Informational: the runtime reads the keys it knows and nothing
            # else. The digest is the pack stamp's, so a pack and a package
            # from two different exports can be told apart.
            manifest["hdTexturePack"] = {
                "file": self.hd_pack["file"],
                "textureDigest": self.hd_pack["textureDigest"],
            }
        return manifest

    def missing_sections(self) -> List[str]:
        """What a playable track still needs.

        Neither object-map slot is optional once a header ships. The header
        leaves 0x36 and 0xBA at zero for the runtime, and zero is a valid index,
        not an absence - the game clamps anything out of range to 0 and loads
        object map 0. A slot with no payload therefore points at another level's
        objects. An empty map is how a track says it has none.
        """
        missing = [s for s in ("LEVEL_HEADERS",) if s not in self.payloads]
        if "LEVEL_HEADERS" not in missing:
            missing += [
                "LEVEL_OBJECT_MAPS (%s)" % slot
                for slot in OBJECT_MAP_SLOTS
                if slot not in self.object_maps
            ]
        elif "structure" not in self.object_maps:
            missing.append("LEVEL_OBJECT_MAPS (structure)")
        return missing

    def write(self) -> str:
        """Create the directory, copy payloads in and write the manifest."""
        os.makedirs(self.directory, exist_ok=True)
        for section, source_path in self.payloads.items():
            destination = os.path.join(self.directory, SECTIONS[section])
            if os.path.abspath(source_path) != os.path.abspath(destination):
                shutil.copyfile(source_path, destination)

        # The music is copied before the manifest that claims it, and checked
        # against the digest recorded when it was validated: an author who
        # saves over the file mid-export gets an error, not a package whose
        # manifest describes different bytes.
        if self.music and self.sequence is not None:
            # A converted sequence: the bytes are the addon's own, and the MIDI
            # file they came from goes under source/ for the author.
            source_path, descriptor = self.music
            destination = os.path.join(self.directory, *descriptor["file"].split("/"))
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            with open(destination, "wb") as handle:
                handle.write(self.sequence)
            if source_path and os.path.isfile(source_path):
                kept = os.path.join(self.directory, SOURCE_DIR, SEQUENCE_SOURCE_FILE)
                os.makedirs(os.path.dirname(kept), exist_ok=True)
                if os.path.abspath(source_path) != os.path.abspath(kept):
                    shutil.copyfile(source_path, kept)
            self._remove_music_copies(keep=os.path.basename(destination))
        elif self.music:
            source_path, descriptor = self.music
            destination = os.path.join(self.directory, *descriptor["file"].split("/"))
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            with open(source_path, "rb") as handle:
                data = handle.read()
            if hashlib.sha256(data).hexdigest() != descriptor["sha256"]:
                raise DkrMapError("the music file changed while exporting; export again")
            if os.path.abspath(source_path) != os.path.abspath(destination):
                with open(destination, "wb") as handle:
                    handle.write(data)
            self._remove_music_copies(keep=os.path.basename(destination))
        else:
            self._remove_music_copies(keep=None)

        manifest_path = os.path.join(self.directory, MANIFEST_NAME)
        with open(manifest_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(self.manifest(), indent=2, sort_keys=True) + "\n")

        self._write_build_notes()
        return manifest_path

    def _write_build_notes(self) -> None:
        """Leave the author instructions for the step the addon cannot do."""
        missing = self.missing_sections()
        path = os.path.join(self.directory, "HOW-TO-BUILD.md")
        lines = [
            "# %s" % self.name,
            "",
            "Written by the DKR track editor Blender addon.",
            "",
            "`%s/` holds the authored object map as the glTF pair that the" % SOURCE_DIR,
            "decomp's `dkr_assets_tool` consumes. The addon stops there: that tool",
            "builds a whole `assets.bin` from the decomp's asset tree rather than",
            "emitting one section at a time, so producing the `.bin` payloads is a",
            "separate step.",
            "",
        ]
        if self.object_maps:
            lines += [
                "## Object maps",
                "",
                "A level has **two**, and the runtime patches a different header",
                "field from each: `0xBA` from `structure`, `0x36` from",
                "`collectables`. Both are compiled here by the addon, and are the",
                "same bytes the asset tool would produce - the encoder is checked",
                "against all 136 retail maps in",
                "`tools/blender/tests/test_encoder.py`.",
                "",
            ] + [
                "- `%s` (%s slot)" % (os.path.basename(path), slot)
                for slot, path in sorted(self.object_maps.items())
            ] + [""]
        if self.texture_payloads:
            lines += [
                "## This track's own textures",
                "",
                "These are artwork the ROM does not hold. DKR-R publishes a",
                "longer `ASSET_TEXTURES_3D` table for them and the level model",
                "is rewritten as it is served, so the ids in `model.bin` are",
                "placeholders rather than the indices the game will use - the",
                "real ones depend on how many textures the player's ROM has.",
                "",
                "**Order is identity.** Entry *n* in the manifest becomes",
                "texture *n* of this track. Reordering or renumbering these",
                "files repaints the track without any error.",
                "",
            ] + [
                "- `%s/%d.bin`" % (TEXTURE_DIR, position)
                for position in range(len(self.texture_payloads))
            ] + [""]
        if self.minimap:
            lines += [
                "## Minimap",
                "",
                "The track's own minimap: `%s` is the picture, an IA8"
                % MINIMAP_FILES[MINIMAP_TEXTURE_SECTION],
                "texture, and `%s` the sprite that anchors it on the"
                % MINIMAP_FILES[MINIMAP_SPRITE_SECTION],
                "HUD. DKR-R appends them to `ASSET_TEXTURES_2D` and `ASSET_SPRITES`",
                "and points the model's `minimapSpriteIndex` at the sprite, so",
                "like the track's own textures they load from the next launch",
                "after the track is installed.",
                "",
            ]
        if self.hd_pack:
            pack = self.hd_pack["file"]
            lines += [
                "## High-resolution textures",
                "",
                "`%s`, beside this directory, holds the full-resolution" % pack,
                "originals of %d of this track's own textures. The track carries"
                % self.hd_pack["textures"],
                "each at the size the console can load - 64x32 for a colour image -",
                "and is complete without the pack. With it, DKR-R's renderer draws",
                "the original in place of the reduction.",
                "",
            ]
            if self.hd_pack.get("minimap"):
                lines += [
                    "It also holds the minimap drawn again at %d times the size,"
                    % minimap_module.HD_SCALE,
                    "from the same road edges, so it covers what the small one does",
                    "and the racers' dots stay on the road.",
                    "",
                ]
            lines += [
                "You do not install it by hand. Import the track (below) with",
                "`%s` sitting beside this folder: DKR-R installs the pack with" % pack,
                "the track, enabled, filed against it rather than added to the",
                "texture browser. Track Lab then shows *HD textures: restart to",
                "load*, and one **Restart & play in HD** arms the track, switches",
                "to Modern, and relaunches straight into it.",
                "",
                "The pack applies to the **Modern** preset; importing or playing",
                "the track switches to it for you. Accurate always draws the",
                "track's own reduced textures, which is correct as well.",
                "",
                "The pack belongs to this export: `manifest.json` and the pack's",
                "`%s` both carry the texture digest `%s`."
                % (rice_pack.STAMP_NAME, self.hd_pack["textureDigest"]),
                "DKR-R compares them and leaves a pack from another export out,",
                "so keep the two together.",
                "",
            ]
        if missing:
            lines += [
                "## Still needed",
                "",
                "This track will not load yet. Missing payloads: %s."
                % ", ".join(missing),
                "",
                "`LEVEL_HEADERS` has to come from the level header the track is",
                "based on, rebuilt with its own name and geometry. The addon does",
                "not write one yet.",
                "",
                "Drop the missing `.bin` into this directory and export again; the",
                "manifest picks up whatever is present.",
                "",
            ]
        else:
            lines += [
                "## Ready",
                "",
                "Every payload the manifest needs is present.",
                "",
            ]
        lines += [
            "## Do not commit this package",
            "",
            "A remix keeps whatever the base track had, so the payloads here",
            "contain retail object-map data verbatim wherever you did not change",
            "it. `docs/ASSET_POLICY.md` forbids committing extracted maps, and",
            "notes that deleting a file later does not remove it from history.",
            "",
            "Share the `.blend` and this addon instead; anyone with the decomp",
            "can rebuild the package. Only a track built from synthetic data,",
            "with nothing carried over, is safe to publish.",
            "",
            "## Installing",
            "",
            "In DKR-R's Track Lab, **Import a copy**, and point the picker at",
            "this folder, at the `.dkrmap`, or at the folder that holds both it",
            "and any `-hd.zip`. A `.zip` of this folder works too. Copying it",
            "into `custom-tracks/` by hand still works. Not `mods/`: librecomp",
            "owns that for its own `.nrm` format and rejects anything else.",
            "",
            "Track Lab draws in either presentation profile now; import, arm or",
            "play switches to Modern (custom tracks need it) and says so.",
            "",
            "Payloads you drop in here are kept: re-exporting from Blender",
            "rewrites the object maps and leaves everything else alone.",
            "",
            "See `docs/CUSTOM_TRACKS.md` in DKR-R for the manifest contract and",
            "`docs/LEVEL_OBJECT_MAP_FORMAT.md` for the binary format.",
            "",
        ]
        if self.notes:
            lines += ["## Notes", ""] + ["- %s" % n for n in self.notes] + [""]
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines))


def hd_pack_path(package_directory: str) -> str:
    """Where a package's high-resolution texture pack goes: beside it.

    ``my-track.dkrmap`` gets ``my-track-hd.zip`` in the same folder. Never
    inside - the runtime serves a package's files byte for byte, and a pack in
    there would be carried and never read.
    """
    stem = os.path.splitext(os.path.normpath(package_directory))[0]
    return stem + "-hd.zip"


def package_path(directory: str, track_id: str) -> str:
    """``custom-tracks/`` path for a track: the id with the ``.dkrmap`` suffix."""
    return os.path.join(directory, track_id + SUFFIX)


def read_manifest(package_directory: str) -> Optional[Dict[str, object]]:
    path = os.path.join(package_directory, MANIFEST_NAME)
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)
