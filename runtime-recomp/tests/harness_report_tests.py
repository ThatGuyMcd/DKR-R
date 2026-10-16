#!/usr/bin/env python3
"""Exercise report portability, failure semantics and untrusted diagnostics."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from harness_report import write_reports


class HarnessReportTests(unittest.TestCase):
    def report(self):
        return {
            "schema_version": 1,
            "suite": "Suite <script>alert(1)</script>",
            "started_at": "2026-10-11T10:00:00Z",
            "identity": {"revision": "a&b"},
            "summary": {"passed": 1, "failed": 1, "skipped": 1},
            "scenarios": [
                {"id": "pass", "status": "passed", "metrics": {"wall_ms": 12.5}, "attempts": [
                    {"index": 1, "phase": "measured", "status": "passed", "elapsed_ms": 12.5,
                     "exit_code": 0, "stdout": "logs/log one.txt", "stderr": "logs/stderr.txt"}]},
                {"id": "failure", "status": "failed", "errors": ["bad <tag>\x1b"], "comparisons": [
                    {"metric": "time", "baseline": 1, "current": 2, "limit": 1.1, "status": "failed"}]},
                {"id": "skip", "status": "skipped", "errors": ["ROM absent"]},
            ],
        }

    def test_reports_preserve_results_and_escape_markup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = self.report()
            write_reports(root, report)
            self.assertEqual(json.loads((root / "report.json").read_text()), report)
            html = (root / "report.html").read_text()
            self.assertNotIn("<script>", html)
            self.assertIn("&lt;script&gt;", html)
            self.assertIn('href="logs/log%20one.txt"', html)
            self.assertIn("not frame time", html)
            xml = ET.parse(root / "junit.xml").getroot()
            self.assertEqual(xml.attrib["failures"], "1")
            self.assertEqual(xml.attrib["skipped"], "1")
            cases = {item.attrib["name"]: item for item in xml.findall("testcase")}
            self.assertIsNone(cases["pass"].find("failure"))
            self.assertIn("baseline=1", cases["failure"].find("failure").text)
            self.assertEqual(cases["skip"].find("skipped").attrib["message"], "ROM absent")

    def test_log_links_reject_active_and_escaping_paths(self):
        for path in ["javascript:alert(1)", "https://example.org", "//example.org", "../secret", "/secret", "logs/../../secret", "C:\\secret", "//[bad", "\nmalicious"]:
            with self.subTest(path=path), tempfile.TemporaryDirectory() as directory:
                report = self.report()
                report["scenarios"][0]["attempts"][0]["stdout"] = path
                write_reports(Path(directory), report)
                html = (Path(directory) / "report.html").read_text()
                self.assertIn("stdout unavailable", html)

    def test_nonfinite_metrics_fail_without_partial_output(self):
        for value in [float("nan"), float("inf"), float("-inf")]:
            with self.subTest(value=value), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "report"
                report = self.report()
                report["scenarios"][0]["metrics"]["bad"] = value
                with self.assertRaises(ValueError):
                    write_reports(output, report)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
