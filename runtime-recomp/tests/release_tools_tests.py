#!/usr/bin/env python3
"""scripts/release/: finalize.py refuses a release unless every required check
ran and passed and none is pending (and the real registry still has pending
C5 gates, so it refuses today); the package checks catch pre-release labels;
the release steps read their version from VERSION."""

import contextlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/release"))
import check_docs  # noqa: E402
import evidence  # noqa: E402
import finalize  # noqa: E402
import lock_inputs  # noqa: E402
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


class InputLockTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def tree(self, name, files):
        base = self.root / name
        for relative, data in files.items():
            (base / relative).parent.mkdir(parents=True, exist_ok=True)
            (base / relative).write_bytes(data)
        return base

    def test_line_endings_and_provenance_do_not_count(self):
        lf = self.tree("lf", {"funcs_0.c": b"int a;\nint b;\n", "sub/x.h": b"#pragma once\n", "manifest.json": b"{}"})
        crlf = self.tree(
            "crlf", {"funcs_0.c": b"int a;\r\nint b;\r\n", "sub/x.h": b"#pragma once\r\n", "manifest.json": b"[1]"}
        )
        self.assertEqual(lock_inputs.digest(lf), lock_inputs.digest(crlf))
        changed = self.tree("changed", {"funcs_0.c": b"int a;\nint c;\n", "sub/x.h": b"#pragma once\n"})
        self.assertNotEqual(lock_inputs.digest(lf), lock_inputs.digest(changed))
        renamed = self.tree("renamed", {"funcs_1.c": b"int a;\nint b;\n", "sub/x.h": b"#pragma once\n"})
        self.assertNotEqual(lock_inputs.digest(lf), lock_inputs.digest(renamed))

    @unittest.skipUnless(shutil.which("cmake"), "needs cmake")
    def test_cmake_computes_the_same_digest(self):
        base = self.tree("in", {"b.c": b"int b;\r\n", "a/z.h": b"x\n", "a/y.toml": b"path"})
        script = self.root / "digest.cmake"
        script.write_text(
            f'include("{(ROOT / "runtime-recomp/cmake/BuildInputs.cmake").as_posix()}")\n'
            f'dkr_input_digest(d "{base.as_posix()}")\nmessage("DIGEST=${{d}}")\n'
        )
        done = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True, check=True)
        self.assertIn(f"DIGEST={lock_inputs.digest(base)}", done.stderr)

    def test_the_lock_names_every_prepared_input(self):
        lock = json.loads(lock_inputs.LOCK.read_text())
        self.assertEqual(sorted(lock["inputs"]), sorted(lock_inputs.INPUTS))
        self.assertEqual(set(lock["pins"]), {"n64recomp", "dkr-decomp"})


class EvidenceTests(unittest.TestCase):
    def test_machine_paths_become_placeholders(self):
        norm = evidence.Paths(Path("/work/tree/build/release-linux"), Path("/work/tree/build/prepared"))
        self.assertEqual(norm("/work/tree/build/prepared/owned-v77/x.c"), "$PREPARED/owned-v77/x.c")
        self.assertEqual(norm("/work/tree/build/release-linux/gen.c"), "$BUILD/gen.c")

    def test_compare_finds_source_identity_and_commit_differences(self):
        base = {
            "version": "1", "tag": "Version1", "commit": "c", "identity": {"A": "1"}, "inputs": {"locked": {}},
            "targets": {"T": {"sources": {"$ROOT/a.cpp": ["CXX", "X=1"]}}},
        }  # fmt: skip
        same = json.loads(json.dumps(base))
        self.assertEqual(evidence.compare(base, same, False), [])
        other = json.loads(json.dumps(base))
        other["targets"]["T"]["sources"]["$ROOT/a.cpp"] = ["CXX", "X=2"]
        other["identity"] = {"A": "2"}
        other["commit"] = "d"
        problems = evidence.compare(base, other, False)
        self.assertIn("identity differs", problems)
        self.assertIn("commit differs", problems)
        self.assertIn("T: $ROOT/a.cpp: language or definitions differ", problems)
        self.assertNotIn("commit differs", evidence.compare(base, other, True))


class TagTests(unittest.TestCase):
    def test_the_tag_must_be_version_of_version(self):
        script = ROOT / "scripts/validate_release_metadata.py"
        v = release_common.version()
        ok = subprocess.run([sys.executable, str(script), "--tag", f"Version{v}"], capture_output=True, text=True)
        self.assertEqual(ok.returncode, 0, ok.stderr)
        bad = subprocess.run([sys.executable, str(script), "--tag", f"v{v}"], capture_output=True, text=True)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("doesn't match VERSION", bad.stderr)


if __name__ == "__main__":
    unittest.main()
