#!/usr/bin/env python3
"""Zero-dependency tests for the build tooling: the dependency patch pipeline,
the reviewed-ELF image digest, and repository contracts the shell build
entry points rely on. Needs only git and Python."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import apply_dependency_patches as patches  # noqa: E402
import bootstrap_dependencies as bootstrap  # noqa: E402
import legacy_character_presentation_policy as presentation  # noqa: E402
import package_source  # noqa: E402
import validate_release_metadata as release_metadata  # noqa: E402

# Hermetic: the developer's global config (signing, hooks, autocrlf) must not
# reach the fixture repositories.
GIT_ENV = {
    **os.environ,
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "t",
    "GIT_AUTHOR_EMAIL": "t@example.invalid",
    "GIT_COMMITTER_NAME": "t",
    "GIT_COMMITTER_EMAIL": "t@example.invalid",
}


def git(repo, *args):
    return subprocess.run(
        ["git", "-C", str(repo), *args], check=True, env=GIT_ENV, capture_output=True, text=True
    ).stdout.strip()


def write(path, text):
    # Byte-exact: Windows text mode would otherwise turn \n into \r\n.
    path.write_text(text, newline="\n")


class SourcePackageTests(unittest.TestCase):
    def test_new_custom_backgrounds_and_authored_shaders_are_included(self):
        selected = [
            Path("assets/ui/CustomTrackBackgrounds/custom-row-1.png"),
            Path("assets/ui/CustomTrackBackgrounds/rt64.json"),
            Path("patches/enhanced-water/native/LavaLightingCS.hlsl"),
            Path("patches/enhanced-water/native/LavaFlow.hlsli"),
        ]
        excluded = [
            Path("downloads/example.png"),
            Path("assets/ui/CustomTrackBackgrounds/game.z64"),
            Path("patches/private.dkr-probe"),
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in selected + excluded:
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                (root / path).write_bytes(b"fixture")
            with (
                patch.object(package_source, "ROOT", root),
                patch.object(package_source, "git_paths", side_effect=[[], selected + excluded]),
            ):
                self.assertEqual(package_source.source_files(), sorted(selected))


class ReleaseMetadataTests(unittest.TestCase):
    def test_current_release_metadata(self):
        self.assertEqual(release_metadata.main(), 0)

    def test_patched_dependencies_require_matching_lock_entries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "packaging/linux").mkdir(parents=True)
            (root / "patches").mkdir()
            (root / "VERSION").write_text("1.0.5")
            (root / "packaging/linux/dkr-port.appdata.xml").write_text('<release version="1.0.5"/>')
            for name in (
                "README.md",
                "THIRD_PARTY.md",
                "docs/README.md",
                "docs/BUILDING.md",
                "docs/PLATFORMS.md",
                "packaging/RELEASE-README.md",
                "packaging/MACOS-BUILD-README.md",
            ):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("")
            manifest = {"dependencies": [{"name": "NewDependency", "expectedCommit": "abc123", "patches": []}]}
            (root / "patches/manifest.json").write_text(json.dumps(manifest))
            lock_path = root / "dependencies.lock.json"
            with patch.object(release_metadata, "ROOT", root):
                lock_path.write_text(json.dumps({"dependencies": []}))
                with self.assertRaisesRegex(RuntimeError, "missing from lockfile"):
                    release_metadata.main()
                lock = {"dependencies": [{"name": "newdependency", "commit": "ABC123"}]}
                lock_path.write_text(json.dumps(lock))
                self.assertEqual(release_metadata.main(), 0)
                lock["dependencies"][0]["commit"] = "def456"
                lock_path.write_text(json.dumps(lock))
                with self.assertRaisesRegex(RuntimeError, "Pinned commit mismatch"):
                    release_metadata.main()

    def test_invalid_version_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for version in (
                "1.0.5-beta.0",
                "1.0.5-beta.15-playtest.0",
                "1.0.5-beta.15-playtest.15-fix.0",
                "1.0.5-garbage",
            ):
                with self.subTest(version=version):
                    (root / "VERSION").write_text(version)
                    with patch.object(release_metadata, "ROOT", root):
                        with self.assertRaisesRegex(RuntimeError, "semantic version"):
                            release_metadata.main()

    def test_unknown_dependency_names_and_historical_notes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in (
                "README.md",
                "THIRD_PARTY.md",
                "docs/README.md",
                "docs/BUILDING.md",
                "docs/PLATFORMS.md",
                "packaging/RELEASE-README.md",
                "packaging/MACOS-BUILD-README.md",
            ):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("Current documentation\n")
            (root / "VERSION").write_text("1.0.5")
            (root / "packaging/linux").mkdir()
            (root / "packaging/linux/dkr-port.appdata.xml").write_text('<release version="1.0.5"/>')
            (root / "patches").mkdir()
            (root / "patches/manifest.json").write_text(
                json.dumps({"dependencies": [{"name": "new-library", "expectedCommit": "abc", "patches": []}]})
            )
            (root / "dependencies.lock.json").write_text(
                json.dumps({"dependencies": [{"name": "new-library", "commit": "abc"}]})
            )
            (root / "docs/historical-notes.md").write_text("Earlier milestone and release candidate\n")
            with patch.object(release_metadata, "ROOT", root):
                self.assertEqual(release_metadata.main(), 0)
                (root / "dependencies.lock.json").write_text('{"dependencies": []}')
                with self.assertRaisesRegex(RuntimeError, "missing from lockfile: new-library"):
                    release_metadata.main()


class PatchFixture:
    """Stacked patches where the second edits the first's file and adds a new
    one, which is exactly what broke the old all-or-reset logic."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        upstream = self.root / "upstream"
        upstream.mkdir()
        git(upstream, "init", "-q")
        write(upstream / "a.txt", "one\ntwo\nthree\n")
        write(upstream / "unrelated.txt", "original\n")
        (upstream / "blob.bin").write_bytes(bytes(range(256)))
        git(upstream, "add", ".")
        git(upstream, "commit", "-qm", "base")
        self.commit = git(upstream, "rev-parse", "HEAD")

        def make_patch(name, edit):
            edit(upstream)
            git(upstream, "add", "-A")
            diff = git(upstream, "diff", "--cached", "--binary") + "\n"
            git(upstream, "commit", "-qm", name)
            path = self.root / "patches" / f"{name}.patch"
            path.parent.mkdir(exist_ok=True)
            write(path, diff)
            return path

        first = make_patch("0001", lambda r: write(r / "a.txt", "one\nTWO\nthree\n"))
        # The second patch also creates a binary and an empty file and deletes
        # one: shapes a patch-text parser gets wrong.
        second = make_patch(
            "0002",
            lambda r: (
                write(r / "a.txt", "one\nTWO\nTHREE\n"),
                write(r / "new.txt", "created\n"),
                (r / "new.bin").write_bytes(b"\0\xff" * 8),
                (r / "empty.txt").write_bytes(b""),
                (r / "blob.bin").unlink(),
            ),
        )
        self.final_a = "one\nTWO\nTHREE\n"
        self.patches = [first, second]
        # The dependency is checked out through `git worktree`, giving it a
        # .git *file* like a submodule (the old pipeline skipped those).
        self.repo = self.root / "dep"
        git(upstream, "worktree", "add", "-q", "--detach", str(self.repo), self.commit)
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(
            json.dumps(
                {
                    "schemaVersion": 1,
                    "dependencies": [
                        {
                            "name": "dep",
                            "repositoryPath": "dep",
                            "expectedCommit": self.commit,
                            "patches": [
                                {
                                    "path": str(p.relative_to(self.root)),
                                    "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
                                }
                                for p in self.patches
                            ],
                        }
                    ],
                }
            )
        )

    def tearDown(self):
        self.temp.cleanup()

    def apply(self):
        return patches.apply_manifest(self.root, self.manifest)

    def assert_patched(self):
        self.assertEqual((self.repo / "a.txt").read_text(), self.final_a)
        self.assertEqual((self.repo / "new.txt").read_text(), "created\n")
        self.assertEqual((self.repo / "new.bin").read_bytes(), b"\0\xff" * 8)
        self.assertEqual((self.repo / "empty.txt").read_bytes(), b"")
        self.assertFalse((self.repo / "blob.bin").exists())


