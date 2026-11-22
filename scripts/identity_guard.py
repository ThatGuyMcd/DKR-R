#!/usr/bin/env python3
"""Fail when simulation inputs change without a deliberate online-identity call.

The owned rollback contracts in runtime-recomp/src/game/netplay/legacy_identity.hpp
are frozen literals (docs/contracts/ONLINE_IDENTITY.md). Before the fold they
were a hash over the owned engine's sources, so any change split old and new
peers on its own. Now nothing does, and a simulation change that keeps the
same contract lets incompatible peers into one lobby, where they desync.

architecture/online-identity-inputs.json lists the inputs that shape the owned
engine, the generated guest code and the online wire behaviour (as globs), the
digest of each together with the reason it last changed, and the identity they
were recorded against. `check` fails when any digest differs from the record.
`update` re-records the inputs that changed and makes the author say which
case it is:

    identity_guard.py check
    identity_guard.py check --against origin/main   # every changed digest has a new reason
    identity_guard.py update                        # legacy_identity.hpp changed too
    identity_guard.py update --compatible "REASON"  # peers stay compatible

`update` without --compatible refuses while the identity is unchanged, and
--compatible refuses when it changed. Reviewers see the lock diff, including
the reason on each changed input. A reason is stored beside the digest it
explains and is replaced when that input changes again, so two branches that
change different inputs merge without a textual conflict and `check` passes on
the merge without anyone re-running `update`. Inputs are hashed with LF line
endings so Windows checkouts match.

Exit status: 0 clean or recorded, 1 a mismatch or refusal.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "architecture/online-identity-inputs.json"
HEADER = ROOT / "runtime-recomp/src/game/netplay/legacy_identity.hpp"
SCHEMA = 2
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


def entries(data: dict[str, Any]) -> dict[str, dict[str, str]]:
    """The recorded inputs as {path: {"sha256": ..., "reason": ...}}."""
    if data.get("schema") != SCHEMA:
        raise SystemExit(f"online-identity-inputs.json schema is {data.get('schema')!r}, expected {SCHEMA}")
    raw: dict[str, Any] = data["inputs"]
    return {name: {"sha256": str(entry["sha256"]), "reason": str(entry["reason"])} for name, entry in raw.items()}


def recorded_digests(data: dict[str, Any]) -> dict[str, str]:
    return {name: entry["sha256"] for name, entry in entries(data).items()}


def write(lock: Path, data: dict[str, Any]) -> None:
    ordered = {
        "schema": SCHEMA,
        "globs": data["globs"],
        "identity": data["identity"],
        "inputs": dict(sorted(data["inputs"].items())),
    }
    lock.write_text(json.dumps(ordered, indent=2) + "\n", encoding="utf-8")


def check(lock: Path = LOCK, root: Path = ROOT, header: Path = HEADER) -> list[str]:
    data = load(lock)
    recorded = entries(data)
    problems = differences({name: entry["sha256"] for name, entry in recorded.items()}, inputs(data["globs"], root))
    problems += [
        f"no reason recorded: {name}" for name, entry in sorted(recorded.items()) if not entry["reason"].strip()
    ]
    if data["identity"] != identity(header):
        problems.append("legacy_identity.hpp changed since the inputs were recorded")
    return problems


def reasons_changed(old: dict[str, Any], new: dict[str, Any]) -> list[str]:
    """Inputs whose digest differs between two records but whose reason does not.

    Every digest change has to bring a new reason into the diff. A hand-edited
    digest that keeps the old reason, or a merge that dropped a reason, shows here.
    """
    before, after = entries(old), entries(new)
    return [
        f"digest changed without a new reason: {name}"
        for name in sorted(before.keys() & after.keys())
        if before[name]["sha256"] != after[name]["sha256"] and before[name]["reason"] == after[name]["reason"]
    ]


def record_at(ref: str, root: Path = ROOT) -> dict[str, Any] | None:
    """The record as of the merge base of `ref` and HEAD.

    None when that commit has no record to compare with: the file did not exist
    yet, or it predates per-input reasons. A ref git cannot resolve is an error,
    not a reason to skip: a shallow checkout would otherwise pass unchecked.
    """

    def git(*arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(["git", *arguments], cwd=root, capture_output=True, text=True)

    if git("rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}").returncode != 0:
        raise SystemExit(f"identity_guard: cannot resolve {ref}; --against needs that commit fetched")
    base = git("merge-base", ref, "HEAD")
    commit = base.stdout.strip() if base.returncode == 0 and base.stdout.strip() else ref
    shown = git("show", f"{commit}:{LOCK.relative_to(ROOT).as_posix()}")
    if shown.returncode != 0:
        return None
    old = json.loads(shown.stdout)
    return old if old.get("schema") == SCHEMA else None


def update(compatible: str | None, lock: Path = LOCK, root: Path = ROOT, header: Path = HEADER) -> str | None:
    """Re-record the inputs that changed; returns a refusal message, or None when written."""
    data = load(lock)
    recorded = entries(data)
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
    reason = compatible if compatible is not None else f"online identity changed to {current_identity[:12]}"
    fresh = inputs(data["globs"], root)
    merged: dict[str, dict[str, str]] = {}
    for name, digest_now in fresh.items():
        old = recorded.get(name)
        merged[name] = (
            old if old is not None and old["sha256"] == digest_now else {"sha256": digest_now, "reason": reason}
        )
    data["identity"] = current_identity
    data["inputs"] = merged
    write(lock, data)
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    checker = commands.add_parser("check")
    checker.add_argument(
        "--against", metavar="REF", help="also require a new reason for every digest changed since REF"
    )
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
        if args.against:
            old = record_at(args.against)
            if old is None:
                print(f"identity_guard: no comparable record at {args.against}; skipped --against", file=sys.stderr)
                return 0
            missing = reasons_changed(old, load(LOCK))
            for problem in missing:
                print(f"  {problem}", file=sys.stderr)
            if missing:
                print(
                    'Re-run `identity_guard.py update --compatible "REASON"` so each changed input says why. '
                    "A reason worded exactly like the one it replaces counts as unchanged.",
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
