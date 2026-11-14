#!/usr/bin/env python3
"""scripts/prepare_payloads.py: the portrait stage pins the folded native
sources, and the owned engine's runtime isolation leaves self-isolated probe
names alone. The full chain needs ROM-derived payloads."""

import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import camera_scene_kernel  # noqa: E402
import character_portrait_pipeline  # noqa: E402
import compose_payloads  # noqa: E402
import generate_replay_probe  # noqa: E402
import prepare_payloads  # noqa: E402

ORIGINAL_SHA1 = compose_payloads.RETAIL_SHA1[77]
TOML = 'elf_path = "{root}/a.elf"\nrom_file_path = "{root}/a.z64"\noutput_func_path = "{root}/funcs"\nentrypoint = 1\n'


def setUpModule():
    # Prepared output and the kernels must lie under the work tree's build/.
    (ROOT / "build").mkdir(exist_ok=True)


def write(path: Path, data: str | bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, bytes):
        path.write_bytes(data)
    else:
        path.write_bytes(data.encode())


class PreparePayloadsTests(unittest.TestCase):
    def run_main(self, *argv: str) -> tuple[int, str]:
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            try:
                status = prepare_payloads.main(list(argv))
            except SystemExit as exit_:
                status = int(exit_.code or 0)
        return status, err.getvalue()

    def test_input_revisions_are_checked(self):
        status, err = self.run_main("--input", "78", "a", "b", "c")
        self.assertEqual(status, 2)
        self.assertIn("77 or 80", err)
        args = ("--input", "77", "a", "b", "c")
        self.assertEqual(self.run_main(*args, *args)[0], 2)

    def test_output_outside_build_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            status, err = self.run_main("--output", temp, "--input", "77", "a", "b", "c")
        self.assertEqual(status, 1)
        self.assertIn("must lie inside", err)

    def test_an_unmarked_output_is_never_removed(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temp:
            keep = Path(temp) / "keep.txt"
            keep.write_text("mine")
            status, err = self.run_main("--output", temp, "--input", "77", "a", "b", "c")
            self.assertEqual(status, 1)
            self.assertIn("was not made by prepare_payloads.py", err)
            self.assertTrue(keep.exists())

    def test_missing_inputs_name_the_prepare_script(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temp:
            out = Path(temp) / "prepared"
            status, err = self.run_main("--output", str(out), "--input", "77", temp + "/no", "b", "c")
        self.assertEqual(status, 1)
        self.assertIn("Prepare-DKR-Runtime-macOS.sh", err)

    def test_options_round_trip_through_a_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            manifest = Path(temp) / "manifest.json"
            manifest.write_text(
                json.dumps({"revision": 77, "runtime_symbol_isolation": True, "full_scenes": True, "roots": ["a", "b"]})
            )
            options = prepare_payloads.options_from(manifest, 77)
            self.assertEqual(options, ["--isolate-symbols", "--full-scenes", "--roots", "a", "b"])
            with self.assertRaises(prepare_payloads.PrepareError):
                prepare_payloads.options_from(manifest, 80)


class ComposeRomTests(unittest.TestCase):
    """compose_payloads.py rom: byte order is normalised before the SHA-1 check."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.z64 = bytes.fromhex("80371240") + bytes(range(256)) * 4
        compose_payloads.RETAIL_SHA1[77] = hashlib.sha1(self.z64).hexdigest()
        self.addCleanup(compose_payloads.RETAIL_SHA1.__setitem__, 77, ORIGINAL_SHA1)

    def test_every_byte_order_normalises_to_z64(self):
        v64 = bytearray(self.z64)
        v64[0::2], v64[1::2] = v64[1::2], v64[0::2]
        n64 = bytearray(self.z64)
        for i in range(0, len(n64), 4):
            n64[i : i + 4] = n64[i : i + 4][::-1]
        for name, data in (("z64", self.z64), ("v64", bytes(v64)), ("n64", bytes(n64))):
            with self.subTest(order=name):
                source, target = self.base / f"rom.{name}", self.base / f"out-{name}.z64"
                source.write_bytes(data)
                compose_payloads.normalise_rom(source, target, 77)
                self.assertEqual(target.read_bytes(), self.z64)

    def test_a_different_dump_or_revision_is_refused(self):
        source = self.base / "rom.z64"
        source.write_bytes(self.z64[:-1] + b"\x00")
        with self.assertRaisesRegex(compose_payloads.ComposeError, "SHA-1"):
            compose_payloads.normalise_rom(source, self.base / "out.z64", 77)
        source.write_bytes(self.z64)
        with self.assertRaisesRegex(compose_payloads.ComposeError, "US v80"):
            compose_payloads.normalise_rom(source, self.base / "out.z64", 80)
        source.write_bytes(b"\x00" * 16)
        with self.assertRaisesRegex(compose_payloads.ComposeError, "not an N64 ROM"):
            compose_payloads.normalise_rom(source, self.base / "out.z64", 77)


class StageTests(unittest.TestCase):
    def test_portrait_stage_pins_the_folded_native_sources(self):
        character_portrait_pipeline.verify_native_files()

    def test_portrait_receipt_must_name_the_folded_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            receipt = {
                "schema": 1,
                "revision": 77,
                "function": {"original_sha256": character_portrait_pipeline.GUEST_PINS[77], "patched_sha256": "x"},
                "world_source_sha256": character_portrait_pipeline.sha(
                    character_portrait_pipeline.WORLD_PORTRAIT.read_bytes()
                ),
                "native_sha256": "the old native stage",
            }
            (Path(temp) / "character-portrait-receipt.json").write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError, "native sources disagree"):
                character_portrait_pipeline.owned_receipt(Path(temp), 77)

    def test_kernel_output_stays_in_the_build_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                camera_scene_kernel.new_output(Path(temp) / "kernel")
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temp:
            with self.assertRaises(ValueError):
                camera_scene_kernel.new_output(Path(temp), Path(temp) / "input")

    def test_self_isolated_probe_names_are_not_renamed_twice(self):
        header = generate_replay_probe.runtime_isolation_header((), (), 77)
        self.assertNotIn("define dkr_probe_sdk2_pulse ", header)
        self.assertIn("make_owned_game", header)


if __name__ == "__main__":
    unittest.main()
