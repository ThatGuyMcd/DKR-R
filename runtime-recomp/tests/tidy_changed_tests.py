"""tidy_changed.py: layout-only changes drop out, real changes keep only their lines."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import tidy_changed as tidy


def collapse(data: bytes, path: str) -> bytes:
    """Stand-in formatter: one statement per line, `;` ends a line, no indentation."""
    text = data.decode().replace("\n", " ")
    return "".join(part.strip() + ";\n" for part in text.split(";") if part.strip()).encode()


class LineDiffTests(unittest.TestCase):
    def test_identical_is_empty(self):
        self.assertEqual(tidy.line_diff(b"a;\nb;\n", b"a;\nb;\n", "x.cpp"), "")

    def test_paths_survive_dash_p1_and_hunks_are_zero_context(self):
        diff = tidy.line_diff(b"a;\nb;\nc;\n", b"a;\nB;\nc;\n", "src/x.cpp")
        self.assertIn("+++ b/src/x.cpp\n", diff)
        self.assertIn("@@ -2 +2 @@", diff)

    def test_added_file_is_all_new(self):
        self.assertIn("@@ -0,0 +1,2 @@", tidy.line_diff(b"", b"a;\nb;\n", "x.cpp"))


class RepoTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        patcher = patch.object(tidy, "ROOT", self.root)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.git("init", "-q")
        self.git("config", "user.email", "t@example.com")
        self.git("config", "user.name", "t")

    def git(self, *args: str) -> str:
        env = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_SYSTEM": os.devnull}
        return subprocess.run(["git", *args], cwd=self.root, env=env, check=True, capture_output=True, text=True).stdout

    def commit(self, files: dict[str, str]) -> str:
        for name, text in files.items():
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text)
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "c")
        return self.git("rev-parse", "HEAD").strip()

    def test_layout_only_change_drops_out_and_real_change_keeps_its_lines(self):
        base = self.commit(
            {
                "runtime-recomp/src/layout.cpp": "int a=1;int b=2;\n",
                "runtime-recomp/src/real.cpp": "int a=1;int b=2;\n",
                "runtime-recomp/src/gone.txt": "not C++\n",
            }
        )
        self.commit(
            {
                "runtime-recomp/src/layout.cpp": "int a=1;\nint b=2;\n",
                "runtime-recomp/src/real.cpp": "int a;\nint b=2;\n",
                "runtime-recomp/src/gone.txt": "still not C++\n",
                "runtime-recomp/src/new.hpp": "int n;\n",
                "docs/outside.cpp": "int x;\n",
            }
        )
        diffs = tidy.file_diffs(base, collapse)
        self.assertEqual(sorted(diffs), ["runtime-recomp/src/new.hpp", "runtime-recomp/src/real.cpp"])
        self.assertIn("-int a=1;", diffs["runtime-recomp/src/real.cpp"])
        self.assertIn("+int a;", diffs["runtime-recomp/src/real.cpp"])
        self.assertNotIn("int b", diffs["runtime-recomp/src/real.cpp"].split("@@", 1)[1])

    def test_rename_is_compared_with_its_old_path(self):
        body = "".join(f"int v{n}=0;\n" for n in range(20))
        base = self.commit({"runtime-recomp/src/old.cpp": body + "int b=2;\n"})
        self.git("mv", "runtime-recomp/src/old.cpp", "runtime-recomp/src/new.cpp")
        (self.root / "runtime-recomp/src/new.cpp").write_text(body + "int b=3;\n")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "rename")
        diffs = tidy.file_diffs(base, collapse)
        self.assertEqual(list(diffs), ["runtime-recomp/src/new.cpp"])
        self.assertIn("+int b=3;", diffs["runtime-recomp/src/new.cpp"])
        self.assertNotIn("+int v", diffs["runtime-recomp/src/new.cpp"])


if __name__ == "__main__":
    unittest.main()
