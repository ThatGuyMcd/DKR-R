#!/usr/bin/env python3
"""Run seeded CTest flake rounds, stopping on failure and retaining evidence."""

import argparse
import copy
import json
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

from ci_summary import escape, junit_summary

MAX_SEED = 2**32 - 1
REQUIRED_TESTS = {
    "DKRDirectSession",
    "DKRFriendService",
    "DKRQuickJoinTransport",
    "DKRExperimentalSession",
    "DKRExperimentalNetwork",
    "DKRExperimentalLaunch",
    "DKRNetplayAdverseNetwork",
}

OPTIONAL_TESTS = {
    "DKRFriendDelivery",
    "DKRFriendLocalIntegration",
    "DKRQuickJoinLifecycle",
    "DKRExperimentalQuickJoin",
}


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def seed_value(value):
    number = int(value)
    if not 0 <= number <= MAX_SEED:
        raise argparse.ArgumentTypeError("seed must be an unsigned 32-bit integer")
    return number


def collect_junit(path, selected, number):
    text = path.read_text(encoding="utf-8")
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper():
        raise ValueError("JUnit DTD/entity declarations are unsupported")
    root = ET.fromstring(text)
    if root.tag not in ("testsuite", "testsuites"):
        raise ValueError("Expected JUnit testsuite or testsuites")
    cases = list(root.iter("testcase"))
    names = [case.get("name") for case in cases]
    if len(names) != len(selected) or set(names) != set(selected):
        raise ValueError("JUnit names do not match the full selected suite")
    suite = ET.Element("testsuite", name=f"Round {number}")
    failed = False
    for original in cases:
        case = copy.deepcopy(original)
        case.set("name", f"round-{number:03d}: {original.get('name')}")
        incomplete = any(case.find(tag) is not None for tag in ("failure", "error", "skipped")) or case.get(
            "status", "run"
        ) not in ("run", "passed", "pass")
        if incomplete:
            failed = True
        else:
            # Full stdout remains in per-round XML/logs; keep the aggregate small.
            for tag in ("system-out", "system-err"):
                for log in case.findall(tag):
                    case.remove(log)
        suite.append(case)
    # Validate timings before preserving this suite in the aggregate.
    junit_summary(ET.tostring(suite, encoding="unicode"))
    return suite, failed


