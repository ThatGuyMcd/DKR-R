#!/usr/bin/env python3
"""scripts/prepare_payloads.py and the gate level that checks it
(scripts/fold/prepared.py): the allowed differences are exactly the named ones,
the portrait stage pins the folded native sources, and the owned engine's
runtime isolation leaves self-isolated probe names alone. The full chain needs
ROM-derived payloads; scripts/fold/run_equivalence.sh runs it."""

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts/fold"))
import prepared  # noqa: E402

import camera_scene_kernel  # noqa: E402
import character_portrait_pipeline  # noqa: E402
import generate_replay_probe  # noqa: E402
import prepare_payloads  # noqa: E402

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


class PreparedComparisonTests(unittest.TestCase):
    """A synthetic reference and prepared tree, then one change at a time."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.ours, self.theirs = base / "prepared", base / "reference"
        self.options = {}
        for rev in (77, 80):
            suffix = "" if rev == 77 else "_V80"
            for side, root in ((self.ours, "/ours"), (self.theirs, "/theirs")):
                prefix = "" if side is self.ours else "ref-"
                write(side / f"{prefix}generated-v{rev}/funcs_0.c", "int f;\n")
                write(
                    side / f"{prefix}generated-v{rev}/character-portrait-receipt.json",
                    json.dumps({"schema": 1, "native_sha256" if side is self.theirs else "native_files": "x"}),
                )
                write(side / f"{prefix}v{rev}.toml", TOML.format(root=root))
                owned = {"revision": rev, "full_scenes": True, "a": {"file": root + "/funcs_0.c"}}
                owned["input_native_inputs"] = {"src/x.cpp": root}
                write(side / f"{prefix}owned-v{rev}/manifest.json", json.dumps(owned))
                write(side / f"{prefix}owned-v{rev}/owned_0.c", "int g;\n")
                lines = "#define A 1\n#define B 2\n" if side is self.ours else "#define B 2\n#define A 1\n"
                write(side / f"{prefix}model-v{rev}/visual_model_addresses.h", lines)
                kernel = side / f"{prefix}camera-kernel-v{rev}"
                body = b"int k;\n" if side is self.ours else b"int k;\r\n"
                write(kernel / "camera_scene_functions.c", body)
                self.kernel_manifest(kernel, recipe=root)
            ref = self.theirs
            self.options |= {
                f"DKR_GENERATED_SOURCE_V{rev}": str(ref / f"ref-generated-v{rev}"),
                f"DKR_CODE_POLICY_V{rev}": str(ref / f"ref-v{rev}.toml"),
                f"DKR_EXPERIMENTAL_OWNED_GENERATED{suffix}": str(ref / f"ref-owned-v{rev}"),
                f"DKR_VISUAL_MODEL_GENERATED_V{rev}": str(ref / f"ref-model-v{rev}"),
                f"DKR_CAMERA_KERNEL_V{rev}": str(ref / f"ref-camera-kernel-v{rev}"),
            }

    @staticmethod
    def kernel_manifest(kernel: Path, recipe: str) -> None:
        files = {
            p.name: prepared.sha(p.read_bytes()) for p in kernel.iterdir() if p.is_file() and p.name != "manifest.json"
        }
        (kernel / "manifest.json").write_text(json.dumps({"schema": 1, "recipe": recipe, "files": files}))

    def compare(self):
        return prepared.compare(self.options, self.ours)

    def test_only_named_differences_remain(self):
        counts, unexplained, allowed = self.compare()
        self.assertEqual(unexplained, [])
        reasons = {line.split(": ", 1)[1] for line in allowed}
        self.assertEqual(reasons, {"line endings", "line order", "machine paths", "provenance"})
        self.assertEqual(counts["identical"], 4)

    def test_compiled_bytes_must_match(self):
        write(self.ours / "owned-v80/owned_0.c", "int h;\n")
        self.assertEqual(self.compare()[1], ["owned-v80/owned_0.c: bytes"])

    def test_line_endings_only_for_kernels(self):
        write(self.ours / "generated-v77/funcs_0.c", "int f;\r\n")
        self.assertEqual(self.compare()[1], ["generated-v77/funcs_0.c: bytes"])

    def test_kernel_manifest_files_must_describe_the_directory(self):
        write(self.ours / "camera-kernel-v77/camera_scene_functions.c", "int k2;\n")
        unexplained = self.compare()[1]
        self.assertIn("camera-kernel-v77/camera_scene_functions.c: bytes", unexplained)
        self.assertTrue(any(line.startswith("camera-kernel-v77/manifest.json: ") for line in unexplained))
        with self.assertRaises(ValueError):
            prepared.kernel_manifest(self.ours / "camera-kernel-v77")

    def test_policy_paths_only(self):
        write(self.ours / "v77.toml", TOML.format(root="/ours").replace("entrypoint = 1", "entrypoint = 2"))
        self.assertEqual(self.compare()[1], ["v77.toml: bytes"])

    def test_manifest_fields_cmake_reads_compare_exactly(self):
        path = self.ours / "owned-v77/manifest.json"
        manifest = json.loads(path.read_text())
        manifest["full_scenes"] = False
        path.write_text(json.dumps(manifest))
        self.assertEqual(self.compare()[1], ["owned-v77/manifest.json: fields .full_scenes"])

    def test_native_audits_keep_their_file_set(self):
        path = self.ours / "owned-v77/manifest.json"
        manifest = json.loads(path.read_text())
        manifest["input_native_inputs"] = {"src/y.cpp": "/ours"}
        path.write_text(json.dumps(manifest))
        self.assertEqual(len(self.compare()[1]), 1)

    def test_line_order_only_for_the_model_addresses(self):
        write(self.ours / "model-v77/visual_model_addresses.h", "#define A 1\n#define B 3\n")
        self.assertEqual(self.compare()[1], ["model-v77/visual_model_addresses.h: bytes"])

    def test_missing_and_extra_files_fail(self):
        (self.ours / "owned-v77/owned_0.c").unlink()
        write(self.ours / "model-v80/extra.h", "")
        unexplained = self.compare()[1]
        self.assertIn("owned-v77/owned_0.c: missing from the prepared inputs", unexplained)
        self.assertIn("model-v80/extra.h: missing from DKR_VISUAL_MODEL_GENERATED_V80", unexplained)

    def test_reference_must_name_every_input(self):
        del self.options["DKR_CAMERA_KERNEL_V80"]
        with self.assertRaises(ValueError):
            self.compare()


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
