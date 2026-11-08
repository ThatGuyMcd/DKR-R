#!/usr/bin/env python3
"""Exercise the stress runner with real, ROM-free CTest fixtures."""

import argparse
import contextlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import stress_tests as runner


def ctest_supported():
    try:
        text = subprocess.check_output(["ctest", "--version"], text=True)
        match = re.search(r"(\d+)\.(\d+)\.(\d+)", text)
        return bool(match and tuple(map(int, match.groups())) >= (4, 1, 0))
    except (OSError, subprocess.SubprocessError):
        return False


CTEST_SUPPORTED = ctest_supported()


class StressTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="dkr stress ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.output = self.root / "results"

    def fixture(self, fail=False, disabled=False):
        # Use the actual installed CMake executable, portable across platforms.
        executable = Path(shutil.which("cmake")).as_posix()
        command = "false" if fail else "true"
        (self.build / "CTestTestfile.cmake").write_text(
            f'add_test(example "{executable}" -E {command})\n'
            'set_tests_properties(example PROPERTIES LABELS "flake")\n'
            + ("set_tests_properties(example PROPERTIES DISABLED TRUE)\n" if disabled else "")
        )

    def invoke(self, *options):
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/stress_tests.py"),
                "--build-dir",
                str(self.build),
                "--output",
                str(self.output),
                "--seed",
                "100",
                "--rounds",
                "2",
                "--test-regex",
                "example",
                *options,
            ],
            capture_output=True,
            text=True,
        )

    @unittest.skipUnless(CTEST_SUPPORTED, "Real seeded CTest fixtures require CTest >= 4.1")
    def test_real_pass_rounds_preserve_and_merge(self):
        self.fixture()
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual([r["seed"] for r in data["rounds"]], [100, 101])
        self.assertEqual(data["selected_tests"], ["example"])
        self.assertEqual(data["config"], "Release")
        self.assertEqual(Path(data["build_dir"]), self.build.resolve())
        discovery = json.loads((self.output / "ctest-tests.json").read_text())
        self.assertEqual(discovery["tests"][0]["name"], "example")
        self.assertTrue(discovery["tests"][0]["command"])
        self.assertTrue(discovery["tests"][0]["properties"])
        cases = list(ET.parse(self.output / "junit.xml").iter("testcase"))
        self.assertEqual([c.get("name") for c in cases], ["round-001: example", "round-002: example"])
        for number in (1, 2):
            folder = self.output / f"round-{number:03d}"
            self.assertTrue((folder / "ctest.log").is_file())
            self.assertTrue((folder / "CTestTemporary" / "LastTest.log").is_file())
        self.assertIn("2 passed", (self.output / "summary.md").read_text())
        self.assertNotEqual(self.invoke().returncode, 0, "must not overwrite evidence")

    @unittest.skipUnless(CTEST_SUPPORTED, "Real seeded CTest fixtures require CTest >= 4.1")
    def test_failure_stops_without_retry(self):
        self.fixture(fail=True)
        result = self.invoke()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        data = json.loads((self.output / "manifest.json").read_text())
        self.assertFalse(data["passed"])
        self.assertEqual(len(data["rounds"]), 1)
        self.assertFalse((self.output / "round-002").exists())
        self.assertIn("1 failed", (self.output / "summary.md").read_text())

    @unittest.skipUnless(CTEST_SUPPORTED, "Real seeded CTest fixtures require CTest >= 4.1")
    def test_disabled_not_false_pass(self):
        self.fixture(disabled=True)
        self.assertEqual(self.invoke().returncode, 1)

    @unittest.skipUnless(CTEST_SUPPORTED, "Real seeded CTest fixtures require CTest >= 4.1")
    def test_empty_selection_and_list(self):
        self.fixture()
        result = self.invoke("--test-regex", "^absent$")
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.output.exists())
        result = self.invoke("--list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("example", result.stdout)
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(CTEST_SUPPORTED, "Real seeded CTest fixtures require CTest >= 4.1")
    def test_missing_default_core_suite(self):
        self.fixture()
        result = self.invoke("--test-regex", "")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Default flake suite is incomplete", result.stderr)
        self.assertFalse(self.output.exists())

    def test_seed_validation(self):
        for seed in ("-1", "4294967296", "4294967295", "word"):
            self.assertEqual(self.invoke("--seed", seed).returncode, 2, seed)
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(CTEST_SUPPORTED, "Real seeded CTest fixtures require CTest >= 4.1")
    def test_missing_executable_preflight(self):
        (self.build / "CTestTestfile.cmake").write_text(
            'add_test(example "definitely_missing_dkr_fixture_executable")\n'
            'set_tests_properties(example PROPERTIES LABELS "flake")\n'
        )
        result = self.invoke()
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing executables", result.stderr)
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(CTEST_SUPPORTED, "Real seeded CTest fixtures require CTest >= 4.1")
    def test_missing_and_malformed_real_reports_fail_with_evidence(self):
        self.fixture()
        collect = runner.collect_junit
        for malformed in (False, True):
            output = self.root / str(malformed)

            def damage(path, selected, number):
                if malformed:
                    path.write_text("<broken")
                else:
                    path.unlink()
                return collect(path, selected, number)

            args = argparse.Namespace(
                build_dir=self.build,
                output=output,
                seed=10,
                rounds=2,
                parallel=2,
                test_regex="example",
                list=False,
                config="Release",
            )
            with mock.patch.object(runner, "collect_junit", side_effect=damage):
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(runner.run(args), 1)
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(len(manifest["rounds"]), 1)
            self.assertFalse(manifest["passed"])
            self.assertIn("Incomplete or invalid JUnit", manifest["rounds"][0]["error"])
            self.assertTrue((output / "round-001" / "ctest.log").is_file())
            self.assertIsNotNone(ET.parse(output / "junit.xml").find(".//error"))

    def test_invalid_junit_is_not_success(self):
        path = self.root / "report.xml"
        with self.assertRaises(OSError):
            runner.collect_junit(path, ["example"], 1)
        for value in (
            "<bad",
            "<wrong/>",
            "<testsuite/>",
            '<testsuite><testcase name="different"/></testsuite>',
            '<testsuite><testcase name="example"/><testcase name="example"/></testsuite>',
        ):
            path.write_text(value)
            with self.assertRaises((ValueError, ET.ParseError), msg=value):
                runner.collect_junit(path, ["example"], 1)


if __name__ == "__main__":
    unittest.main()
