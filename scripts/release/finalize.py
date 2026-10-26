#!/usr/bin/env python3
"""Collect the verified desktop packages into the handoff folder for signing.

    finalize.py --work DIR --output DIR

WORK holds, per platform (windows, linux, arm64):

* packages/<platform>/: package_release.py's output (the package,
  BUILD-RECEIPT.json, ARCHIVE-REPORT.json);
* <platform>/ctest.xml: the release build's CTest run
  (ctest --output-junit), checked against required_checks.toml;
* <platform>/packaged-worker-probe/PROBE-RECEIPT.json (probe_worker.py);
* <platform>/elf-inspection.json for Linux (inspect_elf.py);

and evidence/ with check_docs.py's reports. Every package must be this
VERSION with the release notes and README it was packaged with, the same
rollback contracts on every platform, read back exactly, and its worker must
pass the probe. Every check in required_checks.toml must have run and passed
on every platform, and none may still be pending.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import shutil
import sys
from typing import Any
import xml.etree.ElementTree as ElementTree

import tomllib

sys.dont_write_bytecode = True
from release_common import ROOT, release_docs, sha, steam_deck, version  # noqa: E402

REGISTRY = Path(__file__).resolve().parent / "required_checks.toml"
PLATFORMS = {"windows": "x86_64", "linux": "x86_64", "arm64": "aarch64"}
GLIBC, GLIBCXX = "2.38", "3.4.32"


class FinalizeError(ValueError):
    pass


def need(ok: bool, why: str) -> None:
    if not ok:
        raise FinalizeError(why)


@dataclass(frozen=True)
class Check:
    id: str
    why: str
    label: str | None = None
    test: str | None = None
    pending: str | None = None


def registry(path: Path = REGISTRY) -> list[Check]:
    checks: list[Check] = []
    for entry in tomllib.loads(path.read_text(encoding="utf-8"))["check"]:
        label, test = entry.get("ctest_label"), entry.get("ctest_test")
        if (label is None) == (test is None):
            raise FinalizeError(f"{path.name}: check {entry['id']} needs exactly one of ctest_label, ctest_test")
        checks.append(Check(entry["id"], entry["why"], label, test, entry.get("pending")))
    return checks


def ctest_results(path: Path) -> dict[str, tuple[str, set[str]]]:
    """CTest JUnit: test name -> (outcome, labels). outcome is passed,
    failed or skipped."""
    results: dict[str, tuple[str, set[str]]] = {}
    for case in ElementTree.parse(path).getroot().iter("testcase"):
        labels: set[str] = set()
        for prop in case.iter("property"):
            if prop.get("name") == "cmake_labels":
                labels.update(filter(None, (prop.get("value") or "").split(";")))
        if case.find("failure") is not None or case.find("error") is not None or case.get("status") == "fail":
            outcome = "failed"
        elif case.find("skipped") is not None or case.get("status") in ("notrun", "disabled"):
            outcome = "skipped"
        else:
            outcome = "passed"
        results[case.get("name", "")] = (outcome, labels)
    return results


def verify_checks(checks: list[Check], runs: dict[str, dict[str, tuple[str, set[str]]]]) -> dict[str, Any]:
    """Every non-pending check ran and passed on every platform, and nothing
    is pending. Returns what ran, per platform and check."""
    pending = [c for c in checks if c.pending]
    report: dict[str, Any] = {}
    problems: list[str] = []
    for platform, results in runs.items():
        report[platform] = {}
        for check in checks:
            if check.pending:
                continue
            if check.label is not None:
                tests = {name: outcome for name, (outcome, labels) in results.items() if check.label in labels}
            else:
                tests = {check.test: results[check.test][0]} if check.test in results else {}
            failed = sorted(name for name, outcome in tests.items() if outcome != "passed")
            if not tests:
                problems.append(f"{platform}: {check.id} did not run")
            elif failed:
                problems.append(f"{platform}: {check.id} has tests that didn't pass: {', '.join(failed[:5])}")
            report[platform][check.id] = len(tests)
    need(not problems, "Required checks failed:\n  " + "\n  ".join(problems))
    need(
        not pending,
        "Required checks still pending, so no release can be finalized from this tree yet:\n  "
        + "\n  ".join(f"{c.id} ({c.pending}): {c.why}" for c in pending),
    )
    return report


def verify_package(work: Path, platform: str, arch: str) -> dict[str, Any]:
    folder = work / "packages" / platform
    receipt = json.loads((folder / "BUILD-RECEIPT.json").read_text())
    item = receipt["artifact"]
    artifact = folder / item["name"]
    need(
        receipt["version"] == version()
        and receipt["arch"] == arch
        and sha(artifact) == item["sha256"]
        and artifact.stat().st_size == item["bytes"],
        "Stale release artifact: " + platform,
    )
    need(
        receipt["files"]["RELEASE-NOTES.txt"]["sha256"] == sha(release_docs() / "RELEASE-NOTES.txt"),
        "Packaged release notes differ: " + platform,
    )
    need(
        receipt["files"]["README.txt"]["sha256"] == sha(ROOT / "packaging/RELEASE-README.md"),
        "Packaged README differs: " + platform,
    )
    archive = json.loads((folder / "ARCHIVE-REPORT.json").read_text())
    need(
        archive["exact_payload_match"] and archive["artifact_sha256"] == sha(artifact),
        "Archive read-back failed: " + platform,
    )
    probe = json.loads((work / platform / "packaged-worker-probe/PROBE-RECEIPT.json").read_text())
    worker = "DKR-R-ModWorker.exe" if platform == "windows" else "DKR-R-ModWorker"
    packaged = [row["sha256"] for name, row in receipt["files"].items() if name.endswith("/" + worker)]
    need(packaged == [probe["worker_sha256"]], "Packaged worker hash differs: " + platform)
    need(
        probe["online_prepare_batch"] == 1 and not probe["roms_or_mods_loaded"], "Worker capability failed: " + platform
    )
    if platform != "windows":
        elf = json.loads((work / platform / "elf-inspection.json").read_text())
        need(
            not elf["missing_non_system_dependencies"] and elf["arch"] == arch,
            "Linux ELF closure incomplete: " + platform,
        )
        need(elf["max_glibc"] == GLIBC and elf["max_glibcxx"] == GLIBCXX, "Linux requirements changed: " + platform)
    return receipt


def finalize(work: Path, output: Path) -> None:
    need(not output.exists(), "Choose a fresh release handoff folder")
    checks = registry()
    receipts = {platform: verify_package(work, platform, arch) for platform, arch in PLATFORMS.items()}
    ids = {json.dumps(r["rollback_build_ids"], sort_keys=True) for r in receipts.values()}
    need(len(ids) == 1, "Rollback contracts differ across platforms")
    runs = {platform: ctest_results(work / platform / "ctest.xml") for platform in PLATFORMS}
    ran = verify_checks(checks, runs)
    output.mkdir(parents=True)
    evidence = output / "Evidence"
    evidence.mkdir()
    for platform, r in receipts.items():
        folder = work / "packages" / platform
        shutil.copy2(folder / r["artifact"]["name"], output / r["artifact"]["name"])
        shutil.copy2(folder / "BUILD-RECEIPT.json", evidence / f"{platform.upper()}-BUILD-RECEIPT.json")
        shutil.copy2(folder / "ARCHIVE-REPORT.json", evidence / f"{platform.upper()}-ARCHIVE-REPORT.json")
        target = evidence / platform
        for path in (work / platform).rglob("*"):
            if path.is_file() and path.suffix in (".json", ".log", ".xml"):
                copy = target / path.relative_to(work / platform)
                copy.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, copy)
    shutil.copy2(output / receipts["linux"]["artifact"]["name"], output / steam_deck())
    for path in (work / "evidence").glob("*"):
        if path.is_file() and path.suffix in (".log", ".json"):
            shutil.copy2(path, evidence / path.name)
    for name in ("DOCUMENTATION-CHECKS.json", "DOCUMENTATION-INVENTORY.json"):
        need((evidence / name).is_file(), "Missing documentation evidence (check_docs.py): " + name)
    docs = release_docs()
    for source, name in (
        (docs / "RELEASE-NOTES.txt", "RELEASE-NOTES.txt"),
        (docs / "RELEASE-NOTES.txt", f"DKR-R-v{version()}-Changelog.txt"),
        (docs / "UPDATER.txt", "UPDATER.txt"),
        (docs / "pages/RELEASE-README.md", "README.txt"),
    ):
        shutil.copy2(source, output / name)
    (evidence / "REQUIRED-CHECKS.json").write_text(json.dumps(ran, indent=2) + "\n")
    lines = [f"DKR-R v{version()} Release Build", ""]
    lines += [f"PASS: {', '.join(PLATFORMS)} packaged, read back exactly, worker probes passed."]
    lines += ["PASS: Linux ELF closures and requirements (GLIBC 2.38, GLIBCXX 3.4.32)."]
    lines += ["PASS: The same rollback contracts on every platform. Steam Deck is the Linux x86_64 package."]
    for check in checks:
        counts = ", ".join(f"{platform} {ran[platform][check.id]}" for platform in PLATFORMS)
        lines.append(f"PASS: {check.id} ({counts} tests): {check.why}")
    lines += ["", "No game, user ROM, RSP/GPU workload or live multiplayer was executed.", "Nothing is published.", ""]
    (output / "VALIDATION.txt").write_text("\n".join(lines))
    print("Verified release handoff prepared:", output)


def main(argv: list[str] | None = None) -> int:
    import argparse

    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("work", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args(argv)
    try:
        finalize(a.work.resolve(), a.output.resolve())
    except (FinalizeError, OSError, KeyError, ElementTree.ParseError) as error:
        print(f"finalize: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
