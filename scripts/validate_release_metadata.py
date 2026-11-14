#!/usr/bin/env python3
"""Validate DKR-R release versions, dependency patches, and final documentation."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise RuntimeError(message)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="the release tag being built; it must be Version<VERSION>")
    args = parser.parse_args(argv)
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if args.tag is not None and args.tag != f"Version{version}":
        fail(f"Tag {args.tag!r} doesn't match VERSION {version!r} (expected Version{version})")
    if not re.fullmatch(
        r"\d+\.\d+\.\d+(?:-beta\.[1-9]\d*(?:-playtest\.[1-9]\d*(?:-fix\.[1-9]\d*)?)?|\+beta\.[1-9]\d*)?",
        version,
    ):
        fail(f"VERSION is not a supported release/beta semantic version: {version!r}")

    appdata = (ROOT / "packaging/linux/dkr-port.appdata.xml").read_text(encoding="utf-8")
    if f'<release version="{version}"' not in appdata:
        fail("Linux AppStream metadata does not match VERSION")

    lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
    manifest = json.loads((ROOT / "patches/manifest.json").read_text(encoding="utf-8"))
    locked = {item["name"].lower(): item["commit"].lower() for item in lock["dependencies"]}
    aliases = {"n64modernruntime": "n64-modern-runtime"}
    checked = 0
    for dependency in manifest["dependencies"]:
        name = dependency["name"].lower()
        key = aliases.get(name, name)
        if key not in locked:
            fail(f"Patched dependency is missing from lockfile: {dependency['name']}")
        if locked[key] != dependency["expectedCommit"].lower():
            fail(f"Pinned commit mismatch for {dependency['name']}")
        for entry in dependency["patches"]:
            path = ROOT / entry["path"]
            if not path.is_file():
                fail(f"Missing patch: {entry['path']}")
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != entry["sha256"].lower():
                fail(f"Patch checksum mismatch: {entry['path']}")
            checked += 1

    forbidden = re.compile(
        r"\bmilestone\b|\brelease candidate\b|\brc\d+\b",
        re.IGNORECASE,
    )
    # Historical engineering notes may describe earlier release stages. Only
    # current release-facing documents must avoid stale release wording.
    for relative in (
        "README.md",
        "THIRD_PARTY.md",
        "docs/README.md",
        "docs/BUILDING.md",
        "docs/PLATFORMS.md",
        "packaging/RELEASE-README.md",
        "packaging/MACOS-BUILD-README.md",
    ):
        path = ROOT / relative
        match = forbidden.search(path.read_text(encoding="utf-8"))
        if match:
            fail(f"Historical release wording in {path.relative_to(ROOT)}: {match.group(0)}")

    print(f"Release metadata valid for DKR-R {version}; {checked} patch files verified")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
