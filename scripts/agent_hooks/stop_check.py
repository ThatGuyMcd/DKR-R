#!/usr/bin/env python3
"""Stop hook: don't let the agent finish with a new pattern-guard problem.

Compares the work tree with the HEAD recorded by session_start.py and runs the
pattern guard on every path changed since then (commits, local edits and
untracked files). It reports only problems the session introduced: a finding
the baseline doesn't list, a stale baseline entry for a changed file, or a
baseline entry added since the session base. Problems already present when the
session started are subtracted, and untouched baselined files are never
scanned. scripts/check_registries.py runs too once it exists, and blocks only
if it passed when the session started and fails now.

When ``stop_hook_active`` is set the agent is already continuing because of
this hook, so it exits 0: an unfixable finding never loops the session. The
git pre-commit hook and CI remain the gates.

Without a recorded session (hooks enabled mid-session, or a Codex session that
started before the hooks were trusted), the base is HEAD.

Exit status: 0 allow, 2 keep working (the problems are on stderr).
"""

# pyright: strict

from __future__ import annotations

import sys
from typing import cast

import hooklib


def main() -> int:
    payload = hooklib.read_payload()
    if payload.get("stop_hook_active"):
        return 0
    root = hooklib.project_root(payload)
    if root is None:
        return 0
    session = hooklib.load_sessions(root).get(str(payload.get("session_id")), {})
    base = session.get("head") or hooklib.git(root, "rev-parse", "--verify", "HEAD")
    if not isinstance(base, str) or hooklib.git(root, "cat-file", "-t", base) != "commit":
        return 0
    changed = hooklib.changed_paths(root, base)
    if not changed:
        return 0
    recorded = session.get("problems")
    known = {str(key) for key in cast(list[object], recorded)} if isinstance(recorded, list) else set[str]()
    problems = {key: text for key, text in hooklib.guard_problems(root, changed, base).items() if key not in known}
    messages = [problems[key] for key in sorted(problems)]
    registries = hooklib.registries_failure(root)
    if registries is not None and not session.get("registries_failed"):
        messages.append(registries)
    if not messages:
        return 0
    print("This session added pattern-guard problems; fix them before finishing (see AGENTS.md):", file=sys.stderr)
    for message in messages:
        print(message, file=sys.stderr)
    print("Check with: python3 scripts/check.py --suite guard", file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (hooklib.pattern_guard.GuardError, OSError, UnicodeError) as error:
        print(f"stop_check: skipped ({error})", file=sys.stderr)
        raise SystemExit(0) from None
