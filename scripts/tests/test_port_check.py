"""port_check: command construction, verdicts and what it refuses to print."""

from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("port_check", Path(__file__).parents[1] / "port_check.py")
port_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(port_check)

SECRET = b"GUEST-RAM-MARKER"


class Fixtures(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def file(self, name, data=SECRET):
        path = self.root / name
        path.write_bytes(data)
        return path


class ArgumentTests(Fixtures):
    def test_revision_pairs_must_name_files(self):
        path = self.file("a")
        self.assertEqual(port_check.revision_paths([f"77={path}"], "fixture"), {77: path.resolve()})
        with self.assertRaises(SystemExit):
            port_check.revision_paths([f"78={path}"], "fixture")
        with self.assertRaises(SystemExit):
            port_check.revision_paths([f"77={self.root / 'missing'}"], "fixture")

    def test_frames_are_bounded_and_revisions_must_match(self):
        fixture, rom = self.file("f"), self.file("r")
        base = ["--out", str(self.root / "o"), "--fixture", f"77={fixture}", "--rom", f"77={rom}"]
        self.assertEqual(port_check.parse_args(base).frames, 300)
        for bad in (["--frames", "11"], ["--frames", "301"]):
            with self.assertRaises(SystemExit), mock.patch("sys.stderr", io.StringIO()):
                port_check.parse_args(base + bad)
        with self.assertRaises(SystemExit), mock.patch("sys.stderr", io.StringIO()):
            port_check.parse_args(["--out", "o", "--fixture", f"77={fixture}", "--rom", f"80={rom}"])


class CommandTests(Fixtures):
    def test_native_wrap_is_identity(self):
        self.assertEqual(port_check.Environment().wrap(["echo", "x"]), ["echo", "x"])

    def test_docker_wrap_is_throwaway_limited_and_read_only_for_inputs(self):
        environment = port_check.Environment("img", "linux/riscv64", "2", [self.root], [self.root / "out"])
        command = environment.wrap(["probe"], env={"A": "1"}, cwd=self.root)
        self.assertEqual(command[:2], ["docker", "run"])
        self.assertIn("--rm", command)
        self.assertEqual(command[command.index("--cpus") + 1], "2")
        self.assertEqual(command[command.index("--platform") + 1], "linux/riscv64")
        self.assertIn(f"{self.root}:{self.root}:ro", command)
        self.assertIn(f"{self.root / 'out'}:{self.root / 'out'}", command)
        self.assertEqual(command[-2:], ["img", "probe"])

    def test_cmake_arguments(self):
        args = port_check.parse_args(
            [
                "--out",
                "o",
                "--fixture",
                f"77={self.file('f')}",
                "--rom",
                f"77={self.file('r')}",
                "--ubsan",
                "--toolchain-file",
                "t.cmake",
                "--cmake-arg=-GNinja",
            ]
        )
        command = port_check.probe_cmake_args(args, Path("/g"), Path("/b"))
        self.assertIn("-DDKR_PROBE_GENERATED=/g", command)
        self.assertIn("-DCMAKE_TOOLCHAIN_FILE=t.cmake", command)
        self.assertIn("-GNinja", command)
        self.assertTrue(any(a.startswith("-DCMAKE_CXX_FLAGS=-fsanitize=") for a in command))
        self.assertNotIn("-ffast-math", " ".join(command))

    def test_emulator_prefixes_the_workload(self):
        args = port_check.parse_args(
            [
                "--out",
                "o",
                "--fixture",
                f"77={self.file('f')}",
                "--rom",
                f"77={self.file('r')}",
                "--emulator",
                "qemu-x -cpu y",
            ]
        )
        command = port_check.workload_command(args, Path("/p"), Path("/f"))
        self.assertEqual(command[:3], ["qemu-x", "-cpu", "y"])
        self.assertEqual(command[-2:], ["--offline-services", "--authored-owned-driver"])


class ReportTests(unittest.TestCase):
    def test_ubsan_sites_are_distinct_and_counted(self):
        text = "\n".join(
            [
                "a/rsp.hpp:1112:45: runtime error: signed integer overflow: 1 * 2 cannot be represented",
                "a/rsp.hpp:1112:45: runtime error: signed integer overflow: 3 * 4 cannot be represented",
                "a/rsp.hpp:947:9: runtime error: signed integer overflow: 3 * 4 cannot be represented",
                "unrelated line",
            ]
        )
        self.assertEqual(port_check.summarize_ubsan(text), [("a/rsp.hpp:1112", 2), ("a/rsp.hpp:947", 1)])

    def test_contract_line_parses(self):
        match = port_check.CONTRACT_LINE.search("[contract] architecture=riscv64 contract=strict-ieee754-v1\n")
        self.assertEqual(match.groups(), ("riscv64", "strict-ieee754-v1"))

    def test_difference_never_includes_byte_values(self):
        lines = port_check.describe_difference(
            {
                "errors": [],
                "identity_differences": [],
                "first_state_difference": {"byte_offset": 979620, "left_byte": 199, "right_byte": 198},
                "first_effect_difference": {"frame": 1, "byte_offset": 52, "left_byte": 11, "right_byte": 164},
            }
        )
        text = "\n".join(lines)
        self.assertIn("979620", text)
        self.assertIn("frame 1", text)
        for value in ("199", "198", "164"):
            self.assertNotIn(value, text)


class CompareTests(Fixtures):
    def setup_runs(self, ref_fixture="aa", frames=300, mine_fixture="aa"):
        reference, out = self.root / "ref", self.root / "out"
        for path, fixture in ((reference, ref_fixture), (out, mine_fixture)):
            path.mkdir()
            manifest = {"frames": frames if path is reference else 300, "fixtures": {"77": fixture}, "contract": "x"}
            (path / "port-check.json").write_text(json.dumps(manifest))
        args = mock.Mock(reference=reference)
        return args, out

    def run_compare(self, args, out, comparator):
        buffer = io.StringIO()
        with mock.patch.object(port_check, "load_comparator", return_value=comparator), redirect_stdout(buffer):
            code = port_check.compare(args, out, [77])
        return code, buffer.getvalue()

    def test_identical(self):
        args, out = self.setup_runs()
        comparator = mock.Mock(compare_references=mock.Mock(return_value={"status": "equal"}))
        code, text = self.run_compare(args, out, comparator)
        self.assertEqual(code, 0)
        self.assertIn("IDENTICAL, 3 of 3", text)
        self.assertEqual(comparator.compare_references.call_count, 3)

    def test_different_reports_first_difference_and_exits_one(self):
        args, out = self.setup_runs()
        different = {
            "status": "different",
            "errors": [],
            "identity_differences": [],
            "first_state_difference": {"byte_offset": 7, "left_byte": 1, "right_byte": 2},
            "first_effect_difference": None,
        }
        comparator = mock.Mock(compare_references=mock.Mock(return_value=different))
        code, text = self.run_compare(args, out, comparator)
        self.assertEqual(code, 1)
        self.assertIn("DIFFERENT, 0 of 3", text)
        self.assertIn("byte 7", text)

    def test_other_fixture_is_inconclusive_not_a_pass(self):
        args, out = self.setup_runs(ref_fixture="aa", mine_fixture="bb")
        comparator = mock.Mock()
        code, text = self.run_compare(args, out, comparator)
        self.assertEqual(code, 2)
        self.assertIn("INCONCLUSIVE", text)
        comparator.compare_references.assert_not_called()

    def test_other_frame_count_is_inconclusive(self):
        args, out = self.setup_runs(frames=120)
        code, text = self.run_compare(args, out, mock.Mock())
        self.assertEqual(code, 2)
        self.assertIn("frames differs", text)

    def test_reference_without_manifest_is_inconclusive(self):
        args, out = self.setup_runs()
        (args.reference / "port-check.json").unlink()
        code, _text = self.run_compare(args, out, mock.Mock())
        self.assertEqual(code, 2)


class HashTests(Fixtures):
    def test_only_the_hash_leaves_the_fixture(self):
        path = self.file("fixture")
        digest = port_check.file_sha256(path)
        self.assertEqual(len(digest), 64)
        self.assertNotIn(SECRET.decode(), digest)


if __name__ == "__main__":
    unittest.main()
