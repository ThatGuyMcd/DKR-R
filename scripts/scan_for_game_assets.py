#!/usr/bin/env python3
"""Fail when repository files look like ROMs or generated game-resource archives."""

from __future__ import annotations

import argparse
import os
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKIP_PARTS = {
    ".git",
    ".deps",
    "build",
    "build-logs",
    "dist",
    "runtime",
    "extern",
    "node_modules",
    "run-data",
    "--portable",
    "squashfs-root",
    "__pycache__",
}
DENIED_SUFFIXES = {".z64", ".v64", ".n64", ".o2r", ".otr", ".eep", ".mpk"}
N64_HEADERS = {
    b"\x80\x37\x12\x40": ".z64 big-endian ROM",
    b"\x37\x80\x40\x12": ".v64 byte-swapped ROM",
    b"\x40\x12\x37\x80": ".n64 little-endian ROM",
}
MAX_SOURCE_SIZE = 5 * 1024 * 1024
ALLOW_LARGE: set[str] = set()


def source_files(root: pathlib.Path):
    def fail_walk(error):
        raise error

    for directory, children, files in os.walk(root, followlinks=False, onerror=fail_walk):
        children[:] = sorted(name for name in children if name not in SKIP_PARTS)
        for name in sorted(files):
            path = pathlib.Path(directory) / name
            if name not in SKIP_PARTS and path.is_file():
                yield path


def main(root: pathlib.Path | None = None) -> int:
    root = (root or ROOT).resolve()
    if not root.is_dir():
        print(f"Asset scan FAILED: root is not a directory: {root}", file=sys.stderr)
        return 2
    failures: list[str] = []
    inspected = 0
    # os.walk prunes excluded build/dependency trees before examining files,
    # including WSL-only links which the Windows host cannot stat.
    for path in source_files(root):
        inspected += 1
        relative = path.relative_to(root).as_posix()
        if path.suffix.lower() in DENIED_SUFFIXES:
            failures.append(f"Denied game-data extension: {relative}")
            continue
        size = path.stat().st_size
        if size > MAX_SOURCE_SIZE and relative not in ALLOW_LARGE:
            failures.append(f"Unexplained file larger than 5 MiB: {relative} ({size} bytes)")
        with path.open("rb") as stream:
            header = stream.read(4)
        if header in N64_HEADERS:
            failures.append(f"Nintendo 64 ROM header ({N64_HEADERS[header]}): {relative}")
        if zipfile.is_zipfile(path) and path.suffix.lower() not in {".zip"}:
            failures.append(f"Unexpected ZIP-compatible binary archive: {relative}")

    if inspected == 0:
        failures.append("No repository files were inspected; refusing a fail-open result")
    if failures:
        print("Asset scan FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(f"Asset scan passed: {inspected} repository files inspected.")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    try:
        raise SystemExit(main(args.root))
    except OSError as error:
        print(f"Asset scan FAILED: {error}", file=sys.stderr)
        raise SystemExit(2)