def run(args):
    build = args.build_dir.resolve()
    if not build.is_dir():
        raise ValueError(f"Build directory does not exist: {build}")
    version = subprocess.check_output(["ctest", "--version"], text=True).splitlines()[0]
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", version)
    if not match or tuple(map(int, match.groups())) < (4, 1, 0):
        raise ValueError("CTest >= 4.1 is required for reproducible random seeds")
    command = ["ctest", "--test-dir", str(build), "-C", args.config, "-L", "^flake$"]
    if args.test_regex:
        command += ["-R", args.test_regex]
    discovery = subprocess.run([*command, "--show-only=json-v1"], capture_output=True, text=True, check=True)
    tests = json.loads(discovery.stdout)["tests"]
    selected = [test["name"] for test in tests]
    if not selected or len(set(selected)) != len(selected):
        raise ValueError("Selection must contain a nonempty set of distinct flake tests")
    if not args.test_regex:
        inventory = subprocess.run(
            ["ctest", "--test-dir", str(build), "-C", args.config, "--show-only=json-v1"],
            capture_output=True,
            text=True,
            check=True,
        )
        available = {test["name"] for test in json.loads(inventory.stdout)["tests"]}
        expected = REQUIRED_TESTS | (OPTIONAL_TESTS & available)
        if expected - set(selected):
            raise ValueError(
                "Default flake suite is incomplete; missing labels/tests: "
                + ", ".join(sorted(expected - set(selected)))
            )
    missing = [test["name"] for test in tests if not test.get("command")]
    if missing:
        raise ValueError("Selected tests have missing executables: " + ", ".join(missing))
    seed = args.seed if args.seed is not None else secrets.randbelow(MAX_SEED - args.rounds + 2)
    if seed + args.rounds - 1 > MAX_SEED:
        raise ValueError("Seed plus rounds exceeds the 32-bit seed range")
    print(f"{version}\nBase seed: {seed}\nSelected {len(selected)} tests:\n" + "\n".join(selected), flush=True)
    if args.list:
        return 0
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / "ctest-tests.json").write_text(discovery.stdout, encoding="utf-8")
    manifest = dict(
        schema=1,
        ctest_version=version,
        config=args.config,
        build_dir=str(build),
        seed=seed,
        requested_rounds=args.rounds,
        parallel=args.parallel,
        selected_tests=selected,
        rounds=[],
    )
    aggregate = ET.Element("testsuites")
    status = 0
    for number in range(1, args.rounds + 1):
        folder = output / f"round-{number:03d}"
        folder.mkdir()
        junit = folder / "junit.xml"
        invocation = [
            *command,
            "--parallel",
            str(args.parallel),
            "--schedule-random",
            "--schedule-random-seed",
            str(seed + number - 1),
            "--no-tests=error",
            "--output-on-failure",
            "--output-junit",
            str(junit),
        ]
        record = dict(round=number, seed=seed + number - 1, command=invocation)
        manifest["rounds"].append(record)
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"Round {number}/{args.rounds}, seed {record['seed']}", flush=True)
        with (folder / "ctest.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(invocation, stdout=log, stderr=subprocess.STDOUT, text=True)
        record["returncode"] = result.returncode
        temporary = build / "Testing" / "Temporary"
        if temporary.is_dir():
            shutil.copytree(temporary, folder / "CTestTemporary")
        failed = False
        try:
            suite, failed = collect_junit(junit, selected, number)
            aggregate.append(suite)
            if failed:
                record["error"] = "Selected tests failed, skipped, or did not run"
        except (OSError, ValueError, ET.ParseError) as error:
            record["error"] = f"Incomplete or invalid JUnit: {error}"
        record["passed"] = result.returncode == 0 and "error" not in record
        if not record["passed"]:
            status = 1
            if not failed:
                # Make infrastructure failures visible to JUnit-only consumers.
                suite = ET.SubElement(aggregate, "testsuite", name=f"Round {number} infrastructure")
                case = ET.SubElement(suite, "testcase", name=f"round-{number:03d}: CTest evidence", time="0")
                ET.SubElement(case, "error").text = record.get("error", f"CTest exited {result.returncode}")
            print((folder / "ctest.log").read_text(encoding="utf-8", errors="replace"), end="")
            break
    manifest["passed"] = status == 0
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    ET.indent(aggregate)
    ET.ElementTree(aggregate).write(output / "junit.xml", encoding="utf-8", xml_declaration=True)
    lines = [
        "## Flake stress tests",
        "",
        f"Base seed: {seed}; attempted {len(manifest['rounds'])}/{args.rounds} rounds.",
        f"Result: {'passed' if not status else 'FAILED (stopped at first failed round)'}.",
        "",
        "Seeds reproduce CTest scheduling order, not operating-system thread scheduling.",
        "",
    ]
    for record in manifest["rounds"]:
        lines.append(
            f"- Round {record['round']}, seed {record['seed']}: " + ("passed" if record["passed"] else "FAILED")
        )
        if "error" in record:
            lines.append("  " + escape(record["error"], 1000))
    lines += ["", *junit_summary(ET.tostring(aggregate, encoding="unicode"))]
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Evidence: {output}", flush=True)
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--rounds", type=positive, default=5)
    parser.add_argument("--parallel", type=positive, default=2)
    parser.add_argument("--seed", type=seed_value)
    parser.add_argument("--output", type=Path, default=Path("build/stress"))
    parser.add_argument("--test-regex")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.rounds > MAX_SEED + 1:
        parser.error("Too many rounds for the seed range")
    try:
        return run(args)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"stress_tests: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
