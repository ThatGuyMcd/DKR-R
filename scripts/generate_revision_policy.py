#!/usr/bin/env python3
"""Translate a DKR Patch Pipeline policy between symbol-compatible revisions.

The policy stores hook sites as exact VRAM addresses because N64Recomp needs
that precision.  This tool deliberately derives every destination from the
named function and its source-revision offset; it refuses missing symbols,
unaligned sites, or sites that fall beyond the next destination symbol.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

SYMBOL = re.compile(r"^([A-Za-z_.$][\w.$]*)\s*=\s*0x([0-9A-Fa-f]+);")


def read_symbols(path: Path) -> tuple[dict[str, int], list[tuple[int, str]]]:
    by_name: dict[str, int] = {}
    ordered: list[tuple[int, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SYMBOL.match(line)
        if match is None:
            continue
        address = int(match.group(2), 16)
        name = match.group(1)
        by_name[name] = address
        ordered.append((address, name))
    ordered.sort()
    return by_name, ordered


def parse_address(value: str) -> int:
    return int(value, 0)


def format_address(value: int) -> str:
    return f"0x{value:08X}"


def next_symbol_address(ordered: list[tuple[int, str]], address: int) -> int | None:
    return next((candidate for candidate, _ in ordered if candidate > address), None)


def map_site(
    name: str,
    source_site: int,
    source_symbols: dict[str, int],
    destination_symbols: dict[str, int],
    destination_ordered: list[tuple[int, str]],
) -> int:
    if name not in source_symbols:
        raise ValueError(f"source symbol is missing: {name}")
    if name not in destination_symbols:
        raise ValueError(f"destination symbol is missing: {name}")
    offset = source_site - source_symbols[name]
    if offset < 0 or offset % 4 != 0:
        raise ValueError(f"invalid site for {name}: {format_address(source_site)} (offset {offset})")
    destination_site = destination_symbols[name] + offset
    next_address = next_symbol_address(destination_ordered, destination_symbols[name])
    if next_address is not None and destination_site >= next_address:
        raise ValueError(
            f"mapped site leaves {name}: {format_address(destination_site)} >= "
            f"next symbol {format_address(next_address)}"
        )
    return destination_site


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--source-symbols", required=True, type=Path)
    parser.add_argument("--destination-symbols", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--manual-override",
        action="append",
        default=[],
        metavar="NAME=ADDRESS",
        help="Destination address for an intentionally absent static symbol",
    )
    arguments = parser.parse_args()

    source_by_name, _ = read_symbols(arguments.source_symbols)
    destination_by_name, destination_ordered = read_symbols(arguments.destination_symbols)
    for override in arguments.manual_override:
        name, separator, value = override.partition("=")
        if not separator:
            raise ValueError(f"invalid manual override: {override}")
        destination_by_name[name] = parse_address(value)
        destination_ordered.append((destination_by_name[name], name))
    destination_ordered.sort()

    policy = json.loads(arguments.policy.read_text(encoding="utf-8"))
    if policy.get("schemaVersion") != 1:
        raise ValueError("unsupported Patch Pipeline policy schema")

    for entry in policy.get("manualFunctions", []):
        name = entry["name"]
        source_site = parse_address(entry["vram"])
        if name not in source_by_name:
            # Static functions can be intentionally absent from both symbol
            # files. Their destination must be supplied explicitly.
            if name not in destination_by_name:
                raise ValueError(f"manual function needs an override: {name}")
            destination_site = destination_by_name[name]
        else:
            destination_site = map_site(
                name,
                source_site,
                source_by_name,
                destination_by_name,
                destination_ordered,
            )
        entry["vram"] = format_address(destination_site)

    for entry in policy.get("instructionPatches", []):
        entry["vram"] = format_address(
            map_site(
                entry["function"],
                parse_address(entry["vram"]),
                source_by_name,
                destination_by_name,
                destination_ordered,
            )
        )

    for entry in policy.get("functionHooks", []):
        entry["beforeVram"] = format_address(
            map_site(
                entry["function"],
                parse_address(entry["beforeVram"]),
                source_by_name,
                destination_by_name,
                destination_ordered,
            )
        )

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(policy, indent=2) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
