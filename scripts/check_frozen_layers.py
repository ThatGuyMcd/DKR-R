#!/usr/bin/env python3
"""Fail when a first-party file under patches/ changes while the fold is open.

    check_frozen_layers.py            check the work tree against the record
    check_frozen_layers.py --write    record the current files

The fold (docs/fold-in/ on its branch) rewrites every first-party layer under
patches/ into runtime-recomp/src from what those layers produce. An edit to a
layer after that comparison is lost unless the whole comparison is redone, so
the layers are frozen until the fold merges.

Frozen: every tracked file under patches/ except patches/manifest.json and the
third-party patches it registers, which stay editable with their sha256.
architecture/frozen-layers.txt holds one "<git blob id> <path>" line per
frozen file. A changed, added or deleted file fails. --write is for a change
both owners agreed to make in a layer anyway.
"""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RECORD = "architecture/frozen-layers.txt"
MANIFEST = "patches/manifest.json"
HEADER = (
    "# First-party files under patches/, frozen until the fold merges.\n"
    "# One '<git blob id> <path>' line per file. scripts/check_frozen_layers.py\n"
    "# checks it and explains the freeze.\n"
)


def git(root: Path, *args: str, stdin: str | None = None) -> str:
    return subprocess.run(["git", *args], cwd=root, input=stdin, capture_output=True, text=True, check=True).stdout


def registered(node: object) -> set[str]:
    """Every "path" under patches/ that the dependency manifest names."""
    if isinstance(node, dict):
        found = {node["path"]} if isinstance(node.get("path"), str) and node["path"].startswith("patches/") else set()
        return found.union(*(registered(value) for value in node.values()))
    if isinstance(node, list):
        return set().union(*(registered(value) for value in node))
    return set()


def frozen(root: Path) -> dict[str, str]:
    """path -> git blob id of each frozen file present in the work tree."""
    third_party = registered(json.loads((root / MANIFEST).read_text(encoding="utf-8"))) | {MANIFEST}
    tracked = [name for name in git(root, "ls-files", "-z", "patches").split("\0") if name]
    paths = [name for name in tracked if name not in third_party and (root / name).is_file()]
    # hash-object applies the checkout's clean filters, so line-ending
    # conversion on Windows does not look like an edit.
    ids = git(root, "hash-object", "--stdin-paths", stdin="".join(path + "\n" for path in paths)).split()
    return dict(zip(paths, ids, strict=True))


def recorded(root: Path) -> dict[str, str]:
    entries = {}
    for line in (root / RECORD).read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            blob, path = line.split(" ", 1)
            entries[path] = blob
    return entries


def problems(root: Path) -> list[str]:
    current, expected = frozen(root), recorded(root)
    found = [f"{path}: changed" for path in sorted(current) if path in expected and current[path] != expected[path]]
    found += [f"{path}: added" for path in sorted(current.keys() - expected.keys())]
    found += [f"{path}: deleted" for path in sorted(expected.keys() - current.keys())]
    return found


def main(argv: list[str] | None = None, root: Path = ROOT) -> int:
    args = sys.argv[1:] if argv is None else argv
    try:
        git(root, "rev-parse", "--git-dir")
    except (OSError, subprocess.CalledProcessError):
        print("frozen layers: not a Git checkout, nothing to compare")
        return 0
    if args == ["--write"]:
        current = frozen(root)
        body = "".join(f"{blob} {path}\n" for path, blob in sorted(current.items()))
        (root / RECORD).write_text(HEADER + body, encoding="utf-8", newline="\n")
        print(f"frozen layers: recorded {len(current)} files")
        return 0
    if args:
        print(__doc__, file=sys.stderr)
        return 2
    found = problems(root)
    for problem in found:
        print(problem)
    if found:
        print(
            f"frozen layers: {len(found)} problem(s). The first-party layers under patches/ are frozen until "
            "the fold merges; see the transition note in AGENTS.md.",
            file=sys.stderr,
        )
        return 1
    print(f"frozen layers: {len(recorded(root))} files unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
