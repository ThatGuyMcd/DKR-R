#!/usr/bin/env python3
"""services/online-directory/build.py over the library API Worker that
services/library-workflow/build.py stages: the gateway lands once, a moved
entry point fails, and --expect-sha256 refuses other bytes. The bundle's
behaviour is tests/runtime.cjs."""

import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SERVICES = HERE.parents[1]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


build = load("online_directory_build", HERE.parent / "build.py")
library = load("library_workflow_build", SERVICES / "library-workflow/build.py")


def run(main, *argv: str) -> tuple[int, str]:
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        status = main(list(argv))
    return status, err.getvalue()


class ComposeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        status, err = run(library.main, "stage", "--output", str(cls.root / "library"))
        assert status == 0, err
        cls.api = cls.root / "library/PRIVATE-API/worker-single-file.mjs"

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_the_gateway_goes_in_front_once(self):
        out = self.root / "bundle"
        self.assertEqual(run(build.main, "--api-worker", str(self.api), "--output", str(out))[0], 0)
        worker = (out / "PRIVATE-API/worker-single-file.mjs").read_text(encoding="utf-8")
        self.assertEqual(worker.count((HERE.parent / "gateway.inl").read_text(encoding="utf-8")), 1)
        self.assertEqual(worker.count("await handleOnlineDirectory(request, env);"), 1)
        self.assertEqual((out / "ROLLBACK/worker-single-file.mjs").read_bytes(), self.api.read_bytes())
        self.assertEqual(run(build.main, "--verify", "--output", str(out))[0], 0)

    def test_a_moved_entry_point_fails(self):
        moved = self.root / "moved.mjs"
        moved.write_text(
            self.api.read_text(encoding="utf-8").replace(build.FETCH, "export default {"), encoding="utf-8"
        )
        status, err = run(build.main, "--api-worker", str(moved), "--output", str(self.root / "moved"))
        self.assertEqual(status, 1)
        self.assertIn("API entry point changed", err)

    def test_expect_sha256_refuses_other_bytes(self):
        digest = hashlib.sha256(self.api.read_bytes()).hexdigest()
        ok = run(
            build.main, "--api-worker", str(self.api), "--expect-sha256", digest, "--output", str(self.root / "pinned")
        )
        self.assertEqual(ok[0], 0)
        status, err = run(
            build.main, "--api-worker", str(self.api), "--expect-sha256", "0" * 64, "--output", str(self.root / "other")
        )
        self.assertEqual(status, 1)
        self.assertIn("not the reviewed API Worker", err)


if __name__ == "__main__":
    unittest.main()
