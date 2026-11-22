#!/usr/bin/env python3
"""Offline check that relative Markdown links, images and #anchors resolve.

    check_doc_links.py [--root DIR]

Scans every tracked *.md file. External links are not fetched. Fenced code
blocks and inline code are not read as links. Anchors use GitHub's slug rules
(duplicate headings get -1, -2, ...). Exit 1 and one line per problem on a
missing file or anchor.

Two kinds of link are skipped by rule: pages under docs/releases/<version>/pages/
are written for where a release stages them, so their links don't resolve in
place, and links into extern/ only resolve once the dependencies are fetched.
"""

from __future__ import annotations

import argparse
from collections.abc import Iterator
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
# Release pages are copied elsewhere before anyone reads them
# (scripts/release/finalize.py ships RELEASE-README.md as the handoff's
# README.txt), and the older ones were staged over docs/ and the root.
STAGED_PAGES = re.compile(r"^docs/releases/[^/]+/pages/")
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})")
HEADING = re.compile(r"^ {0,3}#{1,6}[ \t]+(.*?)(?:[ \t]+#+)?[ \t]*$")
INLINE_LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*(<[^>]*>|[^)\s]*)[^)]*\)")
REF_DEF = re.compile(r"^ {0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)")
HTML_ID = re.compile(r"""<[a-z][^>]*?\b(?:id|name)=["']([^"']+)["']""", re.I)
CODE_SPAN = re.compile(r"(`+)(.+?)\1")
EXTERNAL = re.compile(r"^(?:[a-z][a-z0-9+.-]*:|//)", re.I)


def tracked_markdown(root: Path) -> list[str]:
    out = subprocess.run(["git", "ls-files", "-z", "*.md"], cwd=root, check=True, capture_output=True, text=True).stdout
    return sorted(name for name in out.split("\0") if name)


def prose_lines(text: str) -> Iterator[tuple[int, str]]:
    """Yield (line number, line) outside fenced code blocks."""
    fence: str | None = None
    for number, line in enumerate(text.splitlines(), 1):
        match = FENCE.match(line)
        if fence:
            if match and match.group(1)[0] == fence[0] and len(match.group(1)) >= len(fence):
                fence = None
        elif match:
            fence = match.group(1)
        else:
            yield number, line


def slug(heading: str) -> str:
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading)
    text = CODE_SPAN.sub(r"\2", text)
    text = re.sub(r"<[^>]+>", "", text)
    # `*` and `~` are emphasis markers; `_` is only one at a word boundary (snake_case is literal).
    text = re.sub(r"[*~]+|(?<![^\W_])_+|_+(?![^\W_])", "", text).strip().lower()
    return re.sub(r"[^\w\- ]", "", text).replace(" ", "-")


def anchors(text: str) -> set[str]:
    found: set[str] = set()
    seen: dict[str, int] = {}
    for _, line in prose_lines(text):
        match = HEADING.match(line)
        if match:
            base = slug(match.group(1))
            count = seen.get(base, 0)
            seen[base] = count + 1
            found.add(base if not count else f"{base}-{count}")
        found.update(HTML_ID.findall(line))
    return found


def link_targets(text: str) -> Iterator[tuple[int, str]]:
    """Yield (line number, raw link target) for inline links, images and reference definitions."""
    for number, line in prose_lines(text):
        line = CODE_SPAN.sub(lambda m: " " * len(m.group(0)), line)
        targets = [str(m.group(1)) for m in INLINE_LINK.finditer(line)]
        ref = REF_DEF.match(line)
        if ref:
            targets.append(ref.group(1))
        for target in targets:
            yield number, target.strip("<>")


def check(root: Path, files: list[str] | None = None) -> list[str]:
    """One line per broken link in files (default: every tracked Markdown file under root)."""
    root = root.resolve()
    problems: list[str] = []
    cache: dict[Path, set[str]] = {}
    for name in files if files is not None else tracked_markdown(root):
        if STAGED_PAGES.match(name):
            continue
        source = root / name
        text = source.read_text(encoding="utf-8")
        for number, target in link_targets(text):
            if not target or EXTERNAL.match(target):
                continue
            path_part, _, fragment = target.partition("#")
            path_part = unquote(path_part.partition("?")[0])
            linked = root / path_part.lstrip("/") if path_part.startswith("/") else source.parent / path_part
            # Normalised by name, not through symlinks: extern/<dep> can be a link to a
            # checkout another worktree shares, and its path is still this repository's.
            linked = Path(os.path.normpath(linked)) if path_part else source
            if not linked.is_relative_to(root):
                # It may exist on this machine; it won't in anyone else's clone.
                problems.append(f"{name}:{number}: {target} points outside the repository")
            elif path_part and not linked.exists():
                if not linked.is_relative_to(root / "extern"):
                    problems.append(f"{name}:{number}: missing file {target}")
            elif fragment and linked.suffix == ".md" and linked.is_file():
                if linked not in cache:
                    cache[linked] = anchors(linked.read_text(encoding="utf-8"))
                if unquote(fragment).lower() not in cache[linked]:
                    problems.append(f"{name}:{number}: no anchor #{fragment} in {linked.relative_to(root).as_posix()}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    problems = check(args.root)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"{len(problems)} broken Markdown link(s)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
