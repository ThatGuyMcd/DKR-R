#!/usr/bin/env python3
"""scripts/release/: finalize.py refuses a release unless every required check
ran and passed and none is pending (and the real registry still has pending
C5 gates, so it refuses today); the package checks catch pre-release labels;
the release steps read their version from VERSION."""

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/release"))
import check_docs  # noqa: E402
import finalize  # noqa: E402
import package_release  # noqa: E402
import qualify_updates  # noqa: E402
import release_common  # noqa: E402


def junit(cases):
    """A CTest JUnit file: (name, labels, outcome) per test."""
    rows = []
    for name, labels, outcome in cases:
        body = f'<properties><property name="cmake_labels" value="{";".join(labels)}"/></properties>'
        status = {"passed": "run", "failed": "fail", "skipped": "notrun"}[outcome]
        extra = {"passed": "", "failed": "<failure/>", "skipped": "<skipped/>"}[outcome]
        rows.append(f'<testcase name="{name}" status="{status}">{body}{extra}</testcase>')
    return f'<?xml version="1.0"?><testsuite>{"".join(rows)}</testsuite>'


class RequiredChecksTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def registry(self, text):
        path = self.root / "required_checks.toml"
        path.write_text(textwrap.dedent(text))
        return finalize.registry(path)

    def runs(self, cases):
        path = self.root / "ctest.xml"
        path.write_text(junit(cases))
        results = finalize.ctest_results(path)
        return {platform: results for platform in finalize.PLATFORMS}

    def test_the_real_registry_refuses_with_its_pending_checks_named(self):
        checks = finalize.registry()
        pending = [c for c in checks if c.pending]
        self.assertEqual(len(pending), 15)
        self.assertTrue(all(c.pending == "C5" for c in pending))
        everything = [(c.test or f"{c.label}-test", [c.label] if c.label else [], "passed") for c in checks]
        with self.assertRaises(finalize.FinalizeError) as caught:
            finalize.verify_checks(checks, self.runs(everything))
        message = str(caught.exception)
        self.assertIn("still pending", message)
        for check in pending:
            self.assertIn(check.id, message)

    def test_a_missing_or_failed_check_fails(self):
        checks = self.registry(
            """
            [[check]]
            id = "runtime"
            ctest_label = "runtime"
            why = "w"
            [[check]]
            id = "one"
            ctest_test = "DKROne"
            why = "w"
            """
        )
        with self.assertRaisesRegex(finalize.FinalizeError, "one did not run"):
            finalize.verify_checks(checks, self.runs([("DKRA", ["runtime"], "passed")]))
        with self.assertRaisesRegex(finalize.FinalizeError, "runtime has tests that didn't pass: DKRB"):
            finalize.verify_checks(
                checks,
                self.runs([("DKRA", ["runtime"], "passed"), ("DKRB", ["runtime"], "failed"), ("DKROne", [], "passed")]),
            )
        with self.assertRaisesRegex(finalize.FinalizeError, "didn't pass: DKROne"):
            finalize.verify_checks(checks, self.runs([("DKRA", ["runtime"], "passed"), ("DKROne", [], "skipped")]))

    def test_a_pending_check_fails_even_when_everything_else_passed(self):
        checks = self.registry(
            """
            [[check]]
            id = "runtime"
            ctest_label = "runtime"
            why = "w"
            [[check]]
            id = "fixture"
            ctest_test = "DKRFixture"
            why = "the old fixture"
            pending = "C5"
            """
        )
        with self.assertRaisesRegex(finalize.FinalizeError, r"still pending.*\n  fixture \(C5\): the old fixture"):
            finalize.verify_checks(checks, self.runs([("DKRA", ["runtime"], "passed"), ("DKRFixture", [], "passed")]))

    def test_all_checks_passed_and_none_pending_succeeds(self):
        checks = self.registry(
            """
            [[check]]
            id = "runtime"
            ctest_label = "runtime"
            why = "w"
            """
        )
        report = finalize.verify_checks(checks, self.runs([("DKRA", ["host", "runtime"], "passed")]))
        self.assertEqual(report["windows"], {"runtime": 1})

    def test_an_entry_needs_exactly_one_selector(self):
        with self.assertRaisesRegex(finalize.FinalizeError, "exactly one"):
            self.registry('[[check]]\nid = "x"\nwhy = "w"\n')


class PackageCheckTests(unittest.TestCase):
    def test_prerelease_labels_are_found_in_narrow_and_wide_text(self):
        for label in ("1.0.7+beta.10", "1.0.10-vehicle-playtest.3"):
            for data in (b"\0x" + label.encode() + b"\0", b"\0" + label.encode("utf-16-le")):
                with self.subTest(label=label, wide=len(data) > 20):
                    self.assertIsNotNone(package_release.prerelease_label(data))
        self.assertIsNone(package_release.prerelease_label(b"DKR-R 1.0.11 Release Build\0"))

    def test_the_runtime_must_carry_both_contracts(self):
        data = b"".join(package_release.FEATURES)
        with self.assertRaisesRegex(ValueError, "Rollback contract"):
            package_release.features(data)
        ids = release_common.contracts()
        self.assertEqual(set(ids), {"77", "80"})
        self.assertEqual(package_release.features(data + b"".join(v.encode() for v in ids.values())), ids)


class ReleaseIdentityTests(unittest.TestCase):
    def test_names_follow_the_version_file(self):
        v = (ROOT / "VERSION").read_text().strip()
        self.assertEqual(release_common.version(), v)
        self.assertEqual(release_common.tag(), "Version" + v)
        self.assertEqual(release_common.packages()["windows-x64"][2], f"DKR-R-{v}-Windows-x64.zip")
        self.assertTrue((release_common.release_docs() / "RELEASE-NOTES.txt").is_file())

    def test_qualify_updates_refuses_a_package_that_differs_from_its_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            previous = Path(temp) / "DKR-R-1.0.10-Windows-x64.zip"
            previous.write_bytes(b"zip")
            receipt = Path(temp) / "receipt.json"
            artifact = {"name": previous.name, "sha256": "0" * 64}
            receipt.write_text(json.dumps({"version": "1.0.10", "platform": "windows", "artifact": artifact}))
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                status = qualify_updates.main(["--previous", str(previous), "--previous-receipt", str(receipt)])
        self.assertEqual(status, 1)
        self.assertIn("differs from its receipt", err.getvalue())

    def test_doc_dispositions(self):
        v = release_common.version()
        self.assertEqual(check_docs.disposition(f"docs/releases/{v}/RELEASE-NOTES.txt", set()), "release-notes")
        self.assertEqual(check_docs.disposition("docs/BUILDING.md", {"docs/BUILDING.md"}), "updated-current-document")
        self.assertEqual(check_docs.disposition("docs/releases/1.0.0/X.md", set()), "historical-record-preserved")


if __name__ == "__main__":
    unittest.main()
