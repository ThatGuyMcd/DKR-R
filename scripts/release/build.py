#!/usr/bin/env python3
"""Configure, build and test a release from a clean, prepared checkout.

    build.py --platform windows|linux|macos --record DIR [--prepared DIR] [--allow-dirty]

Runs `cmake --preset release-<platform>` (runtime-recomp/CMakePresets.json:
generated payloads, RT64, rollback where supported, DKR_WERROR and input
verification on), builds everything, and runs the tests with
`ctest --output-junit`. The prepared inputs (scripts/prepare_payloads.py,
build/prepared by default) must match build-inputs.lock.json first.

RECORD gets GIT-HEAD.txt, GIT-STATUS.txt, SUBMODULES.txt, ctest.xml and
BUILD.json; evidence.py turns it into RELEASE-EVIDENCE.json, and finalize.py
reads ctest.xml. The source tree must be clean unless --allow-dirty (a dry
run), and BUILD.json says which.

Exit status: 0 built and every test passed, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from release_common import ROOT, tag, version  # noqa: E402

RUNTIME = ROOT / "runtime-recomp"


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True)


def run(command: list[str], log: Path) -> int:
    with log.open("w", encoding="utf-8") as out:
        return subprocess.run(command, cwd=RUNTIME, stdout=out, stderr=subprocess.STDOUT).returncode


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    p.add_argument("--record", type=Path, required=True, help="a NEW folder for the build record")
    p.add_argument("--prepared", type=Path, default=ROOT / "build/prepared")
    p.add_argument("--allow-dirty", action="store_true", help="a dry run from an uncommitted tree")
    a = p.parse_args(argv)
    record, prepared = a.record.resolve(), a.prepared.resolve()
    if record.exists():
        p.error("choose a NEW record folder")
    status = git("status", "--porcelain")
    if status.strip() and not a.allow_dirty:
        print(
            "build: the tree has uncommitted changes; commit them (or pass --allow-dirty for a dry run)",
            file=sys.stderr,
        )
        return 1
    record.mkdir(parents=True)
    head = git("rev-parse", "HEAD").strip()
    (record / "GIT-HEAD.txt").write_text(head + "\n")
    (record / "GIT-STATUS.txt").write_text(status)
    (record / "SUBMODULES.txt").write_text(git("submodule", "status", "--recursive"))
    here = Path(__file__).resolve().parent
    lock = subprocess.run([sys.executable, "-B", str(here / "lock_inputs.py"), "check", "--prepared", str(prepared)])
    if lock.returncode:
        return 1
    preset = f"release-{a.platform}"
    build = ROOT / "build" / preset
    # The CMake File API replies evidence.py and the input-lock test read.
    queries = build / ".cmake/api/v1/query"
    queries.mkdir(parents=True, exist_ok=True)
    for kind in ("codemodel-v2", "cmakeFiles-v1"):
        (queries / kind).touch()
    steps = (
        ("configure", ["cmake", "--preset", preset, f"-DDKR_PREPARED_DIR={prepared.as_posix()}"]),
        ("build", ["cmake", "--build", "--preset", preset, "--parallel"]),
        ("test", ["ctest", "--preset", preset, "--output-junit", str(record / "ctest.xml")]),
    )
    results: dict[str, int] = {}
    for name, command in steps:
        results[name] = run(command, record / f"{name}.log")
        if results[name] and name != "test":
            break
    tags = git("tag", "--points-at", "HEAD").split()
    summary = {
        "schema": 1,
        "platform": a.platform,
        "preset": preset,
        "build": build.as_posix(),
        "prepared": prepared.as_posix(),
        "version": version(),
        "commit": head,
        "tag_at_head": tag() in tags,
        "clean_tree": not status.strip(),
        "steps": results,
    }
    (record / "BUILD.json").write_text(json.dumps(summary, indent=2) + "\n")
    failed = [name for name, code in results.items() if code]
    if failed or len(results) != len(steps):
        print(f"build: {', '.join(failed) or 'a step'} failed; see {record}", file=sys.stderr)
        return 1
    print(f"build: {preset} built and tested; record in {record}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
