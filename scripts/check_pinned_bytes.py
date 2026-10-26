#!/usr/bin/env python3
"""Fail when a checksum-pinned file no longer matches its pin.

Three kinds of file are pinned by sha256 and must keep their exact bytes, even
where they carry trailing whitespace or CRLF line endings that a formatter
would "fix":

* every patch listed in patches/manifest.json (``dependencies[].patches[]``
  and ``stagedPatches[]``);
* the address-only SDK kits, modding/reference-us-v*/, against the ``files``
  map in each kit's receipt.json;
* the content-addressed source capsules, patches/release-prep/*/source-capsule-*/,
  whose file names are their own sha256;
* the capsules' "base" files, first-party sources whose CRLF-normalised bytes
  the capsule manifest pins (``base_sha256``) because capsule.py rebuilds its
  roles from them. Bases the fold has rewritten are listed in
  scripts/fold/released-bases.txt and are not pinned (see RELEASED_BASES).

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
# Fold transition, steps B1 to B8: the fold writes the shipped bytes over the
# capsule bases. Like DRIFTED_BASES, every one of them still has its pinned
# bytes at the capsule's base commit (34ed57ae), which is where
# restore_capsule.py reads a drifted base from; nothing builds from the capsule
# once the sources are folded. The list is checked both ways: every entry must
# be a capsule base whose bytes no longer match its pin, so it cannot excuse
# any other file. B8 deletes the capsule, the list and this block.
RELEASED_BASES = "scripts/fold/released-bases.txt"
CAPSULE = re.compile(r"^patches/release-prep/[^/]+/source-capsule-[^/]+/(?P<sha>[0-9a-f]{64})\.[A-Za-z0-9]+$")


class Pin(NamedTuple):
    """One pinned file: its expected sha256 and the file that pins it."""

    sha256: str
    source: str
    # capsule.py hashes its bases after CRLF -> LF (capsule.py canonical()).
    crlf_normalised: bool = False


def released(root: Path) -> set[str]:
    path = root / RELEASED_BASES
    if not path.exists():
        return set()
    return {line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()}


def base_pins(root: Path) -> dict[str, Pin]:
    """Every capsule base with its pin, released or not."""
    bases: dict[str, Pin] = {}
    for manifest_path in sorted(root.glob(CAPSULE_MANIFESTS)):
        source = manifest_path.relative_to(root).as_posix()
        capsule = json.loads(manifest_path.read_text(encoding="utf-8"))
        for entries in capsule["roles"].values():
            for entry in entries.values():
                base = entry.get("base")
                if base and base not in DRIFTED_BASES:
                    bases[base] = Pin(entry["base_sha256"], source, crlf_normalised=True)
    return bases


def canonical_sha(path: Path) -> str | None:
    """sha256 of a capsule base as capsule.py canonical() reads it."""
    try:
        return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
    except FileNotFoundError:
        return None


def released_problems(root: Path) -> list[str]:
    """Entries of RELEASED_BASES that are not a capsule base, or whose bytes match their pin again."""
    bases = base_pins(root)
    problems: list[str] = []
    for path in sorted(released(root)):
        if path not in bases:
            problems.append(f"{path}: listed in {RELEASED_BASES} but not a capsule base")
        elif canonical_sha(root / path) == bases[path].sha256:
            problems.append(f"{path}: listed in {RELEASED_BASES} but matches its pin again; remove the entry")
    return problems


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
    free = released(root)
    pinned.update({base: pin for base, pin in base_pins(root).items() if base not in free})
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
    problems = released_problems(root) if listed is None or RELEASED_BASES in listed else []
    for path in sorted(pinned) if listed is None else selected(pinned, listed):
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
