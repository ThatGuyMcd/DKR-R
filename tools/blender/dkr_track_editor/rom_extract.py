"""Extract the addon's asset tree straight from a Diddy Kong Racing ROM.

Everything the addon draws - a coin's sprite, a frog's mesh, a track's textures
- comes from an extracted decomp asset tree (:mod:`.assets`). Making an author
clone the decomp, build its Linux asset tool and run ``extract.sh`` just to get
that tree is the single biggest hurdle the addon has, so this module produces
the same tree from the ROM the author already owns: DKR-R itself does not run
without one.

It is a port of the decomp's ``dkr_assets_tool extract`` for exactly the asset
types the addon reads, and it writes them where that tool writes them, under
the same names, in the same JSON - so :class:`.assets.AssetTree` cannot tell
the two apart. The names are not in the ROM; they come from
``data/rom_tables.json.gz``, which ``generate_rom_tables.py`` distils from the
decomp: each record is identified by its SHA1, exactly as the asset tool does.

Types the addon never reads (text, fonts, particles, animations, ghosts...) are
still named in the section manifests, so every ``order`` list is complete, but
their files are not written.

Deliberately free of ``bpy``. :func:`extract` takes a progress callback and a
cancel check so the Blender operator can run it on a worker thread.
"""

from __future__ import annotations

import gzip
import hashlib
import json
import math
import os
import shutil
import struct
import tempfile
import zlib
from typing import Callable, Dict, List, Optional

TABLES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "data", "rom_tables.json.gz")

#: The revisions DKR-R runs. The tables know PAL and Japanese ROMs too, which is
#: what lets the addon say *which* unsupported ROM it was handed.
SUPPORTED_VERSIONS = ("us.v77", "us.v80")

#: Cartridge magic in each byte order the dumping tools produce.
_MAGIC = {
    b"\x80\x37\x12\x40": "z64",
    b"\x37\x80\x40\x12": "v64",
    b"\x40\x12\x37\x80": "n64",
}

#: Enough for any retail cartridge, small enough to refuse a stray video file
#: before reading it into memory.
MAX_ROM_BYTES = 64 * 1024 * 1024

#: Types written to disk. Everything else is named but skipped.
EXTRACTED_TYPES = frozenset((
    "Binary", "Texture", "Sprite", "ObjectHeader", "ObjectModel", "LevelModel",
    "LevelHeader", "LevelObjectMap", "LevelObjectTranslationTable", "Audio",
))

_VERSION_NUMBER = {"v77": 77, "v79": 79, "v80": 80}

TEXTURE_FORMATS = ("RGBA32", "RGBA16", "I8", "I4", "IA16", "IA8", "IA4", "CI4", "CI8")
TEXTURE_RENDER_MODES = ("TRANSPARENT", "OPAQUE", "TRANSPARENT_2", "OPAQUE_2")
TEXTURE_HEADER_SIZE = 0x20
TEXTURE_FPS = 60.0

LOTT_SIZE = 512


class RomError(Exception):
    """The file is not a ROM this addon can extract from. The message is
    written for the author."""


class Cancelled(Exception):
    """The author stopped the extraction."""


# ---------------------------------------------------------------------------
# Tables
# ---------------------------------------------------------------------------

_tables_cache: Dict[str, dict] = {}


def load_tables(path: str = TABLES_PATH) -> dict:
    if path not in _tables_cache:
        with open(path, "rb") as handle:
            _tables_cache[path] = json.loads(gzip.decompress(handle.read()).decode("utf-8"))
    return _tables_cache[path]


# ---------------------------------------------------------------------------
# Identifying a ROM
# ---------------------------------------------------------------------------

class RomIdentity:
    """What a file turned out to be."""

    __slots__ = ("path", "name", "version", "sha1", "byte_order", "assets",
                 "assets_end")

    def __init__(self, path, name, version, sha1, byte_order, assets, assets_end):
        self.path = path
        self.name = name
        self.version = version
        self.sha1 = sha1
        self.byte_order = byte_order
        self.assets = assets
        self.assets_end = assets_end

    @property
    def supported(self) -> bool:
        return self.version in SUPPORTED_VERSIONS

    def __repr__(self):
        return "RomIdentity(%r, %s, %s)" % (self.name, self.version, self.byte_order)


def normalise(data: bytes) -> (bytes, str):
    """The ROM in big-endian ``z64`` order, and the order it arrived in.

    The byte order is read from the cartridge header, never the extension: a
    ``.n64`` that is really a ``.z64`` is common.
    """
    kind = _MAGIC.get(bytes(data[:4]))
    if kind is None:
        raise RomError("not an N64 ROM: the file does not start with a cartridge "
                       "header. If it is zipped, unzip it first")
    if kind == "z64":
        return bytes(data), kind
    width = 2 if kind == "v64" else 4
    if len(data) % width:
        raise RomError("the ROM's size is not a whole number of words; the dump "
                       "is truncated")
    swapped = bytearray(len(data))
    for index in range(width):
        swapped[index::width] = data[width - index - 1::width]
    return bytes(swapped), kind


def read_rom(path: str, tables: Optional[dict] = None):
    """``(normalised bytes, RomIdentity)`` for a file on disk."""
    tables = tables or load_tables()
    try:
        size = os.path.getsize(path)
    except OSError as error:
        raise RomError("cannot read %s: %s" % (path, error.strerror or error)) from error
    if size < 0x1000 or size > MAX_ROM_BYTES:
        raise RomError("%s is not an N64 ROM (%d bytes)" % (os.path.basename(path), size))
    with open(path, "rb") as handle:
        raw = handle.read()
    data, order = normalise(raw)
    sha1 = hashlib.sha1(data).hexdigest()
    for entry in tables["inputs"]:
        if entry["sha1"] == sha1:
            return data, RomIdentity(path, entry["name"], entry["dkr_version"].lower(),
                                     sha1, order, int(entry["assets"]),
                                     int(entry["assets_end"]))
    title = data[0x20:0x34].decode("ascii", "replace").strip(" \0")
    if title.upper().startswith("DIDDY"):
        raise RomError("this Diddy Kong Racing ROM is not a known clean dump "
                       "(SHA1 %s). Modified, hacked or overdumped ROMs cannot be "
                       "extracted; use an unmodified USA ROM" % sha1[:12])
    raise RomError("this is not a Diddy Kong Racing ROM (header says %r)" % title)


