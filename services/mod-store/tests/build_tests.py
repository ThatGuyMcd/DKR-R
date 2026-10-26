#!/usr/bin/env python3
"""services/mod-store/build.py against tests/fixture/, a base that holds each
anchor the splices need exactly once: the stage succeeds, every authored
fragment lands, and losing any one anchor fails the stage."""

import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import build  # noqa: E402

FIXTURE = HERE / "fixture"


def run(*argv: str) -> tuple[int, str]:
    err = io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
        status = build.main(list(argv))
    return status, err.getvalue()


class StageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)

    def stage(self, worker: Path, frontend: Path = FIXTURE) -> tuple[int, str, Path]:
        out = self.base / f"out-{len(list(self.base.iterdir()))}"
        status, err = run("--worker", str(worker), "--frontend", str(frontend), "--output", str(out))
        return status, err, out

    def test_the_fixture_stages_with_every_fragment(self):
        status, err, out = self.stage(FIXTURE / "worker-single-file.mjs")
        self.assertEqual(status, 0, err)
        worker = (out / "backend/worker-single-file.mjs").read_text(encoding="utf-8")
        for name in ("launcher-auth.inl", "launcher-service.inl", "upload-integrity.inl"):
            self.assertIn((build.HERE / name).read_text(encoding="utf-8"), worker, name)
        self.assertIn("'/auth/launcher/start': ['POST', launcherStart]", worker)
        uploads = (out / "FTP-PATCH/assets/uploads-client.js").read_text(encoding="utf-8")
        self.assertIn((build.HERE / "upload-hash.inl").read_text(encoding="utf-8"), uploads)
        for name in build.COPIED:
            self.assertTrue((out / "backend" / name).is_file(), name)

    def test_every_worker_anchor_is_required(self):
        lines = (FIXTURE / "worker-single-file.mjs").read_text(encoding="utf-8").splitlines()[1:]
        optional = "completeAtFrontend(config.frontend, flow.client_state, { error })"
        for index, line in enumerate(lines):
            if line == optional or not line.strip():
                continue
            with self.subTest(anchor=line[:60]):
                broken = self.base / f"worker-{index}.mjs"
                broken.write_text("\n".join(lines[:index] + lines[index + 1 :]) + "\n", encoding="utf-8")
                status, err, _ = self.stage(broken)
                self.assertEqual(status, 1)
                self.assertIn("anchor changed", err)

    def test_every_frontend_anchor_is_required(self):
        for name in build.FRONTEND:
            lines = (FIXTURE / "assets" / name).read_text(encoding="utf-8").splitlines()[1:]
            # The first anchor of each file is enough to show the file is checked.
            portal = self.base / f"portal-{name}"
            (portal / "assets").mkdir(parents=True)
            for other in build.FRONTEND:
                text = (FIXTURE / "assets" / other).read_text(encoding="utf-8")
                if other == name:
                    text = "\n".join(lines[1:]) + "\n"
                (portal / "assets" / other).write_text(text, encoding="utf-8")
            with self.subTest(file=name):
                status, err, _ = self.stage(FIXTURE / "worker-single-file.mjs", portal)
                self.assertEqual(status, 1)
                self.assertIn("anchor changed", err)

    def test_unsafe_backfill_is_refused(self):
        backfill = self.base / "backfill.json"
        backfill.write_text('{"schema":1,"records":[{"id":"release_x","sha256":"0","bytes":1,"url":"http://x"}]}')
        status, err = run(
            "--worker",
            str(FIXTURE / "worker-single-file.mjs"),
            "--output",
            str(self.base / "o"),
            "--backfill",
            str(backfill),
        )
        self.assertEqual(status, 1)
        self.assertIn("Unsafe backfill record", err)


if __name__ == "__main__":
    unittest.main()
