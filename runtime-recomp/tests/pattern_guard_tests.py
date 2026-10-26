#!/usr/bin/env python3
"""Every pattern guard rule fires on its positive fixture and never on its negative one."""

import contextlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/pattern-guard"
SPEC = importlib.util.spec_from_file_location("pattern_guard", ROOT / "scripts/pattern_guard.py")
pattern_guard = importlib.util.module_from_spec(SPEC)
sys.modules["pattern_guard"] = pattern_guard  # dataclasses resolve their module by name
SPEC.loader.exec_module(pattern_guard)


def findings(case):
    tree = pattern_guard.Tree(FIXTURES / case)
    return pattern_guard.scan(tree, tree.files)


def run_guard(root, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = pattern_guard.main(["--root", str(root), *args])
    return status, out.getvalue(), err.getvalue()


def git(root, *args):
    subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)


class FixtureTests(unittest.TestCase):
    def test_every_rule_has_a_positive_and_a_negative_fixture(self):
        cases = {path.relative_to(FIXTURES).as_posix() for path in FIXTURES.glob("*/*") if path.is_dir()}
        expected = {f"{rule}/{kind}" for rule in pattern_guard.RULES for kind in ("positive", "negative")}
        self.assertEqual(cases, expected)

    def test_positive_fixtures_fire_only_their_rule(self):
        for rule in pattern_guard.RULES:
            with self.subTest(rule=rule):
                hits = findings(f"{rule}/positive")
                self.assertTrue(hits, f"{rule} did not fire")
                self.assertEqual({hit.rule for hit in hits}, {rule}, [hit.render() for hit in hits])

    def test_positive_fixtures_flag_every_file(self):
        for rule in pattern_guard.RULES:
            with self.subTest(rule=rule):
                tree = pattern_guard.Tree(FIXTURES / rule / "positive")
                flagged = {hit.path for hit in pattern_guard.scan(tree, tree.files)}
                unflagged = set(tree.files) - flagged - {"patches/manifest.json"}
                self.assertEqual(unflagged, set(), "each positive file should be one violating form")

    def test_negative_fixtures_are_clean(self):
        for rule in pattern_guard.RULES:
            with self.subTest(rule=rule):
                self.assertEqual([hit.render() for hit in findings(f"{rule}/negative")], [])

    def test_every_rule_explains_itself(self):
        for rule in pattern_guard.RULES:
            status, out, _ = run_guard(ROOT, "--explain", rule)
            self.assertEqual(status, 0)
            self.assertIn("instead:", out)
        self.assertEqual(run_guard(ROOT, "--explain", "PG999")[0], 2)


class AllowlistTests(unittest.TestCase):
    def test_only_the_two_named_stages_may_write_guest_code(self):
        # AGENTS.md names exactly these; anything else goes in hooks.toml.
        self.assertEqual(
            set(pattern_guard.OWNED_INPUT_STAGES),
            {"scripts/save_layout_pipeline.py", "scripts/character_portrait_pipeline.py"},
        )
        self.assertEqual(
            pattern_guard.GUEST_REWRITE_ALLOWED - set(pattern_guard.OWNED_INPUT_STAGES),
            {"scripts/guest_hooks.py", "runtime-recomp/cmake/GuestHooks.cmake"},
        )


class ServicesScopeTests(unittest.TestCase):
    def test_pipeline_rules_skip_services_and_nothing_else(self):
        # A string-replace splice over first-party-named text: PG004 in the
        # runtime tree, out of scope under services/.
        source = (
            "from pathlib import Path\n"
            "text = Path('runtime-recomp/src/game/x.cpp').read_text()\n"
            "Path('out.cpp').write_text(text.replace('a', 'b'))\n"
        )
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for path in ("scripts/splice.py", "services/web/build.py"):
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_text(source)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "add", "-A"], check=True)
            _, out, err = run_guard(root, "--tree", "--baseline", "ignore")
        self.assertIn("scripts/splice.py:3: PG004", out + err)
        self.assertNotIn("services/", out + err)


class CheckContentTests(unittest.TestCase):
    def test_a_patch_registered_before_it_is_written_is_still_checked(self):
        # Registering first must not hide PG008: PG007 hashes the planned
        # content instead of reading a file that doesn't exist yet.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repo"
            shutil.copytree(FIXTURES / "PG008/positive", root)
            patch = root / "patches/rt64/0001-sidebar.patch"
            text = patch.read_text(encoding="utf-8")
            patch.unlink()
            findings = pattern_guard.check_content(root, "patches/rt64/0001-sidebar.patch", text)
        self.assertIn("PG008", {finding.rule for finding in findings})


class CMakeTokenizerTests(unittest.TestCase):
    def test_comments_quotes_brackets_and_lines(self):
        commands = pattern_guard.parse_cmake(
            '# set(ignored)\n#[[ set(also ignored)\n]]\nSET(a "b c" [=[d)e]=] f) # tail\nif(x AND (y OR z))\nendif()\n'
        )
        self.assertEqual(
            [(c.name, c.args, c.line) for c in commands],
            [
                ("set", ("a", "b c", "d)e", "f"), 4),
                ("if", ("x", "AND", "y", "OR", "z"), 5),
                ("endif", (), 6),
            ],
        )


class BaselineTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name) / "repo"
        shutil.copytree(FIXTURES / "PG001/positive", self.root)
        self.baseline = self.root / pattern_guard.BASELINE

    def write_baseline(self, *entries):
        self.baseline.parent.mkdir(parents=True, exist_ok=True)
        self.baseline.write_text(pattern_guard.BASELINE_HEADER + "".join(f"{e}\n" for e in entries))

    def test_unlisted_findings_fail_and_listed_ones_pass(self):
        status, out, _ = run_guard(self.root, "--tree")
        self.assertEqual(status, 1)
        self.assertIn("patches/feature/ReleaseOverlay.cmake:1: PG001 ", out)
        self.assertIn("    instead: ", out)
        self.assertEqual(run_guard(self.root, "--tree", "--baseline", "write")[0], 0)
        self.assertEqual(run_guard(self.root, "--tree")[0], 0)
        self.assertIn("patches/feature/CMakeLists.txt PG001\n", self.baseline.read_text())

    def test_fixed_findings_must_leave_the_baseline(self):
        run_guard(self.root, "--tree", "--baseline", "write")
        (self.root / "patches/feature/CMakeLists.txt").unlink()
        status, out, _ = run_guard(self.root, "--tree")
        self.assertEqual(status, 1)
        self.assertIn("PG001 stale entry, patches/feature/CMakeLists.txt no longer matches", out)

    def test_malformed_baselines_are_usage_errors(self):
        for entries in (
            ["patches/feature/*.cmake PG001"],
            ["patches/feature/ReleaseOverlay.cmake PG001", "patches/feature/CMakeLists.txt PG001"],
            ["patches/feature/CMakeLists.txt PG999"],
        ):
            with self.subTest(entries=entries):
                self.write_baseline(*entries)
                status, _, err = run_guard(self.root, "--tree")
                self.assertEqual(status, 2, err)

    def test_ignore_reports_every_hit(self):
        run_guard(self.root, "--tree", "--baseline", "write")
        status, out, _ = run_guard(self.root, "--tree", "--baseline", "ignore")
        self.assertEqual((status, out.count(": PG001 ")), (1, 2))


@unittest.skipIf(shutil.which("git") is None, "diff scopes need git")
class DiffScopeTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        git(self.root, "init", "--quiet")
        git(self.root, "config", "user.email", "guard@example.invalid")
        git(self.root, "config", "user.name", "Guard")
        shutil.copytree(FIXTURES / "PG001/positive", self.root, dirs_exist_ok=True)
        run_guard(self.root, "--tree", "--baseline", "write")
        git(self.root, "add", ".")
        git(self.root, "commit", "--quiet", "-m", "base")

    def test_staged_scope_checks_only_staged_paths(self):
        layer = self.root / "patches/new-layer/ReleaseOverlay.cmake"
        layer.parent.mkdir(parents=True)
        layer.write_text('message(STATUS "layer 44")\n')
        self.assertEqual(run_guard(self.root, "--staged")[0], 0)
        git(self.root, "add", str(layer))
        status, out, _ = run_guard(self.root, "--staged")
        self.assertEqual(status, 1)
        self.assertIn("patches/new-layer/ReleaseOverlay.cmake:1: PG001", out)

    def test_checkout_root_and_ignored_nested_builds(self):
        nested = self.root / "source" / "nested"
        nested.mkdir(parents=True)
        self.assertEqual(pattern_guard._git_toplevel(self.root), self.root.resolve())
        self.assertEqual(pattern_guard._git_toplevel(nested), self.root.resolve())
        (self.root / ".gitignore").write_text(".deps/\npackaging/**/build/\n")
        for name in (".deps/library/build/cache.lib", "packaging/android/app/build/generated/cache.json"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("generated\n")
        tree = pattern_guard.Tree(self.root)
        self.assertTrue(tree.is_git)
        self.assertFalse(any(path.startswith((".deps/", "packaging/")) for path in tree.files))

    def test_diff_scope_sees_untracked_files(self):
        (self.root / "patches/feature/NOTES.txt").write_text("notes\n")
        status, out, _ = run_guard(self.root, "--diff", "HEAD")
        self.assertEqual(status, 1)
        self.assertIn("patches/feature/NOTES.txt:1: PG013", out)

    def test_baseline_growth_needs_the_transition_label(self):
        layer = self.root / "patches/new-layer/ReleaseOverlay.cmake"
        layer.parent.mkdir(parents=True)
        layer.write_text('message(STATUS "layer 44")\n')
        run_guard(self.root, "--tree", "--baseline", "write")
        status, out, _ = run_guard(self.root, "--diff", "HEAD")
        self.assertEqual(status, 1)
        self.assertIn("PG001 new baseline entry for patches/new-layer/ReleaseOverlay.cmake", out)
        self.assertEqual(run_guard(self.root, "--diff", "HEAD", "--transition-layer")[0], 0)

    def test_shrinking_the_baseline_passes(self):
        (self.root / "patches/feature/CMakeLists.txt").unlink()
        self.assertEqual(run_guard(self.root, "--diff", "HEAD")[0], 1)
        run_guard(self.root, "--tree", "--baseline", "write")
        self.assertEqual(run_guard(self.root, "--diff", "HEAD")[0], 0)

    def test_unknown_base_is_a_usage_error(self):
        self.assertEqual(run_guard(self.root, "--diff", "no-such-ref")[0], 2)


if __name__ == "__main__":
    unittest.main()