def identify(path: str, tables: Optional[dict] = None) -> RomIdentity:
    return read_rom(path, tables)[1]


def candidate_roms() -> List[str]:
    """ROMs DKR-R already knows about, so the author need not browse for one.

    DKR-R keeps a canonical copy of the ROM in its config directory
    (``rom-cache``) and remembers the last one picked in the launcher. The
    config directory is ``%APPDATA%/DKRPort`` on Windows and
    ``$XDG_CONFIG_HOME/dkr-port`` (``~/.config/dkr-port``) elsewhere. A
    portable install keeps it beside the executable, which cannot be found from
    here and is simply not offered.
    """
    roots = []
    appdata = os.environ.get("APPDATA")
    if appdata:
        roots.append(os.path.join(appdata, "DKRPort"))
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.join(
        os.path.expanduser("~"), ".config")
    roots.append(os.path.join(xdg, "dkr-port"))

    found = []

    def add(path):
        path = os.path.normpath(path.strip().strip('"'))
        if path and os.path.isfile(path) and path not in found:
            found.append(path)

    for root in roots:
        for folder in (os.path.join(root, "rom-cache"), root):
            try:
                names = sorted(os.listdir(folder))
            except OSError:
                continue
            for name in names:
                if name.lower().endswith((".z64", ".n64", ".v64")):
                    add(os.path.join(folder, name))
        for listing in ("last-rom.txt", "rom-catalog.txt"):
            try:
                with open(os.path.join(root, listing), "r", encoding="utf-8") as handle:
                    for line in handle:
                        if line.strip() and not line.startswith("#"):
                            add(line.split("\t")[0])
            except OSError:
                continue
    return found


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

def _s32(data, offset):
    return struct.unpack_from(">i", data, offset)[0]


def _align16(value):
    return (value + 15) & ~15


def _with_extension(filename: str, extension: str) -> str:
    """``std::filesystem::path::replace_extension`` on a bare filename."""
    stem = filename
    dot = filename.rfind(".")
    if dot > 0 and filename not in (".", ".."):
        stem = filename[:dot]
    return stem + extension


def rare_inflate(data: bytes) -> bytes:
    """Rare's compression: little-endian size, a level byte, raw DEFLATE."""
    if len(data) < 5:
        raise RomError("a compressed record is truncated")
    expected = struct.unpack_from("<I", data, 0)[0]
    out = zlib.decompressobj(-15).decompress(bytes(data[5:]))
    if len(out) < expected:
        raise RomError("a compressed record decoded short (%d of %d bytes)"
                       % (len(out), expected))
    return out[:expected]


def dumps_json(value, indent=4) -> str:
    """JSON as the asset tool writes it: sorted keys, raw UTF-8, newline."""
    return json.dumps(value, indent=indent, sort_keys=True, ensure_ascii=False) + "\n"


def _set(document, path, value):
    """``document[a][0][b] = value`` from ``"a/0/b"``, creating as it goes,
    the way the asset tool fills JSON through pointers."""
    keys = path.strip("/").split("/")
    node = document
    for key, following in zip(keys, keys[1:]):
        child_is_list = following.isdigit()
        if isinstance(node, list):
            index = int(key)
            while len(node) <= index:
                node.append(None)
            if node[index] is None:
                node[index] = [] if child_is_list else {}
            node = node[index]
        else:
            if key not in node:
                node[key] = [] if child_is_list else {}
            node = node[key]
    last = keys[-1]
    if isinstance(node, list):
        index = int(last)
        while len(node) <= index:
            node.append(None)
        node[index] = value
    else:
        node[last] = value


# ---------------------------------------------------------------------------
# PNG
# ---------------------------------------------------------------------------

def write_png(path: str, pixels, width: int, height: int, channels: int) -> None:
    """An 8-bit PNG: grey+alpha (2) or RGBA (4), as ``stbi_write_png`` saves
    the asset tool's IA and RGBA images."""
    import numpy as np

    rows = np.asarray(pixels, dtype=np.uint8).reshape(height, width * channels)
    filtered = np.zeros((height, width * channels + 1), dtype=np.uint8)
    filtered[:, 1:] = rows
    colour_type = 6 if channels == 4 else 4

    def chunk(kind, body):
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(filtered.tobytes(), 6))
            + chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(data)


# ---------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------

def _bits_per_pixel(texture_format: str) -> int:
    return {"RGBA32": 32, "RGBA16": 16, "IA16": 16, "I8": 8, "IA8": 8,
            "CI8": 8}.get(texture_format, 4)


def _image_size(width, height, texture_format) -> int:
    return width * height * _bits_per_pixel(texture_format) // 8


def _deinterlace(data, width, height, bits):
    """``N64Image::interlace``: undo TMEM's swizzle of every odd row."""
    import numpy as np

    bytes_per_row = width // 2 if bits == 4 else width * (bits // 8)
    chunk = 8 if bits == 32 else 4
    stride = chunk * 2
    swaps = bytes_per_row // stride
    if swaps <= 0 or height < 2:
        return data
    rows = data[:bytes_per_row * height].reshape(height, bytes_per_row).copy()
    odd = rows[1::2, :swaps * stride].reshape(-1, swaps, 2, chunk)
    rows[1::2, :swaps * stride] = odd[:, :, ::-1, :].reshape(odd.shape[0], -1)
    out = data.copy()
    out[:bytes_per_row * height] = rows.reshape(-1)
    return out


def _flip(data, width, height, bits):
    """``N64Image::flip_vertically`` on the raw texels."""
    bytes_per_row = width // 2 if bits == 4 else width * (bits // 8)
    rows = data[:bytes_per_row * height].reshape(height, bytes_per_row)[::-1]
    out = data.copy()
    out[:bytes_per_row * height] = rows.reshape(-1)
    return out


