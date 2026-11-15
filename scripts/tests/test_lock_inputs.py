"""lock_inputs: the lock goes stale when an embedded source pin moves."""

import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

SCRIPTS = Path(__file__).parents[1]
RELEASE = SCRIPTS / "release"
sys.path[:0] = [str(RELEASE), str(SCRIPTS)]
spec = importlib.util.spec_from_file_location("lock_inputs", RELEASE / "lock_inputs.py")
lock_inputs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lock_inputs)

import generate_replay_probe  # noqa: E402


class EmbeddedSources(unittest.TestCase):
    def test_the_committed_lock_matches_the_pinned_sources(self):
        # Fails when a dependency patch or pin change altered a source the
        # owned engines embed and the inputs were not prepared and locked again.
        lock = json.loads(lock_inputs.LOCK.read_text(encoding="utf-8"))
        self.assertIsNone(lock_inputs.stale_sources(lock))

    def test_a_moved_pin_is_reported(self):
        lock = {"sources": {"owned": lock_inputs.embedded_sources()}}
        name = next(iter(generate_replay_probe.RSP_INPUT_HASH))
        with mock.patch.dict(generate_replay_probe.RSP_INPUT_HASH, {name: "0" * 64}):
            self.assertIn("lock_inputs.py write", lock_inputs.stale_sources(lock))
        with mock.patch.object(generate_replay_probe, "HEADER_HASH", "0" * 64):
            self.assertIsNotNone(lock_inputs.stale_sources(lock))

    def test_a_lock_without_the_record_is_stale(self):
        self.assertIsNotNone(lock_inputs.stale_sources({}))

    def test_every_copied_source_table_is_covered(self):
        # A new table of pinned paths in the generator must be either added
        # to embedded_sources() (it is copied into the output) or listed here
        # (it is only audited).
        audited = {"INPUT_NATIVE_HASH", "FULL_SCENE_NATIVE_HASH", "REVIEWED_MAIN_MERGE_HASH"}
        tables = {
            name
            for name, value in vars(generate_replay_probe).items()
            if isinstance(value, dict) and value and all(isinstance(key, str) and "/" in key for key in value)
        }
        self.assertEqual(tables - audited, {"RSP_INPUT_HASH"})


if __name__ == "__main__":
    unittest.main()
