#!/usr/bin/env python3
"""Checksum-pinned files stay byte-exact: the checker catches changes, and the
pre-commit exclude list keeps every fixer away from them."""

import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("check_pinned_bytes", ROOT / "scripts/check_pinned_bytes.py")
check_pinned_bytes = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check_pinned_bytes)
CONFIG = (ROOT / ".pre-commit-config.yaml").read_text(encoding="utf-8")
CAPSULE_TEXT = b"pinned capsule line \r\n"
CAPSULE_NAME = hashlib.sha256(CAPSULE_TEXT).hexdigest()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run_checker(root, *paths):
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        status = check_pinned_bytes.main(["--root", str(root), *paths])
    return status, err.getvalue()


def top_level_exclude():
    """The top-level `exclude: |` block scalar, without needing PyYAML."""
    match = re.search(r"^exclude: \|\n((?:  .*\n)+)", CONFIG, re.M)
    return re.compile("".join(line[2:] for line in match[1].splitlines(keepends=True)))


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class RepositoryTests(unittest.TestCase):
    def test_every_pin_in_the_tree_matches(self):
        self.assertEqual(check_pinned_bytes.check(ROOT), [])

    def test_each_kind_of_pin_is_found(self):
        sources = {pin.source for pin in check_pinned_bytes.pins(ROOT).values()}
        self.assertIn("patches/manifest.json", sources)
        self.assertIn("patches/release-prep/v1.0.7/source-capsule-r2/manifest.json", sources)
        self.assertTrue(any(source.startswith("modding/reference-us-v") for source in sources), sources)
        self.assertIn("services/library-workflow/baseline/SHA256SUMS", sources)

    def test_staged_patches_are_pinned(self):
        manifest = json.loads((ROOT / "patches/manifest.json").read_text(encoding="utf-8"))
        staged = {patch["path"] for patch in manifest["stagedPatches"]}
        self.assertTrue(staged)
        self.assertLessEqual(staged, set(check_pinned_bytes.pins(ROOT)))

    def test_pre_commit_never_hands_a_pinned_file_to_a_fixer(self):
        exclude = top_level_exclude()
        reachable = [path for path in check_pinned_bytes.pins(ROOT) if not exclude.search(path)]
        self.assertEqual(reachable, [])

    def test_pinned_bytes_hook_runs_without_file_arguments(self):
        # The pinned files are excluded, so pre-commit would never pass them in.
        hook = CONFIG[CONFIG.index("id: pinned-bytes") :]
        self.assertIn("always_run: true", hook)
        self.assertIn("pass_filenames: false", hook)

    def test_hook_revisions_match_the_ci_tool_pins(self):
        lint_tools = (ROOT / ".github/lint-tools.txt").read_text(encoding="utf-8")
        workflows = "".join(
            (ROOT / ".github/workflows" / name).read_text(encoding="utf-8")
            for name in ("tooling.yml", "workflow-lint.yml")
        )

        def pinned(name):
            return re.search(rf"^{re.escape(name)}==(\S+)", lint_tools, re.M)[1]

        def released(tool):
            return re.search(rf"/{tool}/releases/download/v([0-9.]+)/", workflows)[1]

        expected = {
            "astral-sh/ruff-pre-commit": f"v{pinned('ruff')}",
            "pre-commit/mirrors-clang-format": f"v{pinned('clang-format')}",
            "BlankSpruce/gersemi-pre-commit": pinned("gersemi"),
            "shellcheck-py/shellcheck-py": f"v{pinned('shellcheck-py')}",
            "scop/pre-commit-shfmt": f"v{released('sh')}-1",
            "rhysd/actionlint": f"v{released('actionlint')}",
            "zizmorcore/zizmor-pre-commit": f"v{released('zizmor')}",
        }
        revs = dict(re.findall(r"- repo: https://github\.com/(\S+)\n\s+rev: (\S+)", CONFIG))
        self.assertEqual({repo: revs.get(repo) for repo in expected}, expected)


class CheckerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.patch = b"--- a/x\n+++ b/x\n@@ -1,3 +1,3 @@\n \n-old\n+new\n \n"
        self.symbols = b"symbol = 0x80000000\r\n"
        manifest = {"dependencies": [{"patches": [{"path": "patches/dep/0001.patch", "sha256": sha(self.patch)}]}]}
        write(self.root / "patches/manifest.json", json.dumps(manifest).encode())
        write(self.root / "patches/dep/0001.patch", self.patch)
        receipt = {"files": {"symbols.ld": sha(self.symbols)}}
        write(self.root / "modding/reference-us-v77/receipt.json", json.dumps(receipt).encode())
        write(self.root / "modding/reference-us-v77/symbols.ld", self.symbols)
        write(self.root / f"patches/release-prep/v1/source-capsule-r1/{CAPSULE_NAME}.txt", CAPSULE_TEXT)

    def test_untouched_pins_pass(self):
        self.assertEqual(run_checker(self.root), (0, ""))

    def test_stripped_whitespace_on_a_patch_context_line_fails(self):
        # What trailing-whitespace does to a blank context line.
        write(self.root / "patches/dep/0001.patch", b"--- a/x\n+++ b/x\n@@ -1,3 +1,3 @@\n\n-old\n+new\n\n")
        status, err = run_checker(self.root)
        self.assertEqual(status, 1)
        self.assertIn("patches/dep/0001.patch: sha256", err)
        self.assertIn("pinned by patches/manifest.json", err)

    def test_crlf_normalisation_of_a_receipt_file_fails(self):
        write(self.root / "modding/reference-us-v77/symbols.ld", b"symbol = 0x80000000\n")
        status, err = run_checker(self.root)
        self.assertEqual(status, 1)
        self.assertIn("pinned by modding/reference-us-v77/receipt.json", err)

    def test_changed_service_baseline_fails(self):
        page = b"<html>  \n</html>\n"
        write(self.root / "services/web/baseline/index.html", page)
        write(self.root / "services/web/baseline/SHA256SUMS", f"{sha(page)}  index.html\n".encode())
        self.assertEqual(run_checker(self.root), (0, ""))
        write(self.root / "services/web/baseline/index.html", b"<html>\n</html>\n")
        status, err = run_checker(self.root)
        self.assertEqual(status, 1)
        self.assertIn("pinned by services/web/baseline/SHA256SUMS", err)

    def test_changed_capsule_file_fails(self):
        write(self.root / f"patches/release-prep/v1/source-capsule-r1/{CAPSULE_NAME}.txt", CAPSULE_TEXT.strip())
        status, err = run_checker(self.root)
        self.assertEqual(status, 1)
        self.assertIn("pinned by its file name", err)

    def write_capsule_manifest(self, base_text):
        capsule = {
            "roles": {
                "ROLE": {
                    "object.txt": {"sha256": CAPSULE_NAME, "object": f"{CAPSULE_NAME}.txt"},
                    "base.py": {"sha256": "0" * 64, "base": "src/base.py", "base_sha256": sha(base_text)},
                }
            }
        }
        write(self.root / "patches/release-prep/v1/source-capsule-r1/manifest.json", json.dumps(capsule).encode())

    def test_deleted_object_the_capsule_manifest_references_fails(self):
        self.write_capsule_manifest(b"base\n")
        write(self.root / "src/base.py", b"base\n")
        self.assertEqual(run_checker(self.root), (0, ""))
        (self.root / f"patches/release-prep/v1/source-capsule-r1/{CAPSULE_NAME}.txt").unlink()
        status, err = run_checker(self.root, "patches/release-prep/v1/source-capsule-r1/manifest.json")
        self.assertEqual(status, 1)
        self.assertIn(f"{CAPSULE_NAME}.txt: missing", err)

    def test_capsule_base_is_compared_after_crlf_normalisation(self):
        self.write_capsule_manifest(b"base\n")
        write(self.root / "src/base.py", b"base\r\n")
        self.assertEqual(run_checker(self.root), (0, ""))
        write(self.root / "src/base.py", b"base  \n")  # what a formatter might leave
        status, err = run_checker(self.root, "src/base.py")
        self.assertEqual(status, 1)
        self.assertIn("pinned by patches/release-prep/v1/source-capsule-r1/manifest.json", err)

    def test_missing_pinned_file_fails(self):
        (self.root / "patches/dep/0001.patch").unlink()
        status, err = run_checker(self.root)
        self.assertEqual(status, 1)
        self.assertIn("missing", err)

    def write_capsule_base(self, data):
        base = b"first-party base\n"
        manifest = {"roles": {"ROLE": {"x.cpp": {"base": "runtime-recomp/src/x.cpp", "base_sha256": sha(base)}}}}
        write(self.root / "patches/release-prep/v1/source-capsule-r1/manifest.json", json.dumps(manifest).encode())
        write(self.root / "runtime-recomp/src/x.cpp", data)

    def test_changed_capsule_base_fails(self):
        self.write_capsule_base(b"folded bytes\n")
        status, err = run_checker(self.root)
        self.assertEqual(status, 1)
        self.assertIn("runtime-recomp/src/x.cpp: sha256", err)

    def test_released_capsule_base_may_change(self):
        self.write_capsule_base(b"folded bytes\n")
        write(self.root / check_pinned_bytes.RELEASED_BASES, b"runtime-recomp/src/x.cpp\n")
        self.assertEqual(run_checker(self.root), (0, ""))

    def test_released_list_names_only_changed_capsule_bases(self):
        self.write_capsule_base(b"first-party base\n")
        write(self.root / check_pinned_bytes.RELEASED_BASES, b"runtime-recomp/src/x.cpp\nscripts/other.py\n")
        status, err = run_checker(self.root)
        self.assertEqual(status, 1)
        self.assertIn("runtime-recomp/src/x.cpp: listed in", err)
        self.assertIn("matches its pin again", err)
        self.assertIn("scripts/other.py: listed in", err)
        self.assertIn("not a capsule base", err)

    def test_paths_limit_the_check_and_manifests_expand_to_their_files(self):
        write(self.root / "patches/dep/0001.patch", b"changed\n")
        self.assertEqual(run_checker(self.root, "modding/reference-us-v77/symbols.ld")[0], 0)
        self.assertEqual(run_checker(self.root, "unrelated.txt")[0], 0)
        self.assertEqual(run_checker(self.root, "patches/dep/0001.patch")[0], 1)
        self.assertEqual(run_checker(self.root, "./patches/manifest.json")[0], 1)


