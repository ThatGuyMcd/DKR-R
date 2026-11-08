"""Expand reviewed adapter hunks to full checked contexts (mechanical only)."""

import difflib
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
patch = root / "patches/enhanced-water" / (sys.argv[2] if len(sys.argv) > 2 else "dkr-adapter.patch")
stage = Path(sys.argv[1]).resolve()
parts = []
for section in re.split(r"(?=^diff --git )", patch.read_text(), flags=re.M):
    match = re.match(r"diff --git a/(\S+) b/\S+", section)
    if not match:
        continue
    relative = match[1]
    if relative.startswith("/") or ".." in Path(relative).parts:
        raise SystemExit("Invalid stage path")
    before = (stage / relative).read_text()
    after = before
    for hunk in re.split(r"(?=^@@ )", section, flags=re.M)[1:]:
        old = []
        new = []
        for line in hunk.splitlines(keepends=True)[1:]:
            if line.startswith(" "):
                old.append(line[1:])
                new.append(line[1:])
            elif line.startswith("-"):
                old.append(line[1:])
            elif line.startswith("+"):
                new.append(line[1:])
        old = "".join(old)
        new = "".join(new)
        if not old or after.count(old) != 1:
            raise SystemExit(f"Ambiguous adapter hunk in {relative}: {old!r}")
        after = after.replace(old, new, 1)
    parts.append(f"diff --git a/{relative} b/{relative}\n")
    for line in difflib.unified_diff(
        before.splitlines(True), after.splitlines(True), fromfile=f"a/{relative}", tofile=f"b/{relative}", n=3
    ):
        parts.append(line if line.endswith("\n") else line + "\n\\ No newline at end of file\n")
patch.write_text("".join(parts), newline="\n")
print("Adapter hunks expanded with exact staged-source context.")
