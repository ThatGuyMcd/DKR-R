#!/usr/bin/env python3
"""Build the maintained API modules and verify fresh bundles and rollback bytes."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("online_directory_build", HERE.parent / "build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def run(*argv):
    errors = io.StringIO()
    with contextlib.redirect_stderr(errors), contextlib.redirect_stdout(io.StringIO()):
        status = build.main(list(argv))
    return status, errors.getvalue()


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_native_modules_bundle_with_exact_rollback_inputs(self):
        previous = self.root / "previous.mjs"
        previous.write_bytes(b"export default {};\n")
        out = self.root / "bundle"
        status, error = run("--output", str(out), "--rollback-api", str(previous))
        self.assertEqual(status, 0, error)
        inputs = json.loads((out / "API-BUILD-INPUTS.json").read_text())["inputs"]
        self.assertEqual({Path(name).name for name in inputs}, {"worker.mjs", "library_api.mjs", "profile_api.mjs"})
        self.assertEqual((out / "ROLLBACK/worker-single-file.mjs").read_bytes(), previous.read_bytes())
        self.assertTrue((out / "PRIVATE-API/002-launcher-profiles.sql").is_file())
        self.assertEqual(run("--verify", "--output", str(out))[0], 0)
        (out / "PRIVATE-API/worker-single-file.mjs").write_text("changed")
        self.assertIn("Bundle file changed", run("--verify", "--output", str(out))[1])

    def test_cli_output_is_relative_to_the_caller(self):
        result = subprocess.run(
            [sys.executable, str(HERE.parent / "build.py"), "--output", "bundle"],
            cwd=self.root,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / "bundle/PRIVATE-API/worker-single-file.mjs").is_file())

    def test_wrong_bundler_version_cannot_create_a_bundle(self):
        out = self.root / "bundle"
        status, error = run("--output", str(out), "--esbuild", sys.executable)
        self.assertEqual(status, 1)
        self.assertIn("pinned esbuild", error)
        self.assertFalse(out.exists())

    def test_existing_output_is_preserved(self):
        out = self.root / "bundle"
        out.mkdir()
        marker = out / "owner-file"
        marker.write_text("preserve")
        self.assertEqual(run("--output", str(out))[0], 1)
        self.assertEqual(marker.read_text(), "preserve")

    def test_failed_compilation_cannot_produce_a_verified_receipt(self):
        out = self.root / "bundle"
        with patch.object(build.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "esbuild")):
            self.assertEqual(run("--output", str(out))[0], 1)
        self.assertFalse((out / "BUNDLE-RECEIPT.json").exists())


if __name__ == "__main__":
    unittest.main()
