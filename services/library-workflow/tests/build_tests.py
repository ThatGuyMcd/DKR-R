#!/usr/bin/env python3
"""services/library-workflow/build.py: stages over baseline/, refuses a base
that differs from its SHA256SUMS, and fails when an anchor is missing. The
staged bundle's behaviour is tests/run.cjs and tests/d1-runtime.cjs."""

import contextlib
import io
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import build  # noqa: E402


def run(*argv: str) -> tuple[int, str]:
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        status = build.main(list(argv))
    return status, err.getvalue()


class StageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def copy_base(self, keep_sums: bool) -> Path:
        base = self.root / "base"
        shutil.copytree(build.HERE / "baseline", base)
        if not keep_sums:
            (base / "SHA256SUMS").unlink()
        return base

    def test_the_baseline_stages_and_verifies(self):
        out = self.root / "stage"
        self.assertEqual(run("stage", "--output", str(out)), (0, ""))
        self.assertEqual(run("verify", "--output", str(out))[0], 0)
        index = (out / "FTP-PATCH/index.html").read_text(encoding="utf-8")
        self.assertIn(build.REVISION, index)
        self.assertTrue((out / "ROLLBACK/PRIVATE-API/worker-single-file.mjs").is_file())

    def test_a_base_that_differs_from_its_sums_is_refused(self):
        base = self.copy_base(keep_sums=True)
        with (base / "assets/app.js").open("a", encoding="utf-8") as f:
            f.write("\n")
        status, err = run("stage", "--base", str(base), "--output", str(self.root / "stage"))
        self.assertEqual(status, 1)
        self.assertIn("differs from its SHA256SUMS", err)

    def test_a_missing_anchor_fails(self):
        for name, anchor in (
            ("index.html", '  <script src="assets/app.js'),
            ("PRIVATE-API/worker-single-file.mjs", "async function handleLibrary(request, env, ctx) {"),
        ):
            with self.subTest(file=name):
                shutil.rmtree(self.root / "base", ignore_errors=True)
                base = self.copy_base(keep_sums=False)
                path = base / name
                path.write_text(path.read_text(encoding="utf-8").replace(anchor, ""), encoding="utf-8")
                status, err = run("stage", "--base", str(base), "--output", str(self.root / f"stage-{name[:5]}"))
                self.assertEqual(status, 1)
                self.assertIn("anchor changed", err)


if __name__ == "__main__":
    unittest.main()
