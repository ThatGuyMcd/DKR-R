#!/usr/bin/env python3
"""Changed first-party C/C++ lines for the `tidy` CI job, ignoring layout.

clang-tidy-diff only looks at changed lines, so a formatting-only commit (the
tree-wide clang-format pass) makes every line "changed". This compares HEAD
against the merge-base version of each file *after formatting that version
with the pinned clang-format and HEAD's .clang-format*. Layout-only changes
vanish and real changes keep just their real lines.

  tidy_changed.py changed --merge-base SHA      files with a non-empty diff, one per line
  tidy_changed.py diff --merge-base SHA --files LIST
                                                the -U0 unified diff of those files, for
                                                clang-tidy-diff.py -p1

The formatter must be the version pinned in .github/lint-tools.txt (the one
`format_sources.py --check --local` and the lint job use); anything else is
refused, since a different version would reintroduce layout noise.

If HEAD's file is not itself clang-formatted, the diff includes that
formatting noise. That is accepted: the lint job fails such a PR anyway.
Added files have no base and are entirely new. A renamed file is compared
against its old path; files outside the formatted trees (format_sources.NEVER)
are compared unformatted.
"""

import argparse
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
import difflib
from pathlib import Path
import re
import shlex
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import format_sources  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PATHSPEC = (
    "runtime-recomp/src",
    "runtime-recomp/tests",
    "modding",
    ":(exclude)runtime-recomp/src/game/generated",
)
WANTED = re.compile(r"\.(c|cc|cpp|h|hpp|inl)$")

Formatter = Callable[[bytes, str], bytes]


def git(*args: str, check: bool = True) -> bytes:
    return subprocess.run(["git", *args], cwd=ROOT, check=check, capture_output=True).stdout


def pinned_version() -> str:
    text = (ROOT / ".github/lint-tools.txt").read_text(encoding="utf-8")
    found = re.search(r"^clang-format==(\S+)", text, re.MULTILINE)
    if not found:
        raise SystemExit("tidy_changed: lint-tools.txt no longer pins clang-format")
    return found.group(1)


def make_formatter(command: str) -> Formatter:
    """A formatter running `command` (shell-split) with HEAD's .clang-format."""
    argv = shlex.split(command)
    version = subprocess.run([*argv, "--version"], capture_output=True, text=True, check=True).stdout
    pin = pinned_version()
    if not re.search(rf"\bversion {re.escape(pin)}\b", version):
        raise SystemExit(f"tidy_changed: need clang-format {pin} (lint-tools.txt), got: {version.strip()}")
    style = f"--style=file:{ROOT / '.clang-format'}"

    def run(data: bytes, path: str) -> bytes:
        result = subprocess.run([*argv, style, f"--assume-filename={path}"], input=data, capture_output=True)
        if result.returncode:
            raise SystemExit(f"tidy_changed: clang-format failed on {path}: {result.stderr.decode(errors='replace')}")
        return result.stdout

    return run


def changed_paths(merge_base: str) -> list[tuple[str | None, str]]:
    """(old path or None, new path) for added, copied, modified and renamed files."""
    raw = git("diff", "--name-status", "-z", "-M", "--diff-filter=ACMR", merge_base, "HEAD", "--", *PATHSPEC)
    fields = raw.decode().split("\0")
    pairs: list[tuple[str | None, str]] = []
    index = 0
    while index < len(fields) and fields[index]:
        status = fields[index]
        if status[0] in "RC":
            old, new = fields[index + 1], fields[index + 2]
            index += 3
            pairs.append((old, new))
        else:
            pairs.append((None if status[0] == "A" else fields[index + 1], fields[index + 1]))
            index += 2
    return [(old, new) for old, new in pairs if WANTED.search(new)]


def line_diff(before: bytes, after: bytes, path: str) -> str:
    """Unified diff with no context, `a/` and `b/` prefixed so `-p1` finds the path."""
    old = before.decode(errors="surrogateescape").splitlines(keepends=True)
    new = after.decode(errors="surrogateescape").splitlines(keepends=True)
    return "".join(difflib.unified_diff(old, new, f"a/{path}", f"b/{path}", n=0))


def file_diffs(merge_base: str, fmt: Formatter) -> dict[str, str]:
    """New path -> diff of the formatted merge-base file against HEAD, non-empty only."""
    pairs = changed_paths(merge_base)

    def one(pair: tuple[str | None, str]) -> tuple[str, str]:
        old, new = pair
        head = git("show", f"HEAD:{new}")
        if old is None:
            return new, line_diff(b"", head, new)
        base = git("show", f"{merge_base}:{old}")
        if not old.startswith(format_sources.NEVER):
            base = fmt(base, new)
        return new, line_diff(base, head, new)

    with ThreadPoolExecutor() as pool:
        return {path: diff for path, diff in pool.map(one, pairs) if diff}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=("changed", "diff"))
    p.add_argument("--merge-base", required=True)
    p.add_argument("--files", type=Path, help="diff: only the files listed here, one per line")
    p.add_argument("--clang-format", default="clang-format", help="the pinned formatter command (default: PATH)")
    a = p.parse_args(argv)

    diffs = file_diffs(a.merge_base, make_formatter(a.clang_format))
    if a.mode == "changed":
        sys.stdout.write("".join(f"{name}\n" for name in sorted(diffs)))
        return 0
    if a.files is None:
        p.error("diff needs --files")
    wanted = {line.strip() for line in a.files.read_text().splitlines() if line.strip()}
    sys.stdout.write("".join(diffs[name] for name in sorted(diffs) if name in wanted))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
