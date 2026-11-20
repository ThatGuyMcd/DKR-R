#!/usr/bin/env python3
"""Exercise the real boss-finish guest control flow, when generated input exists.

Set DKR_TEST_GENERATED_ROOT to build/generated and DKR_DEPENDENCIES_DIR to the
prepared dependencies to run this check from an isolated worktree. No ROM code
is stored in the test: scripts/guest_hooks.py applies the production hook.
"""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("guest_hooks", ROOT / "scripts/guest_hooks.py")
hooks = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = hooks
SPEC.loader.exec_module(hooks)


class BossFinishTests(unittest.TestCase):
    def test_real_guest_finish_and_stock_parity(self):
        if sys.platform == "win32":
            self.skipTest("guest integration harness currently requires a Unix C compiler/linker")
        generated = Path(os.environ.get("DKR_TEST_GENERATED_ROOT", ROOT / "build/generated"))
        dependencies = Path(os.environ.get("DKR_DEPENDENCIES_DIR", ROOT / "extern"))
        include = dependencies / "n64-modern-runtime/N64Recomp/include"
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler or not (include / "recomp.h").exists():
            self.skipTest("needs a C compiler and prepared N64Recomp headers")
        if not all((generated / f"v{r}/RecompiledFuncs/funcs_20.c").exists() for r in (77, 80)):
            self.skipTest("needs generated v77 and v80 guest functions")
        for revision in ("77", "80"):
            with self.subTest(revision=revision), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source = generated / f"v{revision}/RecompiledFuncs/funcs_20.c"
                selected = [
                    h
                    for h in hooks.load(ROOT / "runtime-recomp/guest-hooks/hooks.toml", revision, "funcs")
                    if h.id == "custom-boss-finish"
                ]
                self.assertEqual(len(selected), 1)
                output, problems = hooks.apply([source], selected)
                self.assertEqual(problems, [])
                hooked = directory / source.name
                hooked.write_text(output[source.name])
                stock_outputs = []
                for label, unit in (("original", source), ("hooked", hooked)):
                    executable = directory / label
                    dead_strip = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
                    command = [
                        compiler,
                        "-std=c11",
                        "-O1",
                        "-ffunction-sections",
                        "-fdata-sections",
                        "-I",
                        str(include),
                        "-I",
                        str(source.parent),
                        str(unit),
                        str(ROOT / "runtime-recomp/tests/custom_boss_finish_harness.c"),
                        dead_strip,
                        "-lm",
                        "-o",
                        str(executable),
                    ]
                    built = subprocess.run(command, capture_output=True, text=True, timeout=120)
                    self.assertEqual(built.returncode, 0, built.stderr[-10000:])
                    stock_outputs.append(subprocess.check_output([str(executable), "0"], timeout=20))
                    if label == "hooked":
                        subprocess.run([str(executable), "1"], check=True, capture_output=True, timeout=20)
                self.assertEqual(stock_outputs[0], stock_outputs[1], "stock registers, RAM or callbacks changed")


if __name__ == "__main__":
    unittest.main()