class PatchPipelineTests(PatchFixture, unittest.TestCase):
    def test_submodule_style_checkout_is_patched(self):
        self.assertTrue((self.repo / ".git").is_file())
        self.apply()
        self.assert_patched()

    def test_rerun_is_a_no_op(self):
        self.apply()
        stamps = {}
        for name in ("a.txt", "new.txt", "new.bin", "empty.txt"):
            stamp = (self.repo / name).stat().st_mtime_ns - 10**9
            os.utime(self.repo / name, ns=(stamp, stamp))
            stamps[name] = (self.repo / name).stat().st_mtime_ns
        self.assertEqual(patches.sync_dependency(self.repo, self.patches), "unchanged")
        for name, stamp in stamps.items():
            self.assertEqual((self.repo / name).stat().st_mtime_ns, stamp, name)
        self.assert_patched()

    def test_interrupted_checkout_requires_explicit_repair(self):
        # First patch applied, plus the second patch's new file left behind
        # by an interrupted run (`git checkout -- .` doesn't remove it).
        git(self.repo, "apply", str(self.patches[0]))
        (self.repo / "new.txt").write_text("stale\n")
        with self.assertRaisesRegex(SystemExit, "unrecognized local edits"):
            self.apply()
        patches.apply_manifest(self.root, self.manifest, repair=True)
        self.assert_patched()

    def test_local_edit_is_preserved_and_explicit_repair_keeps_unrelated_untracked(self):
        self.apply()
        (self.repo / "a.txt").write_text("local edit\n")
        (self.repo / "helper-symlink-target").write_text("keep me\n")
        with self.assertRaisesRegex(SystemExit, "unrecognized local edits"):
            self.apply()
        self.assertEqual((self.repo / "a.txt").read_text(), "local edit\n")
        patches.apply_manifest(self.root, self.manifest, repair=True)
        self.assert_patched()
        self.assertEqual((self.repo / "helper-symlink-target").read_text(), "keep me\n")

    def test_exact_intermediate_patch_state_resumes(self):
        git(self.repo, "apply", str(self.patches[0]))
        self.apply()
        self.assert_patched()

    def test_check_validates_stacked_patches_without_checkout_mutation(self):
        before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.repo.iterdir() if p.is_file()}
        index_path = Path(git(self.repo, "rev-parse", "--git-path", "index"))
        index = index_path.read_bytes()
        self.assertEqual(patches.apply_manifest(self.root, self.manifest, check=True), 0)
        self.assertEqual(index_path.read_bytes(), index)
        self.assertEqual(
            before, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.repo.iterdir() if p.is_file()}
        )

    def test_required_missing_checkout_and_empty_check_are_rejected(self):
        data = json.loads(self.manifest.read_text())
        data["dependencies"].append({**data["dependencies"][0], "name": "missing", "repositoryPath": "absent"})
        self.manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(SystemExit, "dependency checkout is missing"):
            patches.apply_manifest(self.root, self.manifest, require_all=True)
        self.assertEqual((self.repo / "a.txt").read_text(), "one\ntwo\nthree\n")
        data["dependencies"] = data["dependencies"][1:]
        self.manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(SystemExit, "No dependency checkouts found"):
            patches.apply_manifest(self.root, self.manifest, check=True)

    def test_cli_check_and_failure_exit_codes(self):
        command = [
            sys.executable,
            str(ROOT / "scripts/apply_dependency_patches.py"),
            "--root",
            str(self.root),
            "--manifest",
            str(self.manifest),
            "--check",
        ]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("validated", result.stdout)
        self.assertEqual((self.repo / "a.txt").read_text(), "one\ntwo\nthree\n")
        write(self.repo / "a.txt", "developer work\n")
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unrecognized local edits", result.stderr)
        self.assertEqual((self.repo / "a.txt").read_text(), "developer work\n")

    def test_all_dependencies_preflight_before_first_write(self):
        data = json.loads(self.manifest.read_text())
        other = self.root / "other"
        git(self.root / "upstream", "worktree", "add", "-q", "--detach", str(other), self.commit)
        second = {**data["dependencies"][0], "name": "other", "repositoryPath": "other"}
        data["dependencies"].append(second)
        self.manifest.write_text(json.dumps(data))
        write(other / "a.txt", "developer work\n")
        with self.assertRaisesRegex(SystemExit, "unrecognized local edits"):
            self.apply()
        self.assertEqual((self.repo / "a.txt").read_text(), "one\ntwo\nthree\n")
        write(other / "a.txt", "one\ntwo\nthree\n")
        second["patches"] = [{**second["patches"][0], "sha256": "bad"}]
        self.manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(SystemExit, "checksum mismatch"):
            self.apply()
        self.assertEqual((self.repo / "a.txt").read_text(), "one\ntwo\nthree\n")

    def test_staged_managed_local_edit_is_preserved(self):
        write(self.repo / "a.txt", "staged developer work\n")
        git(self.repo, "add", "a.txt")
        write(self.repo / "a.txt", "one\ntwo\nthree\n")
        index = git(self.repo, "write-tree")
        with self.assertRaisesRegex(SystemExit, "unrecognized local edits"):
            self.apply()
        self.assertEqual(git(self.repo, "write-tree"), index)

    def test_checksum_is_enforced(self):
        write(self.patches[0], self.patches[0].read_text() + "\n")
        with self.assertRaisesRegex(SystemExit, "checksum mismatch"):
            self.apply()

    def test_commit_is_enforced(self):
        manifest = json.loads(self.manifest.read_text())
        manifest["dependencies"][0]["expectedCommit"] = "0" * 40
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(SystemExit, "commit mismatch"):
            self.apply()

    def test_unrelated_tracked_edit_is_preserved(self):
        (self.repo / "unrelated.txt").write_text("developer edit\n")
        with self.assertRaisesRegex(SystemExit, "local edits outside the patch set"):
            self.apply()
        self.assertEqual((self.repo / "unrelated.txt").read_text(), "developer edit\n")
        self.assertEqual((self.repo / "a.txt").read_text(), "one\ntwo\nthree\n")


