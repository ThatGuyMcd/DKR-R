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
* the capsules' "base" files (``base_sha256``). Evolving first-party bases
  reconstructed by restore_capsule.py are checked against their pinned Git
  blobs; the remaining bases still require exact working-tree bytes. The
  evolving ones are listed in scripts/pinned-drifted-bases.txt.

With no arguments every pin is checked. With paths (what pre-commit passes),
only the pins those paths belong to are checked; a manifest or receipt in the
list checks every file it pins, and the drifted-bases list checks every capsule
base. Bytes are read from the work tree, which is
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
import subprocess
import sys
from typing import NamedTuple

ROOT = Path(__file__).resolve().parents[1]
PATCH_MANIFEST = "patches/manifest.json"
RECEIPTS = "modding/reference-us-v*/receipt.json"
CAPSULE_MANIFESTS = "patches/release-prep/*/source-capsule-*/manifest.json"
# Capsule bases that have moved on from their pinned bytes, one path per line.
# restore_capsule.py reads those from the pinned Git commit, never from the
# evolving first-party file, so the check verifies that historical blob instead
# of weakening the hash or adding another source archive/layer.
DRIFTED_BASES = "scripts/pinned-drifted-bases.txt"
CAPSULE = re.compile(r"^patches/release-prep/[^/]+/source-capsule-[^/]+/(?P<sha>[0-9a-f]{64})\.[A-Za-z0-9]+$")


class Pin(NamedTuple):
    """One pinned file: its expected sha256 and the file that pins it."""

    sha256: str
    source: str
    # capsule.py hashes its bases after CRLF -> LF (capsule.py canonical()).
    crlf_normalised: bool = False
    git_revision: str = ""


def drifted_bases(root: Path) -> frozenset[str]:
    """The capsule bases listed as drifted; none if the tree has no list."""
    try:
        lines = (root / DRIFTED_BASES).read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        return frozenset()
    return frozenset(line.strip() for line in lines if line.strip() and not line.lstrip().startswith("#"))


def pins(root: Path) -> dict[str, Pin]:
    """Map each pinned path to (expected sha256, file that pins it)."""
    pinned: dict[str, Pin] = {}
    drifted = drifted_bases(root)
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
                if base:
                    revision = ""
                    if base in drifted:
                        history = root / "patches/upstream-sync/pins.json"
                        revision = json.loads(history.read_text())["base"]
                        if not re.fullmatch(r"[0-9a-f]{40}", revision):
                            raise ValueError("Invalid frozen capsule Git revision")
                    pinned[base] = Pin(entry["base_sha256"], source, crlf_normalised=True, git_revision=revision)
    return pinned


def selected(pinned: dict[str, Pin], paths: Iterable[str]) -> list[str]:
    """The pinned paths that the given paths (files or pinning manifests) cover."""
    chosen: set[str] = set()
    for path in (Path(p).as_posix().removeprefix("./") for p in paths):
        if path in pinned:
            chosen.add(path)
        chosen.update(name for name, pin in pinned.items() if pin.source == path)
        if path == DRIFTED_BASES:
            # Only capsule bases are CRLF-normalised. A removed entry has to
            # match its work-tree pin again, so check them all.
            chosen.update(name for name, pin in pinned.items() if pin.crlf_normalised)
    return sorted(chosen)


def digest(raw: bytes, crlf_normalised: bool) -> str:
    """The sha256 a pin is compared with."""
    if crlf_normalised:
        raw = raw.replace(b"\r\n", b"\n")
    return hashlib.sha256(raw).hexdigest()


def check(root: Path, paths: Iterable[str] | None = None) -> list[str]:
    """One message per pinned file that is missing or whose sha256 changed."""
    pinned = pins(root)
    problems: list[str] = []
    if paths is not None:
        paths = [Path(p).as_posix().removeprefix("./") for p in paths]
    if paths is None or DRIFTED_BASES in paths:
        bases = {path for path, pin in pinned.items() if pin.crlf_normalised}
        for path in sorted(drifted_bases(root) - bases):
            problems.append(f"{path}: listed in {DRIFTED_BASES}, but no capsule pins it as a base")
    for path in sorted(pinned) if paths is None else selected(pinned, paths):
        expected, source, crlf_normalised, revision = pinned[path]
        try:
            if revision:
                result = subprocess.run(
                    ["git", "-C", str(root), "show", f"{revision}:{path}"], capture_output=True, check=False
                )
                if result.returncode:
                    problems.append(f"{path}: pinned historical Git blob {revision} is unavailable")
                    continue
                raw = result.stdout
                # An entry for an untouched file would only switch its work-tree check off.
                current = root / path
                if current.is_file() and digest(current.read_bytes(), crlf_normalised) == expected:
                    problems.append(f"{path}: listed in {DRIFTED_BASES}, but still matches its pin; remove the entry")
            else:
                raw = (root / path).read_bytes()
        except FileNotFoundError:
            problems.append(f"{path}: missing, but pinned by {source}")
            continue
        actual = digest(raw, crlf_normalised)
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
            "(git checkout -- <path>); if the change is intended, update the pin in the same commit "
            f"(for a capsule base, add its path to {DRIFTED_BASES}).",
            file=sys.stderr,
        )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
