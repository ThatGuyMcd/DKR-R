#!/usr/bin/env python3
"""scripts/check_doc_links.py flags missing files and anchors, ignores code, and
passes on the tracked docs."""

from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import check_doc_links  # noqa: E402


class DocLinkTests(unittest.TestCase):
    def problems(self, files: dict[str, str]) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "repo"
            for name, text in files.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text, encoding="utf-8")
            return check_doc_links.check(root, sorted(name for name in files if name.endswith(".md")))

    def test_missing_file(self):
        found = self.problems({"a.md": "x\n[gone](nope.md) ![img](pics/a.png)\n"})
        self.assertEqual(found, ["a.md:2: missing file nope.md", "a.md:2: missing file pics/a.png"])

    def test_bad_anchor(self):
        found = self.problems({"a.md": "[x](b.md#nowhere)\n", "b.md": "# Real\n"})
        self.assertEqual(found, ["a.md:1: no anchor #nowhere in b.md"])

    def test_good_links(self):
        docs = {
            "a.md": "[s](b.md#my-heading-1) [t](b.md#code-here) [u](#top) [v](https://x.test/missing.md)\n# Top\n",
            # "My Heading" twice gives my-heading and my-heading-1.
            "b.md": "# My `Heading`\n## My Heading\n# `code` here!\n",
        }
        self.assertEqual(self.problems(docs), [])

    def test_links_resolve_from_the_linking_file_or_the_root(self):
        docs = {
            "docs/a.md": "[up](../README.md) [beside](b.md) [rooted](/docs/b.md) [script](../tool.py)\n",
            "docs/b.md": "[wrong](docs/a.md)\n",
            "README.md": "",
            "tool.py": "",
        }
        self.assertEqual(self.problems(docs), ["docs/b.md:1: missing file docs/a.md"])

    def test_slug_keeps_snake_case_and_strips_emphasis(self):
        docs = {
            "a.md": (
                "[1](b.md#scriptscheck_doc_linkspy) [2](b.md#magic_codes-format) "
                "[3](b.md#emphasised) [4](b.md#bold-move)\n"
            ),
            "b.md": "## scripts/check_doc_links.py\n## MAGIC_CODES format\n## _emphasised_\n## **bold** move\n",
        }
        self.assertEqual(self.problems(docs), [])

    def test_fenced_code_and_code_spans_ignored(self):
        text = "```\n[x](missing.md)\n# Fake\n```\n`[y](missing.md)` ok\n[z](#fake)\n"
        self.assertEqual(self.problems({"a.md": text}), ["a.md:6: no anchor #fake in a.md"])

    def test_a_link_out_of_the_repository_is_a_problem_even_when_it_exists(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "neighbour.md").write_text("", encoding="utf-8")
            root = Path(tmp) / "repo"
            root.mkdir()
            (root / "a.md").write_text("[n](../neighbour.md)\n", encoding="utf-8")
            found = check_doc_links.check(root, ["a.md"])
        self.assertEqual(found, ["a.md:1: ../neighbour.md points outside the repository"])

    def test_staged_release_pages_are_skipped_and_their_neighbours_are_not(self):
        docs = {
            # Written for docs/, where the release stages it.
            "docs/releases/1.0.11/pages/PLATFORMS.md": "[b](BUILDING.md)\n",
            "docs/releases/1.0.11/NOTES.md": "[gone](gone.md) [page](pages/PLATFORMS.md)\n",
        }
        self.assertEqual(self.problems(docs), ["docs/releases/1.0.11/NOTES.md:1: missing file gone.md"])

    def test_links_into_absent_dependency_checkouts_are_skipped(self):
        docs = {"a.md": "[rt64](extern/rt64/README.md) [typo](extren/rt64/README.md)\n"}
        self.assertEqual(self.problems(docs), ["a.md:1: missing file extren/rt64/README.md"])

    def test_a_symlinked_dependency_checkout_counts_as_inside(self):
        # A worktree can share another checkout's extern/<dep> through a symlink.
        with tempfile.TemporaryDirectory() as tmp:
            shared = Path(tmp) / "shared" / "rt64"
            shared.mkdir(parents=True)
            (shared / "README.md").write_text("# Build\n", encoding="utf-8")
            root = Path(tmp) / "repo"
            (root / "extern").mkdir(parents=True)
            try:
                (root / "extern" / "rt64").symlink_to(shared, target_is_directory=True)
            except OSError:
                self.skipTest("this account cannot create symlinks")
            text = "[ok](extern/rt64/README.md#build) [bad](extern/rt64/README.md#nope)\n"
            (root / "a.md").write_text(text, encoding="utf-8")
            found = check_doc_links.check(root, ["a.md"])
        self.assertEqual(found, ["a.md:1: no anchor #nope in extern/rt64/README.md"])

    def test_the_tracked_docs_have_no_broken_links(self):
        found = check_doc_links.check(ROOT)
        self.assertEqual(found, [], "\n" + "\n".join(found))


if __name__ == "__main__":
    unittest.main()
