#!/usr/bin/env python3
"""scripts/identity_guard.py: simulation inputs can't change without either an
online identity bump or an explicit, reasoned "still compatible" record."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import identity_guard  # noqa: E402

HEADER = """#pragma once
#define DKR_LEGACY_OWNED_BUILD_ID_V77 "{v77}"
#define DKR_LEGACY_OWNED_BUILD_ID_V80 "{v80}"
"""


class IdentityGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "sim").mkdir()
        self.input = self.root / "sim/engine.c"
        self.input.write_text("int step(void) { return 1; }\n")
        self.header = self.root / "legacy_identity.hpp"
        self.write_header("aa", "bb")
        self.lock = self.root / "lock.json"
        self.write_lock()

    def tearDown(self):
        self.temp.cleanup()

    def write_lock(self):
        identity_guard.write(
            self.lock,
            {
                "globs": ["sim/*.c"],
                "identity": identity_guard.identity(self.header),
                "inputs": {
                    name: {"sha256": digest, "reason": "baseline"}
                    for name, digest in identity_guard.inputs(["sim/*.c"], self.root).items()
                },
            },
        )

    def record(self):
        return json.loads(self.lock.read_text())["inputs"]

    def write_header(self, v77, v80):
        self.header.write_text(HEADER.format(v77=v77, v80=v80))

    def check(self):
        return identity_guard.check(self.lock, self.root, self.header)

    def update(self, compatible=None):
        return identity_guard.update(compatible, self.lock, self.root, self.header)

    def test_repository_record_is_current(self):
        self.assertEqual(identity_guard.check(), [])

    def test_wire_contract_sources_are_guarded(self):
        # Codec and schema changes need a compatibility decision even if no
        # rollback engine source or protocol version was edited.
        wire_sources = (
            "netplay/netplay_protocol.cpp",
            "netplay/netplay_protocol.hpp",
            "netplay/netplay_types.cpp",
            "netplay/netplay_types.hpp",
            "netplay/netplay_build_identity.hpp",
            "netplay/authoritative_state.cpp",
            "netplay/authoritative_state.hpp",
            "netplay/authoritative_state_codec.cpp",
            "netplay/authoritative_state_codec.hpp",
            "mods/online_mod_manifest.cpp",
            "mods/online_mod_manifest.hpp",
            "mods/online_mod_transfer.cpp",
            "mods/online_mod_transfer.hpp",
            "mods/online_mod_wire_version.hpp",
        )
        record = identity_guard.load(identity_guard.LOCK)
        guarded = identity_guard.inputs(record["globs"])
        for source in wire_sources:
            with self.subTest(source=source):
                self.assertIn(f"runtime-recomp/src/game/{source}", guarded)

    def test_clean_tree_passes(self):
        self.assertEqual(self.check(), [])

    def test_crlf_checkout_matches_lf_record(self):
        self.input.write_bytes(b"int step(void) { return 1; }\r\n")
        self.assertEqual(self.check(), [])

    def test_changed_added_and_removed_inputs_fail(self):
        self.input.write_text("int step(void) { return 2; }\n")
        (self.root / "sim/new.c").write_text("int extra;\n")
        self.assertEqual(self.check(), ["changed: sim/engine.c", "added: sim/new.c"])
        self.input.unlink()
        self.assertIn("removed: sim/engine.c", self.check())

    def test_plain_update_refuses_without_identity_change(self):
        self.input.write_text("int step(void) { return 2; }\n")
        self.assertIsNotNone(self.update())
        self.assertNotEqual(self.check(), [])

    def test_compatible_update_records_the_reason_beside_the_changed_input(self):
        self.input.write_text("int step(void) { return 2; }\n")
        self.assertIsNone(self.update("comment-only change"))
        self.assertEqual(self.check(), [])
        entry = self.record()["sim/engine.c"]
        self.assertEqual(entry["reason"], "comment-only change")
        self.assertEqual(entry["sha256"], identity_guard.digest(self.input))

    def test_unchanged_inputs_keep_their_old_reason_and_a_changed_one_replaces_it(self):
        other = self.root / "sim/other.c"
        other.write_text("int other;\n")
        self.assertIsNone(self.update("first"))
        self.input.write_text("int step(void) { return 2; }\n")
        self.assertIsNone(self.update("second"))
        record = self.record()
        self.assertEqual(record["sim/other.c"]["reason"], "first")
        self.assertEqual(record["sim/engine.c"]["reason"], "second")
        self.input.write_text("int step(void) { return 3; }\n")
        self.assertIsNone(self.update("third"))
        self.assertEqual(self.record()["sim/engine.c"]["reason"], "third")
        self.assertEqual(self.record()["sim/other.c"]["reason"], "first")

    def test_removed_inputs_are_dropped_from_the_record(self):
        self.input.unlink()
        (self.root / "sim/keep.c").write_text("int keep;\n")
        self.assertIsNone(self.update("moved"))
        self.assertEqual(list(self.record()), ["sim/keep.c"])

    def test_identity_bump_is_recorded_by_plain_update_only(self):
        self.input.write_text("int step(void) { return 2; }\n")
        self.write_header("cc", "dd")
        self.assertIn("legacy_identity.hpp changed since the inputs were recorded", self.check())
        self.assertIsNotNone(self.update("claims compatible"))
        self.assertIsNone(self.update())
        self.assertEqual(self.check(), [])
        reason = self.record()["sim/engine.c"]["reason"]
        self.assertTrue(reason.startswith("online identity changed to "))
        self.assertIn(identity_guard.identity(self.header)[:12], reason)

    def test_an_input_without_a_reason_fails_the_check(self):
        data = json.loads(self.lock.read_text())
        data["inputs"]["sim/engine.c"]["reason"] = " "
        identity_guard.write(self.lock, data)
        self.assertEqual(self.check(), ["no reason recorded: sim/engine.c"])

    def test_the_old_flat_record_is_rejected(self):
        self.lock.write_text(json.dumps({"globs": [], "identity": "", "inputs": {"a": "b"}}))
        with self.assertRaises(SystemExit):
            self.check()

    def test_a_changed_digest_must_bring_a_new_reason(self):
        before = json.loads(self.lock.read_text())
        self.input.write_text("int step(void) { return 2; }\n")
        self.assertIsNone(self.update("why"))
        self.assertEqual(identity_guard.reasons_changed(before, json.loads(self.lock.read_text())), [])
        # A hand-edited digest that keeps the old reason is caught.
        edited = json.loads(self.lock.read_text())
        edited["inputs"]["sim/engine.c"]["sha256"] = "0" * 64
        edited["inputs"]["sim/engine.c"]["reason"] = "why"
        self.assertEqual(
            identity_guard.reasons_changed(json.loads(self.lock.read_text()), edited),
            ["digest changed without a new reason: sim/engine.c"],
        )

    def test_an_unresolvable_base_is_an_error_not_a_skip(self):
        # A shallow checkout without the base must not pass --against unchecked.
        with self.assertRaises(SystemExit):
            identity_guard.record_at("refs/heads/no-such-branch-for-the-identity-guard")

    def test_two_branches_that_record_different_inputs_merge_and_check_cleanly(self):
        """Each branch changes one input and records why; the merge has both, with no re-run of update."""
        for name in ("a", "b", "c"):
            (self.root / f"sim/{name}.c").write_text(f"int {name};\n")
        self.assertIsNone(self.update("seed"))
        base = self.lock.read_text()

        def branch(name: str, reason: str) -> dict[str, dict[str, str]]:
            self.lock.write_text(base)
            (self.root / f"sim/{name}.c").write_text(f"int {name}_changed;\n")
            self.assertIsNone(self.update(reason))
            return self.record()

        ours, theirs = branch("a", "reason A"), branch("b", "reason B")
        merged = {path: ours[path] if ours[path] != json.loads(base)["inputs"][path] else theirs[path] for path in ours}
        data = json.loads(base)
        data["inputs"] = merged
        identity_guard.write(self.lock, data)
        (self.root / "sim/a.c").write_text("int a_changed;\n")
        (self.root / "sim/b.c").write_text("int b_changed;\n")
        self.assertEqual(self.check(), [])
        self.assertEqual(
            {path: merged[path]["reason"] for path in ("sim/a.c", "sim/b.c", "sim/c.c")},
            {"sim/a.c": "reason A", "sim/b.c": "reason B", "sim/c.c": "seed"},
        )

    def test_git_merges_branches_that_record_neighbouring_inputs_textually(self):
        """A plain git merge (no structured driver) of two such branches has no conflict."""
        for name in ("a", "b", "c", "d"):
            (self.root / f"sim/{name}.c").write_text(f"int {name};\n")
        self.assertIsNone(self.update("seed"))

        def git(*args: str) -> str:
            done = subprocess.run(
                [
                    "git",
                    "-c",
                    "core.attributesFile=/dev/null",
                    "-c",
                    "user.name=t",
                    "-c",
                    "user.email=t@example.com",
                    *args,
                ],
                cwd=self.root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(done.returncode, 0, done.stderr + done.stdout)
            return done.stdout

        git("init", "-q", "-b", "main")
        git("add", "-A")
        git("commit", "-q", "-m", "base")
        for name, reason in (("b", "reason B"), ("c", "reason C")):
            git("checkout", "-q", "-b", name, "main")
            (self.root / f"sim/{name}.c").write_text(f"int {name}_changed;\n")
            self.assertIsNone(self.update(reason))
            git("commit", "-qam", name)
        git("checkout", "-q", "b")
        git("merge", "-q", "--no-edit", "c")
        self.assertEqual(self.check(), [])
        record = self.record()
        self.assertEqual((record["sim/b.c"]["reason"], record["sim/c.c"]["reason"]), ("reason B", "reason C"))

    def test_unmatched_glob_fails_loudly(self):
        with self.assertRaises(SystemExit):
            identity_guard.inputs(["sim/*.missing"], self.root)


if __name__ == "__main__":
    unittest.main()
