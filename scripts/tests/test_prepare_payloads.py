"""prepare_payloads: a failed run keeps the last complete bundle."""

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("prepare_payloads", SCRIPTS / "prepare_payloads.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class Prepare(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        (self.root / "build").mkdir()
        self.out = self.root / "build/prepared"
        self.previous = self.root / "build/prepared.previous"
        patch = mock.patch.object(prepare, "ROOT", self.root)
        patch.start()
        self.addCleanup(patch.stop)

    def run_main(self, stage):
        with (
            mock.patch.object(prepare, "prepare_revision", stage),
            mock.patch.object(sys, "stderr"),
            redirect_stdout(io.StringIO()),
        ):
            return prepare.main(["--output", str(self.out)])

    @staticmethod
    def good(out, revision, *_inputs):
        (out / f"v{revision}.toml").write_text(f"good {revision}\n")

    @staticmethod
    def bad(out, revision, *_inputs):
        (out / f"v{revision}.toml").write_text("partial\n")
        raise prepare.PrepareError("stage failed")

    def assert_good_bundle(self):
        self.assertTrue((self.out / prepare.MARKER).is_file())
        self.assertFalse((self.out / prepare.IN_PROGRESS).exists())
        self.assertEqual((self.out / "v77.toml").read_text(), "good 77\n")
        self.assertFalse(self.previous.exists())

    def test_success_publishes_and_leaves_nothing_beside_it(self):
        self.assertEqual(self.run_main(self.good), 0)
        self.assert_good_bundle()
        self.assertEqual(self.run_main(self.good), 0)
        self.assert_good_bundle()

    def test_failure_keeps_the_last_complete_bundle(self):
        self.assertEqual(self.run_main(self.good), 0)
        self.assertEqual(self.run_main(self.bad), 1)
        self.assert_good_bundle()

    def test_first_failure_leaves_no_directory_to_refuse(self):
        self.assertEqual(self.run_main(self.bad), 1)
        self.assertFalse(self.out.exists())
        self.assertEqual(self.run_main(self.good), 0)
        self.assert_good_bundle()

    def test_interrupt_restores_and_reraises(self):
        self.assertEqual(self.run_main(self.good), 0)

        def interrupted(out, revision, *_inputs):
            (out / "half").write_text("x")
            raise KeyboardInterrupt

        with self.assertRaises(KeyboardInterrupt):
            self.run_main(interrupted)
        self.assert_good_bundle()

    def test_killed_run_is_recovered_by_the_next_one(self):
        self.assertEqual(self.run_main(self.good), 0)
        # What a kill mid-run leaves: the bundle set aside, a partial OUT.
        prepare.fresh_output(self.out)
        (self.out / "half").write_text("x")
        self.assertEqual(self.run_main(self.bad), 1)
        self.assert_good_bundle()

    def test_foreign_directories_are_refused_untouched(self):
        self.out.mkdir()
        (self.out / "mine.txt").write_text("keep")
        self.assertEqual(self.run_main(self.good), 1)
        self.assertEqual((self.out / "mine.txt").read_text(), "keep")

    def test_foreign_previous_directory_is_refused_untouched(self):
        self.assertEqual(self.run_main(self.good), 0)
        self.previous.mkdir()
        (self.previous / "mine.txt").write_text("keep")
        self.assertEqual(self.run_main(self.good), 1)
        self.assertEqual((self.previous / "mine.txt").read_text(), "keep")
        self.assertTrue((self.out / prepare.MARKER).is_file())


if __name__ == "__main__":
    unittest.main()
