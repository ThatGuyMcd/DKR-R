#!/usr/bin/env python3
"""Run fast contributor checks without a ROM, C++ build, or dependency checkout."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TOOLING_TESTS = (
    "build_tooling_tests.py",
    "host_task_policy_tests.py",
    "legacy_mod_pipeline_tests.py",
    "tooling_utilities_tests.py",
    "source_packaging_tests.py",
    "developer_entrypoint_tests.py",
    "asset_scan_tests.py",
    "regression_harness_tests.py",
    "harness_report_tests.py",
    "replay_capture_tests.py",
    "ci_summary_tests.py",
    "stress_runner_tests.py",
)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", action="append", choices=("tooling", "metadata", "assets", "track-lab"),
                        help="select checks (repeatable); defaults to tooling, metadata and assets")
    parser.add_argument("--list", action="store_true", help="print selected checks without running them")
    args = parser.parse_args(argv)
    suites = args.suite or ["tooling", "metadata", "assets"]
    commands = {
        "tooling": [[sys.executable, f"runtime-recomp/tests/{test}"] for test in TOOLING_TESTS],
        "metadata": [[sys.executable, "scripts/validate_release_metadata.py"]],
        "assets": [[sys.executable, "scripts/scan_for_game_assets.py"]],
        "track-lab": [[sys.executable, "tools/blender/run_tests.py", "--skip-blender"]],
    }
    for suite in dict.fromkeys(suites):
        for command in commands[suite]:
            print(f"[{suite}] {' '.join(command[1:])}", flush=True)
            if args.list:
                continue
            try:
                result = subprocess.run(command, cwd=ROOT)
            except OSError as error:
                print(f"Could not run {command[1]}: {error}", file=sys.stderr)
                return 1
            if result.returncode:
                print(f"Failed: {command[1]} (exit {result.returncode})", file=sys.stderr)
                return result.returncode if result.returncode > 0 else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
