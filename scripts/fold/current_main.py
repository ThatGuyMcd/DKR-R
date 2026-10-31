#!/usr/bin/env python3
"""Check the current-main fold's source checkpoint without private payloads.

The historical v1.0.11 closure is separate and remains unchanged. This
checkpoint follows the compiler-selected current-main comparison described
in docs/fold-in/REFERENCE.md. It pins the final include spellings as well as
the C/C++ tokens; whitespace and comments do not affect it. It is a migration
regression check, not a substitute for platform builds or release gates.

This checker deliberately has no command to regenerate its checkpoint.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = Path(__file__).with_name("current-main-closure.json")
sys.path.insert(0, str(ROOT / "scripts"))
from format_tokens import tokens  # noqa: E402


def digest(source: bytes) -> str:
    return hashlib.sha256("\0".join(tokens(source)).encode("utf-8", "surrogateescape")).hexdigest()


def compare(root: Path, files: dict[str, str]) -> list[str]:
    if not files:
        raise ValueError("the compiler-selected source checkpoint is empty")
    problems = []
    for name, expected in sorted(files.items()):
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or relative.parts[0] != "runtime-recomp":
            raise ValueError(f"invalid source path: {name}")
        path = root.joinpath(*relative.parts)
        if not path.is_file():
            problems.append(f"{name}: missing")
        elif digest(path.read_bytes()) != expected:
            problems.append(f"{name}: differs from the current-main fold checkpoint")
    return problems


def main() -> int:
    try:
        snapshot = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
        if snapshot["schema"] != 1 or snapshot["source_files"] != len(snapshot["files"]):
            raise ValueError("invalid checkpoint schema or source count")
        problems = compare(ROOT, snapshot["files"])
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"current-main checkpoint: {error}", file=sys.stderr)
        return 2
    for problem in problems:
        print(problem)
    print(f"current-main checkpoint: {len(snapshot['files'])} files, {len(problems)} differences")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
