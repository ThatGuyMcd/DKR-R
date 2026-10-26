#!/usr/bin/env python3
"""Patch Pipeline: isolated, bounded model-only presentation kernels.

Protected generated inputs and dependencies are read-only. There is no world
renderer, behaviour, allocator, code-mod callback, audio or network entry here.
The five reviewed leaf routines run against a decoder image with copy-on-write
scalar globals and a separate vertex/command arena; they cannot write guest RAM.
This is the local model kernel (DKR_VISUAL_MODEL_GENERATED_V77/V80), a
supported prepare step: scripts/prepare_payloads.py runs it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
from typing import cast

from kernel_inputs import CALL, DEF, HEADER_HASH, elf_symbols, strip

PINS = {
    77: {
        "obj_shade_fast": "ae2f5e243b3928109f1c2c76117b6d7e9bb49e257bb71f56a96631a76b747b34",
        "material_set": "e892d9ba93dc3ed57b96385c61b75892b8c87e3147ae4ba4025637c697130850",
        "obj_animate": "129f0cac28948697172e4765307a6f306a5734b635060b98f46c466a1ab287a3",
        "set_animated_texture_header": "03c4a910a5ac8706c063f68f3c0a2e0034ac61d372827fda32f9b3cf2aeac1bc",
        "render_mesh": "cb84a1ea9a41e0ea2f61147aa0d46a9b08564aa2fbf76e1d7d697163efbfa38c",
    },
    80: {
        "obj_shade_fast": "af718882437565cf1fbe859572da676b1495b7a7ac4f2faafb4daa3a3bc03349",
        "material_set": "b3e4b17b7e207a4d1164d3b25a73f1532d12354f18ec387c90c3cbdc29f16c20",
        "obj_animate": "69afe6c4255996372f4b55caf1212ab37f92ffe39a0cd365be856a34a79b9028",
        "set_animated_texture_header": "50fa347945c1031846d214c88368f806316cf8880d1bf8e134ee5cc6f03417e3",
        "render_mesh": "37038c5b33b9932e8624417ea52aaed7c6b1221321a99146cd1cce49988c0c10",
    },
}
GLOBALS = (
    "gObjectCurrDisplayList",
    "D_8011D644",
    "gObjectTexAnim",
    "gForceFlags",
    "gCurrentTextureHeader",
    "gCurrentRenderFlags",
    "gUsingTexture",
    "gUseDirectionalLighting",
    "gBlockedRenderFlags",
)


def generate(source: Path, header_path: Path, elf: Path, revision: int, output: Path) -> None:
    source, output = source.resolve(), output.resolve()
    if (
        output.exists()
        or output == source
        or source in output.parents
        or header_path.resolve().parent in output.parents
    ):
        raise ValueError("Visual kernel output must be a NEW separate directory")
    header = header_path.read_bytes()
    if hashlib.sha256(header).hexdigest() != HEADER_HASH:
        raise ValueError("Unreviewed visual kernel register/memory ABI")
    functions: dict[str, str] = {}
    for path in sorted(source.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        matches = list(DEF.finditer(text))
        for i, match in enumerate(matches):
            if match[1] in PINS[revision]:
                if match[1] in functions:
                    raise ValueError("Duplicate visual kernel function")
                body = text[match.start() : matches[i + 1].start() if i + 1 < len(matches) else len(text)]
                if hashlib.sha256(body.encode()).hexdigest() != PINS[revision][match[1]]:
                    raise ValueError("Unreviewed visual kernel: " + match[1])
                functions[match[1]] = body
    if set(functions) != set(PINS[revision]):
        raise ValueError("Incomplete model-only closure")
    symbols = elf_symbols(elf, (0, 1, 2))
    addresses: dict[str, int] = {}
    for name in GLOBALS:
        entries = symbols.get(name, set())
        if len(entries) != 1 or next(iter(entries))[1] not in (2, 4):
            raise ValueError("Unreviewed model scalar global: " + name)
        addresses[name] = next(iter(entries))[0]
    prefix = f"dkr_visual_v{revision}_"
    names = set(functions) | {"dkr_fix_car_steering_wheel_material"}
    h = header.decode()
    h = h.replace("// Compiler definition", '#include "visual_model_kernel_guest.h"\n// Compiler definition', 1)
    for name, typ, width in (
        ("W", "int32_t", 4),
        ("H", "int16_t", 2),
        ("B", "int8_t", 1),
        ("HU", "uint16_t", 2),
        ("BU", "uint8_t", 1),
    ):
        pattern = r"#define MEM_" + name + r"\(offset, reg\) \\\n[^\n]*"
        h, count = re.subn(
            pattern,
            f"#define MEM_{name}(offset, reg) (*({typ}*)dkr_visual_read((uint64_t)(reg)+(uint64_t)(offset),{width}))",
            h,
        )
        if count != 1:
            raise ValueError("Visual memory macro shape changed")
    # The selected leaf routines do not use doubleword/unaligned helpers.
    # Keep their definitions unreachable rather than granting unchecked access.
    allowed = set(re.findall(r"^#define\s+(\w+)\s*\(", header.decode(), re.M))
    allowed.update(re.findall(r"static\s+inline\s+\w+\s+(\w+)\s*\(", header.decode()))
    allowed |= {"if", "while", "switch", "for", "return", "sizeof", "void"} | names
    emitted: list[str] = []
    for name, body in sorted(functions.items()):
        extra = set(CALL.findall(strip(body))) - allowed
        if extra or re.search(r"\brdram\s*(?:\[|\+)", strip(body)):
            raise ValueError("Visual kernel escaped its leaf closure: " + str(extra))
        # Every store is independently checked. Reads of retail scalar globals
        # see the kernel's shadow words, never mutations of the immutable image.
        stores = len(re.findall(r"MEM_[WHB]\([^\n]+?\)\s*=", body))
        body, changed = re.subn(
            r"MEM_([WHB])\(([^,\n]+), ([^)\n]+)\) = ([^;\n]+);",
            lambda m: (
                f"dkr_visual_write((uint64_t)({m[3]})+(uint64_t)({m[2]}), {dict(W=4, H=2, B=1)[m[1]]}, (uint32_t)({m[4]}));"
            ),
            body,
        )
        if changed != stores:
            raise ValueError("Uninstrumented visual store")
        body = body.replace("{", "{\n    dkr_visual_step();", 1)
        body = re.sub(r"^(L_[A-Fa-f0-9]+:)\s*$", r"\1 dkr_visual_step();", body, flags=re.M)
        for symbol in sorted(names, key=len, reverse=True):
            body = re.sub(r"\b" + symbol + r"\b", prefix + symbol, body)
        emitted.append(body)
    output.mkdir(parents=True)
    (output / "recomp.h").write_text(h, encoding="utf-8")
    (output / "funcs.h").write_text(
        '#pragma once\n#include "recomp.h"\n'
        + "".join(f"void {prefix}{n}(uint8_t*,recomp_context*);\n" for n in sorted(names)),
        encoding="utf-8",
    )
    (output / "visual_model_functions.c").write_text('#include "funcs.h"\n' + "".join(emitted), encoding="utf-8")
    constants = "\n".join(f"#define DKR_VISUAL_{n} 0x{v:08X}U" for n, v in addresses.items())
    (output / "visual_model_addresses.h").write_text(
        "#pragma once\n"
        + constants
        + "\n"
        + "\n".join(f"#define {n} {prefix}{n}" for n in sorted(names))
        + "\n"
        + f"#define DKR_VISUAL_ENTRY {prefix}model\n",
        encoding="utf-8",
    )
    # The project-owned bridge is compiled normally, not copied or rewritten.
    (output / "manifest.json").write_text(
        json.dumps(
            {
                "revision": revision,
                "schema": 1,
                "functions": PINS[revision],
                "scalar_globals": addresses,
                "guest_writes": False,
                "world_redraw": False,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--revision", type=int, choices=(77, 80), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(
        cast(Path, args.generated),
        cast(Path, args.header),
        cast(Path, args.elf),
        cast(int, args.revision),
        cast(Path, args.output),
    )
