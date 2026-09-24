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

import json
import os
import re
import shutil
from typing import Dict, List, Optional

from . import (gltf_io, level_header, level_model_encoder,
               object_map_encoder, rice_pack, textures as texture_module)
from .gltf_io import ObjectMap

MANIFEST_NAME = "manifest.json"
SCHEMA_VERSION = 1
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
        payload = level_header.encode(document, enum_values, asset_index)
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
            "schemaVersion": SCHEMA_VERSION,
            "id": self.track_id,
            "name": self.name,
            "adds": adds,
        }
        if self.author:
            manifest["author"] = self.author
        if self.revision:
            manifest["builtFrom"] = self.revision
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
