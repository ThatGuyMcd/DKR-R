#!/usr/bin/env python3
"""Produce checked, project-owned menu hook metadata from the reviewed ELF.

This is an explicit adapter-authoring step, not automatic permission to patch
an unknown binary. Only the reviewed ELF images are admitted; no generated C is edited.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from compose_legacy_mod_policy import elf_functions, elf_sections, MENU_FIELDS

from legacy_character_presentation_policy import ELFS, reviewed_revision
# ABI order is mirrored by legacy_track_menu_adapter.hpp. Every field comes
# from an actual symbol, including its expected allocation size.
FIELDS = MENU_FIELDS


def prepare(elf: Path, revision: str):
    if reviewed_revision(elf) != revision:
        raise ValueError("The ELF differs from the reviewed menu adapter input")
    symbols = elf_functions(elf, (1, 2))
    functions = elf_functions(elf)
    words = {start + i: struct.unpack_from(">I", data, i)[0]
             for start, data in elf_sections(elf) for i in range(0, len(data), 4)}

    def one(name):
        entries = symbols.get(name, set())
        if len(entries) != 1:
            raise ValueError(f"Missing/ambiguous symbol {name}")
        return next(iter(entries))

    fields = []
    for name, size in FIELDS:
        address, actual = one(name)
        if actual != size:
            raise ValueError(f"Unexpected data allocation {name}")
        fields.append({"name": name, "address": hex(address), "size": size})
    hooks = []

    def hook(name, address, event, returns=False):
        begin, size = one(name)
        at = min(address, begin + size - 12)
        if at < begin or at % 4 or not begin <= address < begin + size:
            raise ValueError(f"Hook outside function {name}")
        hooks.append({"function": name, "vram": hex(address), "event": event,
                      "returns": returns, "expectedAt": hex(at),
                      "expected": [hex(words[at + i * 4]) for i in range(3)]})
        if event == 7:
            # Both revisions set success in the JR delay slot. A before-JR
            # callback must not inspect the caller's previous v0 value.
            delay = words[address+4]
            if delay == 0x24020001:
                hooks[-1]["observedReturn"] = 1
            elif delay == 0 and words[address-4] == 0x00001025:
                hooks[-1]["observedReturn"] = 0
            else:
                raise ValueError("Unreviewed background-loader return sequence")

    def entry(name, event, returns=False):
        hook(name, one(name)[0], event, returns)

    def exits(name, event):
        begin, size = one(name)
        sites = [pc for pc in range(begin, begin + size, 4) if words[pc] == 0x03e00008]
        if not sites:
            raise ValueError(f"Missing bounded return in {name}")
        for pc in sites:
            hook(name, pc, event)

    entry("menu_track_select_init", 0); exits("menu_track_select_init", 1)
    entry("trackmenu_input", 2); exits("trackmenu_input", 3)
    begin, size = one("trackmenu_render_names")
    target = one("camDisableUserView")[0]
    sites = [pc for pc in range(begin, begin + size, 4)
             if words[pc] == 0x0c000000 | ((target >> 2) & 0x03ffffff)]
    if len(sites) != 1:
        raise ValueError("The render-details/draw boundary is ambiguous")
    hook("trackmenu_render_names", sites[0], 4)
    entry("trackmenu_track_view", 5)
    entry("bgload_start", 6); exits("bgload_start", 7)
    entry("load_level_for_menu", 8)
    entry("level_name", 9, True)
    entry("leveltable_vehicle_default", 10, True)
    entry("leveltable_vehicle_usable", 11, True)
    entry("trackmenu_assets", 12)
    entry("func_80092188", 13)
    entry("load_level_game", 14)
    entry("menu_init", 15)
    entry("func_8008F618", 16); exits("func_8008F618", 17)
    bodies = {}
    for name in {h["function"] for h in hooks}:
        begin, size = one(name)
        body = b"".join(struct.pack(">I", words[pc]) for pc in range(begin, begin + size, 4))
        bodies[name] = {"vram": hex(begin), "size": size, "sha256": hashlib.sha256(body).hexdigest()}
    return {"schema": 1, "revision": revision, "abi": 4, "fields": fields, "functions": bodies, "hooks": hooks}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--revision", choices=ELFS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--replace", action="store_true", help="Explicitly regenerate this project-owned metadata")
    args = parser.parse_args()
    if args.output.exists() and not args.replace:
        raise ValueError("Refusing to overwrite an existing adapter fragment")
    result = prepare(args.elf, args.revision)
    args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(f"Verified {len(result['fields'])} data symbols and {len(result['hooks'])} bounded menu hook sites.")
