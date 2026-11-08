#!/usr/bin/env python3
"""Clone and verify the exact external revisions used by DKR-R."""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
from typing import Any

from apply_dependency_patches import is_reviewed_checkout

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK_FILE = ROOT / "dependencies.lock.json"
RESOLVED_FILE = ROOT / "extern" / ".resolved-dependencies.json"


def run(command: list[str], cwd: pathlib.Path | None = None, capture: bool = False) -> str:
    print("+", " ".join(command))
    result = subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if result.returncode != 0:
        if capture and result.stdout:
            print(result.stdout, file=sys.stderr)
        raise RuntimeError(f"Command failed with exit code {result.returncode}: {' '.join(command)}")
    return (result.stdout or "").strip()


def ensure_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"Required tool was not found on PATH: {name}")


def is_dirty(directory: pathlib.Path) -> bool:
    return bool(
        run(
            ["git", "status", "--porcelain", "--untracked-files=all", "--ignore-submodules=none"],
            cwd=directory,
            capture=True,
        )
    )


def dirty_children(directory: pathlib.Path) -> list[str]:
    children = []
    entries = run(["git", "ls-tree", "-r", "-z", "HEAD"], cwd=directory, capture=True)
    for entry in entries.split("\0"):
        if not entry:
            continue
        metadata, path = entry.split("\t", 1)
        if metadata.startswith("160000 ") and (directory / path / ".git").exists():
            child_commit = run(["git", "rev-parse", "HEAD"], cwd=directory / path, capture=True)
            if child_commit != metadata.split()[2] or is_dirty(directory / path):
                children.append(path)
    return children


def select_entries(entries: list[dict[str, Any]], selected: set[str]) -> list[dict[str, Any]]:
    by_name = {entry["name"]: entry for entry in entries}
    missing = selected - by_name.keys()
    if missing:
        raise RuntimeError("Unknown dependency name(s): " + ", ".join(sorted(missing)))
    wanted = set(selected or by_name)
    for name in list(wanted):
        entry = by_name[name]
        while entry.get("managedBy"):
            entry = parent_entry(entry, entries)
            wanted.add(entry["name"])
    return sorted(
        (entry for entry in entries if entry["name"] in wanted),
        key=lambda entry: len(pathlib.PurePosixPath(entry["destination"]).parts),
    )


def parent_entry(entry: dict[str, Any], entries: list[dict[str, Any]]) -> dict[str, Any]:
    destination = pathlib.PurePosixPath(entry["destination"])
    parents = [
        candidate for candidate in entries if pathlib.PurePosixPath(candidate["destination"]) in destination.parents
    ]
    if not parents:
        raise RuntimeError(f"No locked parent for managed dependency {entry['name']}")
    return max(parents, key=lambda candidate: len(pathlib.PurePosixPath(candidate["destination"]).parts))


