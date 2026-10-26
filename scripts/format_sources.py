#!/usr/bin/env python3
"""Format the first-party C/C++, CMake and Python sources, reproducibly.

    format_sources.py --check     report files that would change (exit 1 if any)
    format_sources.py --write     format them in place
    format_sources.py --check --local   the same with the tools on PATH (CI)
    format_sources.py --prove BASE HEAD [--record JSON]
                                  prove the commits from BASE to HEAD changed
                                  layout only (the fold's second evidence link)

clang-format, gersemi and ruff run inside a pinned image (IMAGE) with the
exact versions .github/lint-tools.txt pins, installed with --require-hashes,
so a reviewer who reruns it gets identical output. --write then proves the pass changed
layout only: every rewritten C/C++ file must lex to the same tokens as before
(comments and whitespace aside; scripts/format_tokens.py). A file that
doesn't is restored and reported, and the run fails.

The C/C++ and CMake set is SOURCES minus NEVER: guest code fragments spliced
into generated payloads byte for byte, pinned kits, generated and third-party
code, and the guard's fixtures. .clang-format-ignore and the pre-commit hook
excludes mirror NEVER so the hooks honour it too. ruff formats the Python
pyproject.toml selects.

--prove reads both trees from git and accepts a changed file only if it is a
C/C++ file with the same tokens, a CMake file with the same words (comments
and layout aside), a Python file with the same syntax tree, or a script that
pins file digests (PIN_HOLDERS) whose only change is re-pinning files the same
commit range reformatted, each old digest to the new one. It lists each re-pin.

Exit status: 0 clean (or formatted, or proven), 1 changes found, a token
mismatch or an unproven change, 2 the image run failed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import format_tokens  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
# python:3.12.12-slim-bookworm (the multi-platform index: the host's own
# platform runs, and the pinned tools give the same output on each).
IMAGE = "python@sha256:593bd06efe90efa80dc4eee3948be7c0fde4134606dd40d8dd8dbcade98e669c"
TOOLS = ("clang-format", "gersemi", "ruff")
# Pinned in lint-tools.txt for the lint job, not needed here (and shellcheck-py
# has no Linux arm64 wheel).
NOT_FORMATTERS = ("shellcheck-py",)
C_FAMILY = re.compile(r"\.(c|cc|cpp|h|hpp|inl)$")
CMAKE = re.compile(r"(^|/)CMakeLists\.txt$|\.cmake$")
SOURCES = (
    "runtime-recomp/src/",
    "runtime-recomp/tests/",
    "runtime-recomp/include/",
    "runtime-recomp/cmake/",
    "modding/",
)
NEVER = (
    "runtime-recomp/guest/",  # spliced into generated payloads byte for byte
    "modding/reference-us-v",  # pinned address-only kits
    "runtime-recomp/RecompiledRSP/",
    "runtime-recomp/third_party/",
    "tests/fixtures/",
)


def files() -> tuple[list[str], list[str]]:
    """(C/C++ files, CMake files) to format."""
    listed = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
    chosen = [
        name
        for name in listed
        if name and (name.startswith(SOURCES) or name == "runtime-recomp/CMakeLists.txt") and not name.startswith(NEVER)
    ]
    return [n for n in chosen if C_FAMILY.search(n)], [n for n in chosen if CMAKE.search(n)]


def requirements() -> str:
    """lint-tools.txt's hash-pinned blocks for TOOLS and their dependencies
    (everything but the linters a format pass doesn't run)."""
    text = (ROOT / ".github/lint-tools.txt").read_text(encoding="utf-8")
    blocks = re.split(r"\n(?=[A-Za-z])", text.split("\n", 1)[1] if text.startswith("#") else text)
    blocks = [b for b in blocks if b.strip() and not b.startswith("#")]
    names = [b.split("==", 1)[0] for b in blocks]
    missing = [tool for tool in TOOLS if tool not in names]
    if missing:
        raise SystemExit("format_sources: lint-tools.txt no longer pins " + ", ".join(missing))
    return "\n".join(b.rstrip() for b in blocks if b.split("==", 1)[0] not in NOT_FORMATTERS) + "\n"


def run_tools(c_files: list[str], cmake_files: list[str], write: bool, local: bool) -> subprocess.CompletedProcess[str]:
    lists = ROOT / "build/format-sources"
    lists.mkdir(parents=True, exist_ok=True)
    (lists / "requirements.txt").write_text(requirements())
    (lists / "c-files.txt").write_text("\n".join(c_files) + "\n")
    (lists / "cmake-files.txt").write_text("\n".join(cmake_files) + "\n")
    clang = "clang-format -i" if write else "clang-format --dry-run -Werror --ferror-limit=1"
    gersemi = "gersemi -i" if write else "gersemi --check"
    ruff = "ruff format" if write else "ruff format --check"
    install = (
        "set -e; python3 -m venv /tmp/tools >/dev/null;"
        " /tmp/tools/bin/pip install --quiet --disable-pip-version-check --require-hashes --only-binary=:all:"
        " -r build/format-sources/requirements.txt;"
        " export PATH=/tmp/tools/bin:$PATH;"
    )
    script = (
        ("" if local else install) + " status=0;"
        f" tr '\\n' '\\0' < build/format-sources/c-files.txt | xargs -0 {clang} || status=1;"
        f" tr '\\n' '\\0' < build/format-sources/cmake-files.txt | xargs -0 {gersemi} || status=1;"
        f" {ruff} --quiet || status=1;"
        " exit $status"
    )
    if local:
        return subprocess.run(["bash", "-c", script], cwd=ROOT, text=True, capture_output=True)
    import platform

    native = "linux/arm64" if platform.machine().lower() in ("arm64", "aarch64") else "linux/amd64"
    command = ["docker", "run", "--rm", "--platform", native, "-v", f"{ROOT}:/src", "-w", "/src"]
    command += ["--user", _uid(), "-e", "HOME=/tmp", IMAGE, "bash", "-c", script]
    return subprocess.run(command, text=True, capture_output=True)


# Scripts that pin first-party files by sha256 (native audits, the portrait
# stage). Formatting a pinned file re-pins it there in the same commit.
PIN_HOLDERS = (
    "scripts/generate_replay_probe.py",
    "scripts/character_portrait_pipeline.py",
    "runtime-recomp/tests/replay_probe/test_generator.py",
)
DIGEST = re.compile(r"[0-9a-f]{64}")
CMAKE_TOKEN = re.compile(
    r"(?P<comment>#\[(?P<eq>=*)\[.*?\](?P=eq)\]|#[^\n]*)"
    r'|(?P<word>"(?:\\.|[^"\\])*"|\[(?P<beq>=*)\[.*?\](?P=beq)\]|[^\s()#"]+|[()])',
    re.S,
)


def show(rev: str, name: str) -> bytes | None:
    done = subprocess.run(["git", "show", f"{rev}:{name}"], cwd=ROOT, capture_output=True)
    return done.stdout if done.returncode == 0 else None


def cmake_words(data: bytes) -> list[str]:
    """CMake arguments and parentheses in order; comments and layout dropped."""
    text = data.decode("utf-8", errors="surrogateescape")
    return [m["word"] for m in CMAKE_TOKEN.finditer(text) if m["word"] is not None]


def python_tree(data: bytes, digests: dict[str, str] | None = None) -> str:
    import ast

    tree = ast.parse(data)
    if digests:
        for node in ast.walk(tree):
            if isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value in digests:
                node.value = digests[node.value]
    return ast.dump(tree)


def prove(base: str, head: str) -> tuple[dict[str, object], list[str]]:
    """(record, problems) for the commits from BASE to HEAD."""
    diff = subprocess.check_output(["git", "diff", "--name-status", "--no-renames", base, head], cwd=ROOT, text=True)
    changed: dict[str, str] = {}
    for line in diff.splitlines():
        status, name = line.split("\t", 1)
        changed[name] = status
    problems: list[str] = []
    digests: dict[str, str] = {}
    formatted: list[str] = []
    for name, status in sorted(changed.items()):
        old, new = show(base, name), show(head, name)
        if status != "M" or old is None or new is None:
            continue
        if C_FAMILY.search(name):
            if format_tokens.tokens(old) != format_tokens.tokens(new):
                problems.append(f"{name}: tokens changed")
                continue
            digests[hashlib.sha256(old).hexdigest()] = hashlib.sha256(new).hexdigest()
            formatted.append(name)
    repins: list[dict[str, str]] = []
    for name, status in sorted(changed.items()):
        old, new = show(base, name), show(head, name)
        if C_FAMILY.search(name) and status == "M":
            continue
        if status != "M" or old is None or new is None:
            problems.append(f"{name}: {status} is not a format change")
        elif CMAKE.search(name):
            if cmake_words(old) != cmake_words(new):
                problems.append(f"{name}: CMake words changed")
        elif name.endswith(".py") and name in PIN_HOLDERS:
            if python_tree(old, digests) != python_tree(new):
                problems.append(f"{name}: changed beyond re-pinning reformatted files")
            gone = set(DIGEST.findall(old.decode())) - set(DIGEST.findall(new.decode()))
            repins += [{"pinned_in": name, "before": d, "after": digests[d]} for d in sorted(gone) if d in digests]
        elif name.endswith(".py"):
            if python_tree(old) != python_tree(new):
                problems.append(f"{name}: Python syntax tree changed")
        else:
            problems.append(f"{name}: not a C/C++, CMake or Python file")
    record: dict[str, object] = {
        "schema": 1,
        "base": subprocess.check_output(["git", "rev-parse", base], cwd=ROOT, text=True).strip(),
        "head": subprocess.check_output(["git", "rev-parse", head], cwd=ROOT, text=True).strip(),
        "image": IMAGE,
        "files_changed": len(changed),
        "c_family_token_identical": len(formatted),
        "repins": repins,
        "problems": problems,
        "passed": not problems,
    }
    return record, problems


def _uid() -> str:
    import os

    return f"{os.getuid()}:{os.getgid()}" if hasattr(os, "getuid") else "0:0"


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--prove", nargs=2, metavar=("BASE", "HEAD"))
    p.add_argument("--record", type=Path, help="with --prove: write the proof here")
    p.add_argument(
        "--local",
        action="store_true",
        help="use the tools on PATH (CI installs them from lint-tools.txt), not the image",
    )
    a = p.parse_args(argv)
    if a.prove:
        record, problems = prove(*a.prove)
        if a.record:
            a.record.write_text(json.dumps(record, indent=1) + "\n")
        for problem in problems:
            print(f"format_sources: {problem}", file=sys.stderr)
        print(
            f"format_sources: {record['files_changed']} files, {record['c_family_token_identical']} C/C++ "
            f"token-identical, {len(record['repins'])} re-pins, {len(problems)} unproven"  # type: ignore[arg-type]
        )
        return 1 if problems else 0
    c_files, cmake_files = files()
    before = {name: (ROOT / name).read_bytes() for name in c_files}
    done = run_tools(c_files, cmake_files, a.write, a.local)
    if a.check:
        sys.stderr.write(done.stderr[-4000:])
        return 0 if done.returncode == 0 else 1
    if done.returncode not in (0,):
        sys.stderr.write(done.stderr[-4000:])
        return 2
    changed = [name for name, data in before.items() if (ROOT / name).read_bytes() != data]
    mismatched: list[str] = []
    for name in changed:
        if format_tokens.tokens(before[name]) != format_tokens.tokens((ROOT / name).read_bytes()):
            (ROOT / name).write_bytes(before[name])
            mismatched.append(name)
    print(f"format_sources: {len(changed)} C/C++ files reformatted, {len(cmake_files)} CMake files checked")
    for name in mismatched:
        print(f"format_sources: {name}: tokens changed; restored", file=sys.stderr)
    return 1 if mismatched else 0


if __name__ == "__main__":
    sys.exit(main())
