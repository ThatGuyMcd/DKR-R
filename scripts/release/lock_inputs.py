#!/usr/bin/env python3
"""Pin the ROM-derived build inputs a release compiles.

    lock_inputs.py write [--prepared DIR]   record build/prepared in the lock
    lock_inputs.py check [--prepared DIR]   compare build/prepared with it

runtime-recomp/build-inputs.lock.json records, for the current N64Recomp and
decomp pins, one digest per prepared input directory (scripts/prepare_payloads.py
output): the generated payloads, owned engines, model kernels and Camera++
kernels. A directory's digest covers its compiled files only, with line
endings normalised (Windows writes some of them CRLF); the JSON and TOML
provenance beside them names machine paths and is left out. Release builds
verify the same digests at configure (cmake/BuildInputs.cmake,
DKR_VERIFY_BUILD_INPUTS), so a release can't compile inputs nobody pinned.

Exit status: 0 written or matching, 1 a mismatch, a missing input or stale
pins.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import cast

sys.dont_write_bytecode = True
from release_common import ROOT  # noqa: E402

LOCK = ROOT / "runtime-recomp/build-inputs.lock.json"
INPUTS = tuple(f"{kind}-v{rev}" for kind in ("generated", "owned", "model", "camera-kernel") for rev in (77, 80))
PROVENANCE = (".json", ".toml")


def digest(directory: Path) -> str:
    """sha256 over "relative-path sha256\\n" of every compiled file, sorted;
    each file's bytes with CRLF read as LF. cmake/BuildInputs.cmake computes
    the same value."""
    lines: list[str] = []
    for path in sorted(directory.rglob("*"), key=lambda p: p.relative_to(directory).as_posix()):
        if not path.is_file() or path.suffix in PROVENANCE:
            continue
        data = path.read_bytes().replace(b"\r\n", b"\n")
        lines.append(f"{path.relative_to(directory).as_posix()} {hashlib.sha256(data).hexdigest()}\n")
    return hashlib.sha256("".join(lines).encode()).hexdigest()


def pins() -> dict[str, str]:
    """The pins the prepared inputs depend on: the N64Recomp and decomp commits."""

    def head(path: str) -> str:
        done = subprocess.run(["git", "-C", str(ROOT / path), "rev-parse", "HEAD"], capture_output=True, text=True)
        return done.stdout.strip() if done.returncode == 0 else "unavailable"

    return {"n64recomp": head("extern/n64-modern-runtime/N64Recomp"), "dkr-decomp": head("extern/dkr-decomp")}


def current(prepared: Path) -> dict[str, str]:
    missing = [name for name in INPUTS if not (prepared / name).is_dir()]
    if missing:
        raise SystemExit("lock_inputs: missing prepared inputs: " + ", ".join(missing))
    return {name: digest(prepared / name) for name in INPUTS}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("action", choices=("write", "check"))
    p.add_argument("--prepared", type=Path, default=ROOT / "build/prepared")
    a = p.parse_args(argv)
    found = current(cast(Path, a.prepared).resolve())
    if a.action == "write":
        record = {
            "schema": 1,
            "about": "Digests of the prepared build inputs (scripts/release/lock_inputs.py); "
            "cmake/BuildInputs.cmake verifies them when DKR_VERIFY_BUILD_INPUTS is on.",
            "pins": pins(),
            "inputs": found,
        }
        LOCK.write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8")
        print(f"lock_inputs: {len(found)} inputs recorded")
        return 0
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    problems = [
        f"{name}: {found[name]}, locked {want}" for name, want in lock["inputs"].items() if found.get(name) != want
    ]
    if lock["pins"] != pins():
        problems.append(f"pins changed since the lock was written: {lock['pins']} -> {pins()}")
    for problem in problems:
        print(f"lock_inputs: {problem}", file=sys.stderr)
    print(f"lock_inputs: {len(INPUTS)} inputs checked, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
