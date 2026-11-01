#!/usr/bin/env python3
"""scripts/identity_guard.py: simulation inputs can't change without either an
online identity bump or an explicit, reasoned "still compatible" record."""

from __future__ import annotations

import json
from pathlib import Path
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
        self.lock.write_text(
            json.dumps(
                {
                    "globs": ["sim/*.c"],
                    "identity": identity_guard.identity(self.header),
                    "inputs": identity_guard.inputs(["sim/*.c"], self.root),
                }
            )
        )

    def tearDown(self):
        self.temp.cleanup()

    def write_header(self, v77, v80):
        self.header.write_text(HEADER.format(v77=v77, v80=v80))

    def check(self):
        return identity_guard.check(self.lock, self.root, self.header)

    def update(self, compatible=None):
        return identity_guard.update(compatible, self.lock, self.root, self.header)

    def test_repository_record_is_current(self):
        self.assertEqual(identity_guard.check(), [])

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

    def test_compatible_update_records_reason(self):
        self.input.write_text("int step(void) { return 2; }\n")
        self.assertIsNone(self.update("comment-only change"))
        self.assertEqual(self.check(), [])
        recorded = json.loads(self.lock.read_text())["last_update"]
        self.assertEqual(recorded, {"identity_changed": False, "reason": "comment-only change"})

    def test_identity_bump_is_recorded_by_plain_update_only(self):
        self.input.write_text("int step(void) { return 2; }\n")
        self.write_header("cc", "dd")
        self.assertIn("legacy_identity.hpp changed since the inputs were recorded", self.check())
        self.assertIsNotNone(self.update("claims compatible"))
        self.assertIsNone(self.update())
        self.assertEqual(self.check(), [])
        self.assertTrue(json.loads(self.lock.read_text())["last_update"]["identity_changed"])

    def test_unmatched_glob_fails_loudly(self):
        with self.assertRaises(SystemExit):
            identity_guard.inputs(["sim/*.missing"], self.root)


if __name__ == "__main__":
    unittest.main()
