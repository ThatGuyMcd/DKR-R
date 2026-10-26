#!/usr/bin/env python3
"""Write and compare RELEASE-EVIDENCE.json.

    evidence.py write --record DIR --output JSON [--packages DIR]
    evidence.py compare A.json B.json [--allow-commit-difference]

`write` reads a build.py record and its build directory: the commit, tag,
VERSION and tree state; the online identity constants
(release_common.identity); the prepared input digests (locked and found);
every target's compiled sources and compile definitions from the CMake File
API codemodel, with machine paths replaced by $ROOT, $BUILD and $PREPARED;
the CTest summary; and, with --packages (finalize.py's handoff), each
package's name, size and sha256.

`compare` checks two evidence files describe the same release: identity,
input digests, sources and definitions must match, and so must the commit
unless --allow-commit-difference. Artifact hashes are reported, not required.

Exit status: 0 written or matching, 1 a difference or a missing input.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any, cast
import xml.etree.ElementTree as ElementTree

sys.dont_write_bytecode = True
import lock_inputs  # noqa: E402
from release_common import ROOT, identity, sha, tag, version  # noqa: E402

Json = dict[str, Any]


class Paths:
    """Machine paths, as placeholders."""

    def __init__(self, build: Path, prepared: Path):
        pairs = ((prepared.as_posix(), "$PREPARED"), (build.as_posix(), "$BUILD"), (ROOT.as_posix(), "$ROOT"))
        self.pairs = sorted(pairs, key=lambda pair: -len(pair[0]))

    def __call__(self, text: str) -> str:
        text = text.replace("\\", "/")
        for path, name in self.pairs:
            text = text.replace(path, name)
        return text


def codemodel(build: Path, norm: Paths) -> dict[str, Json]:
    reply = build / ".cmake/api/v1/reply"
    index = json.loads(sorted(reply.glob("index-*.json"))[-1].read_text(encoding="utf-8"))
    model = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text(encoding="utf-8"))
    source = Path(model["paths"]["source"])
    targets: dict[str, Json] = {}
    for entry in model["configurations"][0]["targets"]:
        target = json.loads((reply / entry["jsonFile"]).read_text(encoding="utf-8"))
        groups = target.get("compileGroups", [])
        sources: dict[str, list[str]] = {}
        for item in target.get("sources", []):
            if "compileGroupIndex" not in item:
                continue
            path = Path(item["path"])
            path = path if path.is_absolute() else source / path
            group = groups[item["compileGroupIndex"]]
            defines = sorted(norm(d["define"]) for d in group.get("defines", []))
            sources[norm(path.as_posix())] = [group["language"], *defines]
        if sources:
            targets[target["name"]] = {"sources": dict(sorted(sources.items()))}
    return dict(sorted(targets.items()))


def ctest_summary(path: Path) -> Json:
    if not path.is_file():
        return {"present": False}
    cases = list(ElementTree.parse(path).getroot().iter("testcase"))
    failed = [c.get("name", "") for c in cases if c.find("failure") is not None or c.get("status") == "fail"]
    skipped = [c.get("name", "") for c in cases if c.find("skipped") is not None or c.get("status") == "notrun"]
    return {"present": True, "tests": len(cases), "failed": sorted(failed), "skipped": sorted(skipped)}


def cache_value(build: Path, name: str) -> str | None:
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(name + ":"):
            return line.split("=", 1)[1]
    return None


def artifacts(folder: Path) -> list[Json]:
    return [
        cast(Json, json.loads(receipt.read_text())["artifact"])
        for receipt in sorted((folder / "Evidence").glob("*-BUILD-RECEIPT.json"))
    ]


def write(record: Path, output: Path, packages: Path | None) -> None:
    built = cast(Json, json.loads((record / "BUILD.json").read_text()))
    build, prepared = Path(built["build"]), Path(built["prepared"])
    lock = cast(Json, json.loads(lock_inputs.LOCK.read_text(encoding="utf-8")))
    evidence: Json = {
        "schema": 1,
        "version": version(),
        "tag": tag(),
        "commit": built["commit"],
        "tag_at_head": built["tag_at_head"],
        "clean_tree": built["clean_tree"],
        "platform": built["platform"],
        "preset": built["preset"],
        "identity": identity(),
        "inputs": {"locked": lock["inputs"], "found": lock_inputs.current(prepared), "pins": lock["pins"]},
        "verify_build_inputs": cache_value(build, "DKR_VERIFY_BUILD_INPUTS"),
        "targets": codemodel(build, Paths(build, prepared)),
        "ctest": ctest_summary(record / "ctest.xml"),
        "artifacts": artifacts(packages) if packages else [],
        "steps": built["steps"],
    }
    output.write_text(json.dumps(evidence, indent=1) + "\n")
    print(f"evidence: {output} ({len(evidence['targets'])} targets, sha256 {sha(output)})")


def compare(a: Json, b: Json, allow_commit: bool) -> list[str]:
    problems: list[str] = []
    for key in ["version", "tag", "identity", "inputs"] + ([] if allow_commit else ["commit"]):
        if a.get(key) != b.get(key):
            problems.append(f"{key} differs")
    ta, tb = cast(dict[str, Json], a["targets"]), cast(dict[str, Json], b["targets"])
    for name in sorted(set(ta) | set(tb)):
        if name not in ta or name not in tb:
            problems.append(f"target {name} only in {'B' if name not in ta else 'A'}")
            continue
        sa, sb = cast(dict[str, list[str]], ta[name]["sources"]), cast(dict[str, list[str]], tb[name]["sources"])
        for source in sorted(set(sa) | set(sb)):
            if source not in sa or source not in sb:
                problems.append(f"{name}: {source}: only in {'B' if source not in sa else 'A'}")
            elif sa[source] != sb[source]:
                problems.append(f"{name}: {source}: language or definitions differ")
    return problems


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    w = sub.add_parser("write")
    w.add_argument("--record", type=Path, required=True)
    w.add_argument("--output", type=Path, required=True)
    w.add_argument("--packages", type=Path)
    c = sub.add_parser("compare")
    c.add_argument("a", type=Path)
    c.add_argument("b", type=Path)
    c.add_argument("--allow-commit-difference", action="store_true")
    a = p.parse_args(argv)
    if a.command == "write":
        write(a.record.resolve(), a.output.resolve(), a.packages.resolve() if a.packages else None)
        return 0
    left, right = (cast(Json, json.loads(path.read_text())) for path in (a.a, a.b))
    problems = compare(left, right, a.allow_commit_difference)
    for problem in problems[:200]:
        print(f"evidence: {problem}", file=sys.stderr)
    artifacts_a = {x["name"]: x["sha256"] for x in left.get("artifacts", [])}
    artifacts_b = {x["name"]: x["sha256"] for x in right.get("artifacts", [])}
    same = sorted(n for n in artifacts_a if artifacts_b.get(n) == artifacts_a[n])
    print(f"evidence: {len(problems)} differences; identical artifacts: {', '.join(same) or 'none'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
