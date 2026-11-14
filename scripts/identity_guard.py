#!/usr/bin/env python3
"""Fail when simulation inputs change without a deliberate online-identity call.

The owned rollback contracts in runtime-recomp/src/game/netplay/legacy_identity.hpp
are frozen literals (docs/contracts/ONLINE_IDENTITY.md). Before the fold they
were a hash over the owned engine's sources, so any change split old and new
peers on its own. Now nothing does, and a simulation change that keeps the
same contract lets incompatible peers into one lobby, where they desync.

architecture/online-identity-inputs.json lists the inputs that shape the owned
engine, the generated guest code and the online wire behaviour (as globs), the
digest of each, and the identity they were recorded against. `check` fails when
any digest differs from the record. `update` re-records them and makes the
author say which case it is:

    identity_guard.py check
    identity_guard.py update                        # legacy_identity.hpp changed too
    identity_guard.py update --compatible "REASON"  # peers stay compatible

`update` without --compatible refuses while the identity is unchanged, and
--compatible refuses when it changed. Reviewers see the lock diff, including
the reason. Inputs are hashed with LF line endings so Windows checkouts match.

Exit status: 0 clean or recorded, 1 a mismatch or refusal.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "architecture/online-identity-inputs.json"
HEADER = ROOT / "runtime-recomp/src/game/netplay/legacy_identity.hpp"
DEFINE = re.compile(r'^#define (DKR_LEGACY_\w+) "([0-9a-f]+)"$', re.M)


def identity(header: Path = HEADER) -> str:
    """sha256 over every frozen identity define in the header, by name."""
    pairs = sorted(DEFINE.findall(header.read_text(encoding="utf-8")))
    if not pairs:
        raise SystemExit(f"no DKR_LEGACY_* identity defines in {header}")
    return hashlib.sha256("".join(f"{name}={value}\n" for name, value in pairs).encode()).hexdigest()


def digest(path: Path) -> str:
    """sha256 of the file with CRLF line endings read as LF."""
    lines = path.read_bytes().split(b"\n")
    return hashlib.sha256(b"\n".join(line.removesuffix(b"\r") for line in lines)).hexdigest()


def inputs(globs: list[str], root: Path = ROOT) -> dict[str, str]:
    found: dict[str, str] = {}
    for pattern in globs:
        matches = [path for path in root.glob(pattern) if path.is_file()]
        if not matches:
            raise SystemExit(f"identity input pattern matches nothing: {pattern}")
        for path in matches:
            found[path.relative_to(root).as_posix()] = digest(path)
    return dict(sorted(found.items()))


def differences(recorded: dict[str, str], current: dict[str, str]) -> list[str]:
    shared = sorted(recorded.keys() & current.keys())
    changed = [f"changed: {name}" for name in shared if recorded[name] != current[name]]
    added = [f"added: {name}" for name in sorted(current.keys() - recorded.keys())]
    removed = [f"removed: {name}" for name in sorted(recorded.keys() - current.keys())]
    return changed + added + removed


def load(lock: Path) -> dict[str, Any]:
    return json.loads(lock.read_text(encoding="utf-8"))


def check(lock: Path = LOCK, root: Path = ROOT, header: Path = HEADER) -> list[str]:
    data = load(lock)
    problems = differences(data["inputs"], inputs(data["globs"], root))
    if data["identity"] != identity(header):
        problems.append("legacy_identity.hpp changed since the inputs were recorded")
    return problems


def update(compatible: str | None, lock: Path = LOCK, root: Path = ROOT, header: Path = HEADER) -> str | None:
    """Re-record the inputs; returns a refusal message, or None when written."""
    data = load(lock)
    current_identity = identity(header)
    identity_changed = data["identity"] != current_identity
    if compatible is not None and identity_changed:
        return "legacy_identity.hpp changed: record it with plain `update`, not --compatible"
    if compatible is None and not identity_changed:
        return (
            "legacy_identity.hpp is unchanged. If these changes can alter simulation, the wire format or "
            "anything peers compare, bump the contract in legacy_identity.hpp (and "
            'docs/contracts/ONLINE_IDENTITY.md) first. Otherwise pass --compatible "why peers stay compatible".'
        )
    data["identity"] = current_identity
    data["inputs"] = inputs(data["globs"], root)
    data["last_update"] = {"identity_changed": identity_changed, "reason": compatible or "online identity changed"}
    lock.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check")
    recorder = commands.add_parser("update")
    recorder.add_argument("--compatible", metavar="REASON")
    args = parser.parse_args(argv)
    if args.command == "check":
        problems = check()
        if problems:
            print("Online identity inputs changed without being recorded:", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
            print(
                "These shape what peers simulate. Run `python3 scripts/identity_guard.py update` after bumping "
                'legacy_identity.hpp, or `update --compatible "REASON"` if old and new peers stay compatible.',
                file=sys.stderr,
            )
            return 1
        return 0
    if args.compatible is not None and not args.compatible.strip():
        print("--compatible needs a reason", file=sys.stderr)
        return 1
    refusal = update(args.compatible)
    if refusal:
        print(refusal, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
