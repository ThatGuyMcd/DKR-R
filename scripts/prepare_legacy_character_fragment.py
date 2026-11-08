#!/usr/bin/env python3
"""Author the bounded racer-resource hook from the reviewed original ELF.

The original header selection/store/load and all native allocation/free paths
remain. Only the selected, prepared actor's header ID may change before both
the header loader and object lifetime store observe it.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from prepare_legacy_menu_fragment import ELFS, reviewed_revision


def prepare(elf, revision):
    if reviewed_revision(elf) != revision:
        raise ValueError("The ELF is not the reviewed character-adapter input")
    symbols = elf_functions(elf, (1, 2))
    words = {
        base + i: struct.unpack_from(">I", data, i)[0]
        for base, data in elf_sections(elf)
        for i in range(0, len(data), 4)
    }

    def one(name):
        entries = symbols.get(name, set())
        if len(entries) != 1:
            raise ValueError("Missing or ambiguous symbol " + name)
        return next(iter(entries))

    fields = []
    for name, size in [("gCharacterIdSlots", 8)]:
        address, actual = one(name)
        if actual != size:
            raise ValueError("Character field allocation changed")
        fields.append({"name": name, "address": hex(address), "size": size})
    begin, size = one("spawn_object")
    loader = one("load_object_header")[0]
    sites = [
        pc
        for pc in range(begin, begin + size - 8, 4)
        if words[pc] == 0xA7A4004E and words[pc + 4] == 0x0C000000 | ((loader >> 2) & 0x3FFFFFF)
    ]
    if len(sites) != 1:
        raise ValueError("Object header store/load boundary is ambiguous")
    # Both reviewed functions preserve the original entry pointer at caller-SP
    # and allocate 0x68 bytes. A hook cannot reinterpret a changed stack layout.
    if words[begin] != 0x27BDFF98 or 0xAFA40068 not in [words[pc] for pc in range(begin, begin + 32, 4)]:
        raise ValueError("Object entry stack lifetime changed")
    definitions = [
        ("spawn_object", sites[0], 0),
        ("charselect_assign_ai", one("charselect_assign_ai")[0], 1),
        ("menu_character_select_init", one("menu_character_select_init")[0], 2),
    ]
    hooks = []
    bodies = {}
    for name, pc, event in definitions:
        start, length = one(name)
        body = b"".join(struct.pack(">I", words[p]) for p in range(start, start + length, 4))
        bodies[name] = {"vram": hex(start), "size": length, "sha256": hashlib.sha256(body).hexdigest()}
        hooks.append(
            {"function": name, "vram": hex(pc), "event": event, "expected": [hex(words[pc + i * 4]) for i in range(3)]}
        )
    return {"schema": 1, "revision": revision, "abi": 1, "fields": fields, "functions": bodies, "hooks": hooks}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--revision", choices=ELFS, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    result = prepare(args.elf, args.revision)
    with args.output.open("x", encoding="utf-8") as f:
        f.write(json.dumps(result, indent=2) + "\n")
    print("Verified original header lifetime and three character hook boundaries.")
