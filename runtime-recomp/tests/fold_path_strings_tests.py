#!/usr/bin/env python3
"""Reference-path normalization must preserve filesystem expressions."""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/fold"))
import path_strings  # noqa: E402


class PathNormalizationTests(unittest.TestCase):
    def normalize(self, text):
        def resolve(path):
            return Path("/folded/source.cpp") if path == Path("/reference/source.cpp") else None

        return path_strings.normalize(text, resolve)

    def test_actual_file_path_is_mapped(self):
        self.assertEqual(self.normalize('fail("/reference/source.cpp", 42);'), 'fail("/folded/source.cpp", 42);')

    def test_filesystem_division_keeps_its_operators(self):
        source = 'development?directory/"saves"/developer_folder/"paks":prepared->paks;'
        self.assertEqual(self.normalize(source), source)

    def test_adjacent_strings_are_independent(self):
        self.assertEqual(self.normalize('"prefix" "/reference/source.cpp"'), '"prefix" "/folded/source.cpp"')

    def test_escaped_quote_cannot_start_a_path(self):
        source = r'"quoted \"/reference/source.cpp\" text"'
        self.assertEqual(self.normalize(source), source)

    def test_raw_string_body_is_not_normalized(self):
        source = 'R"tag(a "/reference/source.cpp" literal)tag"'
        self.assertEqual(self.normalize(source), source)

    def test_character_quote_does_not_confuse_string_boundaries(self):
        self.assertEqual(self.normalize('\'"\'; "/reference/source.cpp"'), '\'"\'; "/folded/source.cpp"')

    def test_unknown_paths_keep_exact_bytes(self):
        self.assertEqual(self.normalize('"/unknown/a/../b/"'), '"/unknown/a/../b/"')


if __name__ == "__main__":
    unittest.main()
