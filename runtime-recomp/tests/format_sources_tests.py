#!/usr/bin/env python3
"""scripts/format_sources.py and format_tokens.py: the layout-only proof sees
through layout and nothing else, and the never-format list is honoured by
the clang-format ignore file the hooks read."""

from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import format_sources  # noqa: E402
from format_tokens import tokens  # noqa: E402


class TokenTests(unittest.TestCase):
    def same(self, a: str, b: str) -> bool:
        return tokens(a.encode()) == tokens(b.encode())

    def test_layout_and_comments_are_ignored(self):
        self.assertTrue(self.same("int  f(int a,int b){return a+b;}", "int f(int a, int b) {\n    return a + b;\n}\n"))
        self.assertTrue(self.same("x = 1; // one\n/* two */ y = 2;", "x = 1;\ny = 2; // reflowed\n"))
        self.assertTrue(self.same("#define M(a) \\\n  (a)\n", "#define M(a) (a)\n"))
        self.assertTrue(self.same("std::vector<std::vector<int> > v;", "std::vector<std::vector<int>> v;"))

    def test_token_changes_are_seen(self):
        self.assertFalse(self.same("a = b;", "a = c;"))
        self.assertFalse(self.same('s = "a b";', 's = "a  b";'))
        self.assertFalse(self.same('s = "ab";', 's = "a" "b";'))
        self.assertFalse(self.same("using a::X;\nusing a::Y;", "using a::Y;\nusing a::X;"))

    def test_literals_are_kept_whole(self):
        self.assertEqual(tokens(b'u8"//not a comment"'), ['u8"//not a comment"'])
        self.assertEqual(tokens(b'R"x(a "quoted" )" b)x" ;'), ['R"x(a "quoted" )" b)x"', ";"])
        self.assertEqual(tokens(b"c = '\\'';"), ["c", "=", "'\\''", ";"])
        self.assertEqual(tokens(b"n = 1'000'000ull;"), ["n", "=", "1'000'000ull", ";"])


class ProofTests(unittest.TestCase):
    def test_cmake_words_ignore_layout_and_comments_only(self):
        words = format_sources.cmake_words
        self.assertEqual(words(b'set(a "x # y") # note\nf([[b]])\n'), words(b'set(\n    a\n    "x # y")\nf( [[b]] )\n'))
        self.assertNotEqual(words(b'set(a "x")'), words(b'set(a "y")'))
        self.assertNotEqual(words(b"set(a b)"), words(b"set(a b c)"))

    def test_a_pin_holder_may_only_swap_reformatted_digests(self):
        old, new = "a" * 64, "b" * 64
        before = f'PINS = {{"x.cpp": "{old}"}}\n'.encode()
        after = f'PINS = {{\n    "x.cpp": "{new}",\n}}\n'.encode()
        tree = format_sources.python_tree
        self.assertEqual(tree(before, {old: new}), tree(after))
        self.assertNotEqual(tree(before, {}), tree(after))
        self.assertNotEqual(tree(before, {old: new}), tree(after.replace(b"x.cpp", b"y.cpp")))


class NeverListTests(unittest.TestCase):
    def test_every_never_formatted_file_is_in_the_ignore_file(self):
        patterns = [
            line.strip()
            for line in (ROOT / ".clang-format-ignore").read_text().splitlines()
            if line.strip() and not line.startswith("#")
        ]
        listed = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).splitlines()
        never = [n for n in listed if n.startswith(format_sources.NEVER) and format_sources.C_FAMILY.search(n)]
        self.assertTrue(never)
        for name in never:
            with self.subTest(file=name):
                self.assertIn(name.rsplit("/", 1)[0] + "/*", patterns)

    def test_the_format_set_excludes_the_never_list(self):
        c_files, cmake_files = format_sources.files()
        self.assertFalse([n for n in c_files + cmake_files if n.startswith(format_sources.NEVER)])
        self.assertIn("runtime-recomp/CMakeLists.txt", cmake_files)
        self.assertTrue(any(n.startswith("runtime-recomp/src/") for n in c_files))

    def test_the_image_and_tools_are_pinned(self):
        self.assertRegex(format_sources.IMAGE, r"@sha256:[0-9a-f]{64}$")
        requirements = format_sources.requirements()
        for tool in format_sources.TOOLS:
            self.assertRegex(requirements, rf"(?m)^{re.escape(tool)}==[0-9.]+ \\$")


if __name__ == "__main__":
    unittest.main()
