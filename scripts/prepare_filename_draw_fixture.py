#!/usr/bin/env python3
"""Prepare data for initials tests beside freshly emitted per-function CPU fixtures.

Reads a reviewed retail ELF and its symbol map. Never modifies emitted C.
The binary contains private game data: keep this fixture outside source/packages.
"""

import argparse
from pathlib import Path
import re
import struct

from compose_legacy_mod_policy import elf_functions
from legacy_character_presentation_policy import reviewed_revision

ROOT = Path(__file__).resolve().parents[1]
FIELDS = (
    "gNameEntryOffsetX",
    "gNameEntryTargetX",
    "gEnterInitalsY",
    "gCurFilenameCharBeingDrawn",
    "gFileNameValidChars",
    "gNameEntryString",
    "gFilenameFont",
    "gFilenameX",
    "gFilenameY",
    "sMenuCurrDisplayList",
    "gMenuText",
)


def prepare(elf, output):
    revision = reviewed_revision(elf)
    if revision not in ("us.v77", "us.v80") or not (output / "filename_render.c").is_file():
        raise ValueError("Requires a reviewed ELF and fresh per-function filename_render.c")
    symbols = elf_functions(elf, (1, 2))
    address_map = (ROOT / f"extern/dkr-decomp/ver/symbols/symbol_addrs.{revision}.txt").read_text()
    header = "#pragma once\n"
    for name in FIELDS:
        if name in symbols:
            if len(symbols[name]) != 1:
                raise ValueError("Ambiguous initials symbol: " + name)
            address, _ = next(iter(symbols[name]))
        else:
            matches = re.findall(r"^" + re.escape(name) + r" = (0x[0-9A-Fa-f]+);", address_map, re.M)
            if len(matches) != 1:
                raise ValueError("Missing initials symbol: " + name)
            address = int(matches[0], 16)
        if not 0x80000000 <= address < 0x80800000:
            raise ValueError("Initials symbol is outside RAM: " + name)
        header += f"constexpr unsigned {name} = {address:#x}U;\n"
    data = elf.read_bytes()
    memory = bytearray(8 * 1024 * 1024)
    start = struct.unpack_from(">I", data, 32)[0]
    count = struct.unpack_from(">H", data, 48)[0]
    for index in range(count):
        _, kind, flags, address, offset, size, *_ = struct.unpack_from(">10I", data, start + index * 40)
        if kind != 1 or not flags & 2:
            continue
        if offset + size > len(data):
            raise ValueError("Invalid allocated initials fixture section")
        if not 0x80000000 <= address <= address + size <= 0x80800000:
            continue  # Boot/RSP sections do not belong to the RAM fixture.
        for byte in range(size):
            memory[(address - 0x80000000 + byte) ^ 3] = data[offset + byte]
    for name, contents in (("filename_symbols.hpp", header.encode()), ("filename-data.bin", memory)):
        path = output / name
        if path.exists() and path.read_bytes() != contents:
            raise ValueError("Existing fixture differs; choose fresh output: " + str(path))
        path.write_bytes(contents)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.elf, args.output)
    print("Reviewed initials symbols and private RAM fixture prepared; no C source edits.")


if __name__ == "__main__":
    main()
