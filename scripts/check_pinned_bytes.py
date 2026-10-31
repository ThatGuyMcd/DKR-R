#!/usr/bin/env python3
"""Fail when a checksum-pinned file no longer matches its pin.

Three kinds of file are pinned by sha256 and must keep their exact bytes, even
where they carry trailing whitespace or CRLF line endings that a formatter
would "fix":

* every patch listed in patches/manifest.json (``dependencies[].patches[]``
  and ``stagedPatches[]``);
* the address-only SDK kits, modding/reference-us-v*/, against the ``files``
  map in each kit's receipt.json;
* the deployed bases a service splices into, services/*/baseline/, against
  that directory's SHA256SUMS (``sha256sum`` format).

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
import sys
from typing import NamedTuple

ROOT = Path(__file__).resolve().parents[1]
PATCH_MANIFEST = "patches/manifest.json"
RECEIPTS = "modding/reference-us-v*/receipt.json"
SERVICE_BASELINES = "services/*/baseline/SHA256SUMS"


class Pin(NamedTuple):
    """One pinned file: its expected sha256 and the file that pins it."""

    sha256: str
    source: str


def pins(root: Path) -> dict[str, Pin]:
    """Map each pinned path to its pin."""
    pinned: dict[str, Pin] = {}
    manifest = json.loads((root / PATCH_MANIFEST).read_text(encoding="utf-8"))
    for dependency in manifest["dependencies"]:
        for patch in dependency.get("patches", []):
            pinned[patch["path"]] = Pin(patch["sha256"], PATCH_MANIFEST)
    for patch in manifest.get("stagedPatches", []):
        pinned[patch["path"]] = Pin(patch["sha256"], PATCH_MANIFEST)
    for receipt in sorted(root.glob(RECEIPTS)):
        source = receipt.relative_to(root).as_posix()
        kit = receipt.parent.relative_to(root).as_posix()
        for name, sha in json.loads(receipt.read_text(encoding="utf-8"))["files"].items():
            pinned[f"{kit}/{name}"] = Pin(sha, source)
    for sums in sorted(root.glob(SERVICE_BASELINES)):
        source = sums.relative_to(root).as_posix()
        baseline = sums.parent.relative_to(root).as_posix()
        for line in sums.read_text(encoding="utf-8").splitlines():
            sha, name = line.split(maxsplit=1)
            pinned[f"{baseline}/{name}"] = Pin(sha, source)
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
    listed = None if paths is None else [Path(p).as_posix().removeprefix("./") for p in paths]
    problems: list[str] = []
    for path in sorted(pinned) if listed is None else selected(pinned, listed):
        expected, source = pinned[path]
        try:
            raw = (root / path).read_bytes()
        except FileNotFoundError:
            problems.append(f"{path}: missing, but pinned by {source}")
            continue
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