class BootstrapPatchTests(PatchFixture, unittest.TestCase):
    """Exercise bootstrap against local-only origins and the real patch stack."""

    def setUp(self):
        super().setUp()
        self.env = patch.dict(os.environ, GIT_ENV)
        self.env.start()
        self.addCleanup(self.env.stop)
        self.manifest.rename(self.root / "patches" / "manifest.json")
        self.manifest = self.root / "patches" / "manifest.json"
        self.entry = {
            "name": "dep",
            "destination": "dep",
            "commit": self.commit,
            "repository": str(self.root / "upstream"),
        }
        git(self.repo, "remote", "add", "origin", self.entry["repository"])

    def bootstrap(self):
        with patch.object(bootstrap, "ROOT", self.root):
            return bootstrap.prepare_dependency(self.entry, False)

    def test_bootstrap_patch_bootstrap_preserves_files_and_index(self):
        self.bootstrap()
        self.apply()
        before = {p.name: p.stat().st_mtime_ns for p in self.repo.iterdir()}
        index = git(self.repo, "write-tree")
        with patch.object(bootstrap, "run", wraps=bootstrap.run) as commands:
            self.assertEqual(self.bootstrap()["commit"], self.commit)
        self.assertFalse(
            any(call.args[0][1] in ("fetch", "checkout", "reset", "clean") for call in commands.call_args_list)
        )
        self.assert_patched()
        self.assertEqual(git(self.repo, "write-tree"), index)
        self.assertEqual(before, {p.name: p.stat().st_mtime_ns for p in self.repo.iterdir()})

    def test_bootstrap_rejects_unexpected_work_without_mutation(self):
        for kind in ("tracked", "managed", "untracked", "staged"):
            with self.subTest(kind=kind):
                self.apply()
                target = (
                    self.repo
                    / {
                        "tracked": "unrelated.txt",
                        "managed": "a.txt",
                        "untracked": "leftover.txt",
                        "staged": "unrelated.txt",
                    }[kind]
                )
                original = target.read_bytes() if target.exists() else None
                target.write_bytes(b"developer work\n")
                if kind == "staged":
                    git(self.repo, "add", "--", target.name)
                    target.write_bytes(original)
                index = git(self.repo, "write-tree")
                work = target.read_bytes()
                with self.assertRaisesRegex(RuntimeError, "contains local changes"):
                    self.bootstrap()
                self.assertEqual(target.read_bytes(), work)
                self.assertEqual(git(self.repo, "write-tree"), index)
                if kind == "staged":
                    git(self.repo, "reset", "--", target.name)
                if original is None:
                    target.unlink()
                else:
                    target.write_bytes(original)

    def test_bootstrap_clones_and_checks_out_pinned_commit(self):
        self.entry["destination"] = "fresh"
        self.bootstrap()
        self.assertEqual((self.root / "fresh" / "a.txt").read_text(), "one\ntwo\nthree\n")
        self.assertEqual(git(self.root / "fresh", "rev-parse", "HEAD"), self.commit)

    def test_shallow_bootstrap_pins_old_revision_and_preserves_reviewed_edits(self):
        self.entry["destination"] = "fresh"
        with patch.object(bootstrap, "ROOT", self.root):
            bootstrap.prepare_dependency(self.entry, False, shallow=True)
        checkout = self.root / "fresh"
        self.assertEqual(git(checkout, "rev-parse", "HEAD"), self.commit)
        self.assertEqual(git(checkout, "rev-parse", "--is-shallow-repository"), "true")
        self.assertEqual(git(checkout, "rev-list", "--count", "HEAD"), "1")
        manifest = json.loads(self.manifest.read_text())
        manifest["dependencies"][0]["repositoryPath"] = "fresh"
        self.manifest.write_text(json.dumps(manifest))
        self.apply()
        with (
            patch.object(bootstrap, "ROOT", self.root),
            patch.object(bootstrap, "run", wraps=bootstrap.run) as commands,
        ):
            bootstrap.prepare_dependency(self.entry, False, shallow=True)
        self.assertFalse(
            any(call.args[0][1] in ("fetch", "checkout", "reset", "clean") for call in commands.call_args_list)
        )
        self.assertEqual((checkout / "a.txt").read_text(), self.final_a)
        write(checkout / "a.txt", "developer work\n")
        with patch.object(bootstrap, "ROOT", self.root), self.assertRaisesRegex(RuntimeError, "contains local changes"):
            bootstrap.prepare_dependency(self.entry, False, shallow=True)
        self.assertEqual((checkout / "a.txt").read_text(), "developer work\n")

    def test_bootstrap_rejects_incomplete_patch_stack(self):
        git(self.repo, "apply", str(self.patches[0]))
        with self.assertRaisesRegex(RuntimeError, "contains local changes"):
            self.bootstrap()
        self.assertEqual((self.repo / "a.txt").read_text(), "one\nTWO\nthree\n")

    def test_bootstrap_rejects_patch_manifest_pin_mismatch(self):
        self.apply()
        manifest = json.loads(self.manifest.read_text())
        manifest["dependencies"][0]["expectedCommit"] = "0" * 40
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(RuntimeError, "contains local changes"):
            self.bootstrap()
        self.assert_patched()

    def test_bootstrap_rejects_corrupt_reviewed_patch(self):
        self.apply()
        write(self.patches[0], self.patches[0].read_text() + "\n")
        with self.assertRaisesRegex(SystemExit, "checksum mismatch"):
            self.bootstrap()
        self.assert_patched()

    def test_bootstrap_preserves_reviewed_submodule_and_rejects_local_edits(self):
        child_origin = self.root / "child-origin"
        child_origin.mkdir()
        git(child_origin, "init", "-q")
        write(child_origin / "child.txt", "original\n")
        git(child_origin, "add", ".")
        git(child_origin, "commit", "-qm", "child base")
        child_commit = git(child_origin, "rev-parse", "HEAD")
        git(self.repo, "-c", "protocol.file.allow=always", "submodule", "add", str(child_origin), "nested child")
        git(self.repo, "commit", "-qm", "add child")
        self.commit = git(self.repo, "rev-parse", "HEAD")
        self.entry.update(commit=self.commit, recursive=True)
        child = self.repo / "nested child"
        write(child / "child.txt", "reviewed\n")
        patch_path = self.root / "patches/child.patch"
        write(patch_path, git(child, "diff") + "\n")
        manifest = json.loads(self.manifest.read_text())
        manifest["dependencies"][0]["expectedCommit"] = self.commit
        manifest["dependencies"].append(
            {
                "name": "child",
                "repositoryPath": "dep/nested child",
                "expectedCommit": child_commit,
                "patches": [
                    {"path": "patches/child.patch", "sha256": hashlib.sha256(patch_path.read_bytes()).hexdigest()}
                ],
            }
        )
        self.manifest.write_text(json.dumps(manifest))
        self.apply()
        before = (child / "child.txt").stat().st_mtime_ns
        self.bootstrap()
        self.assert_patched()
        self.assertEqual((child / "child.txt").read_text(), "reviewed\n")
        self.assertEqual((child / "child.txt").stat().st_mtime_ns, before)
        for name in ("child.txt", "untracked.txt"):
            with self.subTest(name=name):
                target = child / name
                target.write_text("developer work\n")
                with self.assertRaisesRegex(RuntimeError, "contains local changes"):
                    self.bootstrap()
                self.assertEqual(target.read_text(), "developer work\n")
                if name == "child.txt":
                    target.write_text("reviewed\n")
                else:
                    target.unlink()

        # Even explicit repair must preflight a later nested manifest entry
        # before it discards any managed edits in the parent checkout.
        write(self.repo / "a.txt", "parent developer work\n")
        write(patch_path, patch_path.read_text() + "\n")
        with self.assertRaisesRegex(SystemExit, "checksum mismatch"):
            patches.apply_manifest(self.root, self.manifest, repair=True)
        self.assertEqual((self.repo / "a.txt").read_text(), "parent developer work\n")
        self.assertEqual((child / "child.txt").read_text(), "reviewed\n")

    def test_nested_selection_initializes_parent_and_preserves_resolution_records(self):
        child_origin = self.root / "child-origin"
        child_origin.mkdir()
        git(child_origin, "init", "-q")
        write(child_origin / "child.txt", "original\n")
        git(child_origin, "add", ".")
        git(child_origin, "commit", "-qm", "child base")
        upstream = self.root / "upstream"
        git(upstream, "-c", "protocol.file.allow=always", "submodule", "add", str(child_origin), "nested child")
        git(upstream, "commit", "-qm", "add child")
        parent = {**self.entry, "destination": "fresh", "commit": git(upstream, "rev-parse", "HEAD")}
        child = {
            "name": "child",
            "destination": "fresh/nested child",
            "repository": str(child_origin),
            "commit": git(child_origin, "rev-parse", "HEAD"),
            "managedBy": "dep git submodule",
        }
        self.assertEqual([e["name"] for e in bootstrap.select_entries([child, parent], {"child"})], ["dep", "child"])
        lock = self.root / "lock.json"
        lock.write_text(json.dumps({"dependencies": [child, parent]}))
        resolved = self.root / "resolved.json"
        resolved.write_text(json.dumps({"dependencies": [{"name": "unrelated", "commit": "keep"}]}))
        with (
            patch.object(bootstrap, "ROOT", self.root),
            patch.object(bootstrap, "LOCK_FILE", lock),
            patch.object(bootstrap, "RESOLVED_FILE", resolved),
            patch.object(sys, "argv", ["bootstrap", "--only", "child"]),
            patch.dict(os.environ, {"GIT_ALLOW_PROTOCOL": "file"}),
        ):
            self.assertEqual(bootstrap.main(), 0)
        checkout = self.root / child["destination"]
        self.assertTrue((checkout / ".git").is_file(), "nested dependency must be a submodule, never a clone")
        self.assertEqual((checkout / "child.txt").read_text(), "original\n")
        self.assertIn("unrelated", [e["name"] for e in json.loads(resolved.read_text())["dependencies"]])
        write(checkout / "child.txt", "developer work\n")
        with patch.object(bootstrap, "ROOT", self.root):
            with self.assertRaisesRegex(RuntimeError, "dirty submodule"):
                bootstrap.prepare_dependency(parent, True)
        self.assertEqual((checkout / "child.txt").read_text(), "developer work\n")


