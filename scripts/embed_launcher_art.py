#!/usr/bin/env python3
"""Embed the original, checked-in launcher atlas. Run manually after artwork edits.

This is an asset export tool, not a configure/build source transform.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "runtime-recomp/src/game/launcher_themes"


def encoded(source: Path) -> str:
    data = source.read_bytes()
    if (
        data[:8] != b"\x89PNG\r\n\x1a\n"
        or data[12:16] != b"IHDR"
        or struct.unpack(">II", data[16:24]) != (1024, 640)
        or len(data) > 512 * 1024
    ):
        raise ValueError("Expected the original 1024x640 seasonal atlas, under 512 KiB")
    lines = [
        "#pragma once",
        "// Original launcher artwork. Exported by scripts/embed_launcher_art.py.",
        "// Source: launcher_themes/art/atlas.png. No game assets are embedded.",
        "namespace dkr::launcher_themes {",
        "inline constexpr unsigned char ArtPng[] = {",
    ]
    lines += ["    " + ",".join(f"0x{byte:02x}" for byte in data[at : at + 20]) + "," for at in range(0, len(data), 20)]
    return "\n".join([*lines, "};", "} // namespace dkr::launcher_themes", ""])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify without writing")
    args = parser.parse_args()
    target = ART / "art_data.hpp"
    text = encoded(ART / "art/atlas.png")
    if args.check:
        if not target.is_file() or target.read_bytes() != text.encode():
            parser.error("Embedded atlas differs; run scripts/embed_launcher_art.py")
    else:
        target.write_bytes(text.encode())
    print("Original seasonal atlas and embedded bytes match.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
