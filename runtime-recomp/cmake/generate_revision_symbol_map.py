#!/usr/bin/env python3
"""Generate a compile-time symbol namespace for a second N64Recomp payload.

The generated source directories are protected build products.  This helper
leaves them untouched and emits a force-included header that renames every
CPU function *defined* by the alternate revision while it is compiled.

``funcs.h`` also declares imported libultra and compiler-support functions.
Those imports belong to the one shared runtime and must not be namespaced.
"""

from __future__ import annotations

import pathlib
import re
import sys


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: generate_revision_symbol_map.py <source-dir> <prefix> <output>"
        )

    source_dir = pathlib.Path(sys.argv[1])
    prefix = sys.argv[2]
    output_path = pathlib.Path(sys.argv[3])

    definition_pattern = re.compile(
        r"^\s*(?:RECOMP_FUNC\s+)?void\s+([A-Za-z_][A-Za-z0-9_]*)"
        r"\s*\([^;{}]*\)\s*\{",
        re.M,
    )
    names: set[str] = set()
    for source_path in sorted((*source_dir.glob("*.c"), *source_dir.glob("*.cpp"))):
        source = source_path.read_text(encoding="utf-8")
        names.update(definition_pattern.findall(source))

    if not names or "recomp_entrypoint" not in names:
        raise SystemExit(f"did not find the generated entrypoint in {source_dir}")

    # lookup.cpp and recomp_overlays.inl expose these additional external
    # names outside funcs.h and therefore need the same payload namespace.
    names.update(["get_entrypoint_address", "get_rom_name", "num_sections"])

    output_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#pragma once",
        "// Generated at configure time. Do not edit.",
    ]
    lines.extend(f"#define {name} {prefix}{name}" for name in sorted(names))
    lines.append("")
    content = "\n".join(lines)
    if not output_path.exists() or output_path.read_text(encoding="utf-8") != content:
        output_path.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