def elf32be(sections):
    """Minimal big-endian ELF32 with the given (name, type, flags, addr, data)."""
    names = b"\0" + b"".join(n.encode() + b"\0" for n, *_ in sections) + b".shstrtab\0"
    body, offsets, cursor = b"", [], 52
    for _, kind, _, _, data in sections:
        offsets.append(cursor)
        if kind != 8:
            body += data
            cursor += len(data)
    shstr_offset = cursor
    body += names
    cursor += len(names)
    headers = b"\0" * 40
    name_at = 1
    for (name, kind, flags, addr, data), offset in zip(sections, offsets):
        headers += struct.pack(">10I", name_at, kind, flags, addr, offset, len(data), 0, 0, 4, 0)
        name_at += len(name) + 1
    headers += struct.pack(">10I", name_at, 3, 0, 0, shstr_offset, len(names), 0, 0, 1, 0)
    count = len(sections) + 2
    header = (
        b"\x7fELF\x01\x02\x01"
        + b"\0" * 9
        + struct.pack(">HHIIIIIHHHHHH", 2, 8, 1, 0, 0, cursor, 0, 52, 0, 0, 40, count, count - 1)
    )
    return header + body + headers


class ElfImageDigestTests(unittest.TestCase):
    def digest(self, sections):
        with tempfile.NamedTemporaryFile(suffix=".elf", delete=False) as handle:
            handle.write(elf32be(sections))
        try:
            return presentation.elf_image_digest(Path(handle.name))
        finally:
            os.unlink(handle.name)

    def base(self, symtab=b"\0" * 16, code=b"\x27\xbd\xff\xe0", bss=8):
        return [
            (".main", 1, 6, 0x80000400, code),
            (".bss", 8, 3, 0x80100000, b"\0" * bss),
            (".symtab", 2, 0, 0, symtab),
        ]

    def test_symbol_table_differences_are_ignored(self):
        self.assertEqual(self.digest(self.base()), self.digest(self.base(symtab=b"\1" * 32)))

    def test_code_and_layout_changes_are_detected(self):
        reference = self.digest(self.base())
        self.assertNotEqual(reference, self.digest(self.base(code=b"\x27\xbd\xff\xd8")))
        self.assertNotEqual(reference, self.digest(self.base(bss=16)))

    def test_non_elf_is_rejected(self):
        with tempfile.NamedTemporaryFile(delete=False) as handle:
            handle.write(b"not an elf")
        try:
            with self.assertRaises(ValueError):
                presentation.elf_image_digest(Path(handle.name))
        finally:
            os.unlink(handle.name)

    def test_reviewed_revision_gates_composition(self):
        with tempfile.NamedTemporaryFile(suffix=".elf", delete=False) as handle:
            handle.write(elf32be(self.base()))
        path = Path(handle.name)
        try:
            with patch.dict(presentation.ELFS, {"us.v77": presentation.elf_image_digest(path)}):
                self.assertEqual(presentation.reviewed_revision(path), "us.v77")
                with self.assertRaises(ValueError):
                    presentation.compose_presentation({}, path, "us.v80", [], {})
                path.write_bytes(elf32be(self.base(code=b"\x27\xbd\xff\xd8")))
                self.assertIsNone(presentation.reviewed_revision(path))
        finally:
            os.unlink(path)


