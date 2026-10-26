#!/usr/bin/env python3
"""PostToolUse hook: report lint and format findings for the edited files.

Runs ``pre-commit run --files <paths>`` from .pre-commit-config.yaml and hands
the agent the failing hooks' output plus, for any file a fixer would have
changed, the diff it wanted. The live files are never written: pre-commit runs
in a scratch git repo holding copies of the edited files and the lint config,
so a fixer can't make the agent's next Edit fail with "file modified since
read", and a save made by someone else meanwhile can't be rolled back. The
agent applies the fix itself.

The two local hooks need the real tree, so they run differently:
pattern-guard is skipped (pre_edit_guard and stop_check cover it), and
check_pinned_bytes.py runs read-only against the real files.

Feedback goes out as ``hookSpecificOutput.additionalContext`` JSON on stdout,
which Claude Code and Codex both add next to the tool result. Always exits 0.

Without pre-commit, its config or the installed git hook, it prints one line
(the agent's debug log) and exits instead of spending the hook timeout
installing hook environments. session_start.py already told the agent how to
bootstrap.
"""

# pyright: strict

from __future__ import annotations

import difflib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

import hooklib

# Below the 30s hook timeout in .claude/settings.json and .codex/hooks.json.
PRE_COMMIT_TIMEOUT = 25
# Files the configured hooks read for their settings, copied into the scratch repo.
CONFIG_FILES = (
    ".pre-commit-config.yaml",
    "pyproject.toml",
    ".editorconfig",
    ".clang-format",
    ".clang-tidy",
    ".gersemirc",
)
# Local hooks that read the rest of the tree; see the module docstring.
SCRATCH_SKIP = "pattern-guard,pinned-bytes"
CONTEXT_LIMIT = 6000
HOOK_LINE = re.compile(r"^(.+?)\.{3,}(\(no files to check\))?(Passed|Failed|Skipped)$")


def wanted_change(name: str, before: bytes, after: bytes) -> str | None:
    """The unified diff a fixer made to a scratch copy, or None if it left it alone."""
    if after == before:
        return None
    old = before.decode("utf-8", errors="replace").splitlines(keepends=True)
    new = after.decode("utf-8", errors="replace").splitlines(keepends=True)
    return "".join(difflib.unified_diff(old, new, f"a/{name}", f"b/{name}"))


def run_in_scratch(root: Path, relative: list[str]) -> tuple[str, bool, list[str]]:
    """Run pre-commit over copies of the files; returns (output, failed, diffs)."""
    with tempfile.TemporaryDirectory(prefix="dkr-post-edit-") as temporary:
        scratch = Path(temporary)
        originals: dict[str, bytes] = {}
        for name in (*CONFIG_FILES, *relative):
            source = root / name
            if source.is_file():
                target = scratch / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                if name in relative:
                    originals[name] = source.read_bytes()
        environment = {**os.environ, "SKIP": SCRATCH_SKIP, "GIT_CONFIG_GLOBAL": os.devnull}
        for command in (["git", "init", "-q"], ["git", "add", "--", *originals]):
            subprocess.run(command, cwd=scratch, env=environment, capture_output=True, check=True)
        output, failed = run_process_tree(
            ["pre-commit", "run", "--color", "never", "--files", *originals], scratch, PRE_COMMIT_TIMEOUT, environment
        )
        diffs = [
            diff
            for name, before in originals.items()
            if (diff := wanted_change(name, before, (scratch / name).read_bytes())) is not None
        ]
    return output, failed, diffs


def pinned_bytes_findings(root: Path, relative: list[str]) -> str | None:
    """check_pinned_bytes.py's report for these files, read-only, or None if clean."""
    script = root / "scripts/check_pinned_bytes.py"
    if not script.is_file():
        return None
    result = subprocess.run(
        [sys.executable, str(script), *relative], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    if result.returncode == 0:
        return None
    return result.stdout.strip() or "check_pinned_bytes.py failed"


def run_process_tree(
    command: list[str], cwd: Path, timeout: float, env: dict[str, str] | None = None
) -> tuple[str, bool]:
    """Run command and return (output, failed); on timeout kill it and every child.

    pre-commit launches the formatters as child processes. Killing only
    pre-commit on timeout would leave a fixer running after this returns (and
    holding the scratch directory open), so the command gets its own process
    group (session on POSIX) and the whole group is killed and reaped first.
    """
    if sys.platform == "win32":
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
    else:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env,
            start_new_session=True,
        )
    try:
        output, _ = process.communicate(timeout=timeout)
        return output, process.returncode != 0
    except subprocess.TimeoutExpired:
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True, check=False)
        else:
            os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        return f"pre-commit did not finish in {timeout:g}s; run it yourself.", True


def failing_sections(output: str) -> str:
    """Only the output of hooks that failed (pre-commit prints every hook)."""
    kept: list[str] = []
    keep = False
    for line in output.splitlines():
        status = HOOK_LINE.match(line.strip())
        if status:
            keep = status.group(3) == "Failed"
        if keep:
            kept.append(line)
    return "\n".join(kept)


def main() -> int:
    payload = hooklib.read_payload()
    root = hooklib.project_root(payload)
    if root is None:
        return 0
    paths = [edit.path for edit in hooklib.planned_edits(payload) if edit.path.is_file()]
    relative = [rel for rel in (hooklib.relative(root, path) for path in paths) if rel is not None]
    if not relative:
        return 0
    status = hooklib.hooks_status(root)
    if not (root / ".pre-commit-config.yaml").is_file() or status:
        print(status or "post_edit_check: no .pre-commit-config.yaml, nothing to check")
        return 0
    output, failed, diffs = run_in_scratch(root, relative)
    pinned = pinned_bytes_findings(root, relative)
    if not failed and not diffs and not pinned:
        return 0
    parts = [f"pre-commit findings for {', '.join(relative)} (nothing was rewritten; fix them yourself):"]
    if diffs:
        parts.append("A fixer wanted these changes:\n" + "\n".join(diffs))
    sections = failing_sections(output) if failed else ""
    if sections or (failed and not diffs):
        parts.append(sections or output.strip())
    if pinned:
        parts.append(pinned)
    context = "\n\n".join(parts)
    if len(context) > CONTEXT_LIMIT:
        context = context[:CONTEXT_LIMIT] + "\n[truncated; run pre-commit run --files on the file for the rest]"
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": context}}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except OSError as error:
        print(f"post_edit_check: skipped ({error})")
        raise SystemExit(0) from None