def _nibbles(data, count):
    import numpy as np

    out = np.empty(count, dtype=np.uint16)
    pairs = data[:(count + 1) // 2].astype(np.uint16)
    out[0::2] = (pairs >> 4)[:len(out[0::2])]
    out[1::2] = (pairs & 0xF)[:len(out[1::2])]
    return out


def texels_to_pixels(texels, width, height, texture_format):
    """``(pixels, channels)`` exactly as the asset tool saves them.

    RGBA formats become RGBA; everything else becomes grey+alpha, scaled with
    n64graphics' ``SCALE_5_8`` / ``SCALE_4_8`` / ``SCALE_3_8``.
    """
    import numpy as np

    count = width * height
    if texture_format == "RGBA16":
        words = texels[:count * 2].view(">u2").astype(np.uint32)
        out = np.empty((count, 4), dtype=np.uint8)
        out[:, 0] = ((words >> 11) & 0x1F) * 0xFF // 0x1F
        out[:, 1] = ((words >> 6) & 0x1F) * 0xFF // 0x1F
        out[:, 2] = ((words >> 1) & 0x1F) * 0xFF // 0x1F
        out[:, 3] = np.where(words & 1, 0xFF, 0)
        return out, 4
    if texture_format == "RGBA32":
        return texels[:count * 4].reshape(count, 4), 4

    out = np.empty((count, 2), dtype=np.uint8)
    if texture_format == "IA16":
        pairs = texels[:count * 2].reshape(count, 2)
        out[:] = pairs
    elif texture_format == "IA8":
        values = texels[:count].astype(np.uint16)
        out[:, 0] = (values >> 4) * 0x11
        out[:, 1] = (values & 0xF) * 0x11
    elif texture_format == "IA4":
        bits = _nibbles(texels, count)
        out[:, 0] = ((bits >> 1) & 7) * 0x24
        out[:, 1] = np.where(bits & 1, 0xFF, 0)
    elif texture_format == "I8":
        out[:, 0] = texels[:count]
        out[:, 1] = 0xFF
    elif texture_format == "I4":
        out[:, 0] = _nibbles(texels, count) * 0x11
        out[:, 1] = 0xFF
    else:
        raise RomError("texture format %s cannot be saved" % texture_format)
    return out, 2


# ---------------------------------------------------------------------------
# DKR text
# ---------------------------------------------------------------------------

def decode_dkr_text(data: bytes, offset: int, characters: List[str]):
    """``(text, bytes used)`` of a NUL-terminated DKR string.

    Plain ASCII unless a byte has its top bit set, in which case the whole
    string is DKRJP: two bytes per character, indexing the font table.
    """
    size = 0
    japanese = False
    while offset + size < len(data) and data[offset + size] != 0:
        byte = data[offset + size]
        size += 1
        if byte & 0x80:
            japanese = True
            size += 1
    raw = data[offset:offset + size]
    if not japanese:
        return raw.decode("latin-1"), size
    text = []
    index = 0
    while index < len(raw):
        byte = raw[index]
        if byte & 0x80:
            table_index = ((byte & 0x7F) << 8) | (raw[index + 1] if index + 1 < len(raw) else 0)
            index += 1
            if table_index >= len(characters):
                raise RomError("a level name uses DKRJP character 0x%X, past the "
                               "font table" % table_index)
            text.append(characters[table_index])
        else:
            text.append(chr(byte))
        index += 1
    return "".join(text), size


# ---------------------------------------------------------------------------
# Asset tables inside the ROM (assetTable.cpp)
# ---------------------------------------------------------------------------

def _parse_table(view: bytes, table_type: str):
    """``[(offset, size), ...]`` for one section table."""
    entries = []
    if table_type == "Fixed":
        count = _s32(view, 0)
        for index in range(count):
            start = _s32(view, 4 + index * 4)
            entries.append((start, _s32(view, 8 + index * 4) - start))
        return entries, _align16(4 + count * 4)

    if table_type == "ObjectAnimationIdsTable":
        return entries, 0

    step, first, value_at, scale = 4, 0, 0, 1
    if table_type == "MenuText":
        first = 4
    elif table_type == "TTGhost":
        step, value_at = 8, 4
    elif table_type == "Miscellaneous":
        scale = 4
    if table_type == "Audio":
        entries.append((0, _s32(view, 0)))

    offset = first
    while True:
        value = _s32(view, offset + value_at)
        following = _s32(view, offset + value_at + step)
        if value == -1 or following == -1:
            return entries, _align16(offset)
        if table_type == "GameText":
            size = (following & 0x7FFFFFFF) - (value & 0x7FFFFFFF)
        else:
            size = following - value
        if value < 0 and table_type != "GameText":
            raise RomError("asset table entry 0x%08X is invalid" % (value & 0xFFFFFFFF))
        entries.append((value * scale, size * scale))
        offset += step


_TABLE_TYPES = ("GameText", "MenuText", "TTGhost", "Miscellaneous", "Audio",
                "ObjectAnimationIdsTable")


# ---------------------------------------------------------------------------
# The extraction
# ---------------------------------------------------------------------------

class _Item:
    """One record to extract (``ExtractInfo``)."""

    __slots__ = ("type", "build_id", "filename", "folder", "data", "section",
                 "index")

    def __init__(self, type_, build_id, filename, folder, data, section, index):
        self.type = type_
        self.build_id = build_id
        self.filename = filename
        self.folder = folder
        self.data = data
        self.section = section
        self.index = index


class Extractor:
    """Runs ``AssetExtractor::extract_all`` over an in-memory ROM.

    ``progress(fraction, message)`` is called as work completes, and
    ``cancelled()`` is polled between records; returning ``True`` raises
    :class:`Cancelled`.
    """

    def __init__(self, rom: bytes, identity: RomIdentity, root: str,
                 tables: Optional[dict] = None,
                 progress: Optional[Callable[[float, str], None]] = None,
                 cancelled: Optional[Callable[[], bool]] = None):
        self.rom = rom
        self.identity = identity
        self.root = root
        self.tables = tables or load_tables()
        self._progress = progress or (lambda fraction, message: None)
        self._cancelled = cancelled or (lambda: False)
        self.version_number = _VERSION_NUMBER[identity.version.split(".")[-1]]

        self.sections = self.tables["sections"]
        self.files = self.tables["files"]
        self.by_sha1: Dict[str, List[int]] = {}
        for index, row in enumerate(self.files):
            self.by_sha1.setdefault(row[0], []).append(index)

        #: ``ExtractStats``: section build id -> [(build id, local path)].
        self.stats: Dict[str, List[tuple]] = {}
        self.menu_text_count = 0
        self.groups: Dict[int, List[_Item]] = {}
        self.deferred: Dict[str, List[_Item]] = {}

        self.object_headers: Dict[str, dict] = {}
        self.translation_table: List[Optional[str]] = []
        self.skipped: Dict[str, int] = {}
        self.written = 0

    # -- helpers ----------------------------------------------------------

    def _check(self):
        if self._cancelled():
            raise Cancelled()

    def _row(self, index):
        row = self.files[index]
        return {
            "sha1": row[0], "build-id": row[1], "filename": row[2],
            "folder": row[3], "type": row[4],
            "version": row[5] if len(row) > 5 else "v77",
        }

    def build_id_at(self, section: str, index: int) -> str:
        entries = self.stats.get(section, [])
        if index < 0 or index >= len(entries):
            raise RomError("%s has no entry %d" % (section, index))
        return entries[index][0]

    def _table_index(self, build_id):
        for index, section in enumerate(self.sections):
            if section.get("for") == build_id:
                return index
        return None

    def _pick(self, candidates, section, used):
        """``figure_out_correct_config_entry_index``: several files share a
        hash (duplicate textures, mostly), so drop the ones from a later
        revision, already used, or of the wrong type."""
        section_id = section.get("build-id", "")
        section_type = section.get("default-type", "Binary")
        remaining = []
        for index in candidates:
            row = self._row(index)
            if _VERSION_NUMBER.get(row["version"], 77) > self.version_number:
                continue
            if index in used:
                continue
            file_type = row["type"] or "Binary"
            if file_type != section_type:
                continue
            if file_type == "Texture":
                if section_id == "ASSET_TEXTURES_2D" and "TEX3D" in row["build-id"]:
                    continue
                if section_id == "ASSET_TEXTURES_3D" and "TEX2D" in row["build-id"]:
                    continue
            remaining.append(index)
        if not remaining:
            raise RomError("no name for a record of %s" % section_id)
        return remaining[0]

    def _path(self, item: _Item, extension: str) -> str:
        return os.path.join(self.root, item.folder, _with_extension(item.filename, extension))

    def _write_json(self, item: _Item, document, extension=".json", indent=4):
        path = self._path(item, extension)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(dumps_json(document, indent))
        self.written += 1

    def _write_raw(self, item: _Item):
        path = self._path(item, ".bin")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as handle:
            handle.write(item.data)
        self.written += 1

    # -- planning ---------------------------------------------------------

    def plan(self):
        identity = self.identity
        assets = self.rom[identity.assets:identity.assets_end]
        main, main_size = _parse_table(assets, "Fixed")
        if len(main) != len(self.sections):
            raise RomError("the ROM has %d asset sections where %d were expected"
                           % (len(main), len(self.sections)))
        views = [assets[start + main_size:start + main_size + size]
                 for start, size in main]

        for index, section in enumerate(self.sections):
            section_type = section.get("default-type", "Binary")
            if section_type in ("Empty", "Table"):
                continue
            build_id = section["build-id"]
            section_folder = section.get("folder", "")
            deferred = bool(section.get("defer", False))
            group = int(section.get("group", 0))
            table_index = self._table_index(build_id)

            def add(row, data, entry_index):
                folder = row["folder"]
                local = row["filename"] + ".json"
                if folder:
                    local = folder + "/" + local
                if section_folder:
                    folder = section_folder + "/" + folder
                item = _Item(row["type"] or section_type, row["build-id"],
                             row["filename"], folder, data, build_id, entry_index)
                if deferred:
                    self.deferred.setdefault(build_id, []).append(item)
                elif item.type != "NoExtract":
                    self.groups.setdefault(group, []).append(item)
                self.stats.setdefault(build_id, []).append((row["build-id"], local))

            if table_index is None:
                sha1 = hashlib.sha1(views[index]).hexdigest()
                candidates = self.by_sha1.get(sha1)
                if not candidates:
                    raise RomError("section %s is not one the tables know" % build_id)
                row = self._row(candidates[0])
                # A single-file section is named after the section itself.
                row["build-id"] = build_id
                add(row, views[index], 0)
                continue

            table_type = section_type if section_type in _TABLE_TYPES else "Variable"
            entries, _ = _parse_table(views[table_index], table_type)
            if build_id == "ASSET_MENU_TEXT":
                self.menu_text_count = _s32(views[table_index], 0)
            used = []
            for entry_index, (start, size) in enumerate(entries):
                start &= 0x7FFFFFFF
                data = views[index][start:start + size]
                candidates = self.by_sha1.get(hashlib.sha1(data).hexdigest())
                if not candidates:
                    raise RomError("record %d of %s is not one the tables know; the "
                                   "ROM may be modified" % (entry_index, build_id))
                chosen = (candidates[0] if len(candidates) == 1
                          else self._pick(candidates, section, used))
                used.append(chosen)
                add(self._row(chosen), data, entry_index)

    # -- manifests (generate_main_json_file) -------------------------------

    def write_manifests(self):
        main = {
            "@dkrat-version": self.tables["source"]["dkrat-version"],
            "@dkr-version": self.identity.version,
            "assets": {"order": [], "sections": {}},
        }
        for section in self.sections:
            build_id = section.get("build-id", "")
            section_type = section.get("default-type", "Binary")
            main["assets"]["order"].append(build_id)
            out = {"type": section_type}
            main["assets"]["sections"][build_id] = out
            if section_type == "Table":
                out["for"] = section.get("for", "")
            elif section_type == "ObjectAnimationIdsTable":
                out["for"] = "ASSET_OBJECT_ANIMATIONS"
                out["deferred"] = bool(section.get("defer", False))
            elif section_type == "Empty":
                pass
            elif section.get("defer", False):
                info = section.get("defer-info", {})
                out["deferred"] = True
                out["defer-info"] = {
                    "from-section": info.get("from-section", ""),
                    "id-postfix": info.get("id-postfix", "_UNK"),
                    "output-path": info.get("output-path", "."),
                }
            else:
                filename = build_id.lower() + ".meta.json"
                out["filename"] = filename
                self._write_section_manifest(section, filename)
        self._write_file("assets.meta.json", main)

    def _write_section_manifest(self, section, filename):
        section_type = section.get("default-type", "Binary")
        document = {"type": section_type, "folder": section.get("folder", "")}
        if section_type == "MenuText":
            document["menu-text-build-ids"] = list(
                self.tables["menu-text-build-ids"][:self.menu_text_count])
            document["menu-text-build-ids"] += [""] * (
                self.menu_text_count - len(document["menu-text-build-ids"]))
        entries = self.stats.get(section.get("build-id", ""))
        if entries:
            if len(entries) == 1:
                document["filename"] = entries[0][1]
            else:
                document["files"] = {
                    "order": [build_id for build_id, _ in entries],
                    "sections": {build_id: {"filename": local}
                                 for build_id, local in entries},
                }
        self._write_file(filename, document)

    def _write_file(self, relative, document):
        path = os.path.join(self.root, relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(dumps_json(document))
        self.written += 1

    # -- the run ----------------------------------------------------------

    def run(self):
        self._progress(0.0, "Reading the ROM's asset tables")
        self.plan()
        self.write_manifests()

        items = [item for group in sorted(self.groups) for item in self.groups[group]]
        total = max(1, len(items))
        handlers = {
            "Binary": self._extract_binary,
            "ObjectModel": self._extract_binary,
            "LevelModel": self._extract_binary,
            "Audio": self._extract_audio,
            "Texture": self._extract_texture,
            "Sprite": self._extract_sprite,
            "ObjectHeader": self._extract_object_header,
            "LevelObjectTranslationTable": self._extract_translation_table,
            "LevelHeader": self._extract_level_header,
            "LevelObjectMap": self._extract_object_map,
        }
        labels = {
            "Texture": "textures", "Sprite": "sprites", "ObjectHeader": "objects",
            "ObjectModel": "object models", "LevelModel": "track models",
            "LevelHeader": "level headers", "LevelObjectMap": "object maps",
        }
        for done, item in enumerate(items):
            self._check()
            handler = handlers.get(item.type)
            if handler is None:
                self.skipped[item.type] = self.skipped.get(item.type, 0) + 1
                continue
            if done % 16 == 0:
                self._progress(0.02 + 0.98 * done / total,
                               "Extracting %s" % labels.get(item.type, item.type.lower()))
            handler(item)
        self._progress(1.0, "Done")

    # -- types ------------------------------------------------------------

    def _extract_binary(self, item):
        self._write_json(item, {"type": item.type, "raw": _with_extension(item.filename, ".bin")})
        self._write_raw(item)

    def _extract_audio(self, item):
        if not item.data:
            self._write_json(item, {"type": "Audio", "raw": None})
            return
        self._write_json(item, {"type": "Audio", "raw": _with_extension(item.filename, ".bin")})
        self._write_raw(item)

    def _extract_texture(self, item):
        import numpy as np

        data = item.data
        header = data[:TEXTURE_HEADER_SIZE]
        frames = header[0x12]
        compressed = bool(header[0x1D])
        texels_all = rare_inflate(data[TEXTURE_HEADER_SIZE:]) if compressed else data
        section = next(s for s in self.sections if s.get("build-id") == item.section)
        flip = bool(section.get("flip-textures-by-default", False))

        folder = os.path.join(self.root, item.folder)
        os.makedirs(folder, exist_ok=True)
        images = []
        first_format = None
        offset = 0
        for frame in range(frames):
            width, height, fmt = texels_all[offset], texels_all[offset + 1], texels_all[offset + 2]
            flags = struct.unpack_from(">h", texels_all, offset + 6)[0]
            frame_size = struct.unpack_from(">h", texels_all, offset + 0x16)[0]
            texture_format = TEXTURE_FORMATS[fmt & 0xF]
            first_format = first_format or texture_format
            bits = _bits_per_pixel(texture_format)
            size = _image_size(width, height, texture_format)
            start = offset + TEXTURE_HEADER_SIZE
            texels = np.zeros(_align16(size), dtype=np.uint8)
            chunk = np.frombuffer(texels_all[start:start + size], dtype=np.uint8)
            texels[:len(chunk)] = chunk
            if flags & 0x400:
                texels = _deinterlace(texels, width, height, bits)
            if flip:
                texels = _flip(texels, width, height, bits)
            pixels, channels = texels_to_pixels(texels, width, height, texture_format)
            name = (_with_extension(item.filename, ".png") if frames == 1
                    else "%s_%d.png" % (item.filename, frame))
            write_png(os.path.join(folder, name), pixels, width, height, channels)
            self.written += 1
            images.append(name)
            offset += frame_size

        flags = struct.unpack_from(">h", header, 6)[0]
        render_mode = (header[2] >> 4) & 0xF
        if render_mode >= len(TEXTURE_RENDER_MODES):
            raise RomError("%s has an invalid render mode" % item.build_id)
        document = {
            "images": images,
            "type": "Texture",
            "format": first_format,
            "render-mode": TEXTURE_RENDER_MODES[render_mode],
            "flags": {
                "wrap-s": "Clamp" if flags & 0x40 else "Wrap",
                "wrap-t": "Clamp" if flags & 0x80 else "Wrap",
            },
        }
        if flip:
            document["flipped-image"] = True
        if compressed:
            document["compressed"] = True
        if frames > 1:
            delay = struct.unpack_from(">H", header, 0x14)[0] / TEXTURE_FPS
            document["frame-advance-delay"] = _round_half_away(delay * 1000.0) / 1000.0
        for bit, key in ((0x1, "anti-aliased"), (0x4, "semi-transparent"),
                         (0x10, "cutout"), (0x400, "interlaced")):
            if flags & bit:
                document["flags"][key] = True
        sprite_x = struct.unpack_from(">b", header, 3)[0]
        sprite_y = struct.unpack_from(">b", header, 4)[0]
        if sprite_x or sprite_y:
            document["sprite-x"] = sprite_x
            document["sprite-y"] = sprite_y
        if struct.unpack_from(">h", header, 0x16)[0] != 0:
            document["write-size"] = True
        self._write_json(item, document)

    def _extract_sprite(self, item):
        data = item.data
        start, frames, unk4, unk6 = struct.unpack_from(">hhhh", data, 0)
        counts = [data[12 + i + 1] - data[12 + i] for i in range(max(0, frames))]
        document = {
            "type": "Sprite",
            "start-texture": self.build_id_at("ASSET_TEXTURES_2D", start),
            "unk4": unk4,
            "unk6": unk6,
        }
        if counts:
            document["frame-tex-count"] = counts
        self._write_json(item, document)

    def _enum(self, name, value):
        values = self.tables["enums"][name]["values"]
        key = str(int(value))
        if key not in values:
            raise RomError("enum %s has no member for %d" % (name, value))
        return values[key]

    def _extract_object_header(self, item):
        data = item.data
        u = lambda fmt, offset: struct.unpack_from(">" + fmt, data, offset)[0]  # noqa: E731
        document = {"type": "ObjectHeader"}

        behavior = u("b", 0x54)
        document["behavior"] = self._enum("ObjectBehaviours", behavior) if behavior >= 0 else None
        model_type = u("b", 0x53)
        document["model-type"] = self._enum("ObjectModelType", model_type)

        flags = u("H", 0x30)
        if flags:
            document["flags"] = ["FLAG_%04x" % (1 << bit) for bit in range(16)
                                 if flags & (1 << bit)]

        end = 0x60
        while end < len(data) and data[end] != 0:
            end += 1
        document["internal-name"] = data[0x60:end].decode("latin-1")

        document["shadow-scale"] = u("f", 0x04)
        document["scale"] = u("f", 0x0C)
        document["shade-brightness"] = u("f", 0x28)
        document["shade-ambient"] = u("f", 0x2C)

        unknown = {"unk0": u("i", 0x00), "unk8": u("f", 0x08), "unk34": u("h", 0x34),
                   "unk38": u("h", 0x38)}
        for name, offset in (("unk3A", 0x3A), ("unk3B", 0x3B), ("unk3C", 0x3C),
                             ("unk3D", 0x3D), ("unk59", 0x59), ("unk5B", 0x5B),
                             ("unk5C", 0x5C), ("unk5D", 0x5D), ("unk5E", 0x5E),
                             ("unk5F", 0x5F), ("unk70", 0x70), ("unk71", 0x71),
                             ("unk72", 0x72), ("unk73", 0x73), ("unk74", 0x74),
                             ("unk75", 0x75), ("unk76", 0x76), ("unk77", 0x77)):
            unknown[name] = data[offset]
        for name, offset in (("unk42", 0x42), ("unk44", 0x44), ("unk46", 0x46),
                             ("unk48", 0x48), ("unk4A", 0x4A), ("unk4C", 0x4C),
                             ("unk50", 0x50)):
            unknown[name] = u("h", offset)
        unknown["unk52"] = u("b", 0x52)
        unknown["unk58"] = u("b", 0x58)
        document["unknown"] = unknown

        document["shadow-group"] = u("h", 0x32)
        document["water-effect-group"] = u("h", 0x36)
        document["shade-angle-y"] = u("h", 0x3E)
        document["shade-angle-z"] = u("h", 0x40)
        document["draw-distance"] = u("h", 0x4E)

        model_ids = u("i", 0x10)
        part_ids = u("i", 0x14)
        part_indices = u("i", 0x18)
        particles = u("i", 0x1C)
        model_count = u("b", 0x55)
        part_count = u("b", 0x56)
        particle_count = u("b", 0x57)

        section = {0: "ASSET_OBJECT_MODELS", 4: "ASSET_TEXTURES_2D"}.get(model_type, "ASSET_SPRITES")
        if model_count > 0:
            document["models"] = [self.build_id_at(section, u("i", model_ids + 4 * i))
                                  for i in range(model_count)]
        if part_count > 0:
            document["vehicle-parts"] = [u("i", part_ids + 4 * i) for i in range(part_count)]
            document["vehicle-part-indices"] = [data[part_indices + i] for i in range(part_count)]
        junk = ((max(part_count, 0) + 7) & ~7) - max(part_count, 0)
        if junk > 0:
            document["vehicle-part-indices-junk-bytes"] = [
                data[part_indices + part_count + i] for i in range(junk)]

        if particle_count > 0:
            entries = []
            for i in range(particle_count):
                base = particles + 8 * i
                s0, s1 = u("h", base), u("h", base + 2)
                if s0 == -1:
                    entries.append({"special-case": True, "junk-data": u("i", base + 4),
                                    "arg1": data[base + 2], "arg2": data[base + 3]})
                else:
                    entries.append({"arg1": data[base], "arg2": data[base + 1], "arg3": s1,
                                    "arg4": u("h", base + 4), "arg5": u("h", base + 6)})
            document["particles"] = entries

        junk_start = particles + max(particle_count, 0) * 8
        if junk_start < len(data):
            document["junk-data"] = list(data[junk_start:])

        self.object_headers[item.build_id] = document
        self._write_json(item, document)

    def _extract_translation_table(self, item):
        data = item.data
        if len(data) // 2 != LOTT_SIZE:
            raise RomError("the level object translation table has the wrong size")
        entries = list(struct.unpack_from(">%dh" % LOTT_SIZE, data, 0))
        last = LOTT_SIZE
        while last > 0 and entries[last - 1] == LOTT_SIZE:
            last -= 1
        table = [None if entry == LOTT_SIZE else self.build_id_at("ASSET_OBJECTS", entry)
                 for entry in entries[:last]]
        self.translation_table = table
        document = {"type": "LevelObjectTranslationTable"}
        if table:
            document["table"] = table
        self._write_json(item, document)

    def _extract_level_header(self, item):
        data = item.data
        u = lambda fmt, offset: struct.unpack_from(">" + fmt, data, offset)[0]  # noqa: E731
        s8 = lambda offset: struct.unpack_from(">b", data, offset)[0]  # noqa: E731
        document = {"type": "LevelHeader"}

        names = self.deferred.get("ASSET_LEVEL_NAMES", [])
        if item.index < len(names):
            raw = bytes(names[item.index].data)
            characters = self.tables["dkrjp-characters"]
            language = self.tables["enums"]["Language"]
            offset = 0
            name = {}
            for index in range(language["count"]):
                text, used = decode_dkr_text(raw, offset, characters)
                offset += used + 1
                name[language["values"][str(index)]] = text
            document["name"] = name

        document["world"] = self._enum("World", s8(0x00))
        document["course-height"] = u("f", 0x08)
        document["model"] = self.build_id_at("ASSET_LEVEL_MODELS", u("h", 0x34))
        document["map-collectables"] = self.build_id_at("ASSET_LEVEL_OBJECT_MAPS", u("h", 0x36))
        document["map-2"] = self.build_id_at("ASSET_LEVEL_OBJECT_MAPS", u("h", 0xBA))
        if s8(0x4A) != 0:
            document["max-velocity"] = s8(0x4A)
        document["fov"] = s8(0x9C)
        if s8(0x4B) != 3:
            document["lap-count"] = s8(0x4B)
        pulse = u("i", 0xAC)
        if pulse != -1:
            document["pulsating-lights"] = self.build_id_at("ASSET_MISC", pulse)
        document["race-type"] = self._enum("RaceType", s8(0x4C))
        document["music"] = data[0x52]
        document["instruments"] = u("H", 0x54)
        misc = {}
        for index in range(7):
            value = u("i", 0x74 + 4 * index)
            if value != -1:
                misc[index] = self.build_id_at("ASSET_MISC", value)
        if misc:
            document["misc-assets"] = [misc.get(i) for i in range(max(misc) + 1)]
        document["default-vehicle"] = self._enum("Vehicle", s8(0x4D))
        available = s8(0x4E)
        vehicles = [self._enum("Vehicle", bit) for bit in range(8) if available & (1 << bit)]
        if vehicles:
            document["avaliable-vehicles"] = vehicles

        def ai_levels(offset):
            return {"base": s8(offset), "silver-coins": s8(offset + 1),
                    "completed": s8(offset + 2), "tracks-mode": s8(offset + 3),
                    "trophy-race": s8(offset + 4)}

        document["ai-levels"] = {"adv1": ai_levels(0x20), "adv2": ai_levels(0x25)}
        document["weather"] = {
            "enable": u("h", 0x90), "type": u("h", 0x92),
            "intensity": data[0x94], "opacity": data[0x95],
            "velocity": {"x": u("h", 0x96), "y": u("h", 0x98), "z": u("h", 0x9A)},
        }
        document["fog"] = {
            "near": u("h", 0x3A), "far": u("h", 0x3C),
            "colour": {"red": u("h", 0x3E), "green": u("h", 0x40), "blue": u("h", 0x42)},
        }

        background = {}
        skybox = {}
        if u("h", 0x38) >= 0:
            skybox["id"] = self.build_id_at("ASSET_OBJECTS", u("h", 0x38))
        if s8(0x49) != 0:
            skybox["special-sky"] = s8(0x49)
            skybox["special-sky-texture"] = u("i", 0xA4)
        if skybox:
            background["skybox"] = skybox
        background["colour"] = {"red": data[0x9D], "green": data[0x9E], "blue": data[0x9F]}
        background["multiplayer"] = {"gradient-colour": {
            "bottom": {"red": data[0xBE], "green": data[0xBF], "blue": data[0xC0]},
            "top": {"red": data[0xC1], "green": data[0xC2], "blue": data[0xC3]},
        }}
        document["background"] = background

        unknown = {
            "unk1": data[0x01], "unk2": s8(0x02), "unk3": s8(0x03),
            "unk4": [s8(0x04 + i) for i in range(4)],
            "unkC": [data[0x0C + i] for i in range(10)],
            "unk16": [data[0x16 + i] for i in range(10)],
            "unk2A": data[0x2A],
            "unk2B": [data[0x2B + i] for i in range(9)],
            "unk44": [data[0x44 + i] for i in range(5)],
            "unk4F": [data[0x4F + i] for i in range(3)],
            "unk53": data[0x53],
            "unk72": data[0x72], "unk73": data[0x73],
            "unkA0": u("h", 0xA0), "unkA2": s8(0xA2), "unkA3": s8(0xA3),
            "unkA8": u("h", 0xA8), "unkAA": u("h", 0xAA),
            "unkB0": u("h", 0xB0), "unkB2": data[0xB2], "unkB3": data[0xB3],
            "unkB9": data[0xB9], "unkBC": data[0xBC], "unkBD": s8(0xBD),
            # Read natively (little-endian) by the asset tool, and kept so.
            "unkC4": struct.unpack_from("<i", data, 0xC4)[0],
        }
        document["unknown"] = unknown
        document["waves"] = {
            "subdivisions": data[0x56], "unk57": data[0x57],
            "sine-step-0": data[0x58], "sine-base-0": data[0x59],
            "sine-height-0": u("h", 0x5A),
            "sine-step-1": data[0x5C], "sine-base-1": data[0x5D],
            "sine-height-1": u("h", 0x5E),
            "seed-size": u("h", 0x60), "wave-power": u("h", 0x62),
            "unk64": u("h", 0x64), "unk66": u("h", 0x66),
            "texture-ID": self.build_id_at("ASSET_TEXTURES_2D", u("h", 0x68)),
            "UV-Scale-X": data[0x6A], "UV-Scale-Y": data[0x6B],
            "UV-Scroll-X": s8(0x6C), "UV-Scroll-Y": s8(0x6D),
            "view-distance": u("h", 0x6E),
            "unk70": data[0x70], "unk71": data[0x71],
        }
        document["void"] = {
            "colour": {"red": data[0xB4], "green": data[0xB5], "blue": data[0xB6]},
            "enabled": data[0xB7],
        }
        document["boss-race-id"] = self._enum("BossSetupTypes", s8(0xB8))
        self._write_json(item, document)

    # -- object maps -------------------------------------------------------

    def _member_value(self, member, entry):
        """The integer a struct member holds, as the asset tool reads it."""
        size = member["size"]
        signed = member["signed"]
        offset = member["offset"]
        count = member.get("count")
        if count is None:
            value = int.from_bytes(entry[offset:offset + size], "big", signed=signed)
            # ``int value = get_integer_from_data(...)``
            value = (value + 2 ** 31) % 2 ** 32 - 2 ** 31
            return value
        # Array elements are never sign extended (``get_values_from_data_array``
        # reinterprets the raw bytes as an int64), so an ``s8[5]`` of -1s
        # extracts as 255s.
        return [int.from_bytes(entry[offset + i * size:offset + (i + 1) * size], "big")
                for i in range(count)]

    def _hinted(self, member, value):
        """``(keep, decoded)`` for one value under the member's hint."""
        hint = member.get("hint") or {}
        kind = hint.get("type")
        if not kind:
            return True, int(value)
        if kind == "Enum":
            return True, self._enum(hint["Enum"], value)
        if kind == "AssetId":
            section = hint["AssetId"]
            entries = self.stats.get(section, [])
            if value == -1 or value >= len(entries) or value < 0:
                return False, None
            return True, entries[value][0]
        if kind == "Angle":
            divide = float(hint.get("DivideBy") or 64)
            angle = (value / divide) * 360.0
            if angle > 360.0 and not member["signed"]:
                angle -= ((1 << (8 * member["size"])) / divide) * 360.0
            return True, angle
        if kind == "Scale":
            return True, float(value) / float(hint.get("DivideBy") or 64)
        if kind == "Object":
            if value < 0 or value >= len(self.translation_table):
                return False, None
            found = self.translation_table[value]
            return (found is not None and found != ""), found
        if kind == "Time":
            if value == -1:
                return False, None
            seconds = float(value) / 60.0
            places = hint.get("RoundToPlaces")
            if places:
                scale = 10.0 ** float(places)
                seconds = _round_half_away(seconds * scale) / scale
            return True, seconds
        raise RomError("unknown struct hint %s" % kind)

    def _extract_object_map(self, item):
        if not item.data:
            self._write_json(item, {"type": "LevelObjectMap", "objects": None})
            return
        raw = rare_inflate(item.data)
        structs = self.tables["structs"]
        order = self.tables["default-object-entries-order"]
        behaviours = {v: int(k) for k, v in
                      self.tables["enums"]["ObjectBehaviours"]["values"].items()}
        common = structs["LevelObjectEntryCommon"]

        nodes = [{"name": "objects", "children": []}]
        end = struct.unpack_from(">I", raw, 0)[0] + 16
        offset = 16
        while offset < end:
            entry = raw[offset:]
            object_id = entry[0] | ((entry[1] >> 7) << 8)
            size = entry[1] & 0x7F
            if size < 8:
                raise RomError("%s has a malformed object entry" % item.build_id)
            build_id = self.translation_table[object_id]
            header = self.object_headers.get(build_id)
            if header is None:
                raise RomError("%s names %s, which has no header" % (item.build_id, build_id))
            behaviour = header.get("behavior")
            struct_name = order[behaviours[behaviour]]
            layout = structs[struct_name]
            if layout["size"] != size:
                raise RomError("%s: %s is %d bytes, the entry %d"
                               % (item.build_id, struct_name, layout["size"], size))
            node = {"name": header.get("internal-name", "NoName"),
                    "extras": {"id": build_id}}
            node["translation"] = [float(self._member_value(common["members"][i], entry))
                                   for i in (2, 3, 4)]
            if struct_name != "LevelObjectEntryCommon":
                for member in layout["members"]:
                    if member["type"] not in ("u8", "s8", "u16", "s16", "u32", "s32"):
                        continue
                    value = self._member_value(member, entry)
                    if isinstance(value, list):
                        decoded = []
                        hint = (member.get("hint") or {}).get("type")
                        for element in value:
                            keep, result = self._hinted(member, element)
                            if keep:
                                decoded.append(result)
                            elif hint != "Time":
                                decoded.append(None)
                        node["extras"][member["name"]] = decoded
                    else:
                        keep, result = self._hinted(member, value)
                        if keep:
                            node["extras"][member["name"]] = result
            nodes[0]["children"].append(len(nodes))
            nodes.append(node)
            offset += size

        if not nodes[0]["children"]:
            del nodes[0]["children"]
        gltf = {"asset": {"version": "2.0"}, "nodes": nodes, "scenes": [{"nodes": [0]}]}
        self._write_json(item, gltf, extension=".gltf", indent=2)
        self._write_json(item, {"type": "LevelObjectMap",
                                "objects": _with_extension(item.filename, ".gltf")})


def _round_half_away(value):
    """``std::round``: halves go away from zero, unlike Python's ``round``."""
    return math.floor(value + 0.5) if value >= 0 else -math.floor(-value + 0.5)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

#: The longest path inside an extracted tree is 67 characters
#: (``textures/3d/objects/level_door_silver_balloon_bottom_fullclear.json``);
#: this leaves a little room for a future decomp's longer names.
LONGEST_RELATIVE_PATH = 80

#: Windows' ``MAX_PATH`` less the terminating NUL.
WINDOWS_MAX_PATH = 259


def check_path_length(final: str) -> None:
    """Refuse up front a folder whose deepest file Windows could not open.

    Failing halfway through, on one texture, helps nobody. Windows' long-path
    support does not rescue it: measured on Blender 4.5, with
    ``LongPathsEnabled`` set and ``RtlAreLongPathsEnabled`` answering true,
    Blender's Python still could not create a file past 260 characters (the
    system Python could) - and Blender has to read every file afterwards.
    """
    if os.name != "nt":
        return
    longest = len(os.path.abspath(final)) + 1 + LONGEST_RELATIVE_PATH
    if longest <= WINDOWS_MAX_PATH:
        return
    raise RomError(
        "the asset folder's path is too long for Windows (%d characters, the "
        "limit is 260): %s. Choose a shorter Extraction Folder in the addon's "
        "preferences" % (longest, final))


def extract(rom_path: str, destination: str,
            progress: Optional[Callable[[float, str], None]] = None,
            cancelled: Optional[Callable[[], bool]] = None) -> str:
    """Extract ``rom_path`` into ``destination/<version>`` and return that path.

    The tree is built in a sibling temporary folder and only moved into place
    once it is complete, so a cancelled or failed run never leaves a half tree
    that :meth:`.assets.AssetTree.find` would accept. An existing tree for the
    same version is replaced.
    """
    tables = load_tables()
    data, identity = read_rom(rom_path, tables)
    if not identity.supported:
        raise RomError("%s is not supported: DKR-R runs the USA ROM (1.0 or 1.1) "
                       "only" % identity.name)

    os.makedirs(destination, exist_ok=True)
    final = os.path.join(destination, identity.version)
    check_path_length(final)
    # Short, and hidden from AssetTree's "*" search while it is incomplete.
    staging = tempfile.mkdtemp(prefix=".x", dir=destination)
    try:
        Extractor(data, identity, staging, tables, progress, cancelled).run()
        if os.path.isdir(final):
            retired = final + ".old"
            shutil.rmtree(retired, ignore_errors=True)
            os.replace(final, retired)
            os.replace(staging, final)
            shutil.rmtree(retired, ignore_errors=True)
        else:
            os.replace(staging, final)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return final


def main(argv=None):
    """``python -m dkr_track_editor.rom_extract ROM DESTINATION``"""
    import argparse
    import sys
    import time

    parser = argparse.ArgumentParser(description="Extract the addon's asset tree from a DKR ROM")
    parser.add_argument("rom")
    parser.add_argument("destination")
    args = parser.parse_args(argv)
    started = time.time()
    last = [None]

    def report(fraction, message):
        if message != last[0]:
            print("%3d%%  %s" % (fraction * 100, message))
            last[0] = message

    try:
        path = extract(args.rom, args.destination, report)
    except RomError as error:
        print("error: %s" % error)
        return 1
    print("wrote %s in %.1f s" % (path, time.time() - started))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
