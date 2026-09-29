#!/usr/bin/env python3
"""Patch Pipeline: build an isolated, closed-import guest replay probe.

Inputs (including protected generated CPU code and recomp.h) are read-only.
Output must be a NEW directory outside those inputs. This is not a runtime
adapter: every native import traps, rather than silently pretending success.
The standalone target links no launcher, audio, renderer or online runtime.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

HEADER_HASH = "58977ed0a489f7df8b17c377a87cc6670857a5fba15af78fc7d38132fc59b33f"
DEF = re.compile(r"RECOMP_FUNC\s+void\s+(\w+)\s*\([^{};]*\)\s*\{")
PROTO = re.compile(r"(?:extern\s+)?\b(void|int|float|double|gpr|u?int(?:8|16|32|64)_t|recomp_func_t\s*\*)\s*(\w+)\s*\(([^();{}]*)\)\s*;")
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
# Project-owned import absent from N64Recomp's emitted funcs.h. ABI checked
# against runtime-recomp/src/game/virtual_pak.cpp, not inferred from its name.
PROJECT_IMPORTS = {
    "osPfsInit_recomp": ("void", ["uint8_t*", "recomp_context*"]),
    # runtime-recomp/src/game/runtime_stubs.cpp
    "rmonPrintf_recomp": ("void", ["uint8_t*", "recomp_context*"]),
}


def strip(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', "", source, flags=re.S)


def declarations(source):
    result = {}
    for ret, name, params in PROTO.findall(strip(source)):
        if name in ("if", "return", "switch", "sizeof"):
            continue
        types = []
        for param in params.split(",") if params.strip() not in ("", "void") else []:
            param = re.sub(r"(\*)\s*([A-Za-z_]\w*)$", r"\1 \2", param.strip())
            # All supported imports use simple C scalars/pointers. Refuse a
            # signature we cannot preserve instead of guessing an ABI.
            if not re.fullmatch(r"(?:const\s+)?(?:unsigned\s+)?[A-Za-z_]\w*(?:\s*\*)*(?:\s+\w+)?", param):
                raise ValueError(f"Unsupported native signature {name}: {param}")
            known = {"void", "int", "float", "double", "gpr", "uint8_t", "int8_t",
                     "uint16_t", "int16_t", "uint32_t", "int32_t", "uint64_t", "int64_t",
                     "uintptr_t", "recomp_context", "char"}
            words = param.split()
            if len(words) > 1 and words[-1] not in known and "*" not in words[-1]:
                param = param[:param.rfind(words[-1])].strip()
            # Handle uint8_t* rdram and uint8_t *rdram as well.
            param = re.sub(r"(\*)\s*[A-Za-z_]\w*$", r"\1", param)
            param = re.sub(r"\s*\*\s*", "*", param)
            types.append(param)
        signature = (ret.strip(), types)
        if name in result and result[name] != signature:
            raise ValueError(f"Conflicting prototype for {name}: {result[name]} / {signature}")
        result[name] = signature
    return result


def generate(source_dir: Path, header_path: Path, output: Path, roots, revision):
    source_dir, header_path, output = source_dir.resolve(), header_path.resolve(), output.resolve()
    if output.exists() or output == source_dir or source_dir in output.parents or header_path.parent in output.parents:
        raise ValueError("Probe output must be a new, separate directory; protected inputs are never rewritten")
    header_bytes = header_path.read_bytes()
    if hashlib.sha256(header_bytes).hexdigest() != HEADER_HASH:
        raise ValueError("Unreviewed recomp.h; audit memory macros before updating the probe")
    header = header_bytes.decode("utf-8")
    functions, prototypes, origins = {}, declarations(header), {}
    def merge(items):
        for name, signature in items.items():
            if name in prototypes and prototypes[name] != signature:
                raise ValueError(f"Conflicting native declaration for {name}")
            prototypes[name] = signature
    merge(PROJECT_IMPORTS)
    merge(declarations((source_dir / "funcs.h").read_text(encoding="utf-8")))
    for file in sorted(source_dir.glob("*.c")):
        source = file.read_text(encoding="utf-8")
        matches = list(DEF.finditer(source))
        for i, match in enumerate(matches):
            body = source[match.start():matches[i + 1].start() if i + 1 < len(matches) else len(source)]
            name = match[1]
            if name in functions: raise ValueError(f"Duplicate guest definition {name}")
            functions[name] = body
            merge(declarations(body))
            origins[name] = {"file": str(file), "sha256": hashlib.sha256(body.encode()).hexdigest()}
    helpers = set(re.findall(r"^#define\s+(\w+)\s*\(", header, re.M))
    helpers.update(re.findall(r"static\s+inline\s+\w+\s+(\w+)\s*\(", header))
    helpers.update(("if", "while", "switch", "for", "return", "sizeof", "void"))
    helpers.update(("sqrtf", "sqrt", "truncf", "trunc", "roundf", "round", "fabsf", "fabs",
                    "floorf", "floor", "ceilf", "ceil", "fmodf", "fmod", "abs", "assert"))
    pending, used, blocked, unknown = list(roots), set(), set(), set()
    while pending:
        name = pending.pop()
        if name in used: continue
        if name not in functions: raise ValueError(f"Missing guest root/callee {name}")
        used.add(name)
        for callee in CALL.findall(strip(functions[name])):
            if callee in functions: pending.append(callee)
            elif callee in prototypes: blocked.add(callee)
            elif callee not in helpers: unknown.add((name, callee))
    if unknown: raise ValueError(f"Unclassified calls: {sorted(unknown)}")
    # These can be invoked by header macros/inline helpers, outside the body
    # call scan. They must not resolve to a live runtime via indirect dispatch.
    blocked.update(("get_function", "switch_error", "do_break", "cop0_status_write",
                    "cop0_status_read", "recomp_syscall_handler", "pause_self"))
    for name in blocked:
        if name not in prototypes: raise ValueError(f"No checked prototype for native import {name}")

    # Substitute ALL direct header guest memory accesses, including inline
    # doubleword/unaligned helpers. Macro shape is protected by the header hash.
    insert = '#include "probe_bridge.h"\n'
    header = header.replace("// Compiler definition", insert + "// Compiler definition", 1)
    for name, typ, width, xor in (("W", "int32_t", 4, 0), ("H", "int16_t", 2, 2),
                                 ("B", "int8_t", 1, 3), ("HU", "uint16_t", 2, 2), ("BU", "uint8_t", 1, 3)):
        pattern = r"#define MEM_" + name + r"\(offset, reg\) \\\n[^\n]*"
        replacement = f"#define MEM_{name}(offset, reg) (*({typ}*)dkr_probe_memory(rdram, (uint64_t)(reg) + (uint64_t)(offset), {width}, {xor}))"
        header, count = re.subn(pattern, replacement, header)
        if count != 1: raise ValueError(f"Memory macro mismatch MEM_{name}")
    header, count = re.subn(r"#define SD\(val, offset, reg\) \{ \\\n.*?\n\}",
        "#define SD(val, offset, reg) { MEM_W((offset) + 4, reg) = (uint32_t)(val); MEM_W(offset, reg) = (uint32_t)((gpr)(val) >> 32); }", header, flags=re.S)
    if count != 1 or re.search(r"rdram\s*\+", header): raise ValueError("Uninstrumented guest memory access remains")
    if any(re.search(r"\brdram\s*\[|\brdram\s*\+", strip(functions[n])) for n in used):
        raise ValueError("Guest body has direct pointer arithmetic outside the checked memory macros")

    output.mkdir(parents=True)
    (output / "recomp.h").write_text(header, encoding="utf-8")
    (output / "funcs.h").write_text((source_dir / "funcs.h").read_text(encoding="utf-8"), encoding="utf-8")
    ordered = sorted(used)
    for index in range(0, len(ordered), 50):
        (output / f"guest_{index // 50}.c").write_text('#include "funcs.h"\n' +
            "\n".join(functions[n] for n in ordered[index:index + 50]), encoding="utf-8")
    stubs = ['#include "recomp.h"', '#include "probe_bridge.h"']
    for name in sorted(blocked):
        ret, types = prototypes[name]
        params = ", ".join(f"{t} p{i}" for i, t in enumerate(types)) or "void"
        stubs.append(f'{ret} {name}({params}) {{ dkr_probe_block("{name}"); }}')
    (output / "blocked_imports.c").write_text("\n".join(stubs) + "\n", encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps({"revision": revision, "roots": roots,
        "guest_functions": {n: origins[n] for n in ordered}, "blocked_native_imports": sorted(blocked),
        "recomp_header_sha256": HEADER_HASH, "safety": "Private process; copied RAM; no native imports allowed. NOT a gameplay adapter."}, indent=2) + "\n", encoding="utf-8")
    print(f"Probe v{revision}: {len(used)} real guest functions; {len(blocked)} fenced native imports")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("generated", type=Path)
    p.add_argument("header", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--revision", type=int, choices=(77, 80), required=True)
    p.add_argument("--roots", nargs="+", default=["main_game_loop", "obj_update", "input_swap_id"])
    a = p.parse_args()
    generate(a.generated, a.header, a.output, a.roots, a.revision)