class RepositoryContractTests(unittest.TestCase):
    @unittest.skipUnless(Path("/bin/bash").exists(), "requires system Bash")
    def test_build_prepares_without_cli_arguments_in_system_bash(self):
        # macOS ships Bash 3.2: expanding an empty array under set -u fails.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "bin").mkdir()
            (root / "scripts/macos_build_lock.py").write_text((ROOT / "scripts/macos_build_lock.py").read_text())
            (root / "scripts/lib").mkdir()
            (root / "scripts/lib/apple-build.sh").write_text((ROOT / "scripts/lib/apple-build.sh").read_text())
            build = root / "Build-macOS.sh"
            build.write_text((ROOT / "Build-macOS.sh").read_text())
            prepare = root / "scripts" / "Prepare-DKR-Runtime-macOS.sh"
            prepare.write_text('#!/bin/sh\necho "prepare called"\nexit 42\n')
            prepare.chmod(0o755)
            uname = root / "bin" / "uname"
            uname.write_text("#!/bin/sh\necho Darwin\n")
            uname.chmod(0o755)
            env = dict(
                os.environ, PATH=str(root / "bin") + os.pathsep + os.environ["PATH"], CMAKE_BUILD_PARALLEL_LEVEL="1"
            )
            env.pop("DKR_MAC_GENERATED_SOURCE", None)
            env.pop("DKR_MAC_V80_GENERATED_SOURCE", None)
            result = subprocess.run(["/bin/bash", str(build)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 42, result.stdout + result.stderr)
            self.assertIn("prepare called", result.stdout)

    @unittest.skipIf(os.name == "nt", "macOS shell scripts; Windows has no guaranteed bash")
    def test_macos_option_errors_are_actionable(self):
        # Argument probes must not contend with the real build running CTest.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "scripts/macos_build_lock.py").write_text((ROOT / "scripts/macos_build_lock.py").read_text())
            (root / "scripts/lib").mkdir()
            (root / "scripts/lib/apple-build.sh").write_text((ROOT / "scripts/lib/apple-build.sh").read_text())
            for script in ("Build-macOS.sh", "scripts/Prepare-DKR-Runtime-macOS.sh"):
                (root / script).write_text((ROOT / script).read_text())
                for option in ("--rom-v77", "--rom-v80", "--jobs"):
                    with self.subTest(script=script, option=option):
                        result = subprocess.run(["bash", str(root / script), option], capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("requires", result.stdout + result.stderr)
                        self.assertNotIn("unbound variable", result.stdout + result.stderr)

    @unittest.skipIf(os.name == "nt", "stage-tool checks require Bash")
    def test_macos_decomp_ready_requires_all_stage_tools(self):
        script = (ROOT / "scripts" / "Prepare-DKR-Runtime-macOS.sh").read_text()
        helper = re.search(r"(?ms)^decomp_ready\(\) \{\n.*?^\}", script)
        self.assertIsNotNone(helper)
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary) / "isolated revision with spaces"
            python = tree / ".venv/bin/python3"
            tools = [tree / "tools/dkr_assets_tool", tree / "tools/n64crc", tree / "tools/ido-recomp/macos/cc"]

            def executable(path, status=0):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(f"#!/bin/sh\nexit {status}\n")
                path.chmod(0o755)

            def ready():
                return (
                    subprocess.run(
                        ["bash", "-c", helper.group(0) + '\ndecomp_ready "$1"', "stage-test", str(tree)],
                        capture_output=True,
                        text=True,
                        timeout=5,
                    ).returncode
                    == 0
                )

            self.assertFalse(ready())
            executable(python)
            for tool in tools:
                self.assertFalse(ready())
                executable(tool)
            self.assertTrue(ready())
            for tool in tools:
                tool.unlink()
                self.assertFalse(ready())
                executable(tool)
            executable(python, status=1)
            self.assertFalse(ready())

    def tracked(self):
        output = subprocess.run(
            ["git", "-C", str(ROOT), "ls-files", "-s"], check=True, capture_output=True, text=True
        ).stdout
        for line in output.splitlines():
            mode, _, _, path = line.split(maxsplit=3)
            yield mode, path

    def test_macos_shell_entry_points_are_executable(self):
        if not (ROOT / ".git").exists():
            self.skipTest("not a git checkout")
        entry_points = {
            "Build-macOS.sh",
            "Run-macOS.sh",
            "Setup-macOS.sh",
            "scripts/Prepare-DKR-Runtime-macOS.sh",
            "scripts/Prepare-Game-Source-macOS.sh",
            "scripts/apply-dependency-patches.sh",
        }
        missing = [p for mode, p in self.tracked() if p in entry_points and mode != "100755"]
        self.assertEqual(missing, [], "commit with: git update-index --chmod=+x <file>")

    def test_shell_script_references_match_on_disk_case(self):
        # macOS defaults to case-insensitive APFS, Linux CI does not.
        names = {p.name for p in (ROOT / "scripts").iterdir()}
        lower = {n.lower(): n for n in names}
        wrong = []
        for script in list(ROOT.glob("*.sh")) + list((ROOT / "scripts").glob("*.sh")):
            for ref in re.findall(r"scripts/([A-Za-z0-9_.-]+\.(?:sh|py))", script.read_text()):
                if ref not in names and ref.lower() in lower:
                    wrong.append(f"{script.name}: {ref} -> {lower[ref.lower()]}")
        self.assertEqual(wrong, [])


@unittest.skipIf(os.name == "nt", "macOS packaging and POSIX build lock")
class MacOSDevexTests(unittest.TestCase):
    def test_package_versions_are_distinct_and_overridable(self):
        import package_macos as package

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with patch.object(package.time, "time", return_value=1800000000):
                first = package.bundle_version(root, 1)
                second = package.bundle_version(root, 1)
            self.assertLess(tuple(map(int, first.split("."))), tuple(map(int, second.split("."))))
            self.assertEqual(package.bundle_version(root, 1, "42.3.2"), "42.3.2")
            for invalid in ("0", "10000", "1.100.1", "1.2.100", "beta", "-1"):
                with self.assertRaises(ValueError):
                    package.bundle_version(root, 1, invalid)
            (root / "bin/Release").mkdir(parents=True)
            (root / "bin/Release/dkr-release.json").write_text('{"version":"1.0.5-old","build_number":1}')
            with self.assertRaisesRegex(RuntimeError, "differs from configured"):
                package.package(root, "1.0.5-new")

    def test_notarize_requires_identity_and_profile(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith("DKR_MAC_")}
        for extra in (
            {},
            {"DKR_MAC_SIGN_IDENTITY": "-", "DKR_MAC_NOTARY_PROFILE": "p"},
            {"DKR_MAC_SIGN_IDENTITY": "Developer ID Application: X (T)"},
        ):
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts/package_macos.py"), "build", "1.0.8", "--notarize"],
                capture_output=True,
                text=True,
                env={**env, **extra},
            )
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("DKR_MAC_SIGN_IDENTITY and DKR_MAC_NOTARY_PROFILE", result.stderr)

    @unittest.skipUnless(shutil.which("ditto"), "ditto is macOS-only")
    def test_archive_rejects_appledouble_entries(self):
        import package_macos as package

        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp) / "stage"
            (stage / "DKR-R.app").mkdir(parents=True)
            (stage / "DKR-R.app/._Info.plist").write_text("x")
            with self.assertRaisesRegex(RuntimeError, "metadata files"):
                package.archive(stage, Path(tmp) / "out.zip")
            (stage / "DKR-R.app/._Info.plist").unlink()
            (stage / "DKR-R.app/Info.plist").write_text("x")
            package.archive(stage, Path(tmp) / "out.zip")

    def test_lock_rejects_concurrent_builds_and_allows_nested_prepare(self):
        import fcntl

        helper = ROOT / "scripts/macos_build_lock.py"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            child = root / "child.sh"
            child.write_text("echo nested-ok\n")
            parent = root / "parent.sh"
            parent.write_text(f'"{sys.executable}" "{helper}" "{root}" "{child}"\n')
            cmd = [sys.executable, str(helper), str(root), str(parent)]
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("nested-ok", result.stdout)
            with (root / "build/.macos-build.lock").open("r+") as held:
                fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
                result = subprocess.run(cmd, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Another macOS build/prepare", result.stderr)
            parent.write_text("exit 42\n")
            self.assertEqual(subprocess.run(cmd).returncode, 42)
            parent.write_text("exit 0\n")
            self.assertEqual(subprocess.run(cmd).returncode, 0)

    def test_apple_helpers_validate_jobs_without_source_side_effects(self):
        helper = ROOT / "scripts/lib/apple-build.sh"
        script = r"""
set -euo pipefail
before_pwd="$PWD"
before_options="$-"
# Sourcing must not probe or install anything.
uname() { return 91; }
xcodebuild() { return 92; }
sysctl() { return 93; }
source "$1"
[[ "$PWD" == "$before_pwd" && "$-" == "$before_options" ]]
export CMAKE_BUILD_PARALLEL_LEVEL=7
[[ "$(dkr_apple_build_jobs)" == 7 ]]
[[ "$(dkr_apple_build_jobs 3)" == 3 ]]
for invalid in 0 -1 abc '2 3' 1.5; do
  if dkr_apple_build_jobs "$invalid" >/dev/null 2>&1; then exit 1; fi
done
export CMAKE_BUILD_PARALLEL_LEVEL=invalid
if dkr_apple_build_jobs >/dev/null 2>&1; then exit 1; fi
unset CMAKE_BUILD_PARALLEL_LEVEL
sysctl() { printf '12\n'; }
[[ "$(dkr_apple_build_jobs)" == 12 ]]
sysctl() { return 1; }
[[ "$(dkr_apple_build_jobs)" == 1 ]]
uname() { printf 'Linux\n'; }
if dkr_require_macos >/dev/null 2>&1; then exit 1; fi
uname() { printf 'Darwin\n'; }
dkr_require_macos
dkr_require_tools bash
if dkr_require_tools dkr_nonexistent_tool >/dev/null 2>&1; then exit 1; fi
xcodebuild() { [[ "$1" == -version ]]; }
if dkr_require_xcode >/dev/null 2>&1; then exit 1; fi
xcodebuild() { return 0; }
dkr_require_xcode
"""
        result = subprocess.run(
            ["/bin/bash", "-c", script, "apple-helper-test", str(helper)], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_host_check_rejects_missing_sha1sum_and_old_gmake(self):
        helper = ROOT / "scripts/macos-host-tools.sh"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            def executable(name, body="exit 0"):
                p = root / name
                p.write_text("#!/bin/sh\n" + body + "\n")
                p.chmod(0o755)

            for name in (
                "xcrun",
                "git",
                "python3",
                "cmake",
                "ninja",
                "c++",
                "shasum",
                "pcre2-config",
                "sdl2-config",
            ):
                executable(name)
            executable("uname", "echo Darwin")
            executable("gmake", 'echo "GNU Make 3.81"')
            cmd = ["/bin/bash", "-c", f'source "{helper}"; dkr_macos_host_tools 1']
            env = dict(os.environ, PATH=str(root))
            missing = subprocess.run(cmd, env=env, capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("missing from PATH: sha1sum", missing.stderr)
            executable("sha1sum")
            old_make = subprocess.run(cmd, env=env, capture_output=True, text=True)
            self.assertNotEqual(old_make.returncode, 0)
            self.assertIn("GNU Make 4", old_make.stderr)
            executable("gmake", 'echo "GNU Make 4.4"')
            executable("sha1sum", "exit 1")
            for name in ("mktemp", "rm"):
                (root / name).symlink_to("/usr/bin/" + name if Path("/usr/bin/" + name).exists() else "/bin/" + name)
            incompatible = subprocess.run(cmd, env=env, capture_output=True, text=True)
            self.assertNotEqual(incompatible.returncode, 0)
            self.assertIn("sha1sum must support -c --quiet", incompatible.stderr)


if __name__ == "__main__":
    unittest.main()
