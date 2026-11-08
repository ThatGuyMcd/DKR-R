#!/usr/bin/env python3
"""PreToolUse hook: refuse an edit that would add a pattern-guard violation.

Reconstructs the content each edited file would have after the tool call and
runs the pattern guard rules on it (scripts/pattern_guard.py check_content).
A finding that architecture/pattern-guard-baseline.txt already lists for that
file is tolerated, so editing a grandfathered pipeline still works.

Two rules are left to the Stop hook, pre-commit and CI, because a correct
change trips them halfway through: PG007 (a dependency patch whose manifest
sha256 is updated after the patch) and PG013 on a ``*.patch`` file (a new
dependency dir is registered in the manifest after its first patch). PG008
still stops a patch against first-party code.

This is fast feedback, not the gate. A file written through a shell command
skips it; the Stop hook, the git pre-commit hook and CI see the tree.

Exit status: 0 allow, 2 block (the reason is on stderr).
"""

# pyright: strict

from __future__ import annotations

import sys

import hooklib

import pattern_guard

DEFERRED_RULES = {"PG007"}


def blocking(edit: hooklib.PlannedEdit) -> list[pattern_guard.Finding]:
    """Findings that should stop this edit."""
    root = hooklib.checkout_of(edit.path)
    if root is None:
        return []
    path = hooklib.relative(root, edit.path)
    if path is None:
        return []
    text = edit.text
    if text is None:
        current = root / path
        text = current.read_text(encoding="utf-8", errors="replace") if current.is_file() else ""
    try:
        baseline = pattern_guard.parse_baseline(
            (root / pattern_guard.BASELINE).read_text(encoding="utf-8"), str(pattern_guard.BASELINE)
        )
    except FileNotFoundError:
        baseline = {}
    return [
        finding
        for finding in pattern_guard.check_content(root, path, text)
        if (finding.path, finding.rule) not in baseline
        and finding.rule not in DEFERRED_RULES
        and not (finding.rule == "PG013" and finding.path.endswith(".patch"))
    ]


def main() -> int:
    payload = hooklib.read_payload()
    findings = [finding for edit in hooklib.planned_edits(payload) for finding in blocking(edit)]
    if not findings:
        return 0
    print("pattern-guard: this edit brings back a patch-layer mechanism (see AGENTS.md).", file=sys.stderr)
    for finding in findings:
        print(finding.render(), file=sys.stderr)
    print(
        "Edit the first-party source in place instead. Rule details: scripts/pattern_guard.py --explain PGnnn",
        file=sys.stderr,
    )
    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (pattern_guard.GuardError, OSError, UnicodeError) as error:
        # A broken checkout must not wedge the agent; the Stop hook and CI still run.
        print(f"pre_edit_guard: skipped ({error})", file=sys.stderr)
        raise SystemExit(0) from None