@unittest.skipUnless(
    os.environ.get("DKR_PRE_COMMIT_INTEGRATION") == "1",
    "real pre-commit run; use python3 scripts/check.py --suite host-tools",
)
@unittest.skipUnless(shutil.which("pre-commit"), "pre-commit is not installed (python3 scripts/bootstrap_hooks.py)")
class HookRunTests(unittest.TestCase):
    """Run the real hook set over copies of pinned files and a control file."""

    def test_full_hook_run_leaves_pinned_bytes_alone(self):
        manifest = json.loads((ROOT / "patches/manifest.json").read_text(encoding="utf-8"))
        patches = [patch for dependency in manifest["dependencies"] for patch in dependency.get("patches", [])]
        # A patch with whitespace on a blank context line is what a fixer would break.
        patch = next(p for p in patches if re.search(rb"\n[ \t]+\r?\n", (ROOT / p["path"]).read_bytes()))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in (".pre-commit-config.yaml", "pyproject.toml", ".editorconfig", "scripts/check_pinned_bytes.py"):
                write(root / name, (ROOT / name).read_bytes())
            write(root / patch["path"], (ROOT / patch["path"]).read_bytes())
            reduced = {"dependencies": [{"patches": [patch]}]}
            write(root / "patches/manifest.json", json.dumps(reduced).encode())
            shutil.copytree(ROOT / "modding/reference-us-v77", root / "modding/reference-us-v77")
            write(root / "notes.txt", b"trailing space \nno final newline")
            pinned = {path: sha((root / path).read_bytes()) for path in check_pinned_bytes.pins(root)}
            self.assertGreater(len(pinned), 1)
            for args in (("init", "-q"), ("add", "-A")):
                subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)
            env = {**os.environ, "SKIP": "pattern-guard"}
            result = subprocess.run(
                ["pre-commit", "run", "--all-files", "--color", "never"],
                cwd=root,
                env=env,
                capture_output=True,
                text=True,
            )
            output = result.stdout + result.stderr
            # The control file proves the fixers ran; they fail the run by fixing it.
            self.assertEqual((root / "notes.txt").read_bytes(), b"trailing space\nno final newline\n", output)
            self.assertRegex(output, r"checksum-pinned files untouched by fixers\.+Passed", output)
            self.assertEqual({path: sha((root / path).read_bytes()) for path in pinned}, pinned, output)


if __name__ == "__main__":
    unittest.main()