def prepare_dependency(
    entry: dict[str, Any], force: bool, parent: dict[str, Any] | None = None, shallow: bool = False
) -> dict[str, str]:
    destination = ROOT / entry["destination"]
    repository = entry["repository"]
    commit = entry["commit"]
    recursive = bool(entry.get("recursive", False))

    if entry.get("managedBy"):
        if parent is None:
            raise RuntimeError(f"{entry['name']} must be initialized through its locked parent submodule")
        parent_path = ROOT / parent["destination"]
        relative = destination.relative_to(parent_path).as_posix()
        tree = run(["git", "ls-tree", "HEAD", "--", relative], cwd=parent_path, capture=True)
        fields = tree.split()
        if len(fields) < 3 or fields[0] != "160000" or fields[2].lower() != commit.lower():
            raise RuntimeError(f"Parent submodule pin does not match lock for {entry['name']}")
        if not (destination / ".git").exists():
            run(
                [
                    "git",
                    "submodule",
                    "update",
                    "--init",
                    "--recursive",
                    *(["--depth", "1"] if shallow else []),
                    "--",
                    relative,
                ],
                cwd=parent_path,
            )
        if run(["git", "rev-parse", "HEAD"], cwd=destination, capture=True).lower() != commit.lower():
            raise RuntimeError(f"{entry['name']} submodule is at another revision; restore its locked parent pin first")

    if destination.exists() and not (destination / ".git").exists():
        if any(destination.iterdir()):
            raise RuntimeError(f"{destination} exists but is not a Git checkout. Move or remove it first.")
        destination.rmdir()

    cloned = not destination.exists()
    if cloned:
        destination.parent.mkdir(parents=True, exist_ok=True)
        if shallow:
            # Fetch only the lockfile revision, not a default branch or history.
            run(["git", "init", str(destination)])
            run(["git", "remote", "add", "origin", repository], cwd=destination)
        else:
            run(["git", "clone", "--filter=blob:none", "--no-checkout", repository, str(destination)])
    else:
        current_remote = run(["git", "remote", "get-url", "origin"], cwd=destination, capture=True)
        if (
            current_remote.rstrip("/").removesuffix(".git").lower()
            != repository.rstrip("/").removesuffix(".git").lower()
        ):
            raise RuntimeError(f"Unexpected origin for {entry['name']}: {current_remote}")
        dirty = is_dirty(destination)
        reviewed = dirty and is_reviewed_checkout(ROOT, destination, commit)
        if dirty and not reviewed and not force:
            raise RuntimeError(
                f"{entry['name']} contains local changes. Commit/stash them or rerun with --force to discard them."
            )
        if force and dirty and not reviewed:
            children = dirty_children(destination)
            if children:
                raise RuntimeError(
                    f"{entry['name']} contains dirty submodule(s): {', '.join(children)}. "
                    "Commit/stash or explicitly restore changes inside each child before --force; "
                    "parent reset/clean cannot safely discard nested work."
                )
            run(["git", "reset", "--hard"], cwd=destination)
            run(["git", "clean", "-fd"], cwd=destination)
            if is_dirty(destination):
                raise RuntimeError(
                    f"{entry['name']} still contains local changes after --force. "
                    "Inspect git status and handle nested repositories explicitly; none were deleted."
                )

    # A pinned checkout is already resolved, including one with exactly the
    # reviewed patches. Avoid checkout/fetch churn and support offline reruns.
    current = "" if cloned else run(["git", "rev-parse", "HEAD"], cwd=destination, capture=True)
    if cloned or current.lower() != commit.lower():
        run(["git", "fetch", "--no-tags", *(["--depth", "1"] if shallow else []), "origin", commit], cwd=destination)
        run(["git", "checkout", "--detach", commit], cwd=destination)
    if recursive:
        run(
            ["git", "submodule", "update", "--init", "--recursive", *(["--depth", "1"] if shallow else [])],
            cwd=destination,
        )

    resolved = run(["git", "rev-parse", "HEAD"], cwd=destination, capture=True)
    if resolved.lower() != commit.lower():
        raise RuntimeError(f"Resolved revision mismatch for {entry['name']}: expected {commit}, got {resolved}")
    return {"name": entry["name"], "commit": resolved, "destination": entry["destination"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--only", action="append", help="Prepare this dependency and its required parents (repeatable)."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Discard local changes in selected checkouts; dirty nested submodules must be handled explicitly first.",
    )
    parser.add_argument(
        "--shallow",
        action="store_true",
        help="Fetch only pinned revisions and shallow submodules for compact CI source caches.",
    )
    args = parser.parse_args()

    ensure_tool("git")
    lock = json.loads(LOCK_FILE.read_text(encoding="utf-8"))
    selected = set(args.only or [])
    entries = select_entries(lock["dependencies"], selected)
    resolved = [
        prepare_dependency(
            entry,
            args.force,
            parent_entry(entry, lock["dependencies"]) if entry.get("managedBy") else None,
            shallow=args.shallow,
        )
        for entry in entries
    ]
    previous = json.loads(RESOLVED_FILE.read_text(encoding="utf-8")) if RESOLVED_FILE.exists() else {}
    records = {entry["name"]: entry for entry in previous.get("dependencies", [])}
    records.update({entry["name"]: entry for entry in resolved})
    RESOLVED_FILE.parent.mkdir(parents=True, exist_ok=True)
    RESOLVED_FILE.write_text(
        json.dumps({"schemaVersion": 1, "dependencies": list(records.values())}, indent=2) + "\n", encoding="utf-8"
    )
    print(f"\nPrepared {len(resolved)} dependency checkout(s).")
    print(f"Resolution record: {RESOLVED_FILE.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, KeyError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
