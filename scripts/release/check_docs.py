#!/usr/bin/env python3
"""Inventory the release's documentation and check the changed guides' links.

    check_docs.py --since TAG --evidence DIR

Every tracked .md/.txt/.rst file gets a disposition; the Markdown files
changed since TAG (the previous release) get their local links checked.
Writes EVIDENCE/DOCUMENTATION-INVENTORY.json and DOCUMENTATION-CHECKS.json
(finalize.py copies both into the handoff). Exit 1 on a broken link.
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

sys.dont_write_bytecode = True
from release_common import ROOT, version  # noqa: E402

EXCLUDED = ("extern/", "runtime-recomp/RecompiledFuncs/", "runtime-recomp/RecompiledPatches/")
LINK = re.compile(r'(?<!!)\[[^\]\n]+\]\(([^)\s]+)(?:\s+"[^"]*")?\)')


def files() -> list[str]:
    """Tracked and untracked-but-not-ignored files, as a release ships them."""
    raw = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT)
    names = (n.decode() for n in raw.split(b"\0") if n)
    return sorted(n for n in names if not n.startswith(EXCLUDED) and (ROOT / n).is_file())


def changed_since(since: str) -> set[str]:
    out = subprocess.check_output(["git", "diff", "--name-only", f"{since}..HEAD"], cwd=ROOT, text=True)
    return set(out.split())


def disposition(name: str, changes: set[str]) -> str:
    if name.startswith(f"docs/releases/{version()}/"):
        return "release-notes"
    if name in changes:
        return "updated-current-document"
    if re.search("LICENSE|COPYING|NOTICE|THIRD_PARTY", name, re.I):
        return "licence-attribution-preserved"
    if name.startswith("docs/releases/") or "/history/" in name or re.search("/(?:PUBLIC-|BETA|PLAYTEST)", name):
        return "historical-record-preserved"
    return "reviewed-reference-no-release-change"


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--since", required=True, help="the previous release's tag")
    p.add_argument("--evidence", type=Path, required=True)
    a = p.parse_args(argv)
    changes = changed_since(a.since)
    inventory: list[dict[str, Any]] = []
    broken: list[dict[str, str]] = []
    links = 0
    for name in files():
        path = ROOT / name
        if path.suffix.lower() not in {".md", ".txt", ".rst"} or path.name == "CMakeLists.txt":
            continue
        data = path.read_bytes()
        inventory.append(
            {"path": name, "disposition": disposition(name, changes), "sha256": hashlib.sha256(data).hexdigest()}
        )
        if name not in changes or path.suffix != ".md":
            continue
        for link in LINK.findall(data.decode("utf-8", errors="replace")):
            if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", link) or link.startswith("#"):
                continue
            target = (path.parent / link.split("#")[0]).resolve()
            links += 1
            if not target.is_relative_to(ROOT) or not target.exists():
                broken.append({"path": name, "link": link})
    counts: dict[str, int] = {}
    for row in inventory:
        counts[row["disposition"]] = counts.get(row["disposition"], 0) + 1
    a.evidence.mkdir(parents=True, exist_ok=True)
    (a.evidence / "DOCUMENTATION-INVENTORY.json").write_text(json.dumps(inventory, indent=2) + "\n")
    report = {
        "version": version(),
        "since": a.since,
        "documents": len(inventory),
        "dispositions": counts,
        "local_links_checked": links,
        "broken_links": broken,
    }
    (a.evidence / "DOCUMENTATION-CHECKS.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
