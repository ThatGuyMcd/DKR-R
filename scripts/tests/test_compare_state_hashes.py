"""compare_state_hashes: equal, different and unusable runs."""

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "compare_state_hashes", Path(__file__).parents[1] / "compare_state_hashes.py"
)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def state_line(epoch, frame, value, extra=" us=700"):
    return f"[check][state] epoch={epoch} frame={frame} hash={value:016X}{extra}\n"


class CompareTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)

    def log(self, name, frames, epoch=92, change=None, noise=True):
        path = Path(self.directory.name) / name
        with path.open("w") as stream:
            for frame in frames:
                if noise:
                    stream.write("[rollback][render] epoch=92 frame=1\n")
                value = frame * 7919 + 1
                if change == frame:
                    value ^= 1
                stream.write(state_line(epoch, frame, value))
        return path

    def run_tool(self, *arguments):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = tool.main([str(argument) for argument in arguments])
        return code, out.getvalue(), err.getvalue()

    def test_equal(self):
        a, b = self.log("a", range(30, 1500, 30)), self.log("b", range(30, 1500, 30))
        code, out, _ = self.run_tool(a, b)
        self.assertEqual(code, 0)
        self.assertIn("compared 49 frames", out)

    def test_extra_frames_on_one_side_are_ignored(self):
        a, b = self.log("a", range(30, 1500, 30)), self.log("b", range(30, 1000, 30))
        self.assertEqual(self.run_tool(a, b)[0], 0)

    def test_first_mismatch_is_reported(self):
        a = self.log("a", range(30, 1500, 30), change=600)
        b = self.log("b", range(30, 1500, 30), change=900)
        code, out, _ = self.run_tool(a, b)
        self.assertEqual(code, 1)
        self.assertIn("MISMATCH at epoch 92 frame 600", out)

    def test_three_logs_any_difference(self):
        a, b = self.log("a", range(30, 900, 30)), self.log("b", range(30, 900, 30))
        c = self.log("c", range(30, 900, 30), change=300)
        self.assertEqual(self.run_tool(a, b, c)[0], 1)

    def test_epochs_are_separate_points(self):
        a = self.log("a", range(30, 900, 30), epoch=91)
        b = self.log("b", range(30, 900, 30), epoch=92)
        code, out, _ = self.run_tool(a, b)
        self.assertEqual(code, 2)
        self.assertIn("in common", out)

    def test_too_few_frames_is_not_equal(self):
        a, b = self.log("a", range(30, 150, 30)), self.log("b", range(30, 150, 30))
        code, out, _ = self.run_tool(a, b)
        self.assertEqual(code, 2)
        self.assertIn("need at least", out)
        self.assertEqual(self.run_tool(a, b, "--minimum", 3)[0], 0)

    def test_mismatch_beats_minimum(self):
        a, b = self.log("a", [30, 60], change=60), self.log("b", [30, 60])
        self.assertEqual(self.run_tool(a, b)[0], 1)

    def test_no_state_lines(self):
        a = self.log("a", range(30, 900, 30))
        empty = Path(self.directory.name) / "empty"
        empty.write_text("[rollback][render] epoch=92 frame=1\n")
        self.assertEqual(self.run_tool(a, empty)[0], 2)

    def test_missing_file_and_single_log(self):
        a = self.log("a", range(30, 900, 30))
        self.assertEqual(self.run_tool(a, Path(self.directory.name) / "nope")[0], 2)
        self.assertEqual(self.run_tool(a)[0], 2)
        self.assertEqual(self.run_tool(a, a)[0], 2)

    def test_conflicting_repeat_in_one_log(self):
        a = self.log("a", range(30, 900, 30))
        b = self.log("b", range(30, 900, 30))
        with b.open("a") as stream:
            stream.write(state_line(92, 30, 5))
        self.assertEqual(self.run_tool(a, b)[0], 2)

    def test_old_and_new_line_forms(self):
        path = Path(self.directory.name) / "x"
        path.write_text(state_line(92, 30, 1, extra="") + state_line(92, 60, 2, extra=" us=3"))
        self.assertEqual(tool.read_states(path), {(92, 30): "0000000000000001", (92, 60): "0000000000000002"})


if __name__ == "__main__":
    unittest.main()
