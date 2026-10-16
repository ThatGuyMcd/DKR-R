#!/usr/bin/env python3
"""Validate or apply reviewed dependency patch stacks without discarding local work.

Pristine, fully patched, and exact intermediate patch trees can be resumed.
Unknown local edits are refused unless --repair explicitly authorizes overwriting
managed patch paths. Unrelated tracked edits are always refused.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def git(repo: Path, *args: str, index: Path | None = None) -> str:
    """stdout of a git command; stderr passes through so a patch that no
    longer applies shows git's own diagnostic."""
    env = dict(os.environ)
    if index is not None:
        env["GIT_INDEX_FILE"] = str(index)
    return subprocess.run(["git", "-C", str(repo), *args], env=env, check=True,
                          stdout=subprocess.PIPE, text=True).stdout.strip()


def index_tree(repo: Path, scratch_index: Path) -> str:
    """Inspect staged work without letting write-tree refresh the real index."""
    source = Path(git(repo, "rev-parse", "--git-path", "index"))
    if not source.is_absolute():
        source = repo / source
    if source.exists():
        shutil.copyfile(source, scratch_index)
    else:
        git(repo, "read-tree", "--empty", index=scratch_index)
    return git(repo, "write-tree", index=scratch_index)


def patch_trees(repo: Path, patches: list[Path], expected_index: Path,
                current_index: Path, states: list[str] | None = None) -> tuple[str, str]:
    """Compute reviewed and working trees without touching the real index."""
    git(repo, "read-tree", "HEAD", index=expected_index)
    trees = [git(repo, "write-tree", index=expected_index)]
    for patch in patches:
        try:
            git(repo, "apply", "--cached", "--whitespace=nowarn", str(patch), index=expected_index)
        except subprocess.CalledProcessError:
            raise SystemExit(f"{repo.name}: {patch.name} does not apply to the pinned commit") from None
        trees.append(git(repo, "write-tree", index=expected_index))
    if states is not None:
        states.extend(trees)
    expected = trees[-1]

    # Files the patches create are untracked in the checkout; stage the ones
    # present so they take part in the comparison (lexists: symlinks too).
    added = "\0".join(git(repo, "diff", "--name-only", "-z", "--no-renames",
                           "--diff-filter=A", "HEAD", tree) for tree in trees)
    git(repo, "read-tree", "HEAD", index=current_index)
    git(repo, "add", "-u", index=current_index)
    present = [p for p in added.split("\0") if p and os.path.lexists(repo / p)]
    if present:
        git(repo, "add", "-f", "--", *present, index=current_index)
    current = git(repo, "write-tree", index=current_index)

    return expected, current


def checked_patches(root: Path, dependency: dict) -> list[Path]:
    result = []
    for entry in dependency["patches"]:
        path = root / entry["path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"].lower():
            raise SystemExit(f"Patch checksum mismatch: {entry['path']}")
        result.append(path)
    return result


def is_reviewed_checkout(root: Path, repo: Path, commit: str) -> bool:
    """Accept only the complete reviewed patch tree, without repairing anything.

    Unrelated untracked files and staged edits are intentionally stricter here
    than in the patch repair command: bootstrap must preserve user work.
    """
    manifest = root / "patches" / "manifest.json"
    if not manifest.exists() or git(repo, "rev-parse", "HEAD").lower() != commit.lower():
        return False
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1:
        raise SystemExit("Unsupported patch manifest schema")
    matches = [d for d in data["dependencies"]
               if (root / d["repositoryPath"]).resolve() == repo.resolve()]
    if len(matches) != 1 or matches[0]["expectedCommit"].lower() != commit.lower():
        return False
    reviewed = checked_patches(root, matches[0])
    with tempfile.TemporaryDirectory() as scratch:
        expected, current = patch_trees(repo, reviewed, Path(scratch) / "expected",
                                        Path(scratch) / "current")
        if current != expected:
            return False
        base = git(repo, "rev-parse", "HEAD^{tree}")
        if index_tree(repo, Path(scratch) / "staged") not in (base, expected):
            return False
        added = set(git(repo, "diff", "--name-only", "-z", "--no-renames",
                        "--diff-filter=A", "HEAD", expected).split("\0"))
        untracked = set(git(repo, "ls-files", "--others", "--exclude-standard", "-z").split("\0"))
        if untracked - added - {""}:
            return False
        # Tree IDs cannot reveal working-tree edits inside submodules. Accept
        # each dirty child only when its own complete reviewed stack matches;
        # RT64 and N64ModernRuntime both contain patched submodules.
        for entry in git(repo, "ls-tree", "-r", "-z", "HEAD").split("\0"):
            if not entry:
                continue
            metadata, path = entry.split("\t", 1)
            mode, _, child_commit = metadata.split()
            child = repo / path
            if mode != "160000" or not (child / ".git").exists():
                continue
            if git(child, "status", "--porcelain", "--untracked-files=all",
                   "--ignore-submodules=none") and not is_reviewed_checkout(root, child, child_commit):
                return False
        return True


