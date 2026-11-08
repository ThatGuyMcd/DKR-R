#!/usr/bin/env python3
"""Fail when a checksum-pinned file no longer matches its pin.

Three kinds of file are pinned by sha256 and must keep their exact bytes, even
where they carry trailing whitespace or CRLF line endings that a formatter
would "fix":

* every patch listed in patches/manifest.json (``dependencies[].patches[]``);
* the address-only SDK kits, modding/reference-us-v*/, against the ``files``
  map in each kit's receipt.json;
* the content-addressed source capsules, patches/release-prep/*/source-capsule-*/,
  whose file names are their own sha256;
* the capsules' "base" files, first-party sources whose CRLF-normalised bytes
  the capsule manifest pins (``base_sha256``) because capsule.py rebuilds its
  roles from them.

With no arguments every pin is checked. With paths (what pre-commit passes),
only the pins those paths belong to are checked; a manifest or receipt in the
list checks every file it pins. Bytes are read from the work tree, which is
what pre-commit stashes to before running hooks. Standard library only.

Exit status: 0 all checked pins match, 1 a pinned file is missing or changed.
"""

from __future__ import annotations

import argparse
from collections.abc import Iterable
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import NamedTuple

ROOT = Path(__file__).resolve().parents[1]
PATCH_MANIFEST = "patches/manifest.json"
RECEIPTS = "modding/reference-us-v*/receipt.json"
CAPSULE_MANIFESTS = "patches/release-prep/*/source-capsule-*/manifest.json"
# Bases that main had already changed when the capsule was frozen.
# patches/upstream-sync/restore_capsule.py reads these from the pinned commit
# instead, so they are free to change; every other base must keep its bytes.
DRIFTED_BASES = frozenset(
    {
        "runtime-recomp/src/game/custom_tracks.cpp",
        "runtime-recomp/src/game/mobile_graphics_preset.hpp",
        "runtime-recomp/src/game/mods/magic_code_dispatch.h",
        "runtime-recomp/src/game/runtime_code_mods.cpp",
        "runtime-recomp/src/game/runtime_graphics_ui.inl",
    }
)
CAPSULE = re.compile(r"^patches/release-prep/[^/]+/source-capsule-[^/]+/(?P<sha>[0-9a-f]{64})\.[A-Za-z0-9]+$")


class Pin(NamedTuple):
    """One pinned file: its expected sha256 and the file that pins it."""

    sha256: str
    source: str
    # capsule.py hashes its bases after CRLF -> LF (capsule.py canonical()).
    crlf_normalised: bool = False


def pins(root: Path) -> dict[str, Pin]:
    """Map each pinned path to (expected sha256, file that pins it)."""
    pinned: dict[str, Pin] = {}
    manifest = json.loads((root / PATCH_MANIFEST).read_text(encoding="utf-8"))
    for dependency in manifest["dependencies"]:
        for patch in dependency.get("patches", []):
            pinned[patch["path"]] = Pin(patch["sha256"], PATCH_MANIFEST)
    for receipt in sorted(root.glob(RECEIPTS)):
        source = receipt.relative_to(root).as_posix()
        kit = receipt.parent.relative_to(root).as_posix()
        for name, sha in json.loads(receipt.read_text(encoding="utf-8"))["files"].items():
            pinned[f"{kit}/{name}"] = Pin(sha, source)
    for path in sorted(root.glob("patches/release-prep/*/source-capsule-*/*")):
        relative = path.relative_to(root).as_posix()
        match = CAPSULE.match(relative)
        if match:
            pinned[relative] = Pin(match["sha"], "its file name")
    for manifest_path in sorted(root.glob(CAPSULE_MANIFESTS)):
        source = manifest_path.relative_to(root).as_posix()
        capsule_dir = manifest_path.parent.relative_to(root).as_posix()
        capsule = json.loads(manifest_path.read_text(encoding="utf-8"))
        for entries in capsule["roles"].values():
            for entry in entries.values():
                # Objects and deltas the manifest needs must exist, not just
                # match when present: capsule.py can't rebuild a role without them.
                for stored in (entry.get("object"), entry.get("patch")):
                    if stored:
                        relative = f"{capsule_dir}/{stored}"
                        match = CAPSULE.match(relative)
                        if match:
                            pinned[relative] = Pin(match["sha"], source)
                base = entry.get("base")
                if base and base not in DRIFTED_BASES:
                    pinned[base] = Pin(entry["base_sha256"], source, crlf_normalised=True)
    return pinned


def selected(pinned: dict[str, Pin], paths: Iterable[str]) -> list[str]:
    """The pinned paths that the given paths (files or pinning manifests) cover."""
    chosen: set[str] = set()
    for path in (Path(p).as_posix().removeprefix("./") for p in paths):
        if path in pinned:
            chosen.add(path)
        chosen.update(name for name, pin in pinned.items() if pin.source == path)
    return sorted(chosen)


def check(root: Path, paths: Iterable[str] | None = None) -> list[str]:
    """One message per pinned file that is missing or whose sha256 changed."""
    pinned = pins(root)
    problems: list[str] = []
    for path in sorted(pinned) if paths is None else selected(pinned, paths):
        expected, source, crlf_normalised = pinned[path]
        try:
            raw = (root / path).read_bytes()
        except FileNotFoundError:
            problems.append(f"{path}: missing, but pinned by {source}")
            continue
        if crlf_normalised:
            raw = raw.replace(b"\r\n", b"\n")
        actual = hashlib.sha256(raw).hexdigest()
        if actual != expected:
            problems.append(f"{path}: sha256 {actual} does not match {expected} pinned by {source}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("paths", nargs="*", help="limit the check to these files (default: every pin)")
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    problems = check(args.root, args.paths or None)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(
            "Pinned bytes changed. If a formatter or editor did this, restore the file "
            "(git checkout -- <path>); if the change is intended, update the pin in the same commit.",
            file=sys.stderr,
        )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
