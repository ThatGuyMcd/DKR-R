#!/usr/bin/env python3
"""Read-only, bounded character dependency audit of privately decoded deltas.

An explicit alternate LUT is analysis input, not importer authorization. This
tool never patches a ROM or enables executable changes. Output is metadata.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def u32(data, at):
    return struct.unpack_from(">I", data, at)[0]


def u16(data, at):
    return struct.unpack_from(">H", data, at)[0]


def sections(data, at):
    if len(data) > 64 * 1024 * 1024 or u32(data, at) != 50:
        raise ValueError("Not the reviewed 50-section directory")
    offsets = [u32(data, at + 4 + i * 4) for i in range(51)]
    if offsets[0] != 0 or any(a > b for a, b in zip(offsets, offsets[1:])):
        raise ValueError("Directory offsets are not monotonic from zero")
    start = at + 208
    if offsets[-1] > len(data) - start:
        raise ValueError("Directory exceeds decoded image")
    return [data[start + a : start + b] for a, b in zip(offsets, offsets[1:])]


def records(sec, section):
    table = section + 1 if section in (0, 2, 4, 6, 8, 10, 12, 15) else section - 1
    data = sec[table]
    offsets = [0] if section == 39 else []
    step = 8 if section == 49 else 4
    start = 4 if section in (8, 49) else 0
    for at in range(start, len(data) - 3, step):
        value = u32(data, at)
        if value == 0xFFFFFFFF:
            break
        if section == 6:
            value &= 0x7FFFFFFF
        if section == 15:
            value *= 4
        if value > len(sec[section]) or (offsets and value < offsets[-1]):
            raise ValueError(f"Bad section {section} record offset")
        offsets.append(value)
    else:
        raise ValueError(f"No section {section} table terminator")
    return [sec[section][a:b] for a, b in zip(offsets, offsets[1:])]


def unpack(data):
    if len(data) < 6:
        raise ValueError("Compressed record is truncated")
    size = int.from_bytes(data[:4], "little")
    if not 0 < size <= 4 * 1024 * 1024 or data[4] > 9:
        raise ValueError("Invalid compressed record size/level")
    decoder = zlib.decompressobj(-15)
    result = decoder.decompress(data[5:], size + 1)
    if len(result) != size or not decoder.eof:
        raise ValueError("Compressed record did not match declared size")
    return result


def elf_object_ids(path):
    from compose_legacy_mod_policy import elf_functions

    data = path.read_bytes()
    entries = elf_functions(path, (1,))["gRacerObjectTable"]
    if len(entries) != 1:
        raise ValueError("Ambiguous racer object table")
    address, size = next(iter(entries))
    if size != 118:
        raise ValueError("Unexpected racer object table allocation")
    shoff = u32(data, 32)
    count = u16(data, 48)
    for i in range(count):
        _, kind, _, vram, offset, length, *_ = struct.unpack_from(">10I", data, shoff + i * 40)
        if kind == 1 and vram <= address and address + size <= vram + length:
            return list(struct.unpack_from(">59H", data, offset + address - vram))
    raise ValueError("Racer table has no ELF backing")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--base", type=Path, required=True)
    p.add_argument("--target", type=Path, required=True)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--target-lut", type=lambda s: int(s, 0), default=0xECB60)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    base = args.base.read_bytes()
    target = args.target.read_bytes()
    old = sections(base, 0xECB60)
    new = sections(target, args.target_lut)
    ids = elf_object_ids(args.elf)
    old_records = {s: records(old, s) for s in (2, 4, 29, 32, 34)}
    new_records = {s: records(new, s) for s in (2, 4, 29, 32, 34)}
    names = ("Krunch", "Bumper", "Tiptup", "Conker", "Timber", "Banjo", "Drumstick", "Pipsy", "T.T.", "Diddy")
    audit = {
        "target_sha256": hashlib.sha256(target).hexdigest(),
        "target_lut": hex(args.target_lut),
        "asset_sha256": hashlib.sha256(target[args.target_lut :]).hexdigest(),
        "record_counts": {s: [len(old_records[s]), len(new_records[s])] for s in old_records},
        "characters": [],
    }
    for character, name in enumerate(names):
        item = {"base": name, "id": character, "vehicles": []}
        for vehicle in range(3):
            oid = ids[vehicle * 10 + character]
            hid = u16(new[35], 2 * oid)
            header = new_records[34][hid]
            n = header[0x55]
            at = u32(header, 0x10)
            models = [u32(header, at + 4 * i) for i in range(n)]
            model_data = []
            for mid in models:
                if mid == 0xFFFFFFFF:
                    continue
                b = new_records[29][mid]
                d = unpack(b)
                changed = mid >= len(old_records[29]) or d != unpack(old_records[29][mid])
                first = u16(new[30], mid * 2)
                end = u16(new[30], mid * 2 + 2)
                textures = [u32(d, u32(d, 0) + 8 * i) for i in range(u16(d, 0x22))]
                model_data.append(
                    {
                        "id": mid,
                        "changed": changed,
                        "bytes": len(b),
                        "decoded": len(d),
                        "vertices": u16(d, 0x24),
                        "animated_vertices": u16(d, 0x4A),
                        "animations": [first, end],
                        "animation_sizes": [
                            {
                                "id": a,
                                "frames": u32(unpack(new_records[32][a]), 0),
                                "bytes": len(unpack(new_records[32][a])),
                            }
                            for a in range(first, end)
                        ],
                        "textures": textures,
                        "changed_textures": [
                            t for t in textures if t >= len(old_records[2]) or new_records[2][t] != old_records[2][t]
                        ],
                    }
                )
            item["vehicles"].append(
                {
                    "vehicle": vehicle,
                    "object": oid,
                    "header": hid,
                    "name": header[0x60:0x70].split(b"\0")[0].decode("ascii", "replace"),
                    "header_changed": hid >= len(old_records[34]) or header != old_records[34][hid],
                    "models": model_data,
                }
            )
        audit["characters"].append(item)
    audit["attachment_headers"] = []
    for hid in (188, 189, 190, 191, 192):
        header = new_records[34][hid]
        audit["attachment_headers"].append(
            {
                "id": hid,
                "type": header[0x53],
                "models": [u32(header, u32(header, 0x10) + i * 4) for i in range(header[0x55])],
                "changed": header != old_records[34][hid],
                "differences": [i for i, (a, b) in enumerate(zip(header, old_records[34][hid])) if a != b],
            }
        )
    audit["changed_portraits"] = [
        i
        for i, b in enumerate(new_records[4])
        if 120 <= i <= 139 and (i >= len(old_records[4]) or b != old_records[4][i])
    ]
    text = json.dumps(audit, indent=2) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8") as f:
            f.write(text)
    else:
        print(text)


if __name__ == "__main__":
    main()
