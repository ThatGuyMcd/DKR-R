"""Strict framing and full byte comparison for private DKRCOMP2 artifacts."""

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "compare_replay_references", Path(__file__).parents[1] / "compare_replay_references.py"
)
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.left = Path(self.directory.name) / "left.dkr-component"
        self.right = Path(self.directory.name) / "right.dkr-component"

    def artifact(self, path, effects=(b"abc", b"", b"def"), **overrides):
        metadata = dict(revision=77, players=2, schema=0x434F4D504F4E454E, native_bytes=16, context_bytes=536)
        metadata.update(overrides)
        metadata.setdefault(
            "cpu_bytes",
            compare.STATE_HEADER.size + compare.RAM_BYTES + compare.CONTEXT_SLOTS * metadata["context_bytes"],
        )
        metadata.setdefault("state_bytes", metadata["cpu_bytes"] + metadata["native_bytes"])
        metadata.setdefault("effect_frames", len(effects))
        with path.open("wb") as stream:
            stream.write(compare.HEADER.pack(compare.MAGIC, *(metadata[key] for key in compare.FIELDS)))
            stream.write(compare.STATE_HEADER.pack(b"DKRSTATE", 1, 0, compare.RAM_BYTES, 2))
            stream.seek(compare.HEADER.size + metadata["state_bytes"] - 1)
            stream.write(b"\0")
            for effect in effects:
                stream.write(struct.pack("<Q", len(effect)))
                stream.write(effect)
        return metadata

    def patch_header(self, path, field, value):
        with path.open("r+b") as stream:
            stream.seek(8 + compare.FIELDS.index(field) * 8)
            stream.write(struct.pack("<Q", value))

    def result(self):
        return compare.compare_references(self.left, self.right)

    def test_identical_complete_artifacts_have_metadata_and_sha256(self):
        metadata = self.artifact(self.left)
        self.artifact(self.right)
        result = self.result()
        self.assertEqual(result["status"], "equal")
        self.assertIsNone(result["first_state_difference"])
        self.assertIsNone(result["first_effect_difference"])
        self.assertEqual(result["inputs"][0]["metadata"]["schema"], metadata["schema"])
        self.assertEqual(result["inputs"][0]["sha256"], hashlib.sha256(self.left.read_bytes()).hexdigest())

    def test_first_state_difference_in_late_native_byte_and_effect_reported_together(self):
        metadata = self.artifact(self.left)
        self.artifact(self.right, effects=(b"abc", b"", b"dXf"))
        offset = metadata["state_bytes"] - 1
        with self.right.open("r+b") as stream:
            stream.seek(compare.HEADER.size + offset)
            stream.write(b"\x7f")
        result = self.result()
        self.assertEqual(result["status"], "different")
        self.assertEqual(result["first_state_difference"], {"byte_offset": offset, "left_byte": 0, "right_byte": 127})
        self.assertEqual(result["first_effect_difference"]["frame"], 2)
        self.assertEqual(result["first_effect_difference"]["byte_offset"], 1)

    def test_first_difference_after_chunk_boundary(self):
        self.artifact(self.left)
        self.artifact(self.right)
        offset = compare.CHUNK_BYTES + 17
        with self.right.open("r+b") as stream:
            stream.seek(compare.HEADER.size + offset)
            stream.write(b"x")
        self.assertEqual(self.result()["first_state_difference"]["byte_offset"], offset)

    def test_identical_hashes_do_not_replace_byte_comparison(self):
        self.artifact(self.left, effects=(b"left",))
        self.artifact(self.right, effects=(b"right",))
        digest = mock.Mock()
        digest.hexdigest.return_value = "0" * 64
        with mock.patch.object(compare.hashlib, "sha256", return_value=digest):
            result = self.result()
        self.assertEqual(result["inputs"][0]["sha256"], result["inputs"][1]["sha256"])
        self.assertEqual(result["status"], "different")
        self.assertEqual(result["first_effect_difference"]["byte_offset"], 0)

    def test_effect_length_difference_and_missing_empty_frame(self):
        self.artifact(self.left, effects=(b"abc",))
        self.artifact(self.right, effects=(b"abcd",))
        difference = self.result()["first_effect_difference"]
        self.assertEqual(difference["byte_offset"], 3)
        self.assertIsNone(difference["left_byte"])
        self.assertEqual(difference["right_byte"], ord("d"))
        self.artifact(self.right, effects=(b"abc", b""))
        difference = self.result()["first_effect_difference"]
        self.assertEqual(difference["frame"], 1)
        self.assertEqual(difference["reason"], "missing_frame")
        self.assertIsNone(difference["left_bytes"])
        self.assertEqual(difference["right_bytes"], 0)

    def test_identity_changes_are_differences_even_when_payloads_match(self):
        self.artifact(self.left)
        for field, value in (("revision", 80), ("players", 3), ("schema", 123)):
            with self.subTest(field=field):
                self.artifact(self.right, **{field: value})
                result = self.result()
                self.assertEqual(result["status"], "different")
                self.assertEqual(result["identity_differences"][0]["field"], field)
                self.assertIsNone(result["first_state_difference"])

    def test_self_consistent_abi_mismatch_is_reported(self):
        self.artifact(self.left)
        self.artifact(self.right, context_bytes=544)
        result = self.result()
        self.assertEqual(result["status"], "different")
        self.assertEqual({item["field"] for item in result["identity_differences"]}, {"context_bytes", "cpu_bytes"})
        self.assertIsNotNone(result["first_state_difference"])

    def test_invalid_lengths_and_identities(self):
        self.artifact(self.left)
        for field, value in (
            ("state_bytes", 0),
            ("state_bytes", compare.RAM_BYTES),
            ("state_bytes", (1 << 64) - 1),
            ("effect_frames", (1 << 64) - 1),
            ("effect_frames", 100),
            ("native_bytes", 0),
            ("native_bytes", (1 << 64) - 1),
            ("context_bytes", 0),
            ("context_bytes", 4097),
            ("cpu_bytes", 1),
            ("schema", 0),
            ("players", 1),
            ("revision", 81),
        ):
            with self.subTest(field=field, value=value):
                self.artifact(self.right)
                self.patch_header(self.right, field, value)
                self.assertEqual(self.result()["status"], "invalid")

    def test_effect_length_overflow_and_truncation(self):
        self.artifact(self.left)
        for length in ((1 << 64) - 1, 1000):
            with self.subTest(length=length):
                metadata = self.artifact(self.right)
                with self.right.open("r+b") as stream:
                    stream.seek(compare.HEADER.size + metadata["state_bytes"])
                    stream.write(struct.pack("<Q", length))
                self.assertEqual(self.result()["status"], "invalid")

    def test_truncated_header_state_length_and_payload(self):
        self.artifact(self.left)
        for cut in (0, 7, compare.HEADER.size - 1, compare.HEADER.size + 12, -1, -2, -9):
            with self.subTest(cut=cut):
                self.artifact(self.right)
                size = self.right.stat().st_size
                with self.right.open("r+b") as stream:
                    stream.truncate(cut if cut >= 0 else size + cut)
                self.assertEqual(self.result()["status"], "invalid")

    def test_old_magic_trailing_data_and_bad_embedded_layout(self):
        self.artifact(self.left)
        for offset, payload in (
            (0, b"DKRCOMP1"),
            (compare.HEADER.size, b"BADSTATE"),
            (compare.HEADER.size + 8, struct.pack("<I", 2)),
            (compare.HEADER.size + 16, struct.pack("<I", 1)),
            (compare.HEADER.size + 20, struct.pack("<I", 33)),
            (-1, b"extra"),
        ):
            with self.subTest(offset=offset, payload=payload):
                self.artifact(self.right)
                with self.right.open("r+b") as stream:
                    stream.seek(offset if offset >= 0 else 0, 0 if offset >= 0 else 2)
                    stream.write(payload)
                self.assertEqual(self.result()["status"], "invalid")

    def test_oversized_sparse_artifact_rejected_before_hash_or_payload_read(self):
        self.artifact(self.left)
        self.artifact(self.right)
        with self.right.open("r+b") as stream:
            stream.truncate(compare.MAX_ARTIFACT_BYTES + 1)
        result = self.result()
        self.assertEqual(result["status"], "invalid")
        self.assertIn("1 GiB", result["errors"][0]["message"])

    def test_cli_json_exit_codes_and_file_report(self):
        self.artifact(self.left)
        self.artifact(self.right)
        for status, exit_code in (("equal", 0), ("different", 1), ("invalid", 2)):
            if status == "different":
                self.patch_header(self.right, "players", 3)
            elif status == "invalid":
                self.right.write_bytes(b"broken")
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(compare.main([str(self.left), str(self.right), "--json"]), exit_code)
            self.assertEqual(json.loads(output.getvalue())["status"], status)
        report = Path(self.directory.name) / "result.json"
        with redirect_stdout(io.StringIO()):
            self.assertEqual(compare.main([str(self.left), str(self.right), "--json", str(report)]), 2)
        self.assertEqual(json.loads(report.read_text())["status"], "invalid")

    def test_missing_inputs_report_both_errors(self):
        result = self.result()
        self.assertEqual(result["status"], "invalid")
        self.assertEqual([error["side"] for error in result["errors"]], ["left", "right"])

    def test_json_output_cannot_overwrite_either_input_or_hardlink(self):
        self.artifact(self.left)
        self.artifact(self.right)
        alias = Path(self.directory.name) / "alias.json"
        alias.hardlink_to(self.left)
        original_size = self.left.stat().st_size
        for destination in (self.left, self.right, alias):
            with self.subTest(destination=destination):
                with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()) as error:
                    self.assertEqual(compare.main([str(self.left), str(self.right), "--json", str(destination)]), 2)
                self.assertIn("must not overwrite", error.getvalue())
                self.assertEqual(self.left.stat().st_size, original_size)


if __name__ == "__main__":
    unittest.main()