def plan_dependency(repo: Path, patches: list[Path], expected_index: Path,
                    current_index: Path, repair: bool = False) -> tuple[list[str], list[str]]:
    states: list[str] = []
    expected, current = patch_trees(repo, patches, expected_index, current_index, states)
    managed = set()
    for tree in states:
        managed.update(git(repo, "diff", "--name-only", "--no-renames", "-z",
                           "HEAD", tree).split("\0"))
    for tree in (current, index_tree(repo, current_index.with_name(current_index.name + "-staged"))):
        changed = set(git(repo, "diff", "--name-only", "--no-renames", "-z",
                          "HEAD", tree).split("\0"))
        unrelated = sorted(changed - managed - {""})
        if unrelated:
            raise SystemExit(f"{repo}: local edits outside the patch set; commit or stash: "
                             + ", ".join(unrelated))
        if tree not in states and not repair:
            raise SystemExit(f"{repo}: unrecognized local edits in managed patch paths; "
                             "commit/stash them, or use --repair to discard them")
    paths = [p for p in git(repo, "diff", "--name-only", "--no-renames", "-z",
                           current, expected).split("\0") if p]
    present = set(git(repo, "ls-tree", "-r", "-z", "--name-only", expected,
                      "--", *paths).split("\0")) if paths else set()
    return [p for p in paths if p not in present], [p for p in paths if p in present]


def execute_plan(repo: Path, expected_index: Path, plan: tuple[list[str], list[str]]) -> str:
    remove, restore = plan
    for path in remove:
        (repo / path).unlink(missing_ok=True)
    if restore:
        git(repo, "checkout-index", "-f", "--", *restore, index=expected_index)
    return "updated" if remove or restore else "unchanged"


def sync_dependency(repo: Path, patches: list[Path], repair: bool = False) -> str:
    with tempfile.TemporaryDirectory() as scratch:
        expected_index = Path(scratch) / "expected"
        plan = plan_dependency(repo, patches, expected_index, Path(scratch) / "current", repair)
        return execute_plan(repo, expected_index, plan)


def apply_manifest(root: Path, manifest_path: Path, check: bool = False,
                   repair: bool = False, require_all: bool = False) -> int:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1:
        raise SystemExit("Unsupported patch manifest schema")
    # Preflight the complete manifest before touching any checkout. A bad later
    # checksum or local edit must not leave earlier dependencies half updated.
    with tempfile.TemporaryDirectory() as scratch:
        plans = []
        for number, dependency in enumerate(data["dependencies"]):
            patches = checked_patches(root, dependency)
            name = dependency["name"]
            repo = root / dependency["repositoryPath"]
            if not (repo / ".git").exists():
                if require_all:
                    raise SystemExit(f"{name}: dependency checkout is missing at {repo}; bootstrap dependencies first")
                print(f"[SKIP] {name}: checkout not present at {repo}")
                continue
            commit = git(repo, "rev-parse", "HEAD")
            if commit.lower() != dependency["expectedCommit"].lower():
                raise SystemExit(f"{name} commit mismatch: expected {dependency['expectedCommit']}, got {commit}")
            index = Path(scratch) / f"expected-{number}"
            plan = plan_dependency(repo, patches, index, Path(scratch) / f"current-{number}", repair)
            plans.append((name, repo, patches, index, plan))
        if check and not plans:
            raise SystemExit("No dependency checkouts found to validate; bootstrap dependencies first")
        for name, repo, patches, index, plan in plans:
            state = "validated" if check else execute_plan(repo, index, plan)
            print(f"[OK] {name}: {len(patches)} patch(es) {state}")
    return 0


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--require-all", action="store_true", help="Fail if any manifest checkout is absent (used by Windows preparation).")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="Validate the full ordered patch stack without changing checkout files or the real index.")
    mode.add_argument("--repair", action="store_true", help="Discard unknown working-tree edits on managed patch paths; unrelated tracked edits remain protected. The real index is preserved.")
    args = parser.parse_args()
    root = args.root.resolve()  # patch paths are passed to `git -C <repo>`
    return apply_manifest(root, args.manifest or root / "patches" / "manifest.json", args.check, args.repair, args.require_all)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
