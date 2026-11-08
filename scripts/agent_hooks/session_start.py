#!/usr/bin/env python3
"""SessionStart hook: put the pattern in context and record the session base.

Prints the "The pattern" section of AGENTS.md (both agents add SessionStart
stdout to the model's context), plus a line if the pre-commit git hook is not
installed.

Records, per session id, the HEAD the session started from and the pattern
guard problems already present in locally changed files, in
.claude/session-base (gitignored). stop_check.py reports only problems that
are not in that record, so a dirty tree at startup never blocks a stop. A
resumed or compacted session keeps its original record.

Always exits 0.
"""

# pyright: strict

from __future__ import annotations

from pathlib import Path
import re
import sys
import time

import hooklib


def pattern_summary(root: Path) -> str:
    """The "## The pattern" section of AGENTS.md, without its heading."""
    try:
        text = (root / "AGENTS.md").read_text(encoding="utf-8")
    except OSError:
        return "Read AGENTS.md before editing; it is the project guidance."
    match = re.search(r"^## The pattern[^\n]*\n(.*?)(?=^## |\Z)", text, re.MULTILINE | re.DOTALL)
    return match.group(1).strip() if match else "Read AGENTS.md before editing; it is the project guidance."


def record_base(root: Path, session: str) -> None:
    sessions = hooklib.load_sessions(root)
    if session in sessions:
        return
    head = hooklib.git(root, "rev-parse", "--verify", "HEAD")
    if head is None:
        return
    dirty = hooklib.changed_paths(root, head)
    sessions[session] = {
        "head": head,
        "tree": hooklib.git(root, "rev-parse", f"{head}^{{tree}}"),
        "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "problems": sorted(hooklib.guard_problems(root, dirty, head)),
        "registries_failed": hooklib.registries_failure(root) is not None,
    }
    hooklib.save_sessions(root, sessions)


def main() -> int:
    payload = hooklib.read_payload()
    root = hooklib.project_root(payload)
    if root is None:
        return 0
    print("DKR-R project rules (from AGENTS.md, which is authoritative):")
    print(pattern_summary(root))
    print("Before claiming done: python3 scripts/check.py --suite guard,tooling")
    status = hooklib.hooks_status(root)
    if status:
        print(status)
    session = payload.get("session_id")
    if isinstance(session, str) and session:
        try:
            record_base(root, session)
        except (hooklib.pattern_guard.GuardError, OSError) as error:
            print(f"session_start: could not record the session base ({error})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
